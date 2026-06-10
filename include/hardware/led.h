// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// RGB status LED: solid colors and concurrent blinks.
// Global `LED` object.

#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_timer.h>

// ============================================================
//                            LedColor
// ============================================================

// RGBA logical color.
class LedColor {
  public:
    // --- Constructors ---

    constexpr LedColor() noexcept : value(0) {}

    // --- Factory ---

    // Define an RGB color; alpha is UINT8_MAX.
    static constexpr LedColor rgb(uint8_t r, uint8_t g, uint8_t b) noexcept {
        return rgba(r, g, b, UINT8_MAX);
    }

    // Define an RGBA color.
    static constexpr LedColor rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        uint32_t value = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
                         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
        return LedColor(value);
    }

    // --- Channels ---

    // Red channel value.
    constexpr uint8_t red() const noexcept {
        return static_cast<uint8_t>((this->value >> 24) & 0xFF);
    }

    // Green channel value.
    constexpr uint8_t green() const noexcept {
        return static_cast<uint8_t>((this->value >> 16) & 0xFF);
    }

    // Blue channel value.
    constexpr uint8_t blue() const noexcept {
        return static_cast<uint8_t>((this->value >> 8) & 0xFF);
    }

    // Alpha channel value.
    constexpr uint8_t alpha() const noexcept {
        return static_cast<uint8_t>(this->value & 0xFF);
    }

    // Whether the color is fully transparent.
    constexpr bool isTransparent() const noexcept {
        return this->alpha() == 0;
    }

    // --- Premultiplication ---

    // Premultiplied red channel value.
    constexpr uint8_t premulRed() const noexcept {
        return rescale(this->red() * this->alpha());
    }

    // Premultiplied green channel value.
    constexpr uint8_t premulGreen() const noexcept {
        return rescale(this->green() * this->alpha());
    }

    // Premultiplied blue channel value.
    constexpr uint8_t premulBlue() const noexcept {
        return rescale(this->blue() * this->alpha());
    }

    // --- Composition ---

    // Porter-Duff over: blend other over this color; straight alpha in and out.
    constexpr LedColor over(const LedColor& other) const noexcept {
        if (other.isTransparent()) {
            return *this;
        }

        if (this->isTransparent()) {
            return other;
        }

        // Blend other over this color.
        const uint8_t r = rescale(other.red() * other.alpha() + this->red() * (UINT8_MAX - other.alpha()));
        const uint8_t g = rescale(other.green() * other.alpha() + this->green() * (UINT8_MAX - other.alpha()));
        const uint8_t b = rescale(other.blue() * other.alpha() + this->blue() * (UINT8_MAX - other.alpha()));
        const uint8_t a = rescale(other.alpha() * UINT8_MAX + this->alpha() * (UINT8_MAX - other.alpha()));

        return rgba(r, g, b, a);
    }

    // --- Operators ---

    constexpr bool operator==(const LedColor& other) const noexcept {
        return this->value == other.value;
    }

    constexpr bool operator!=(const LedColor& other) const noexcept {
        return this->value != other.value;
    }

  private:
    // Rescale wide channel math to uint8.
    static constexpr uint8_t rescale(uint16_t v) noexcept {
        return static_cast<uint8_t>((v + (v >> 8) + 1) >> 8);
    }

    constexpr explicit LedColor(uint32_t value) noexcept : value(value) {}

    uint32_t value; // RGBA value (0xRRGGBBAA).
};

// ============================================================
//                              Led
// ============================================================

class Led {
  public:
    // --- Display ---

    // Set base color and update the pin.
    void show(LedColor color);

    // Toggle blink overlay for color.
    void blink(LedColor color, bool active);

    // Turn off the LED.
    void off();

    // --- Lifecycle ---

    // Initialize the LED.
    void begin();

    // Shut down the LED.
    void end();

  private:
    // --- Constants ---

    // Max concurrent blink slots.
    static constexpr size_t SLOT_CAPACITY = 2;

    // --- Types ---

    // One concurrent blink slot.
    struct Slot {
        bool occupied; // Slot occupancy.

        LedColor color;     // Slot color.
        size_t acquireTick; // Acquire tick index.
        size_t releaseTick; // Release tick index (0 = not scheduled).

        // Whether release is due at tick.
        bool isReleaseDue(size_t tick) const noexcept;
    };

    // --- Display state ---

    LedColor baseColor;        // Base display color.
    Slot slots[SLOT_CAPACITY]; // Active blink slot colors.
    esp_timer_handle_t timer;  // Display scheduler.

    size_t currentTick;  // Current tick index.
    size_t currentRound; // Current round-robin index.

    // --- Hardware state ---

    LedColor lastColor; // Last color written to the hardware.

    // --- Hardware I/O ---

    // Write color to the hardware.
    void write(LedColor color);

    // --- Slots ---

    // Find the slot holding color.
    Slot* lookup(LedColor color);

    // Start blinking color.
    void acquire(LedColor color);

    // Stop blinking color after the current cycle.
    void release(LedColor color);

    // Refresh overlay and advance blink phase.
    void tick();

    // Reset display and blink state.
    void clear();

    // --- Timer ---

    // Restart tick processing when the timer is idle.
    void schedule();
};

// Global status LED.
extern Led LED;
