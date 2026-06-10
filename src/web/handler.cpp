// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web handler implementation: Assets, Status, Device, Wi-Fi, SSH, System.

#include "web/handler.h"

#include <set>
#include <utility>

#include <ArduinoJson.h>
#include <WString.h>

#include "config/device.h"
#include "config/ssh.h"
#include "config/wifi.h"

#include "runtime.h"

#include "service/device.h"
#include "service/ssh.h"
#include "service/wifi.h"

#include "web/domain.h"
#include "web/model.h"
#include "web/request.h"
#include "web/restful.h"
#include "web/value.h"

// ============================================================
//                       Wi-Fi mode wire
// ============================================================

namespace {

    // Wire "off"/"sta"/"ap" string -> WifiMode (unknown text -> Off).
    WifiMode wifiModeFromWire(const String& text) {
        if (text.equals(Restful::WIRE_VALUE_WIFI_MODE_STA)) {
            return WifiMode::Sta;
        }
        if (text.equals(Restful::WIRE_VALUE_WIFI_MODE_AP)) {
            return WifiMode::Ap;
        }

        return WifiMode::Off;
    }

    // WifiMode -> wire "off"/"sta"/"ap" string.
    const char* wifiModeToWire(WifiMode mode) {
        switch (mode) {
        case WifiMode::Sta:
            return Restful::WIRE_VALUE_WIFI_MODE_STA;
        case WifiMode::Ap:
            return Restful::WIRE_VALUE_WIFI_MODE_AP;
        default:
            return Restful::WIRE_VALUE_WIFI_MODE_OFF;
        }
    }

} // namespace

// ============================================================
//                            Assets
// ============================================================

void AssetHandler::handle(const Route& route, const HttpRequest& request) const {
    switch (route.dispatchKey) {
    case ASSET_ROUTE_INDEX:
        request.replyAsset(route, INDEX_ASSET);
        break;
    case ASSET_ROUTE_STYLE:
        request.replyAsset(route, STYLE_ASSET);
        break;
    case ASSET_ROUTE_SCRIPT:
        request.replyAsset(route, SCRIPT_ASSET);
        break;
    default:
        break;
    }
}

// ============================================================
//                            Status
// ============================================================

void StatusHandler::handle(const Route& route, const HttpRequest& request) const {
    const DeviceService& device = this->runtime.getDeviceService();
    const WifiService& wifi = this->runtime.getWifiService();
    const SshService& ssh = this->runtime.getSshService();

    JsonDocument doc;
    doc[Restful::WIRE_KEY_STATUS_DEVICE_NAME] = device.getName();
    doc[Restful::WIRE_KEY_STATUS_DEVICE_FW_VERSION] = device.getFwVersion();
    doc[Restful::WIRE_KEY_STATUS_DEVICE_SDK_VERSION] = device.getSdkVersion();
    doc[Restful::WIRE_KEY_STATUS_DEVICE_FREE_MEM] = device.getFreeMemory();
    doc[Restful::WIRE_KEY_STATUS_DEVICE_TOTAL_MEM] = device.getTotalMemory();
    doc[Restful::WIRE_KEY_STATUS_DEVICE_UPTIME] = device.getUptime();
    doc[Restful::WIRE_KEY_STATUS_WIFI_MODE] = wifiModeToWire(wifi.getActiveMode());
    doc[Restful::WIRE_KEY_STATUS_WIFI_RSSI] = wifi.getRssi();
    doc[Restful::WIRE_KEY_STATUS_WIFI_MAC_ADDR] = wifi.getMacAddr();
    doc[Restful::WIRE_KEY_STATUS_WIFI_IPV4_ADDR] = wifi.getIpv4Addr();
    JsonArray ipv6Addrs = doc[Restful::WIRE_KEY_STATUS_WIFI_IPV6_ADDRS].to<JsonArray>();
    const std::set<String>& ipv6AddrSet = wifi.getIpv6Addrs();
    for (auto it = ipv6AddrSet.rbegin(); it != ipv6AddrSet.rend(); ++it) {
        ipv6Addrs.add(*it);
    }
    doc[Restful::WIRE_KEY_STATUS_SSH_CONNECTED] = ssh.isConnected();
    doc[Restful::WIRE_KEY_STATUS_SSH_HOST_KEY] = ssh.getHostKey();

    request.replyJson(route, doc);
}

// ============================================================
//                        ConfigHandler
// ============================================================

void ConfigHandler::handle(const Route& route, const HttpRequest& request) const {
    switch (route.dispatchKey) {
    case CONFIG_ROUTE_QUERY:
        this->handleQuery(route, request);
        break;
    case CONFIG_ROUTE_SAVE:
        this->handleSave(route, request);
        break;
    case CONFIG_ROUTE_RESET:
        this->handleReset(route, request);
        break;
    default:
        break;
    }
}

// ============================================================
//                            Device
// ============================================================

void DeviceConfigHandler::handleQuery(const Route& route, const HttpRequest& request) const {
    const DeviceConfig& config = this->runtime.getDeviceConfig();

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    Value::writeText(root, DEVICE_NAME_FIELD, config.name);

    request.replyJson(route, doc);
}

void DeviceConfigHandler::handleSave(const Route& route, const HttpRequest& request) const {
    JsonDocument doc;

    const Restful::Error* err = request.parseJson(doc);
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    // Merge the request onto the current config, then persist it.
    DeviceConfig config = this->runtime.getDeviceConfig();
    err = this->mergeConfig(config, doc.as<JsonObjectConst>());
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    if (!this->runtime.saveDeviceConfig(config)) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

void DeviceConfigHandler::handleReset(const Route& route, const HttpRequest& request) const {
    if (!this->runtime.resetDeviceConfig()) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

const Restful::Error* DeviceConfigHandler::mergeConfig(DeviceConfig& config, JsonObjectConst body) const {
    if (!Value::readText(config.name, DEVICE_NAME_FIELD, body)) {
        return DEVICE_NAME_FIELD.valueError;
    }
    return nullptr;
}

// ============================================================
//                             Wi-Fi
// ============================================================

void WifiConfigHandler::handleQuery(const Route& route, const HttpRequest& request) const {
    const WifiConfig& config = this->runtime.getWifiConfig();

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    Value::writeText(root, WIFI_PREF_MODE_FIELD, wifiModeToWire(config.prefMode));
    Value::writeText(root, WIFI_STA_SSID_FIELD, config.staSsid);
    Value::writeText(root, WIFI_STA_PASSWORD_FIELD, config.staPassword);
    Value::writeText(root, WIFI_AP_SSID_FIELD, config.apSsid);
    Value::writeText(root, WIFI_AP_PASSWORD_FIELD, config.apPassword);

    request.replyJson(route, doc);
}

void WifiConfigHandler::handleSave(const Route& route, const HttpRequest& request) const {
    JsonDocument doc;

    const Restful::Error* err = request.parseJson(doc);
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    // Merge the request onto the current config, validate, then persist it.
    WifiConfig config = this->runtime.getWifiConfig();
    err = this->mergeConfig(config, doc.as<JsonObjectConst>());
    if (err == nullptr) {
        err = this->validateConfig(config);
    }
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    if (!this->runtime.saveWifiConfig(config)) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

void WifiConfigHandler::handleReset(const Route& route, const HttpRequest& request) const {
    if (!this->runtime.resetWifiConfig()) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

const Restful::Error* WifiConfigHandler::mergeConfig(WifiConfig& config, JsonObjectConst body) const {
    String prefMode;

    if (!Value::readText(prefMode, WIFI_PREF_MODE_FIELD, body)) {
        return WIFI_PREF_MODE_FIELD.valueError;
    }
    if (!Value::readText(config.staSsid, WIFI_STA_SSID_FIELD, body)) {
        return WIFI_STA_SSID_FIELD.valueError;
    }
    if (!Value::readText(config.staPassword, WIFI_STA_PASSWORD_FIELD, body)) {
        return WIFI_STA_PASSWORD_FIELD.valueError;
    }
    if (!Value::readText(config.apSsid, WIFI_AP_SSID_FIELD, body)) {
        return WIFI_AP_SSID_FIELD.valueError;
    }
    if (!Value::readText(config.apPassword, WIFI_AP_PASSWORD_FIELD, body)) {
        return WIFI_AP_PASSWORD_FIELD.valueError;
    }

    // The mode field forbids empty values, so a non-empty read means present.
    if (!prefMode.isEmpty()) {
        config.prefMode = wifiModeFromWire(prefMode);
    }
    return nullptr;
}

const Restful::Error* WifiConfigHandler::validateConfig(const WifiConfig& config) const {
    // Cross-field rule: the preferred mode must keep a usable SSID after the
    // request is merged onto the current config.
    switch (config.prefMode) {
    case WifiMode::Sta:
        if (!Value::isValidText(WIFI_STA_SSID_FIELD, config.staSsid)) {
            return WIFI_STA_SSID_FIELD.valueError;
        }
        break;
    case WifiMode::Ap:
        if (!Value::isValidText(WIFI_AP_SSID_FIELD, config.apSsid)) {
            return WIFI_AP_SSID_FIELD.valueError;
        }
        break;
    default:
        break;
    }

    return nullptr;
}

// ============================================================
//                              SSH
// ============================================================

void SshConfigHandler::handleQuery(const Route& route, const HttpRequest& request) const {
    const SshConfig& config = this->runtime.getSshConfig();

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    Value::writeText(root, SSH_USERNAME_FIELD, config.username);
    Value::writeText(root, SSH_PASSWORD_FIELD, config.password);
    Value::writeText(root, SSH_AUTHORIZED_KEY_FIELD, config.authorizedKey);
    Value::writeBool(root, SSH_ALLOW_NO_AUTH_FIELD, config.allowNoAuth);

    request.replyJson(route, doc);
}

void SshConfigHandler::handleSave(const Route& route, const HttpRequest& request) const {
    JsonDocument doc;

    const Restful::Error* err = request.parseJson(doc);
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    // Merge the request onto the current config, validate, then persist it.
    SshConfig config = this->runtime.getSshConfig();
    err = this->mergeConfig(config, doc.as<JsonObjectConst>());
    if (err == nullptr) {
        err = this->validateConfig(config);
    }
    if (err != nullptr) {
        request.replyError(*err);
        return;
    }

    if (!this->runtime.saveSshConfig(config)) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

void SshConfigHandler::handleReset(const Route& route, const HttpRequest& request) const {
    if (!this->runtime.resetSshConfig()) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }
    request.replyEmpty(route);
}

const Restful::Error* SshConfigHandler::mergeConfig(SshConfig& config, JsonObjectConst body) const {
    if (!Value::readText(config.username, SSH_USERNAME_FIELD, body)) {
        return SSH_USERNAME_FIELD.valueError;
    }
    if (!Value::readText(config.password, SSH_PASSWORD_FIELD, body)) {
        return SSH_PASSWORD_FIELD.valueError;
    }
    if (!Value::readText(config.authorizedKey, SSH_AUTHORIZED_KEY_FIELD, body)) {
        return SSH_AUTHORIZED_KEY_FIELD.valueError;
    }
    if (!Value::readBool(config.allowNoAuth, SSH_ALLOW_NO_AUTH_FIELD, body)) {
        return SSH_ALLOW_NO_AUTH_FIELD.valueError;
    }

    return nullptr;
}

const Restful::Error* SshConfigHandler::validateConfig(const SshConfig& config) const {
    // Domain rule beyond meta constraints: a non-empty authorized key must be
    // a syntactically valid OpenSSH public key.
    if (!config.authorizedKey.isEmpty() &&
        !this->runtime.getSshService().isValidOpenSshPublicKey(config.authorizedKey)) {
        return SSH_AUTHORIZED_KEY_FIELD.valueError;
    }

    return nullptr;
}

// ============================================================
//                            System
// ============================================================

void SystemHandler::handle(const Route& route, const HttpRequest& request) const {
    bool ok = false;

    switch (route.dispatchKey) {
    case SYSTEM_ROUTE_FACTORY_RESET:
        ok = this->runtime.factoryReset();
        break;
    case SYSTEM_ROUTE_REBOOT:
        ok = this->runtime.reboot();
        break;
    default:
        return;
    }

    if (!ok) {
        request.replyError(Restful::ERR_RESPONSE_FAILED);
        return;
    }

    request.replyEmpty(route);
}
