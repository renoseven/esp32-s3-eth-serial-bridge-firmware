// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web API contract: routes, JSON wire keys, HTTP policies, and error shapes.
//
// Header-only constants, grouped as:
//   HTTP transport   - status codes, content types, response headers, policies
//   REST routes      - static assets -> status -> config -> system
//   Wire keys        - JSON property names for status and config
//   POST body limits - byte bounds for POST /config/*/save
//   Error responses  - Error struct + ERR_* (type id in problem+json)

#pragma once

#include "firmware.h"

namespace Restful {
    // ============================================================
    //                        HTTP transport
    // ============================================================

    // --- Status codes ---

    inline constexpr int HTTP_OK = 200;
    inline constexpr int HTTP_NO_CONTENT = 204;
    inline constexpr int HTTP_BAD_REQUEST = 400;
    inline constexpr int HTTP_NOT_FOUND = 404;
    inline constexpr int HTTP_PAYLOAD_TOO_LARGE = 413;
    inline constexpr int HTTP_UNPROCESSABLE_ENTITY = 422;
    inline constexpr int HTTP_INTERNAL_SERVER_ERROR = 500;

    // WebRequestMethod-compatible values (AsyncWebRequestMethodType).
    inline constexpr uint32_t HTTP_GET = 1u << 1;
    inline constexpr uint32_t HTTP_POST = 1u << 3;

    // --- Content types ---

    inline constexpr const char MIME_HTML[] = "text/html; charset=utf-8";
    inline constexpr const char MIME_CSS[] = "text/css; charset=utf-8";
    inline constexpr const char MIME_JS[] = "application/javascript; charset=utf-8";
    inline constexpr const char MIME_JSON[] = "application/json; charset=utf-8";
    inline constexpr const char MIME_PROBLEM_JSON[] = "application/problem+json; charset=utf-8";

    // --- Response headers ---

    // Response header descriptor;
    // Response header policy is a HEADER_END-terminated array.
    struct ResponseHeader {
        const char* key;
        const char* value;
    };

    inline constexpr ResponseHeader HEADER_NOSNIFF = {"X-Content-Type-Options", "nosniff"};
    inline constexpr ResponseHeader HEADER_GZIP = {"Content-Encoding", "gzip"};
    inline constexpr ResponseHeader HEADER_NO_CACHE = {"Cache-Control", "no-cache"};
    inline constexpr ResponseHeader HEADER_CACHE_SHORT = {"Cache-Control", "public, max-age=180"};
    inline constexpr ResponseHeader HEADER_CACHE_IMMUTABLE = {"Cache-Control", "public, max-age=31536000, immutable"};
    inline constexpr ResponseHeader HEADER_CONNECTION_CLOSE = {"Connection", "close"};
    inline constexpr ResponseHeader HEADER_END = {nullptr, nullptr};

    // --- Response policies ---

    inline constexpr ResponseHeader RESPONSE_POLICY_STATIC_PAGE[] = {HEADER_NOSNIFF, HEADER_CACHE_SHORT, HEADER_GZIP,
                                                                     HEADER_END};

    inline constexpr ResponseHeader RESPONSE_POLICY_STATIC_ASSET[] = {HEADER_NOSNIFF, HEADER_CACHE_IMMUTABLE,
                                                                      HEADER_GZIP, HEADER_END};

    inline constexpr ResponseHeader RESPONSE_POLICY_API[] = {HEADER_NOSNIFF, HEADER_NO_CACHE, HEADER_END};

    // API policy for routes whose success response also closes the connection.
    inline constexpr ResponseHeader RESPONSE_POLICY_API_CLOSE[] = {HEADER_NOSNIFF, HEADER_NO_CACHE,
                                                                   HEADER_CONNECTION_CLOSE, HEADER_END};

    // ============================================================
    //                         REST routes
    // ============================================================

    // --- Static assets ---

    inline constexpr const char ROUTE_INDEX[] = "/";
    inline constexpr const char ROUTE_STYLE[] = "/style.css";
    inline constexpr const char ROUTE_SCRIPT[] = "/script.js";

    // --- GET /status ---

    inline constexpr const char ROUTE_STATUS[] = "/status";

    // --- GET /config/device/fields ---

    inline constexpr const char ROUTE_CONFIG_DEVICE_FIELDS[] = "/config/device/fields";

    // --- POST /config/device/save ---
    // Success: 204 No Content. Response may use Connection: close.

    inline constexpr const char ROUTE_CONFIG_DEVICE_SAVE[] = "/config/device/save";

    // --- POST /config/device/reset ---
    // Success: 204 No Content. Response may use Connection: close.

    inline constexpr const char ROUTE_CONFIG_DEVICE_RESET[] = "/config/device/reset";

    // --- GET /config/wifi/fields ---

    inline constexpr const char ROUTE_CONFIG_WIFI_FIELDS[] = "/config/wifi/fields";

    // --- POST /config/wifi/save ---
    // Success: 204 No Content. Response may use Connection: close.

    inline constexpr const char ROUTE_CONFIG_WIFI_SAVE[] = "/config/wifi/save";

    // --- POST /config/wifi/reset ---
    // Success: 204 No Content. Response may use Connection: close.

    inline constexpr const char ROUTE_CONFIG_WIFI_RESET[] = "/config/wifi/reset";

    // --- GET /config/ssh/fields ---

    inline constexpr const char ROUTE_CONFIG_SSH_FIELDS[] = "/config/ssh/fields";

    // --- POST /config/ssh/save ---
    // Success: 204 No Content.

    inline constexpr const char ROUTE_CONFIG_SSH_SAVE[] = "/config/ssh/save";

    // --- POST /config/ssh/reset ---
    // Success: 204 No Content.

    inline constexpr const char ROUTE_CONFIG_SSH_RESET[] = "/config/ssh/reset";

    // --- POST /factory-reset ---
    // Success: 204 No Content, empty body. Response uses Connection: close.

    inline constexpr const char ROUTE_FACTORY_RESET[] = "/factory-reset";

    // --- POST /reboot ---
    // Success: 204 No Content, empty body. Response uses Connection: close.

    inline constexpr const char ROUTE_REBOOT[] = "/reboot";

    // ============================================================
    //                          Wire keys
    // ============================================================
    //
    // JSON property names on GET responses and POST bodies.
    // Constant prefix: WIRE_KEY_<area>_<name>
    //
    // GET /config/*/fields: top-level keys are field names. Each value is an object with optional
    //   WIRE_KEY_FIELD_VALUE, WIRE_KEY_FIELD_MIN_LENGTH, WIRE_KEY_FIELD_MAX_LENGTH,
    //   WIRE_KEY_FIELD_MIN_VALUE, WIRE_KEY_FIELD_MAX_VALUE, WIRE_KEY_FIELD_PATTERN,
    //   WIRE_KEY_FIELD_ALLOW_EMPTY.
    // POST /config/*/save: top-level keys are field names; values are wire values
    //   (partial update - only changed fields required).
    // ============================================================

    // --- GET /config/*/fields - per-field object ---

    inline constexpr const char WIRE_KEY_FIELD_VALUE[] = "value";
    inline constexpr const char WIRE_KEY_FIELD_MIN_LENGTH[] = "minLength";
    inline constexpr const char WIRE_KEY_FIELD_MAX_LENGTH[] = "maxLength";
    inline constexpr const char WIRE_KEY_FIELD_MIN_VALUE[] = "minValue";
    inline constexpr const char WIRE_KEY_FIELD_MAX_VALUE[] = "maxValue";
    inline constexpr const char WIRE_KEY_FIELD_PATTERN[] = "pattern";
    inline constexpr const char WIRE_KEY_FIELD_ALLOW_EMPTY[] = "allowEmpty";

    // GET /config/*/fields - WIRE_KEY_FIELD_VALUE for password keys:
    //   WIRE_VALUE_PASSWORD_EMPTY when none stored;
    //   WIRE_VALUE_PASSWORD_KEEP when stored (not the secret).
    // POST /config/*/save must not send WIRE_VALUE_PASSWORD_KEEP; omit the key to keep stored value.
    inline constexpr const char WIRE_VALUE_PASSWORD_EMPTY[] = "";
    inline constexpr const char WIRE_VALUE_PASSWORD_KEEP[] = "{keep}";

    // --- GET /status ---

    inline constexpr const char WIRE_KEY_STATUS_DEVICE_NAME[] = "deviceName";
    inline constexpr const char WIRE_KEY_STATUS_DEVICE_FW_VERSION[] = "deviceFwVersion";
    inline constexpr const char WIRE_KEY_STATUS_DEVICE_SDK_VERSION[] = "deviceSdkVersion";
    inline constexpr const char WIRE_KEY_STATUS_DEVICE_FREE_MEM[] = "deviceFreeMem";
    inline constexpr const char WIRE_KEY_STATUS_DEVICE_TOTAL_MEM[] = "deviceTotalMem";
    inline constexpr const char WIRE_KEY_STATUS_DEVICE_UPTIME[] = "deviceUptime";
    inline constexpr const char WIRE_KEY_STATUS_WIFI_MODE[] = "wifiMode";
    inline constexpr const char WIRE_KEY_STATUS_WIFI_RSSI[] = "wifiRssi";
    inline constexpr const char WIRE_KEY_STATUS_WIFI_MAC_ADDR[] = "wifiMacAddr";
    inline constexpr const char WIRE_KEY_STATUS_WIFI_IPV4_ADDR[] = "wifiIpv4Addr";
    inline constexpr const char WIRE_KEY_STATUS_WIFI_IPV6_ADDRS[] = "wifiIpv6Addrs";
    inline constexpr const char WIRE_KEY_STATUS_SSH_CONNECTED[] = "sshConnected";
    inline constexpr const char WIRE_KEY_STATUS_SSH_HOST_KEY[] = "sshHostKey";

    // --- GET/POST /config/device/* ---

    inline constexpr const char WIRE_KEY_DEVICE_NAME[] = "name";
    inline constexpr const char WIRE_PATTERN_DEVICE_NAME[] = "^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?$";

    // --- GET/POST /config/wifi/* ---

    inline constexpr const char WIRE_KEY_WIFI_PREF_MODE[] = "prefMode";
    inline constexpr const char WIRE_KEY_WIFI_STA_SSID[] = "staSsid";
    inline constexpr const char WIRE_KEY_WIFI_STA_PASSWORD[] = "staPassword";
    inline constexpr const char WIRE_KEY_WIFI_AP_SSID[] = "apSsid";
    inline constexpr const char WIRE_KEY_WIFI_AP_PASSWORD[] = "apPassword";

    // WIRE_VALUE_WIFI_MODE_* - wire values for WIRE_KEY_WIFI_PREF_MODE and WIRE_KEY_STATUS_WIFI_MODE.
    inline constexpr const char WIRE_VALUE_WIFI_MODE_OFF[] = "off";
    inline constexpr const char WIRE_VALUE_WIFI_MODE_STA[] = "sta";
    inline constexpr const char WIRE_VALUE_WIFI_MODE_AP[] = "ap";
    inline constexpr const char WIRE_PATTERN_WIFI_MODE[] = "^(off|sta|ap)$";

    // --- GET/POST /config/ssh/* ---

    inline constexpr const char WIRE_KEY_SSH_USERNAME[] = "username";
    inline constexpr const char WIRE_KEY_SSH_PASSWORD[] = "password";
    inline constexpr const char WIRE_KEY_SSH_AUTHORIZED_KEY[] = "authorizedKey";
    inline constexpr const char WIRE_KEY_SSH_ALLOW_NO_AUTH[] = "allowNoAuth";

    // ============================================================
    //                       POST body limits
    // ============================================================
    //
    // Raw POST body byte bounds per save route (not exposed on the wire).
    // Saves are partial (only dirty keys), so:
    //   POST_BODY_MIN_*        - shortest single-field body that can be posted
    //   POST_BODY_FULL_EMPTY_* - all-keys body with empty/minimal values
    //   POST_BODY_MAX_*        - FULL_EMPTY + field string limits
    // ============================================================

    // --- POST /config/device/save ---
    // Shortest: {"name":"a"}
    // Full empty: {"name":""}

    inline constexpr size_t POST_BODY_MIN_CONFIG_DEVICE_SAVE = 12;
    inline constexpr size_t POST_BODY_FULL_EMPTY_CONFIG_DEVICE_SAVE = 11;
    inline constexpr size_t POST_BODY_MAX_CONFIG_DEVICE_SAVE =
        POST_BODY_FULL_EMPTY_CONFIG_DEVICE_SAVE + Firmware::LIMIT_DEVICE_NAME_MAX_LEN;

    // --- POST /config/wifi/save ---
    // Shortest: {"apSsid":"a"}
    // Full empty (longest prefMode): {"prefMode":"off","staSsid":"","staPassword":"","apSsid":"","apPassword":""}

    inline constexpr size_t POST_BODY_MIN_CONFIG_WIFI_SAVE = 14;
    inline constexpr size_t POST_BODY_FULL_EMPTY_CONFIG_WIFI_SAVE = 76;
    inline constexpr size_t POST_BODY_MAX_CONFIG_WIFI_SAVE = POST_BODY_FULL_EMPTY_CONFIG_WIFI_SAVE +
                                                             Firmware::LIMIT_WIFI_SSID_MAX_LEN * 2 +
                                                             Firmware::LIMIT_WIFI_PASSWORD_MAX_LEN * 2;

    // --- POST /config/ssh/save ---
    // Shortest: {"password":""}
    // Full empty: {"username":"","password":"","authorizedKey":"","allowNoAuth":false}

    inline constexpr size_t POST_BODY_MIN_CONFIG_SSH_SAVE = 15;
    inline constexpr size_t POST_BODY_FULL_EMPTY_CONFIG_SSH_SAVE = 68;
    inline constexpr size_t POST_BODY_MAX_CONFIG_SSH_SAVE =
        POST_BODY_FULL_EMPTY_CONFIG_SSH_SAVE + Firmware::LIMIT_SSH_USERNAME_MAX_LEN +
        Firmware::LIMIT_SSH_PASSWORD_MAX_LEN + Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX_LEN;

    // ============================================================
    //                       Error responses
    // ============================================================
    //
    // Success:
    //   GET  *                         -> 200 OK, application/json body
    //   POST /config/*/save,
    //        /config/*/reset,
    //        /reboot,
    //        /factory-reset            -> 204 No Content, empty body
    //
    // Failure:
    //   Error.status                   -> HTTP status code
    //   Error.body                     -> application/problem+json
    //                                     {"type":"<typeId>"}
    //   <typeId>                       -> camelCase string; see ERR_* below
    //
    // ERR_REQUEST_*  - inbound JSON: body size (WebServer), syntax/capacity (parseJson).
    // ERR_RESPONSE_* - outbound: response capacity, persistence or runtime command failure.
    // ERR_CONFIG_*   - domain field validation on POST /config/*/save.
    // ============================================================

    struct Error {
        int status;       // HTTP status code
        const char* body; // pre-built application/problem+json payload
    };

    // --- Request ---

    inline constexpr Error ERR_REQUEST_EMPTY = {HTTP_BAD_REQUEST, R"({"type":"requestEmpty"})"};
    inline constexpr Error ERR_REQUEST_TOO_SMALL = {HTTP_BAD_REQUEST, R"({"type":"requestTooSmall"})"};
    inline constexpr Error ERR_REQUEST_TOO_LARGE = {HTTP_PAYLOAD_TOO_LARGE, R"({"type":"requestTooLarge"})"};
    inline constexpr Error ERR_REQUEST_INVALID_FORMAT = {HTTP_BAD_REQUEST, R"({"type":"requestInvalidFormat"})"};
    inline constexpr Error ERR_REQUEST_OVERFLOW = {HTTP_UNPROCESSABLE_ENTITY, R"({"type":"requestOverflow"})"};

    // --- Response ---

    inline constexpr Error ERR_RESPONSE_OVERFLOW = {HTTP_INTERNAL_SERVER_ERROR, R"({"type":"responseOverflow"})"};
    inline constexpr Error ERR_RESPONSE_FAILED = {HTTP_INTERNAL_SERVER_ERROR, R"({"type":"responseFailed"})"};

    // --- Device Config ---

    inline constexpr Error ERR_CONFIG_DEVICE_INVALID_NAME = {HTTP_UNPROCESSABLE_ENTITY,
                                                             R"({"type":"configDeviceInvalidName"})"};

    // --- Wi-Fi Config ---

    inline constexpr Error ERR_CONFIG_WIFI_INVALID_PREF_MODE = {HTTP_UNPROCESSABLE_ENTITY,
                                                                R"({"type":"configWifiInvalidPrefMode"})"};
    inline constexpr Error ERR_CONFIG_WIFI_INVALID_SSID = {HTTP_UNPROCESSABLE_ENTITY,
                                                           R"({"type":"configWifiInvalidSsid"})"};
    inline constexpr Error ERR_CONFIG_WIFI_INVALID_PASSWORD = {HTTP_UNPROCESSABLE_ENTITY,
                                                               R"({"type":"configWifiInvalidPassword"})"};

    // --- SSH Config ---

    inline constexpr Error ERR_CONFIG_SSH_INVALID_USERNAME = {HTTP_UNPROCESSABLE_ENTITY,
                                                              R"({"type":"configSshInvalidUsername"})"};
    inline constexpr Error ERR_CONFIG_SSH_INVALID_PASSWORD = {HTTP_UNPROCESSABLE_ENTITY,
                                                              R"({"type":"configSshInvalidPassword"})"};
    inline constexpr Error ERR_CONFIG_SSH_INVALID_AUTHORIZED_KEY = {HTTP_UNPROCESSABLE_ENTITY,
                                                                    R"({"type":"configSshInvalidAuthorizedKey"})"};
    inline constexpr Error ERR_CONFIG_SSH_INVALID_ALLOW_NO_AUTH = {HTTP_UNPROCESSABLE_ENTITY,
                                                                   R"({"type":"configSshInvalidAllowNoAuth"})"};

} // namespace Restful
