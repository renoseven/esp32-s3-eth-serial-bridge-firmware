// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Runtime: composition root and device control interface.
//
// Exposes configuration queries/commands, service accessors, and lifecycle hooks.

#pragma once

#include <cstdint>
#include <memory>

// ============================================================
//                     Forward declarations
// ============================================================

class DeviceConfig;
class WifiConfig;
class SshConfig;

class DeviceService;
class SshService;
class WifiService;

// ============================================================
//                           Runtime
// ============================================================

class Runtime {
  public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // --- Config queries ---

    // Return the in-memory device configuration mirror.
    const DeviceConfig& getDeviceConfig() const noexcept;

    // Return the in-memory Wi-Fi configuration mirror.
    const WifiConfig& getWifiConfig() const noexcept;

    // Return the in-memory SSH configuration mirror.
    const SshConfig& getSshConfig() const noexcept;

    // --- Service queries ---

    // Return the device identity and platform status service.
    const DeviceService& getDeviceService() const noexcept;

    // Return the Wi-Fi connectivity service.
    const WifiService& getWifiService() const noexcept;

    // Return the SSH serial bridge service.
    const SshService& getSshService() const noexcept;

    // --- Config commands ---

    // *ConfigStorage::update, then apply.
    bool saveDeviceConfig(const DeviceConfig& config);

    // *ConfigStorage::update, then apply.
    bool saveWifiConfig(const WifiConfig& config);

    // *ConfigStorage::update, then apply.
    bool saveSshConfig(const SshConfig& config);

    // Clear device config to defaults, then apply.
    bool resetDeviceConfig();

    // Clear Wi-Fi config to defaults, then apply.
    bool resetWifiConfig();

    // Clear SSH config to defaults, then apply.
    bool resetSshConfig();

    // Clear all configuration, then reboot.
    bool factoryReset();

    // Schedule a deferred reboot.
    bool reboot();

    // --- Lifecycle ---

    // Boot all services in dependency order.
    void begin();

    // Periodic housekeeping; call every loop iteration.
    void loop();

  private:
    // --- Implementation ---

    struct Impl;                // Opaque implementation type.
    std::unique_ptr<Impl> impl; // Implementation instance.

    // --- Configuration reload ---

    // Reload the device configuration mirror from NVS.
    bool reloadDeviceConfig();

    // Reload the Wi-Fi configuration mirror from NVS.
    bool reloadWifiConfig();

    // Reload the SSH configuration mirror from NVS.
    bool reloadSshConfig();

    // Reload all configuration mirrors from NVS.
    bool reloadAllConfigs();

    // --- Configuration apply ---

    // Reload device + Wi-Fi config and schedule a deferred Wi-Fi restart.
    bool applyDeviceConfig();

    // Reload Wi-Fi config and schedule a deferred Wi-Fi restart.
    bool applyWifiConfig();

    // Reload SSH config and schedule a deferred SSH restart.
    bool applySshConfig();

    // --- Scheduling ---

    // Schedule a deferred Wi-Fi service restart.
    bool restartWifi();

    // Schedule a deferred SSH service restart.
    bool restartSsh();

    // Cancel pending work and schedule a deferred reboot.
    bool scheduleReboot();
};
