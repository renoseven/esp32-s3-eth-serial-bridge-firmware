// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Wi-Fi configuration value object and storage API.

#pragma once

#include <cstdint>

#include <WString.h>

#include "firmware.h"

// ============================================================
//                          WifiMode
// ============================================================

// Wi-Fi radio mode.
enum class WifiMode : uint8_t {
    Off = 0, // WIFI_MODE_NULL - Wi-Fi disabled.
    Sta = 1, // WIFI_MODE_STA - connect using saved SSID.
    Ap = 2,  // WIFI_MODE_AP - configuration softAP.
};

static_assert(static_cast<uint8_t>(WifiMode::Ap) == Firmware::DEFAULT_WIFI_PREF_MODE,
              "DEFAULT_WIFI_PREF_MODE must match WifiMode::Ap");

// ============================================================
//                          WifiConfig
// ============================================================

// Wi-Fi configuration plain value object.
struct WifiConfig {
    WifiMode prefMode;  // Preferred radio mode.
    String apSsid;      // AP SSID.
    String apPassword;  // AP passphrase.
    String staSsid;     // STA SSID.
    String staPassword; // STA passphrase.
};

// ============================================================
//                           WifiConfigStorage
// ============================================================

namespace WifiConfigStorage {
    // Load from storage into config.
    bool read(WifiConfig& config);

    // Replace persisted config.
    bool update(const WifiConfig& config);

    // Erase all persisted keys.
    bool clear();
} // namespace WifiConfigStorage
