// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Value implementation: constraint validation, patch reads, and the
// {value, constraints} envelope written on GET /config/*/fields.
//
// readText trims the JSON value as a string view, validates the view, and
// copies it into the destination String only when it passes; rejected
// values allocate nothing.

#include "web/value.h"

#include <ArduinoJson.h>
#include <WString.h>

#include "web/model.h"
#include "web/restful.h"

#include "util/regex.h"

namespace {
    constexpr size_t PATTERN_CACHE_CAPACITY = 4;

    constexpr StringView PASSWORD_KEEP = Restful::WIRE_VALUE_PASSWORD_KEEP;

    // Shared compiled-pattern cache; patterns are string literals in field tables.
    RegexCache<PATTERN_CACHE_CAPACITY> patternCache;

    // Start the field's envelope and emit its constraint metadata.
    JsonObject beginEnvelope(JsonObject root, const Field& field) {
        JsonObject entry = root[field.key].to<JsonObject>();

        const FieldMeta& meta = field.meta;
        if (meta.minLen != UNBOUNDED_MIN_LEN) {
            entry[Restful::WIRE_KEY_FIELD_MIN_LENGTH] = meta.minLen;
        }
        if (meta.maxLen != UNBOUNDED_MAX_LEN) {
            entry[Restful::WIRE_KEY_FIELD_MAX_LENGTH] = meta.maxLen;
        }
        if (meta.minValue != UNBOUNDED_MIN_VALUE) {
            entry[Restful::WIRE_KEY_FIELD_MIN_VALUE] = meta.minValue;
        }
        if (meta.maxValue != UNBOUNDED_MAX_VALUE) {
            entry[Restful::WIRE_KEY_FIELD_MAX_VALUE] = meta.maxValue;
        }
        if (meta.pattern != nullptr) {
            entry[Restful::WIRE_KEY_FIELD_PATTERN] = meta.pattern;
        }
        if (meta.allowEmpty) {
            entry[Restful::WIRE_KEY_FIELD_ALLOW_EMPTY] = true;
        }

        return entry;
    }

} // namespace

namespace Value {
    // --- Validate ---

    bool isValidNumber(const Field& field, int64_t value) noexcept {
        return value >= field.meta.minValue && value <= field.meta.maxValue;
    }

    bool isValidText(const Field& field, StringView value) noexcept {
        const FieldMeta& meta = field.meta;

        if (value.len() == 0) {
            return meta.allowEmpty;
        }

        if (value.len() < meta.minLen || value.len() > meta.maxLen) {
            return false;
        }

        // Password fields must never store the "{keep}" sentinel as the secret.
        if (meta.isPassword && value == PASSWORD_KEEP) {
            return false;
        }

        if (meta.pattern != nullptr && !patternCache.match(meta.pattern, value)) {
            return false;
        }

        return true;
    }

    bool isValidText(const Field& field, const String& value) noexcept {
        return isValidText(field, StringView(value));
    }

    // --- Read ---

    bool readBool(bool& dst, const Field& field, JsonObjectConst body) noexcept {
        const JsonVariantConst variant = body[field.key];
        if (variant.isNull()) {
            return true;
        }

        if (!variant.is<bool>()) {
            return false;
        }

        dst = variant.as<bool>();
        return true;
    }

    bool readNumber(int64_t& dst, const Field& field, JsonObjectConst body) noexcept {
        const JsonVariantConst variant = body[field.key];
        if (variant.isNull()) {
            return true;
        }

        if (!variant.is<int64_t>()) {
            return false;
        }

        const int64_t value = variant.as<int64_t>();
        if (!isValidNumber(field, value)) {
            return false;
        }

        dst = value;
        return true;
    }

    bool readText(String& dst, const Field& field, JsonObjectConst body) noexcept {
        const JsonVariantConst variant = body[field.key];
        if (variant.isNull()) {
            return true;
        }

        if (!variant.is<JsonString>()) {
            return false;
        }

        // JsonString carries the stored length; variant.size() is 0 for strings.
        const JsonString str = variant.as<JsonString>();

        StringView value(str.c_str(), str.size());
        value.trim();

        if (!isValidText(field, value)) {
            return false;
        }

        dst = String(value.str(), value.len());
        return true;
    }

    // --- Write (GET fields envelope) ---

    void writeBool(JsonObject root, const Field& field, bool value) {
        beginEnvelope(root, field)[Restful::WIRE_KEY_FIELD_VALUE] = value;
    }

    void writeNumber(JsonObject root, const Field& field, int64_t value) {
        beginEnvelope(root, field)[Restful::WIRE_KEY_FIELD_VALUE] = value;
    }

    void writeText(JsonObject root, const Field& field, const String& value) {
        if (field.meta.isPassword) {
            beginEnvelope(root, field)[Restful::WIRE_KEY_FIELD_VALUE] =
                value.isEmpty() ? Restful::WIRE_VALUE_PASSWORD_EMPTY : Restful::WIRE_VALUE_PASSWORD_KEEP;
            return;
        }

        beginEnvelope(root, field)[Restful::WIRE_KEY_FIELD_VALUE] = value;
    }

} // namespace Value
