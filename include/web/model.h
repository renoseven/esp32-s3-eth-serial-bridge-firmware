// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web API data model: pure-data descriptors for wire fields, routes, and
// static assets.
//
// Field describes one JSON property of the wire contract - its key,
// declarative constraints, and error shape. Route carries a domain-defined
// dispatch key and the success response policy. Asset is a pure payload
// selected by its route's dispatch key. These types hold data only.

#pragma once

#include <cstddef>
#include <cstdint>

// ============================================================
//                     Forward declarations
// ============================================================

namespace Restful {
    struct Error;
    struct ResponseHeader;
} // namespace Restful

// ============================================================
//                         Unbounded bounds
// ============================================================

// Unconstrained bounds are identity elements (min: lowest, max: highest):
// they pass plain comparisons, so range checks need no sentinel branch.
inline constexpr size_t UNBOUNDED_MIN_LEN = 0;
inline constexpr size_t UNBOUNDED_MAX_LEN = SIZE_MAX;
inline constexpr int64_t UNBOUNDED_MIN_VALUE = INT64_MIN;
inline constexpr int64_t UNBOUNDED_MAX_VALUE = INT64_MAX;

// ============================================================
//                            Route
// ============================================================

// Routed operation a domain serves; route tables are ROUTE_END-terminated.
// Handlers dispatch on dispatchKey; what a key value means stays
// domain knowledge.
struct Route {
    const char* path;                          // HTTP path (required).
    uint32_t method;                           // HTTP_GET / HTTP_POST (WebRequestMethod value, required).
    uint32_t dispatchKey;                      // Domain-defined dispatch key (required).
    const Restful::ResponseHeader* policy;     // Response header policy (required).
    size_t minPostBodyLen = UNBOUNDED_MIN_LEN; // Save: body byte lower bound.
    size_t maxPostBodyLen = UNBOUNDED_MAX_LEN; // Save: body byte upper bound.
};

// Route-table terminator: path == nullptr.
inline constexpr Route ROUTE_END = {};

// ============================================================
//                            Asset
// ============================================================

// Static asset payload served over HTTP; selected by its route's dispatch key.
struct Asset {
    const char* mime;    // Content type.
    const uint8_t* data; // PROGMEM payload.
    size_t len;          // Payload byte count.
};

// ============================================================
//                            Field
// ============================================================

// Declarative constraints for a wire field.
// Exposed as field metadata on GET /config/*/fields and enforced on POST bodies.
struct FieldMeta {
    size_t minLen = UNBOUNDED_MIN_LEN;      // Text: minimum trimmed length.
    size_t maxLen = UNBOUNDED_MAX_LEN;      // Text: maximum trimmed length.
    int64_t minValue = UNBOUNDED_MIN_VALUE; // Number: minimum value.
    int64_t maxValue = UNBOUNDED_MAX_VALUE; // Number: maximum value.
    const char* pattern = nullptr;          // Text: POSIX ERE; null matches all.
    bool isPassword = false;                // Text: mask on output; reject WIRE_VALUE_PASSWORD_KEEP on input.
    bool allowEmpty = false;                // Text: accept empty string regardless of minLen.
};

// Wire field descriptor of a domain.
struct Field {
    const char* key;                  // Wire key (required).
    FieldMeta meta{};                 // Constraints (defaults: unconstrained).
    const Restful::Error* valueError; // Returned when constraint validation fails (required).
};
