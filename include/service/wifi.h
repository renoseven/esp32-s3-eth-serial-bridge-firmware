// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Wi-Fi service API: STA / configuration-AP connectivity with failover.

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>

#include <DNSServer.h>
#include <WString.h>

#include "config/wifi.h"

// ============================================================
//                     Forward declarations
// ============================================================

class DeviceService;

// ============================================================
//                         WifiService
// ============================================================

class WifiService {
  public:
    // --- Constructor ---

    explicit WifiService(const DeviceService& device, const WifiConfig& config);

    // --- Status ---

    // Return the actual radio mode (from hardware).
    WifiMode getActiveMode() const noexcept;

    // Return STA RSSI in dBm (0 when not in STA mode).
    int getRssi() const noexcept;

    // Return the active interface MAC address.
    const String& getMacAddr() const noexcept;

    // Return the active interface IPv4 address.
    const String& getIpv4Addr() const noexcept;

    // Return all assigned IPv6 addresses on the active interface.
    const std::set<String>& getIpv6Addrs() const noexcept;

    // --- Lifecycle ---

    // Connect Wi-Fi in STA or configuration AP mode.
    bool begin();

    // Disconnect and power down Wi-Fi.
    void end();

    // Restart Wi-Fi using the current configuration.
    bool restart();

    // Run failover and link maintenance; call every loop iteration.
    void loop();

  private:
    // --- References ---

    const DeviceService& device; // Device status (name source).
    const WifiConfig& config;    // Wi-Fi configuration.

    // --- Captive portal ---

    DNSServer captiveDns; // DNS server for configuration AP.

    // --- Event ---

    size_t wifiEventId = 0;               // Wi-Fi event subscription handle.
    volatile bool networkChanged = false; // Set by Wi-Fi event callbacks.

    // --- Network addresses ---

    String macAddr;             // Active interface MAC address.
    String ipv4Addr;            // Active interface IPv4 address.
    std::set<String> ipv6Addrs; // Assigned IPv6 addresses on the active interface.

    // --- Timestamps ---

    uint32_t connectTime = 0;   // Nonzero while waiting for first connection.
    uint32_t reconnectTime = 0; // Last reconnect attempt while link is down.
    uint32_t linkDownTime = 0;  // Anchor for recovery timeout after link loss.
    uint32_t recoveryTime = 0;  // Last recovery attempt from fallback AP.

    // --- State queries ---

    // True while the initial STA association is in progress.
    bool isConnecting() const noexcept;

    // True after the STA link has dropped and we are tracking recovery.
    bool isLinkDown() const noexcept;

    // Initial STA association exceeded the connect timeout.
    bool isConnectTimedOut(uint32_t now) const noexcept;

    // STA link has been down longer than the fallback-to-AP timeout.
    bool isLinkDownTimedOut(uint32_t now) const noexcept;

    // Enough time has passed to attempt another STA reconnect.
    bool isReconnectElapsed(uint32_t now) const noexcept;

    // Enough time has passed to probe STA recovery from fallback AP.
    bool isRecoveryElapsed(uint32_t now) const noexcept;

    // --- Timer helpers ---

    // Mark the start of a STA association attempt.
    void setConnectTime(uint32_t now);

    // Record the last STA reconnect attempt while link is down.
    void setReconnectTime(uint32_t now);

    // Anchor the fallback-to-AP deadline after a link loss.
    void setLinkDownTime(uint32_t now);

    // Record the last STA recovery probe from fallback AP.
    void setRecoveryTime(uint32_t now);

    // Clear all failover timestamps.
    void resetTimers();

    // --- Mode control ---

    // Power the radio off and clear per-mode state.
    void resetMode();

    // Enter the mode that matches the saved config.
    bool switchMode();

    // Start configuration softAP and captive portal.
    bool startApMode();

    // Start joining the configured network.
    bool startStaMode();

    // Update cached MAC/IPv4/IPv6 to match the current mode.
    void updateNetworkAddrs();

    // --- Maintenance ---

    // AP-mode upkeep: captive portal and periodic STA recovery probes.
    void maintainAp();

    // STA-mode upkeep: connect timeout, reconnect, and AP fallback.
    void maintainSta();
};
