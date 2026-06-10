// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web API domain constants: the wire-contract Field / Route tables for
// every domain. Fields and routes are named constants collected into
// per-domain arrays; Route::dispatchKey carries the domain-defined
// dispatch key that its handler switches on.

#pragma once

#include <cstdint>

#include "firmware.h"

#include "web/assets.h"
#include "web/model.h"
#include "web/restful.h"

// ============================================================
//                     Route dispatch keys
// ============================================================

// Route::dispatchKey values of the asset domain.
enum AssetRouteKey : uint8_t {
    ASSET_ROUTE_INDEX,  // GET the UI page.
    ASSET_ROUTE_STYLE,  // GET the stylesheet.
    ASSET_ROUTE_SCRIPT, // GET the script.
};

// Route::dispatchKey values of the status domain.
enum StatusRouteKey : uint8_t {
    STATUS_ROUTE_QUERY, // GET the runtime state snapshot.
};

// Route::dispatchKey values of a config domain (device / Wi-Fi / SSH).
enum ConfigRouteKey : uint8_t {
    CONFIG_ROUTE_QUERY, // GET fields.
    CONFIG_ROUTE_SAVE,  // POST partial config update.
    CONFIG_ROUTE_RESET, // POST factory defaults.
};

// Route::dispatchKey values of the system domain.
enum SystemRouteKey : uint8_t {
    SYSTEM_ROUTE_FACTORY_RESET, // POST wipe config and reboot.
    SYSTEM_ROUTE_REBOOT,        // POST reboot.
};

// ============================================================
//                     Constants: Assets
// ============================================================

inline constexpr Asset INDEX_ASSET = {
    .mime = Restful::MIME_HTML,
    .data = Assets::INDEX_HTML,
    .len = sizeof(Assets::INDEX_HTML),
};
inline constexpr Asset STYLE_ASSET = {
    .mime = Restful::MIME_CSS,
    .data = Assets::STYLE_CSS,
    .len = sizeof(Assets::STYLE_CSS),
};
inline constexpr Asset SCRIPT_ASSET = {
    .mime = Restful::MIME_JS,
    .data = Assets::SCRIPT_JS,
    .len = sizeof(Assets::SCRIPT_JS),
};

inline constexpr Route ASSET_ROUTES[] = {
    {
        .path = Restful::ROUTE_INDEX,
        .method = Restful::HTTP_GET,
        .dispatchKey = ASSET_ROUTE_INDEX,
        .policy = Restful::RESPONSE_POLICY_STATIC_PAGE,
    },
    {
        .path = Restful::ROUTE_STYLE,
        .method = Restful::HTTP_GET,
        .dispatchKey = ASSET_ROUTE_STYLE,
        .policy = Restful::RESPONSE_POLICY_STATIC_ASSET,
    },
    {
        .path = Restful::ROUTE_SCRIPT,
        .method = Restful::HTTP_GET,
        .dispatchKey = ASSET_ROUTE_SCRIPT,
        .policy = Restful::RESPONSE_POLICY_STATIC_ASSET,
    },
    ROUTE_END,
};

// ============================================================
//                     Constants: Status
// ============================================================

inline constexpr Route STATUS_ROUTES[] = {
    {
        .path = Restful::ROUTE_STATUS,
        .method = Restful::HTTP_GET,
        .dispatchKey = STATUS_ROUTE_QUERY,
        .policy = Restful::RESPONSE_POLICY_API,
    },
    ROUTE_END,
};

// ============================================================
//                     Constants: Device
// ============================================================

inline constexpr Field DEVICE_NAME_FIELD = {
    .key = Restful::WIRE_KEY_DEVICE_NAME,
    .meta =
        {
            .minLen = Firmware::LIMIT_DEVICE_NAME_MIN_LEN,
            .maxLen = Firmware::LIMIT_DEVICE_NAME_MAX_LEN,
            .pattern = Restful::WIRE_PATTERN_DEVICE_NAME,
        },
    .valueError = &Restful::ERR_CONFIG_DEVICE_INVALID_NAME,
};

inline constexpr Route DEVICE_QUERY_ROUTE = {
    .path = Restful::ROUTE_CONFIG_DEVICE_FIELDS,
    .method = Restful::HTTP_GET,
    .dispatchKey = CONFIG_ROUTE_QUERY,
    .policy = Restful::RESPONSE_POLICY_API,
};
inline constexpr Route DEVICE_SAVE_ROUTE = {
    .path = Restful::ROUTE_CONFIG_DEVICE_SAVE,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_SAVE,
    .policy = Restful::RESPONSE_POLICY_API_CLOSE,
    .minPostBodyLen = Restful::POST_BODY_MIN_CONFIG_DEVICE_SAVE,
    .maxPostBodyLen = Restful::POST_BODY_MAX_CONFIG_DEVICE_SAVE,
};
inline constexpr Route DEVICE_RESET_ROUTE = {
    .path = Restful::ROUTE_CONFIG_DEVICE_RESET,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_RESET,
    .policy = Restful::RESPONSE_POLICY_API_CLOSE,
};

inline constexpr Route DEVICE_ROUTES[] = {DEVICE_QUERY_ROUTE, DEVICE_SAVE_ROUTE, DEVICE_RESET_ROUTE, ROUTE_END};

// ============================================================
//                     Constants: Wi-Fi
// ============================================================

inline constexpr FieldMeta WIFI_SSID_META = {
    .minLen = Firmware::LIMIT_WIFI_SSID_MIN_LEN,
    .maxLen = Firmware::LIMIT_WIFI_SSID_MAX_LEN,
};
inline constexpr FieldMeta WIFI_PASSWORD_META = {
    .minLen = Firmware::LIMIT_WIFI_PASSWORD_MIN_LEN,
    .maxLen = Firmware::LIMIT_WIFI_PASSWORD_MAX_LEN,
    .isPassword = true,
    .allowEmpty = true,
};

inline constexpr Field WIFI_PREF_MODE_FIELD = {
    .key = Restful::WIRE_KEY_WIFI_PREF_MODE,
    .meta =
        {
            .minLen = Firmware::LIMIT_WIFI_MODE_MIN_LEN,
            .maxLen = Firmware::LIMIT_WIFI_MODE_MAX_LEN,
            .pattern = Restful::WIRE_PATTERN_WIFI_MODE,
        },
    .valueError = &Restful::ERR_CONFIG_WIFI_INVALID_PREF_MODE,
};
inline constexpr Field WIFI_STA_SSID_FIELD = {
    .key = Restful::WIRE_KEY_WIFI_STA_SSID,
    .meta = WIFI_SSID_META,
    .valueError = &Restful::ERR_CONFIG_WIFI_INVALID_SSID,
};
inline constexpr Field WIFI_STA_PASSWORD_FIELD = {
    .key = Restful::WIRE_KEY_WIFI_STA_PASSWORD,
    .meta = WIFI_PASSWORD_META,
    .valueError = &Restful::ERR_CONFIG_WIFI_INVALID_PASSWORD,
};
inline constexpr Field WIFI_AP_SSID_FIELD = {
    .key = Restful::WIRE_KEY_WIFI_AP_SSID,
    .meta = WIFI_SSID_META,
    .valueError = &Restful::ERR_CONFIG_WIFI_INVALID_SSID,
};
inline constexpr Field WIFI_AP_PASSWORD_FIELD = {
    .key = Restful::WIRE_KEY_WIFI_AP_PASSWORD,
    .meta = WIFI_PASSWORD_META,
    .valueError = &Restful::ERR_CONFIG_WIFI_INVALID_PASSWORD,
};

inline constexpr Route WIFI_QUERY_ROUTE = {
    .path = Restful::ROUTE_CONFIG_WIFI_FIELDS,
    .method = Restful::HTTP_GET,
    .dispatchKey = CONFIG_ROUTE_QUERY,
    .policy = Restful::RESPONSE_POLICY_API,
};
inline constexpr Route WIFI_SAVE_ROUTE = {
    .path = Restful::ROUTE_CONFIG_WIFI_SAVE,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_SAVE,
    .policy = Restful::RESPONSE_POLICY_API_CLOSE,
    .minPostBodyLen = Restful::POST_BODY_MIN_CONFIG_WIFI_SAVE,
    .maxPostBodyLen = Restful::POST_BODY_MAX_CONFIG_WIFI_SAVE,
};
inline constexpr Route WIFI_RESET_ROUTE = {
    .path = Restful::ROUTE_CONFIG_WIFI_RESET,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_RESET,
    .policy = Restful::RESPONSE_POLICY_API_CLOSE,
};

inline constexpr Route WIFI_ROUTES[] = {WIFI_QUERY_ROUTE, WIFI_SAVE_ROUTE, WIFI_RESET_ROUTE, ROUTE_END};

// ============================================================
//                     Constants: SSH
// ============================================================

inline constexpr Field SSH_USERNAME_FIELD = {
    .key = Restful::WIRE_KEY_SSH_USERNAME,
    .meta =
        {
            .minLen = Firmware::LIMIT_SSH_USERNAME_MIN_LEN,
            .maxLen = Firmware::LIMIT_SSH_USERNAME_MAX_LEN,
        },
    .valueError = &Restful::ERR_CONFIG_SSH_INVALID_USERNAME,
};
inline constexpr Field SSH_PASSWORD_FIELD = {
    .key = Restful::WIRE_KEY_SSH_PASSWORD,
    .meta =
        {
            .maxLen = Firmware::LIMIT_SSH_PASSWORD_MAX_LEN,
            .isPassword = true,
            .allowEmpty = true,
        },
    .valueError = &Restful::ERR_CONFIG_SSH_INVALID_PASSWORD,
};
inline constexpr Field SSH_AUTHORIZED_KEY_FIELD = {
    .key = Restful::WIRE_KEY_SSH_AUTHORIZED_KEY,
    .meta =
        {
            .maxLen = Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX_LEN,
            .allowEmpty = true,
        },
    .valueError = &Restful::ERR_CONFIG_SSH_INVALID_AUTHORIZED_KEY,
};
inline constexpr Field SSH_ALLOW_NO_AUTH_FIELD = {
    .key = Restful::WIRE_KEY_SSH_ALLOW_NO_AUTH,
    .valueError = &Restful::ERR_CONFIG_SSH_INVALID_ALLOW_NO_AUTH,
};

inline constexpr Route SSH_QUERY_ROUTE = {
    .path = Restful::ROUTE_CONFIG_SSH_FIELDS,
    .method = Restful::HTTP_GET,
    .dispatchKey = CONFIG_ROUTE_QUERY,
    .policy = Restful::RESPONSE_POLICY_API,
};
inline constexpr Route SSH_SAVE_ROUTE = {
    .path = Restful::ROUTE_CONFIG_SSH_SAVE,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_SAVE,
    .policy = Restful::RESPONSE_POLICY_API,
    .minPostBodyLen = Restful::POST_BODY_MIN_CONFIG_SSH_SAVE,
    .maxPostBodyLen = Restful::POST_BODY_MAX_CONFIG_SSH_SAVE,
};
inline constexpr Route SSH_RESET_ROUTE = {
    .path = Restful::ROUTE_CONFIG_SSH_RESET,
    .method = Restful::HTTP_POST,
    .dispatchKey = CONFIG_ROUTE_RESET,
    .policy = Restful::RESPONSE_POLICY_API,
};

inline constexpr Route SSH_ROUTES[] = {SSH_QUERY_ROUTE, SSH_SAVE_ROUTE, SSH_RESET_ROUTE, ROUTE_END};

// ============================================================
//                     Constants: System
// ============================================================

inline constexpr Route SYSTEM_ROUTES[] = {
    {
        .path = Restful::ROUTE_FACTORY_RESET,
        .method = Restful::HTTP_POST,
        .dispatchKey = SYSTEM_ROUTE_FACTORY_RESET,
        .policy = Restful::RESPONSE_POLICY_API_CLOSE,
    },
    {
        .path = Restful::ROUTE_REBOOT,
        .method = Restful::HTTP_POST,
        .dispatchKey = SYSTEM_ROUTE_REBOOT,
        .policy = Restful::RESPONSE_POLICY_API_CLOSE,
    },
    ROUTE_END,
};
