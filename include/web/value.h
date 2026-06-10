// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Value: read / validate / write behavior over Field descriptors.
//
// Covers the three field concerns per wire type (Text / Boolean / Number):
//   isValid* - constraint check of one already-read value
//   read*    - copy the field's wire value into the config member, then
//              validate the copy; absent fields are left unchanged
//   write*   - config value -> {value, constraints} envelope for GET fields
//
// Parameter order is uniform: the written destination comes first (dst,
// field, src); validators produce no output and take (field, value).
// Not thread-safe (single AsyncTCP task).

#pragma once

#include <cstdint>

#include <ArduinoJson.h>

#include "util/string.h"

// ============================================================
//                     Forward declarations
// ============================================================

class String;

struct Field;

namespace Value {

    // --- Validate ---
    // Constraint checks of one already-read value; true when it passes.

    // Number: within meta.minValue / maxValue.
    bool isValidNumber(const Field& field, int64_t value) noexcept;

    // Text: length bounds, pattern, allowEmpty of an already-trimmed span;
    // passwords reject WIRE_VALUE_PASSWORD_KEEP. str() need not be null-terminated.
    bool isValidText(const Field& field, StringView value) noexcept;

    // Same checks for an owned String (e.g. config cross-field validation).
    bool isValidText(const Field& field, const String& value) noexcept;

    // --- Read ---
    // Flow: absent field -> no-op (true, dst unchanged); present field ->
    // read, validate, then store into dst. false leaves dst unchanged;
    // callers reply with field.valueError.

    // Read the field's bool value from body into dst and validate it.
    bool readBool(bool& dst, const Field& field, JsonObjectConst body) noexcept;

    // Read the field's numeric value from body into dst and validate it.
    bool readNumber(int64_t& dst, const Field& field, JsonObjectConst body) noexcept;

    // Read the field's trimmed text value from body into dst and validate it.
    bool readText(String& dst, const Field& field, JsonObjectConst body) noexcept;

    // --- Write ---

    // Write the field's {value, constraints} envelope for a bool value.
    void writeBool(JsonObject root, const Field& field, bool value);

    // Write the field's {value, constraints} envelope for a numeric value.
    void writeNumber(JsonObject root, const Field& field, int64_t value);

    // Write the field's {value, constraints} envelope into root. Password
    // fields expose WIRE_VALUE_PASSWORD_EMPTY / _KEEP, never the secret.
    void writeText(JsonObject root, const Field& field, const String& value);

} // namespace Value
