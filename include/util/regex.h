// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Cached regex pattern matcher.
//
// Implementation:
//   - Fixed CAPACITY; the least-recently-used slot is evicted on overflow.
//   - Patterns are stored as const char* and identified by pointer with strcmp fallback;
//     each pattern must outlive the cache (typically string literals).
//   - Values are matched as StringView via REG_STARTEND; empty spans never match a pattern.
//   - Recency is tracked with a small MRU-first index array instead of linked nodes.
//   - Absent (null or empty) pattern matches everything; REG_EXTENDED | REG_NOSUB.
//   - regcomp failure leaves the LRU slot empty so the next miss reuses it before evicting.
//   - Not thread-safe.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sys/types.h>
#include <regex.h>

#include "util/string.h"

// ============================================================
//                         RegexCache
// ============================================================

// Cached regex pattern matcher.
template <size_t CAPACITY> class RegexCache {
    static_assert(CAPACITY >= 1, "RegexCache CAPACITY must be at least 1");
    static_assert(CAPACITY <= UINT8_MAX, "RegexCache CAPACITY must fit in uint8_t");

  public:
    RegexCache() {
        for (size_t i = 0; i < CAPACITY; i++) {
            this->order[i] = static_cast<uint8_t>(i);
        }
    }

    RegexCache(const RegexCache&) = delete;
    RegexCache& operator=(const RegexCache&) = delete;

    RegexCache(RegexCache&&) = delete;
    RegexCache& operator=(RegexCache&&) = delete;

    // Release all compiled patterns.
    ~RegexCache() {
        for (Slot& slot : this->slots) {
            clearSlot(slot);
        }
    }

    // --- Matching ---
    // True when pattern is absent (null or empty) or matches the value.

    // Test str against pattern: cache lookup, compile-on-miss, then regexec.
    // str need not be null-terminated (REG_STARTEND). Empty spans never match
    // a present pattern.
    bool match(const char* pattern, StringView str) {
        if (pattern == nullptr || pattern[0] == '\0') {
            return true;
        }

        if (str.isEmpty()) {
            return false;
        }

        const size_t hitPos = findPattern(pattern);
        if (hitPos != CAPACITY) {
            const uint8_t slotIdx = this->order[hitPos];
            promoteMru(hitPos);
            return matchSlot(this->slots[slotIdx], str);
        }

        // Miss: reuse the LRU slot (empty slots gravitate there).
        Slot& slot = this->slots[this->order[CAPACITY - 1]];
        clearSlot(slot);

        if (regcomp(&slot.regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            return false;
        }

        slot.pattern = pattern;
        promoteMru(CAPACITY - 1);

        return matchSlot(slot, str);
    }

  private:
    struct Slot {
        const char* pattern = nullptr; // Cached pattern; null when unused.
        regex_t regex{};               // Compiled form of pattern (valid iff pattern set).
    };

    // --- Storage ---

    std::array<Slot, CAPACITY> slots{};    // Pattern cache entries.
    std::array<uint8_t, CAPACITY> order{}; // Slot indices, most recent first.

    // --- Pattern ---

    // True when two pattern strings denote the same rule.
    static bool patternEquals(const char* a, const char* b) noexcept {
        if (a == b) {
            return true;
        }
        if (a == nullptr || b == nullptr) {
            return false;
        }

        return strcmp(a, b) == 0;
    }

    // --- Slot helpers ---

    // Clear a slot so it can cache another pattern.
    static void clearSlot(Slot& slot) {
        if (slot.pattern != nullptr) {
            regfree(&slot.regex);
        }
        slot.pattern = nullptr;
    }

    // Test a string view against a cached pattern. REG_STARTEND bounds the
    // match to [rm_so, rm_eo), so str does not need a null terminator.
    static bool matchSlot(const Slot& slot, StringView str) noexcept {
        regmatch_t bounds{};
        bounds.rm_so = 0;
        bounds.rm_eo = static_cast<regoff_t>(str.len());

        return regexec(&slot.regex, str.str(), 1, &bounds, REG_STARTEND) == 0;
    }

    // --- Recency order ---

    // Position in order[] of the slot caching pattern, or CAPACITY when absent.
    size_t findPattern(const char* pattern) const noexcept {
        for (size_t pos = 0; pos < CAPACITY; pos++) {
            if (patternEquals(this->slots[this->order[pos]].pattern, pattern)) {
                return pos;
            }
        }

        return CAPACITY;
    }

    // Move the entry at the given order position to the front (most recent).
    void promoteMru(size_t pos) {
        const uint8_t slotIdx = this->order[pos];
        for (size_t i = pos; i > 0; i--) {
            this->order[i] = this->order[i - 1];
        }
        this->order[0] = slotIdx;
    }
};
