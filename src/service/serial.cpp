// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SerialService implementation.
//
// Flow:
//   - begin(): start USB-CDC and spawn bridge Worker on SERIAL task core.
//   - end(): stop/join Worker and release USB-CDC.
//   - restart(): end() then begin().
//   - Bridge worker (SPSC): CDC -> rxBuf (SSH reads), txBuf -> CDC (SSH writes).
//   - Entry polls ctx.worker.isRunning(); owner stops via Worker::stop().
// One producer and one consumer per buffer; lock-free via util/ringbuf.h.

#include "service/serial.h"

#include "firmware.h"

#include "hardware/led.h"

namespace {
    constexpr LedColor COLOR_TX = LedColor::rgb(0, 255, 0);   // TX - Green
    constexpr LedColor COLOR_RX = LedColor::rgb(255, 150, 0); // RX - Orange

    // --- Worker ---

    constexpr const char* TASK_NAME = "serial";
    constexpr uint32_t TASK_STACK = 4096;
    constexpr uint8_t TASK_PRIORITY = 5;
} // namespace

// ============================================================
//                        SerialService
// ============================================================

// --- Endpoint ---

RingReader SerialService::reader() noexcept {
    return this->rxBuf.read();
}

RingWriter SerialService::writer() noexcept {
    return this->txBuf.write();
}

// --- Lifecycle ---

bool SerialService::begin() {
    static constexpr Task SERIAL_TASK{
        .name = TASK_NAME,
        .stack = TASK_STACK,
        .priority = TASK_PRIORITY,
        .core = Firmware::CORE_SERVICE,
        .entry = SerialService::entry,
    };

    if (this->worker.isAlive()) {
        return false;
    }

    this->cdc.begin();
    if (!this->worker.start(SERIAL_TASK, this)) {
        this->cdc.end();
        return false;
    }

    return true;
}

void SerialService::end() {
    this->worker.stop();
    this->worker.join();
    this->rxBuf.clear();
    this->txBuf.clear();
    this->cdc.end();
}

bool SerialService::restart() {
    this->end();
    return this->begin();
}

// --- Bridge worker ---

void SerialService::entry(const TaskContext& ctx) {
    SerialService& service = *static_cast<SerialService*>(ctx.userdata);

    auto tx = service.txBuf.read();  // network -> CDC
    auto rx = service.rxBuf.write(); // CDC -> network

    while (ctx.worker.isRunning()) {
        size_t rxBytes = 0;
        size_t txBytes = 0;

        // CDC -> rxBuf: move host input toward the network side.
        for (;;) {
            if (service.cdc.available() <= 0) {
                break;
            }

            const size_t len = rx.len();
            if (len == 0) {
                break;
            }

            const int read = service.cdc.read(rx.data(), len);
            if (read <= 0) {
                break;
            }

            rx.commit(static_cast<size_t>(read));
            rxBytes += static_cast<size_t>(read);
        }

        // txBuf -> CDC: flush network output to the host.
        for (;;) {
            const size_t len = tx.len();
            if (len == 0) {
                break;
            }

            const size_t written = service.cdc.write(tx.data(), len);
            if (written == 0) {
                break;
            }

            tx.consume(written);
            txBytes += written;
        }

        LED.blink(COLOR_TX, txBytes != 0);
        LED.blink(COLOR_RX, rxBytes != 0);

        if (rxBytes == 0 && txBytes == 0) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
            continue;
        }

        taskYIELD();
    }
}
