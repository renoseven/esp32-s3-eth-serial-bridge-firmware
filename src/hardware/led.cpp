// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// RGB status LED: solid colors and concurrent blinks.

#include "hardware/led.h"

#include <esp32-hal-rgb-led.h>
#include <esp32-hal.h>
#include <esp_err.h>

#include "firmware.h"

namespace {
    // --- Constants ---

    // Off display state.
    constexpr LedColor COLOR_OFF;

    // Global brightness dimming overlay.
    constexpr LedColor COLOR_BRIGHTNESS = LedColor::rgba(0, 0, 0, UINT8_MAX - Firmware::LED_BRIGHTNESS);

    // Ticks per blink cycle (lit + dark).
    constexpr size_t BLINK_CYCLE_TICKS = 2;

    // Blink phase duration.
    constexpr uint16_t TICK_MS = 25;
    constexpr uint64_t TICK_PERIOD_US = static_cast<uint64_t>(TICK_MS) * 1000ULL;
} // namespace

// --- Types ---

bool Led::Slot::isReleaseDue(size_t tick) const noexcept {
    return this->releaseTick != 0 && tick >= this->releaseTick;
}

// --- Display ---

void Led::show(LedColor color) {
    this->baseColor = color;
    this->write(color);
}

void Led::blink(LedColor color, bool active) {
    if (this->timer == nullptr || color.isTransparent()) {
        return;
    }

    if (active) {
        this->acquire(color);
    } else {
        this->release(color);
    }
}

void Led::off() {
    this->clear();
    this->write(COLOR_OFF);
}

// --- Lifecycle ---

void Led::begin() {
    if (this->timer != nullptr) {
        return;
    }

    this->clear();
    this->write(COLOR_OFF);

    const esp_timer_create_args_t args{
        .callback = +[](void* arg) { static_cast<Led*>(arg)->tick(); },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &this->timer));
}

void Led::end() {
    if (this->timer == nullptr) {
        return;
    }

    this->clear();
    this->write(COLOR_OFF);

    ESP_ERROR_CHECK(esp_timer_delete(this->timer));
    this->timer = nullptr;
}

// --- Hardware I/O ---

void Led::write(LedColor color) {
    if (color == this->lastColor) {
        return;
    }

    this->lastColor = color;

    if (Firmware::LED_BRIGHTNESS != UINT8_MAX) {
        color = color.over(COLOR_BRIGHTNESS);
    }

    rgbLedWrite(Firmware::LED_PIN, color.premulRed(), color.premulGreen(), color.premulBlue());
}

// --- Slots ---

Led::Slot* Led::lookup(LedColor color) {
    for (Slot& slot : this->slots) {
        if (slot.occupied && slot.color == color) {
            return &slot;
        }
    }

    return nullptr;
}

void Led::acquire(LedColor color) {
    Slot* slot = this->lookup(color);
    if (slot != nullptr) {
        if (slot->releaseTick != 0) {
            slot->releaseTick = 0;
            this->schedule();
        }

        return;
    }

    // Prefer vacant slot, else retiring; drop if both slots are active.
    Slot* pick = nullptr;
    for (Slot& slot : this->slots) {
        if (!slot.occupied) {
            pick = &slot;
            break;
        }

        if (pick == nullptr && slot.releaseTick != 0) {
            pick = &slot;
        }
    }

    if (pick == nullptr) {
        return;
    }

    pick->occupied = true;
    pick->color = color;
    pick->acquireTick = this->currentTick;
    pick->releaseTick = 0;

    this->schedule();
}

void Led::release(LedColor color) {
    Slot* slot = this->lookup(color);
    if (slot == nullptr || slot->releaseTick != 0) {
        return;
    }

    // At least one more tick and one full blink cycle since acquire.
    const size_t nextTick = this->currentTick + 1;
    const size_t endTick = slot->acquireTick + BLINK_CYCLE_TICKS;
    slot->releaseTick = nextTick > endTick ? nextTick : endTick;

    this->schedule();
}

void Led::tick() {
    const bool isLitTick = (this->currentTick % 2) == 0;

    LedColor colors[SLOT_CAPACITY];
    size_t count = 0;

    bool idle = true;

    for (Slot& slot : this->slots) {
        if (!slot.occupied) {
            continue;
        }

        if (slot.isReleaseDue(this->currentTick)) {
            slot = Slot{};
            continue;
        }

        idle = false;

        if (isLitTick) {
            colors[count++] = slot.color;
        }
    }

    if (idle) {
        this->currentTick = 0;
        this->currentRound = 0;
        this->write(this->baseColor);

        if (this->timer != nullptr) {
            esp_timer_stop(this->timer);
        }

        return;
    }

    if (isLitTick) {
        this->write(colors[this->currentRound % count]);
    } else {
        this->write(COLOR_OFF);
    }

    // One-shot schedule() leaves the timer inactive; start periodic blink.
    if (!esp_timer_is_active(this->timer)) {
        esp_timer_start_periodic(this->timer, TICK_PERIOD_US);
    }

    if (count >= 2) {
        this->currentRound++;
    }

    this->currentTick++;
}

void Led::clear() {
    this->baseColor = COLOR_OFF;

    for (Slot& slot : this->slots) {
        slot = Slot{};
    }

    if (this->timer != nullptr) {
        esp_timer_stop(this->timer);
    }

    this->currentTick = 0;
    this->currentRound = 0;
}

// --- Timer ---

void Led::schedule() {
    if (this->timer == nullptr) {
        return;
    }

    if (!esp_timer_is_active(this->timer)) {
        esp_timer_start_once(this->timer, 0);
    }
}

Led LED;
