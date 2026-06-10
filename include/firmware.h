// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Project-wide compile-time constants.

#pragma once

#include <cstddef>
#include <cstdint>

#define FW_VERSION_NAME "v0.1a"

#ifndef FW_VERSION_ID
#define FW_VERSION_ID "dev"
#endif

#define FW_VERSION FW_VERSION_NAME " (" FW_VERSION_ID ")"

namespace Firmware {

    // --- Version ---

    inline constexpr const char VERSION[] = FW_VERSION;

    // --- MCU ---

    inline constexpr size_t CACHE_LINE_SIZE = 32;
    inline constexpr uint8_t CORE_PROTOCOL = 0; // Protocol stack.
    inline constexpr uint8_t CORE_SERVICE = 1;  // Arduino loop / application.

    // --- Led ---

    inline constexpr uint8_t LED_PIN = 21;
    inline constexpr uint8_t LED_BRIGHTNESS = 8;

    // --- Network ---

    inline constexpr uint16_t NET_PORT_HTTP = 80;
    inline constexpr uint16_t NET_PORT_SSH = 22;
    inline constexpr uint16_t NET_PORT_DNS = 53; // Captive portal (AP mode).

    // --- Bridge ---

    inline constexpr uint32_t BRIDGE_POLL_INTERVAL_MS = 1;

    // --- Wi-Fi ---

    inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
    inline constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
    inline constexpr uint32_t WIFI_FALLBACK_AP_TIMEOUT_MS = 60000;
    inline constexpr uint32_t WIFI_FALLBACK_STA_INTERVAL_MS = 600000;

    // --- Deferred apply ---

    inline constexpr uint32_t WIFI_RESTART_DELAY_MS = 1500;
    inline constexpr uint32_t SSH_RESTART_DELAY_MS = 500;
    inline constexpr uint32_t REBOOT_DELAY_MS = 2000;

    // --- Factory defaults ---

    inline constexpr const char DEFAULT_DEVICE_NAME_PREFIX[] = "SerialBridge-";

    inline constexpr const char DEFAULT_WIFI_STA_SSID[] = ""; // No saved STA network.
    inline constexpr const char DEFAULT_WIFI_STA_PASSWORD[] = "";
    inline constexpr const char DEFAULT_WIFI_AP_PASSWORD[] = "12345678";
    inline constexpr uint8_t DEFAULT_WIFI_PREF_MODE = 2; // WifiMode::Ap.

    inline constexpr const char DEFAULT_SSH_HOST_KEY[] = ""; // Generate on first SSH start.
    inline constexpr const char DEFAULT_SSH_USERNAME[] = "admin";
    inline constexpr const char DEFAULT_SSH_PASSWORD[] = "admin";
    inline constexpr const char DEFAULT_SSH_AUTHORIZED_KEY[] = ""; // No authorized key.
    inline constexpr bool DEFAULT_SSH_ALLOW_NO_AUTH = false;

    // --- Validation limits ---

    inline constexpr size_t LIMIT_DEVICE_NAME_MIN_LEN = 1;
    inline constexpr size_t LIMIT_DEVICE_NAME_MAX_LEN = 32;

    inline constexpr size_t LIMIT_WIFI_MODE_MIN_LEN = 2; // "ap" / "sta" / "off".
    inline constexpr size_t LIMIT_WIFI_MODE_MAX_LEN = 3;
    inline constexpr size_t LIMIT_WIFI_SSID_MIN_LEN = 1;
    inline constexpr size_t LIMIT_WIFI_SSID_MAX_LEN = 32;
    inline constexpr size_t LIMIT_WIFI_PASSWORD_MIN_LEN = 8;
    inline constexpr size_t LIMIT_WIFI_PASSWORD_MAX_LEN = 63;

    inline constexpr size_t LIMIT_SSH_USERNAME_MIN_LEN = 1;
    inline constexpr size_t LIMIT_SSH_USERNAME_MAX_LEN = 32;
    inline constexpr size_t LIMIT_SSH_PASSWORD_MAX_LEN = 64;
    inline constexpr size_t LIMIT_SSH_KEY_TYPE_MAX_LEN = 64;
    inline constexpr size_t LIMIT_SSH_AUTHORIZED_KEY_MAX_LEN = 1536;

} // namespace Firmware
