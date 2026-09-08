// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WifiService implementation.
//
// Flow:
//   - begin()/restart(): register Wi-Fi events, switchMode() from cfg.prefMode.
//   - switchMode(): Off / STA / AP; empty staSsid forces AP; hardware mode is truth.
//   - loop(): refresh cached addresses on events; maintainAp() or maintainSta().
// Failover (millis anchors, 0 = inactive):
//   - STA first connect > CONNECT_TIMEOUT_MS -> AP fallback.
//   - Link down: reconnect every RECONNECT_INTERVAL_MS; > FALLBACK_AP_TIMEOUT_MS -> AP.
//   - Fallback AP with prefMode STA: probe STA every FALLBACK_STA_INTERVAL_MS.
// All methods run on the Arduino loop task; captive DNS processed in maintainAp().

#include "service/wifi.h"

#include <WiFi.h>

#include "firmware.h"

#include "service/device.h"

namespace {
    // Unassigned MAC address.
    constexpr const char UNASSIGNED_MAC[] = "00:00:00:00:00:00";

    // Unassigned IPv4 address.
    constexpr const char UNASSIGNED_IPV4[] = "0.0.0.0";

    // Unassigned IPv6 address.
    constexpr const char UNASSIGNED_IPV6[] = "::";

    std::set<String> collectIpv6Addrs(const IPAddress* addrs, size_t count) {
        std::set<String> result;

        for (size_t i = 0; i < count; ++i) {
            const IPAddress& addr = addrs[i];
            if (addr.type() == IPv6 && addr != IPAddress(IPv6)) {
                result.insert(addr.toString());
            }
        }

        if (result.empty()) {
            result.insert(UNASSIGNED_IPV6);
        }

        return result;
    }
} // namespace

// ============================================================
//                         WifiService
// ============================================================

// --- Constructor ---

WifiService::WifiService(const DeviceService& device, const WifiConfig& cfg) : device(device), config(cfg) {}

// --- Status ---

WifiMode WifiService::getActiveMode() const noexcept {
    switch (WiFi.getMode()) {
    case WIFI_MODE_AP:
        return WifiMode::Ap;
    case WIFI_MODE_STA:
        return WifiMode::Sta;
    default:
        return WifiMode::Off;
    }
}

int WifiService::getRssi() const noexcept {
    if (this->getActiveMode() != WifiMode::Sta) {
        return 0;
    }

    return WiFi.RSSI();
}

const String& WifiService::getMacAddr() const noexcept {
    return this->macAddr;
}

const String& WifiService::getIpv4Addr() const noexcept {
    return this->ipv4Addr;
}

const std::set<String>& WifiService::getIpv6Addrs() const noexcept {
    return this->ipv6Addrs;
}

// --- Lifecycle ---

bool WifiService::begin() {
    WiFi.persistent(false);
    if (!WiFi.setHostname(this->device.getName().c_str())) {
        return false;
    }

    this->wifiEventId = WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t) {
        switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
            this->networkChanged = true;
            break;
        default:
            break;
        }
    });
    if (this->wifiEventId == 0) {
        return false;
    }

    if (!this->switchMode()) {
        return false;
    }

    return true;
}

void WifiService::end() {
    this->resetMode();
    WiFi.removeEvent(this->wifiEventId);
    this->networkChanged = false;
}

bool WifiService::restart() {
    this->end();
    return this->begin();
}

void WifiService::loop() {
    if (this->networkChanged) {
        this->networkChanged = false;
        this->updateNetworkAddrs();
    }

    switch (this->getActiveMode()) {
    case WifiMode::Off:
        break;
    case WifiMode::Ap:
        this->maintainAp();
        break;
    case WifiMode::Sta:
        this->maintainSta();
        break;
    }
}

// --- State queries ---

bool WifiService::isConnecting() const noexcept {
    return this->connectTime != 0;
}

bool WifiService::isLinkDown() const noexcept {
    return this->linkDownTime != 0;
}

bool WifiService::isConnectTimedOut(uint32_t now) const noexcept {
    return (now - this->connectTime) >= Firmware::WIFI_CONNECT_TIMEOUT_MS;
}

bool WifiService::isLinkDownTimedOut(uint32_t now) const noexcept {
    return (now - this->linkDownTime) >= Firmware::WIFI_FALLBACK_AP_TIMEOUT_MS;
}

bool WifiService::isReconnectElapsed(uint32_t now) const noexcept {
    return (now - this->reconnectTime) >= Firmware::WIFI_RECONNECT_INTERVAL_MS;
}

bool WifiService::isRecoveryElapsed(uint32_t now) const noexcept {
    return (now - this->recoveryTime) >= Firmware::WIFI_FALLBACK_STA_INTERVAL_MS;
}

// --- Timer helpers ---

void WifiService::setConnectTime(uint32_t now) {
    this->connectTime = now;
}

void WifiService::setReconnectTime(uint32_t now) {
    this->reconnectTime = now;
}

void WifiService::setLinkDownTime(uint32_t now) {
    this->linkDownTime = now;
}

void WifiService::setRecoveryTime(uint32_t now) {
    this->recoveryTime = now;
}

void WifiService::resetTimers() {
    this->connectTime = 0;
    this->reconnectTime = 0;
    this->linkDownTime = 0;
    this->recoveryTime = 0;
}

// --- Mode control ---

void WifiService::resetMode() {
    switch (this->getActiveMode()) {
    case WifiMode::Ap:
        this->captiveDns.stop();
        WiFi.softAPdisconnect(true);
        break;
    case WifiMode::Sta:
        WiFi.disconnect(true);
        break;
    case WifiMode::Off:
        break;
    }

    WiFi.mode(WIFI_OFF);
    this->resetTimers();
    this->updateNetworkAddrs();
}

bool WifiService::switchMode() {
    WifiMode targetMode = this->config.prefMode;

    // If the preferred mode is STA and the SSID is empty, fall back to AP.
    if (targetMode == WifiMode::Sta && this->config.staSsid.isEmpty()) {
        targetMode = WifiMode::Ap;
    }

    // If the current mode is the same as the target, do nothing.
    if (this->getActiveMode() == targetMode) {
        return true;
    }

    // Otherwise, reset the current mode and start the target mode.
    switch (targetMode) {
    case WifiMode::Off:
        this->resetMode();
        return true;
    case WifiMode::Sta:
        return this->startStaMode();
    case WifiMode::Ap:
        return this->startApMode();
    }

    return false;
}

bool WifiService::startApMode() {
    this->resetMode();

    bool ret = false;
    do {
        const char* ssid = this->config.apSsid.c_str();
        const char* passphrase = this->config.apPassword.c_str();

        if (!WiFi.mode(WIFI_AP)) {
            break;
        }
        if (!WiFi.softAP(ssid, passphrase)) {
            break;
        }
        if (!WiFi.softAPenableIPv6()) {
            break;
        }
        if (!this->captiveDns.start(Firmware::NET_PORT_DNS, "*", WiFi.softAPIP())) {
            break;
        }

        this->setRecoveryTime(millis());
        ret = true;
    } while (0);

    if (!ret) {
        this->resetMode();
    }

    return ret;
}

bool WifiService::startStaMode() {
    this->resetMode();

    bool ret = false;
    do {
        const char* ssid = this->config.staSsid.c_str();
        const char* passphrase = this->config.staPassword.c_str();

        if (!WiFi.mode(WIFI_STA)) {
            break;
        }
        if (!WiFi.setAutoReconnect(true)) {
            break;
        }
        if (WiFi.begin(ssid, passphrase) == WL_CONNECT_FAILED) {
            break;
        }
        if (!WiFi.enableIPv6()) {
            break;
        }

        this->setConnectTime(millis());
        ret = true;
    } while (0);

    if (!ret) {
        this->resetMode();
    }

    return ret;
}

void WifiService::updateNetworkAddrs() {
    switch (this->getActiveMode()) {
    case WifiMode::Ap: {
        const IPAddress ipv6Addrs[] = {WiFi.softAPlinkLocalIPv6()};
        const size_t ipv6AddrsCount = sizeof(ipv6Addrs) / sizeof(ipv6Addrs[0]);

        this->macAddr = WiFi.softAPmacAddress();
        this->ipv4Addr = WiFi.softAPIP().toString();
        this->ipv6Addrs = collectIpv6Addrs(ipv6Addrs, ipv6AddrsCount);
        break;
    }
    case WifiMode::Sta: {
        const IPAddress ipv6Addrs[] = {WiFi.linkLocalIPv6(), WiFi.globalIPv6()};
        const size_t ipv6AddrsCount = sizeof(ipv6Addrs) / sizeof(ipv6Addrs[0]);

        this->macAddr = WiFi.macAddress();
        this->ipv4Addr = WiFi.localIP().toString();
        this->ipv6Addrs = collectIpv6Addrs(ipv6Addrs, ipv6AddrsCount);
        break;
    }
    case WifiMode::Off:
        this->macAddr = UNASSIGNED_MAC;
        this->ipv4Addr = UNASSIGNED_IPV4;
        this->ipv6Addrs = {UNASSIGNED_IPV6};
        break;
    }
}

// --- Maintenance ---

void WifiService::maintainAp() {
    this->captiveDns.processNextRequest();

    // Only retry STA when preferred mode is STA (we are in fallback AP).
    if (this->config.prefMode != WifiMode::Sta) {
        return;
    }

    const uint32_t now = millis();
    if (!this->isRecoveryElapsed(now)) {
        return;
    }

    this->setRecoveryTime(now);
    (void)this->switchMode();
}

// Per-tick STA upkeep, evaluated in priority order:
//   1. Connected        - clear pending timers (addresses refresh via events).
//   2. Awaiting connect - fall back to AP once CONNECT_TIMEOUT_MS elapses.
//   3. Link lost        - reopen AP after FALLBACK_AP_TIMEOUT_MS,
//                         retry WiFi.begin() every RECONNECT_INTERVAL_MS.
void WifiService::maintainSta() {
    if (WiFi.status() == WL_CONNECTED) {
        if (this->isConnecting() || this->isLinkDown()) {
            this->resetTimers();
        }
        return;
    }

    const uint32_t now = millis();

    if (this->isConnecting()) {
        if (this->isConnectTimedOut(now)) {
            (void)this->startApMode();
        }
        return;
    }

    if (!this->isLinkDown()) {
        this->setLinkDownTime(now);
    }

    if (this->isLinkDownTimedOut(now)) {
        (void)this->startApMode();
    } else if (this->isReconnectElapsed(now)) {
        this->setReconnectTime(now);
        (void)WiFi.begin(this->config.staSsid.c_str(), this->config.staPassword.c_str());
    }
}
