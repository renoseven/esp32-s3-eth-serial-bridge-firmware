// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Mutex and scope lock guard for mutual exclusion.
//
// Implementation:
//   - Static storage avoids heap allocation; Mutex is non-recursive.
//   - LockGuard acquires in the constructor and releases in the destructor.
//   - tryLock() and tryLockFor() support polling and timed waits.
//   - Not ISR-safe (no FromISR wrappers); use only from task context.

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================
//                           Mutex
// ============================================================

// Non-recursive mutex.
class Mutex {
  public:
    // --- Constructor ---

    explicit Mutex() {
        this->handle = xSemaphoreCreateMutexStatic(&this->storage);
        configASSERT(this->handle != nullptr);
    }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    // --- Lock ---

    // Acquire the mutex.
    void lock() {
        const int acquired = xSemaphoreTake(this->handle, portMAX_DELAY);
        configASSERT(acquired == pdTRUE);
    }

    // Try to acquire the mutex.
    bool tryLock() {
        return xSemaphoreTake(this->handle, 0) == pdTRUE;
    }

    // Try to acquire the mutex within ticks.
    bool tryLockFor(TickType_t ticks) {
        return xSemaphoreTake(this->handle, ticks) == pdTRUE;
    }

    // --- Unlock ---

    // Release the mutex.
    void unlock() {
        const int released = xSemaphoreGive(this->handle);
        configASSERT(released == pdTRUE);
    }

  private:
    StaticSemaphore_t storage{};        // Static storage.
    SemaphoreHandle_t handle = nullptr; // Semaphore handle.
};

// ============================================================
//                         LockGuard
// ============================================================

// Scope lock for a mutex.
template <typename M> class LockGuard {
  public:
    // --- Constructor / Destructor ---

    // Acquire the mutex.
    explicit LockGuard(M& mutex) : mutex(mutex) {
        this->mutex.lock();
    }

    // Release the mutex.
    ~LockGuard() {
        this->mutex.unlock();
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

  private:
    M& mutex; // Referenced mutex.
};

// ============================================================
//                         Type Aliases
// ============================================================

// Scope lock for Mutex.
using MutexGuard = LockGuard<Mutex>;
