// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WifiConfigStorage implementation.
//
// Flow:
//   - read(): Nvs::read() with per-key defaults.
//   - update(): Nvs::write().
//   - clear(): erase namespace.
// apSsid default follows the current device name; matching values are omitted from NVS.
// Serialize: fail-fast on first NVS write failure.
// Deserialize: read all keys with defaults; false when apSsid default needs device namespace.

#include "config/wifi.h"

#include <Preferences.h>

#include "firmware.h"

#include "config/device.h"

#include "util/nvs.h"

namespace {
    // --- NVS ---

    constexpr const char NVS_NS[] = "wifi";

    constexpr const char NVS_KEY_PREF_MODE[] = "prefMode";
    constexpr const char NVS_KEY_STA_SSID[] = "staSsid";
    constexpr const char NVS_KEY_STA_PASSWORD[] = "staPassword";
    constexpr const char NVS_KEY_AP_SSID[] = "apSsid";
    constexpr const char NVS_KEY_AP_PASSWORD[] = "apPassword";

    // --- apSsid ---
    // Default AP SSID follows the current device name (device namespace).

    // Read device name from DeviceConfigStorage (cross-namespace).
    bool getDeviceName(String& name) {
        DeviceConfig config;

        if (!DeviceConfigStorage::read(config)) {
            return false;
        }

        name = config.name;
        return true;
    }

    // Persist apSsid; omit the NVS key when it matches the current device name.
    bool putApSsid(Preferences& prefs, const String& apSsid) {
        String deviceName;
        if (!getDeviceName(deviceName)) {
            return false;
        }

        if (apSsid != deviceName) {
            return prefs.putString(NVS_KEY_AP_SSID, apSsid) == apSsid.length();
        }

        return !prefs.isKey(NVS_KEY_AP_SSID) || prefs.remove(NVS_KEY_AP_SSID);
    }

    // Load apSsid from NVS, or derive from the current device name when the key is absent.
    bool getApSsid(Preferences& prefs, String& apSsid) {
        if (!prefs.isKey(NVS_KEY_AP_SSID)) {
            return getDeviceName(apSsid);
        }

        apSsid = prefs.getString(NVS_KEY_AP_SSID);
        return true;
    }

    // --- Serializer ---

    // Write WifiConfig fields into open writable Preferences.
    bool serializeConfig(Preferences& prefs, const WifiConfig& config) {
        if (prefs.putUChar(NVS_KEY_PREF_MODE, static_cast<uint8_t>(config.prefMode)) != sizeof(uint8_t)) {
            return false;
        }
        if (!putApSsid(prefs, config.apSsid)) {
            return false;
        }
        if (prefs.putString(NVS_KEY_AP_PASSWORD, config.apPassword) != config.apPassword.length()) {
            return false;
        }
        if (prefs.putString(NVS_KEY_STA_SSID, config.staSsid) != config.staSsid.length()) {
            return false;
        }
        if (prefs.putString(NVS_KEY_STA_PASSWORD, config.staPassword) != config.staPassword.length()) {
            return false;
        }

        return true;
    }

    // --- Deserializer ---

    // Load WifiConfig fields from open Preferences; missing keys use firmware defaults.
    // apSsid may derive from device namespace; returns false when that read fails.
    bool deserializeConfig(Preferences& prefs, WifiConfig& config) {
        const uint8_t prefMode = prefs.getUChar(NVS_KEY_PREF_MODE, Firmware::DEFAULT_WIFI_PREF_MODE);
        config.prefMode = prefMode <= static_cast<uint8_t>(WifiMode::Ap)
                              ? static_cast<WifiMode>(prefMode)
                              : static_cast<WifiMode>(Firmware::DEFAULT_WIFI_PREF_MODE);
        if (!getApSsid(prefs, config.apSsid)) {
            return false;
        }
        config.apPassword = prefs.getString(NVS_KEY_AP_PASSWORD, Firmware::DEFAULT_WIFI_AP_PASSWORD);
        config.staSsid = prefs.getString(NVS_KEY_STA_SSID, Firmware::DEFAULT_WIFI_STA_SSID);
        config.staPassword = prefs.getString(NVS_KEY_STA_PASSWORD, Firmware::DEFAULT_WIFI_STA_PASSWORD);

        return true;
    }

    // --- Nvs ---

    Nvs<WifiConfig> nvs(NVS_NS, serializeConfig, deserializeConfig);
} // namespace

// ============================================================
//                        WifiConfigStorage
// ============================================================

bool WifiConfigStorage::read(WifiConfig& config) {
    return nvs.read(config);
}

bool WifiConfigStorage::update(const WifiConfig& config) {
    return nvs.write(config);
}

bool WifiConfigStorage::clear() {
    return nvs.clear();
}
