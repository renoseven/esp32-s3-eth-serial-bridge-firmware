// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Non-owning read-only string span.
//
// Implementation:
//   - Stores (data, length) as content bytes; the span need not be null-terminated.
//   - Empty views alias an internal "" literal so str() is never nullptr.
//   - Construct from (data, length), string literals, or const String&.
//   - trim() strips ASCII whitespace by narrowing the span in place with no allocation.
//   - Trivial value type passed by value; not thread-safe if backing storage mutates.

#pragma once

#include <cctype>
#include <cstddef>

#include <WString.h>

// ============================================================
//                         StringView
// ============================================================

// Non-owning read-only string span.
class StringView {
  public:
    // --- Construction ---

    // An empty view.
    constexpr StringView() noexcept : data(EMPTY_STR), length(0) {}

    // View over a byte span.
    constexpr StringView(const char* str, size_t len) noexcept
        : data(str != nullptr ? str : EMPTY_STR), length(str != nullptr ? len : 0) {}

    // View over a string literal or char array (length excludes the null terminator).
    template <size_t N> constexpr StringView(const char (&str)[N]) noexcept : data(str), length(N - 1) {}

    // View over a string.
    explicit StringView(const String& str) noexcept : StringView(str.c_str(), str.length()) {}

    // --- Accessors ---

    // Start of the viewed bytes.
    constexpr const char* str() const noexcept {
        return this->data;
    }

    // Byte length of the view.
    constexpr size_t len() const noexcept {
        return this->length;
    }

    // True when the view has no bytes.
    constexpr bool isEmpty() const noexcept {
        return this->length == 0;
    }

    // --- Mutation ---

    // Remove ASCII whitespace from both ends.
    void trim() noexcept {
        if (this->length == 0) {
            return;
        }

        while (this->length > 0 && isspace(static_cast<unsigned char>(this->data[0]))) {
            this->data += 1;
            this->length -= 1;
        }
        while (this->length > 0 && isspace(static_cast<unsigned char>(this->data[this->length - 1]))) {
            this->length -= 1;
        }
    }

    // --- Comparison ---

    // True when equal to other.
    bool operator==(StringView other) const noexcept {
        if (this->length != other.length) {
            return false;
        }
        if (this->length == 0) {
            return true;
        }

        if (this->data == other.data) {
            return true;
        }

        // Fixed-length equality only; not for constant-time secret comparison.
        return __builtin_bcmp(this->data, other.data, this->length) == 0;
    }

  private:
    // --- Storage ---

    inline static constexpr char EMPTY_STR[] = ""; // Backing for empty views.

    const char* data; // Viewed byte sequence.
    size_t length;    // Number of bytes in the view.
};
