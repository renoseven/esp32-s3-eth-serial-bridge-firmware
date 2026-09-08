// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Lock-free SPSC byte ring buffer.
//
// Implementation:
//   - Single producer and single consumer; each side advances only its own cursor.
//   - Monotonic head/tail counters index through a power-of-two buffer via mask.
//   - acquire/release ordering publishes byte visibility across cores.
//   - head and tail sit on separate cache lines to limit false sharing.
//   - RingReader and RingWriter are non-owning single-side views; not MPMC-safe.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "firmware.h"

// ============================================================
//                          Ring
// ============================================================

// Shared ring buffer state.
struct Ring {
    uint8_t* buf;                                                    // Backing storage (non-owning).
    size_t mask;                                                     // Capacity - 1 (power-of-two index mask).
    alignas(Firmware::CACHE_LINE_SIZE) std::atomic<size_t> head = 0; // Producer cursor (bytes written).
    alignas(Firmware::CACHE_LINE_SIZE) std::atomic<size_t> tail = 0; // Consumer cursor (bytes read).
};

// ============================================================
//                         RingReader
// ============================================================

// Ring buffer reader side.
class RingReader {
  public:
    // Start of the readable region; valid for the next len() bytes.
    const uint8_t* data() const noexcept {
        return &this->ring.buf[this->ring.tail.load(std::memory_order_relaxed) & this->ring.mask];
    }

    // Bytes readable now without wrapping (0 when empty).
    size_t len() const noexcept {
        const size_t mask = this->ring.mask;
        const size_t tail = this->ring.tail.load(std::memory_order_relaxed);
        const size_t head = this->ring.head.load(std::memory_order_acquire);

        const size_t count = head - tail;
        if (count == 0) {
            return 0;
        }

        const size_t index = tail & mask;
        const size_t wrap = (mask + 1) - index;

        return count < wrap ? count : wrap;
    }

    // Release n bytes read from data() back to the producer (advance tail).
    // Only the consumer writes tail, so a plain load + store-release is enough
    // (no atomic RMW cycle needed).
    void consume(size_t n) {
        const size_t tail = this->ring.tail.load(std::memory_order_relaxed);
        this->ring.tail.store(tail + n, std::memory_order_release);
    }

  private:
    template <size_t CAPACITY> friend class RingBuf;

    explicit RingReader(Ring& ring) noexcept : ring(ring) {}

    Ring& ring; // Shared storage + cursors (consumer side).
};

// ============================================================
//                         RingWriter
// ============================================================

// Ring buffer writer side.
class RingWriter {
  public:
    // Start of the writable region; valid for the next len() bytes.
    uint8_t* data() noexcept {
        return &this->ring.buf[this->ring.head.load(std::memory_order_relaxed) & this->ring.mask];
    }

    // Bytes writable now without wrapping (0 when full).
    size_t len() const noexcept {
        const size_t mask = this->ring.mask;
        const size_t head = this->ring.head.load(std::memory_order_relaxed);
        const size_t tail = this->ring.tail.load(std::memory_order_acquire);

        const size_t count = mask - (head - tail);
        if (count == 0) {
            return 0;
        }

        const size_t index = head & mask;
        const size_t wrap = (mask + 1) - index;

        return count < wrap ? count : wrap;
    }

    // Publish n bytes written into data() to the consumer (advance head).
    // Only the producer writes head, so a plain load + store-release is enough
    // (no atomic RMW cycle needed).
    void commit(size_t n) {
        const size_t head = this->ring.head.load(std::memory_order_relaxed);
        this->ring.head.store(head + n, std::memory_order_release);
    }

  private:
    template <size_t CAPACITY> friend class RingBuf;

    explicit RingWriter(Ring& ring) noexcept : ring(ring) {}

    Ring& ring; // Shared storage + cursors (producer side).
};

// ============================================================
//                           RingBuf
// ============================================================

// SPSC byte ring buffer.
template <size_t CAPACITY> class RingBuf {
  public:
    RingBuf() : ring{this->buf, MASK} {}

    RingBuf(const RingBuf&) = delete;
    RingBuf& operator=(const RingBuf&) = delete;
    RingBuf(RingBuf&&) = delete;
    RingBuf& operator=(RingBuf&&) = delete;

    // --- Views ---

    // Consumer-side zero-copy view; at most one reader at a time.
    RingReader read() noexcept {
        return RingReader(this->ring);
    }

    // Producer-side zero-copy view; at most one writer at a time.
    RingWriter write() noexcept {
        return RingWriter(this->ring);
    }

    // Drop all pending bytes. No RingReader/RingWriter may be in use.
    void clear() noexcept {
        const size_t head = this->ring.head.load(std::memory_order_acquire);
        this->ring.tail.store(head, std::memory_order_release);
    }

  private:
    // --- Constants ---

    // Power-of-two capacity: head & MASK and tail & MASK yield indices in [0, CAPACITY - 1].
    static constexpr size_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & MASK) == 0, "RingBuf capacity must be a power of two");

    // --- Storage ---

    uint8_t buf[CAPACITY]; // Backing storage (declared before ring so address is valid).
    Ring ring;             // Mask + cursors; buf pointer into buf[].
};
