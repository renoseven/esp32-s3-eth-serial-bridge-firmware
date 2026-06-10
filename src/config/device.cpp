// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// DeviceConfigStorage implementation.
//
// Flow:
//   - read(): Nvs::read() with per-key defaults.
//   - update(): Nvs::write().
//   - clear(): erase namespace.
// Default name when absent: Firmware::DEFAULT_DEVICE_NAME_PREFIX + eFuse MAC low 16 bits.
// Serialize: fail-fast on first NVS write failure.
// Deserialize: read all keys with defaults.

#include "config/device.h"

#include <Arduino.h>
#include <Preferences.h>

#include "firmware.h"

#include "util/nvs.h"

namespace {
    // --- NVS ---

    constexpr const char NVS_NS[] = "device";

    constexpr const char NVS_KEY_DEVICE_NAME[] = "name";

    // --- defaults ---

    // Return the hardware-derived device id (eFuse MAC low 16 bits).
    uint16_t deviceId() {
        return static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFu);
    }

    // Default device name: base name + device id.
    String defaultDeviceName() {
        char buf[Firmware::LIMIT_DEVICE_NAME_MAX_LEN + 1];
        snprintf(buf, sizeof(buf), "%s%04X", Firmware::DEFAULT_DEVICE_NAME_PREFIX, deviceId());

        return String(buf);
    }

    // --- Serializer ---

    // Write DeviceConfig fields into open writable Preferences.
    bool serializeConfig(Preferences& prefs, const DeviceConfig& config) {
        if (prefs.putString(NVS_KEY_DEVICE_NAME, config.name) != config.name.length()) {
            return false;
        }

        return true;
    }

    // --- Deserializer ---

    // Load DeviceConfig fields from open Preferences; missing name uses hardware default.
    bool deserializeConfig(Preferences& prefs, DeviceConfig& config) {
        config.name = prefs.getString(NVS_KEY_DEVICE_NAME, defaultDeviceName());

        return true;
    }

    // --- Nvs ---

    Nvs<DeviceConfig> nvs(NVS_NS, serializeConfig, deserializeConfig);
} // namespace

// ============================================================
//                          DeviceConfigStorage
// ============================================================

bool DeviceConfigStorage::read(DeviceConfig& config) {
    return nvs.read(config);
}

bool DeviceConfigStorage::update(const DeviceConfig& config) {
    return nvs.write(config);
}

bool DeviceConfigStorage::clear() {
    return nvs.clear();
}
