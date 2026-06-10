// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Deferred scheduler.
//
// Implementation:
//   - Slots live in a fixed array with no heap allocation.
//   - schedule() returns false when entry is null or the queue is full; scheduleExclusive()
//     when entry is null; cancel() when entry is null or no matching slot is pending.
//   - Debounce by Deferred identity: re-scheduling an equal Deferred updates its deadline
//     in place (one pending slot per distinct Deferred).
//   - Slots stay unsorted; a cached next index keeps loop() O(1) on the hot path.
//   - updateNext() updates next in O(1) when possible; rescanNext() handles invalidation.
//   - removeAt() uses swap-with-last instead of sliding; millis() deadlines are wrap-safe.
//   - Mutex is taken once in insert/remove/nextDue; private helpers assume it is held.
//   - loop() releases the lock before running each deferred.

#pragma once

#include <cstddef>
#include <cstdint>

#include <Arduino.h>

#include "util/mutex.h"

// ============================================================
//                         Deferred
// ============================================================

// Schedulable deferred unit.
struct Deferred {
    // --- Types ---

    // Deferred entry function.
    using Entry = void (*)(void* context);

    // --- Fields ---

    Entry entry = nullptr;   // Deferred entry function.
    void* context = nullptr; // Argument passed to entry.

    // --- Methods ---

    // Check whether two deferred units are equal.
    bool operator==(const Deferred& other) const noexcept {
        return this->entry == other.entry && this->context == other.context;
    }

    // Run entry with context.
    void run() const {
        if (this->entry != nullptr) {
            this->entry(this->context);
        }
    }
};

// ============================================================
//                     DeferredScheduler
// ============================================================

// Fixed-capacity deferred scheduler.
template <size_t CAPACITY> class DeferredScheduler {
    static_assert(CAPACITY >= 1, "DeferredScheduler CAPACITY must be at least 1");

  public:
    // --- Constructor ---

    DeferredScheduler() = default;

    DeferredScheduler(const DeferredScheduler&) = delete;
    DeferredScheduler& operator=(const DeferredScheduler&) = delete;

    // --- Scheduling ---

    // Schedule deferred to run after delayMs.
    bool schedule(const Deferred& deferred, uint32_t delayMs) {
        if (deferred.entry == nullptr) {
            return false;
        }

        return this->insert(deferred, static_cast<uint32_t>(millis()) + delayMs);
    }

    // Clear the queue and schedule deferred.
    bool scheduleExclusive(const Deferred& deferred, uint32_t delayMs) {
        if (deferred.entry == nullptr) {
            return false;
        }

        return this->insert(deferred, static_cast<uint32_t>(millis()) + delayMs, true);
    }

    // Cancel a pending deferred.
    bool cancel(const Deferred& deferred) {
        if (deferred.entry == nullptr) {
            return false;
        }

        return this->remove(deferred);
    }

    // --- Loop ---

    // Run every deferred whose deadline has passed.
    void loop() {
        const uint32_t now = millis();

        Deferred deferred;
        while (this->nextDue(deferred, now)) {
            deferred.run();
        }
    }

  private:
    // --- Types ---

    // Queue slot with deferred and deadline.
    struct Slot {
        Deferred deferred;     // Deferred to run when due.
        uint32_t deadline = 0; // Absolute run time (millis).

        // Check whether this deadline is before other's.
        bool isBefore(const Slot& other) const noexcept {
            return static_cast<int32_t>(other.deadline - this->deadline) > 0;
        }

        // Check whether this deadline is due at now.
        bool isDue(uint32_t now) const noexcept {
            return static_cast<int32_t>(now - this->deadline) >= 0;
        }
    };

    // --- State ---

    Slot slots[CAPACITY]; // Pending slot array.
    size_t count = 0;     // Active entries in slots[0..count).
    size_t next = 0;      // Index of the slot with the nearest deadline.
    Mutex mutex;          // Queue synchronization.

    // --- Locked entry ---

    // Insert deferred at deadline.
    bool insert(const Deferred& deferred, uint32_t deadline, bool exclusive = false) {
        MutexGuard guard(this->mutex);

        if (exclusive) {
            this->count = 0;
        }

        const size_t index = this->indexOf(deferred);

        if (index == this->count && this->count == CAPACITY) {
            return false;
        }

        if (index == this->count) {
            this->count++;
        }

        this->slots[index] = Slot{deferred, deadline};
        this->updateNext(index);
        return true;
    }

    // Remove pending deferred by identity.
    bool remove(const Deferred& deferred) {
        MutexGuard guard(this->mutex);

        const size_t index = this->indexOf(deferred);
        if (index >= this->count) {
            return false;
        }

        this->removeAt(index);
        return true;
    }

    // Pop the next due deferred.
    bool nextDue(Deferred& deferred, uint32_t now) {
        MutexGuard guard(this->mutex);

        if (this->count == 0 || !this->slots[this->next].isDue(now)) {
            return false;
        }

        deferred = this->slots[this->next].deferred;
        this->removeAt(this->next);
        return true;
    }

    // --- Lookup (caller must hold mutex) ---

    // Find index of equal deferred.
    size_t indexOf(const Deferred& deferred) const {
        for (size_t i = 0; i < this->count; i++) {
            if (this->slots[i].deferred == deferred) {
                return i;
            }
        }
        return this->count;
    }

    // --- Next cache (caller must hold mutex) ---

    // Rescan for the nearest deadline.
    void rescanNext() {
        if (this->count <= 1) {
            this->next = 0;
            return;
        }

        size_t best = 0;
        for (size_t i = 1; i < this->count; i++) {
            if (this->slots[i].isBefore(this->slots[best])) {
                best = i;
            }
        }

        this->next = best;
    }

    // Update next after a slot change.
    void updateNext(size_t index) {
        if (this->count <= 1) {
            this->next = 0;
            return;
        }

        if (this->slots[index].isBefore(this->slots[this->next])) {
            this->next = index;
            return;
        }

        if (index != this->next) {
            return;
        }

        this->rescanNext();
    }

    // --- Mutation (caller must hold mutex) ---

    // Remove slot at index.
    void removeAt(size_t index) {
        const bool isNext = (index == this->next);
        const size_t last = this->count - 1;

        if (index != last) {
            this->slots[index] = this->slots[last];
        }
        this->count--;

        if (this->count <= 1) {
            this->next = 0;
            return;
        }

        if (isNext) {
            this->rescanNext();
        } else if (index != last && this->slots[index].isBefore(this->slots[this->next])) {
            this->next = index;
        }
    }
};
