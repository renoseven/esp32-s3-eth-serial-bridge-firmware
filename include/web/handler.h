// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web handler layer: the per-domain handler classes behind the HTTP API.
//
// A handler switches on its route's dispatch key and binds wire fields to the
// matching config entities, then sends the reply; it runs to completion
// without blocking. Config domains (device / Wi-Fi / SSH) are self-contained,
// each owning its own query / save / reset flow.

#pragma once

#include <ArduinoJson.h>

// ============================================================
//                     Forward declarations
// ============================================================

class Runtime;
class HttpRequest;

struct Route;
struct DeviceConfig;
struct SshConfig;
struct WifiConfig;

namespace Restful {
    struct Error;
}

// ============================================================
//                        HttpHandler
// ============================================================

// Base class for serving one API domain's routes: reads or mutates state
// through runtime.
class HttpHandler {
  public:
    explicit HttpHandler(Runtime& runtime) : runtime(runtime) {}
    virtual ~HttpHandler() = default;

    HttpHandler(const HttpHandler&) = delete;
    HttpHandler& operator=(const HttpHandler&) = delete;

    // Serve the HTTP request routed to one of this domain's routes.
    virtual void handle(const Route& route, const HttpRequest& request) const = 0;

  protected:
    Runtime& runtime; // Runtime state and services.
};

// ============================================================
//                       ConfigHandler
// ============================================================

// Base for the config domains (device / Wi-Fi / SSH). It owns the shared
// dispatch keyed off ConfigRouteKey and routes each request to one of the three
// per-domain hooks below, which every derived class implements for its own
// config type.
class ConfigHandler : public HttpHandler {
  public:
    explicit ConfigHandler(Runtime& runtime) : HttpHandler(runtime) {}

    // Dispatch on the route's dispatch key to query / save / reset.
    void handle(const Route& route, const HttpRequest& request) const override;

  private:
    // GET fields: reply the current config's {value, constraints} envelope.
    virtual void handleQuery(const Route& route, const HttpRequest& request) const = 0;

    // POST partial config update from the JSON body.
    virtual void handleSave(const Route& route, const HttpRequest& request) const = 0;

    // POST reset to factory defaults.
    virtual void handleReset(const Route& route, const HttpRequest& request) const = 0;
};

// ============================================================
//                      Handler: Assets
// ============================================================

// GET /, /style.css, /script.js (embedded gzip-compressed UI assets).
class AssetHandler final : public HttpHandler {
  public:
    explicit AssetHandler(Runtime& runtime) : HttpHandler(runtime) {}

    // Dispatch on the route's dispatch key to the matching asset.
    void handle(const Route& route, const HttpRequest& request) const override;
};

// ============================================================
//                      Handler: Status
// ============================================================

// GET /status (read-only flat snapshot of device/wifi/ssh state).
class StatusHandler final : public HttpHandler {
  public:
    explicit StatusHandler(Runtime& runtime) : HttpHandler(runtime) {}

    // Serve the flat snapshot of device, Wi-Fi, and SSH runtime state.
    void handle(const Route& route, const HttpRequest& request) const override;
};

// ============================================================
//                      Handler: Device
// ============================================================

// GET /config/device/fields, POST /config/device/{save,reset}.
class DeviceConfigHandler final : public ConfigHandler {
  public:
    explicit DeviceConfigHandler(Runtime& runtime) : ConfigHandler(runtime) {}

  private:
    void handleQuery(const Route& route, const HttpRequest& request) const override;
    void handleSave(const Route& route, const HttpRequest& request) const override;
    void handleReset(const Route& route, const HttpRequest& request) const override;

    // Merge the save request's present fields onto config, validating each.
    const Restful::Error* mergeConfig(DeviceConfig& config, JsonObjectConst body) const;
};

// ============================================================
//                      Handler: Wi-Fi
// ============================================================

// GET /config/wifi/fields, POST /config/wifi/{save,reset}.
class WifiConfigHandler final : public ConfigHandler {
  public:
    explicit WifiConfigHandler(Runtime& runtime) : ConfigHandler(runtime) {}

  private:
    void handleQuery(const Route& route, const HttpRequest& request) const override;
    void handleSave(const Route& route, const HttpRequest& request) const override;
    void handleReset(const Route& route, const HttpRequest& request) const override;

    // Merge the save request's present fields onto config, validating each.
    const Restful::Error* mergeConfig(WifiConfig& config, JsonObjectConst body) const;

    // Cross-field rule on the merged config: the preferred mode must keep a usable SSID.
    const Restful::Error* validateConfig(const WifiConfig& config) const;
};

// ============================================================
//                      Handler: SSH
// ============================================================

// GET /config/ssh/fields, POST /config/ssh/{save,reset}.
class SshConfigHandler final : public ConfigHandler {
  public:
    explicit SshConfigHandler(Runtime& runtime) : ConfigHandler(runtime) {}

  private:
    void handleQuery(const Route& route, const HttpRequest& request) const override;
    void handleSave(const Route& route, const HttpRequest& request) const override;
    void handleReset(const Route& route, const HttpRequest& request) const override;

    // Merge the save request's present fields onto config, validating each.
    const Restful::Error* mergeConfig(SshConfig& config, JsonObjectConst body) const;

    // SSH rule on the merged config: a non-empty authorized key must be a valid OpenSSH public key.
    const Restful::Error* validateConfig(const SshConfig& config) const;
};

// ============================================================
//                      Handler: System
// ============================================================

// POST /factory-reset and POST /reboot (commands, no fields).
class SystemHandler final : public HttpHandler {
  public:
    explicit SystemHandler(Runtime& runtime) : HttpHandler(runtime) {}

    // Dispatch on the route's dispatch key to factory reset or reboot.
    void handle(const Route& route, const HttpRequest& request) const override;
};
