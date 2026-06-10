// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Serial service API: USB-CDC to lock-free byte stream bridge.

#pragma once

#include <cstddef>

#include <USBCDC.h>

#include "util/ringbuf.h"
#include "util/worker.h"

// ============================================================
//                        SerialService
// ============================================================

class SerialService {
  public:
    // --- Constants ---

    static constexpr size_t SERIAL_BUF_SIZE = 8192; // USB RX/TX ring buffer capacity.

    // --- Endpoint ---

    // Read end of rxBuf: bytes the USB host produced.
    RingReader reader() noexcept;

    // Write end of txBuf: bytes destined for the USB host.
    RingWriter writer() noexcept;

    // --- Lifecycle ---

    // Bring up the USB-CDC serial bridge.
    bool begin();

    // Shut down the USB-CDC serial bridge.
    void end();

    // Restart the USB-CDC serial bridge.
    bool restart();

  private:
    // --- Buffers ---

    RingBuf<SERIAL_BUF_SIZE> rxBuf; // USB host -> network (SSH reads)
    RingBuf<SERIAL_BUF_SIZE> txBuf; // network -> USB host (SSH writes)

    // --- USB bridge ---

    USBCDC cdc;    // USB-CDC interface.
    Worker worker; // USB bridge worker (started by begin()).

    // --- Bridge worker ---

    // Bridge worker entry point.
    static void entry(const TaskContext& ctx);
};
