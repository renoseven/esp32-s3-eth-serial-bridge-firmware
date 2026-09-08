// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Counting semaphore.
//
// Implementation:
//   - Static storage avoids heap allocation.
//   - Configurable initial and maximum counts; default (0, 1) is an empty binary semaphore.
//   - give() fails at max count and leaves the count unchanged.
//   - Not ISR-safe (no FromISR wrappers); use only from task context.

#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================
//                         Semaphore
// ============================================================

// Counting semaphore.
class Semaphore {
  public:
    // --- Constructor ---

    explicit Semaphore() : Semaphore(0, 1) {}

    explicit Semaphore(uint32_t initialCount, uint32_t maxCount) {
        this->handle = xSemaphoreCreateCountingStatic(maxCount, initialCount, &this->storage);
        configASSERT(this->handle != nullptr);
    }

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    // --- Take ---

    // Take a token.
    void take() {
        const int acquired = xSemaphoreTake(this->handle, portMAX_DELAY);
        configASSERT(acquired == pdTRUE);
    }

    // Try to take a token.
    bool tryTake() {
        return xSemaphoreTake(this->handle, 0) == pdTRUE;
    }

    // Try to take a token within ticks.
    bool tryTakeFor(TickType_t ticks) {
        return xSemaphoreTake(this->handle, ticks) == pdTRUE;
    }

    // --- Give ---

    // Give a token.
    void give() {
        const int released = xSemaphoreGive(this->handle);
        configASSERT(released == pdTRUE);
    }

  private:
    StaticSemaphore_t storage{};        // Static storage.
    SemaphoreHandle_t handle = nullptr; // Semaphore handle.
};
