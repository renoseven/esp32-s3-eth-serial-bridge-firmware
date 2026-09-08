// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Runtime implementation.
//
// Flow:
//   - begin(): LED/USB hardware, reload NVS mirrors (abort on failure), boot serial -> wifi -> ssh.
//   - save/reset: *ConfigStorage::update/clear (bool), reload mirror, apply (deferred restart).
//   - applyDeviceConfig: reload device + wifi (default AP SSID follows name), schedule wifi.restart().
//   - applyWifiConfig / applySshConfig: reload mirror, schedule service restart.
//   - restart*/scheduleReboot: deferred.schedule(deferred, delayMs) (debounced by Deferred).
//   - loop(): deferred.loop(), then wifi state machine.
// Device and Wi-Fi changes both restart Wi-Fi because the device name feeds the default AP SSID.

#include "runtime.h"

#include <cstdlib>

#include <Arduino.h>
#include <USB.h>

#include "firmware.h"

#include "config/device.h"
#include "config/ssh.h"
#include "config/wifi.h"

#include "hardware/led.h"

#include "service/device.h"
#include "service/serial.h"
#include "service/ssh.h"
#include "service/wifi.h"

#include "util/deferred.h"

// Pin via build_opt.h; AsyncTCP defaults to -1 (any core) when unset.
static_assert(CONFIG_ASYNC_TCP_RUNNING_CORE == static_cast<int>(Firmware::CORE_PROTOCOL),
              "CONFIG_ASYNC_TCP_RUNNING_CORE must match Firmware::CORE_PROTOCOL");

namespace {
    // Fixed capacity for wifi/ssh/reboot deferred jobs (+ spare).
    constexpr size_t DEFERRED_CAPACITY = 4;

    // --- LED colors ---

    // Boot color for config.
    constexpr LedColor BOOT_COLOR_CONFIG = LedColor::rgb(255, 255, 255);

    // Boot color for serial.
    constexpr LedColor BOOT_COLOR_SERIAL = LedColor::rgb(255, 0, 0);

    // Boot color for Wi-Fi.
    constexpr LedColor BOOT_COLOR_WIFI = LedColor::rgb(0, 255, 0);

    // Boot color for SSH.
    constexpr LedColor BOOT_COLOR_SSH = LedColor::rgb(0, 0, 255);
} // namespace

// ============================================================
//                        Runtime::Impl
// ============================================================

struct Runtime::Impl {
    // --- Config ---

    DeviceConfig deviceConfig;
    WifiConfig wifiConfig;
    SshConfig sshConfig;

    // --- Services ---

    DeviceService device;
    SerialService serial;
    WifiService wifi;
    SshService ssh;

    // --- Deferred scheduling ---

    DeferredScheduler<DEFERRED_CAPACITY> deferred;

    Impl() : device(deviceConfig), wifi(device, wifiConfig), ssh(serial, sshConfig) {}
};

// ============================================================
//                           Runtime
// ============================================================

// --- Constructor ---

Runtime::Runtime() : impl(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

// --- Config queries ---

const DeviceConfig& Runtime::getDeviceConfig() const noexcept {
    return this->impl->deviceConfig;
}

const WifiConfig& Runtime::getWifiConfig() const noexcept {
    return this->impl->wifiConfig;
}

const SshConfig& Runtime::getSshConfig() const noexcept {
    return this->impl->sshConfig;
}

// --- Service queries ---

const DeviceService& Runtime::getDeviceService() const noexcept {
    return this->impl->device;
}

const WifiService& Runtime::getWifiService() const noexcept {
    return this->impl->wifi;
}

const SshService& Runtime::getSshService() const noexcept {
    return this->impl->ssh;
}

// --- Config commands ---

bool Runtime::saveDeviceConfig(const DeviceConfig& config) {
    if (!DeviceConfigStorage::update(config)) {
        return false;
    }
    return this->applyDeviceConfig();
}

bool Runtime::saveWifiConfig(const WifiConfig& config) {
    if (!WifiConfigStorage::update(config)) {
        return false;
    }
    return this->applyWifiConfig();
}

bool Runtime::saveSshConfig(const SshConfig& config) {
    if (!SshConfigStorage::update(config)) {
        return false;
    }
    return this->applySshConfig();
}

bool Runtime::resetDeviceConfig() {
    if (!DeviceConfigStorage::clear()) {
        return false;
    }
    return this->applyDeviceConfig();
}

bool Runtime::resetWifiConfig() {
    if (!WifiConfigStorage::clear()) {
        return false;
    }
    return this->applyWifiConfig();
}

bool Runtime::resetSshConfig() {
    if (!SshConfigStorage::clear()) {
        return false;
    }
    return this->applySshConfig();
}

bool Runtime::factoryReset() {
    if (!DeviceConfigStorage::clear()) {
        return false;
    }
    if (!WifiConfigStorage::clear()) {
        return false;
    }
    if (!SshConfigStorage::clear()) {
        return false;
    }
    return this->scheduleReboot();
}

bool Runtime::reboot() {
    return this->scheduleReboot();
}

// --- Lifecycle ---

void Runtime::begin() {
    LED.begin();

    if (!USB.begin()) {
        abort();
    }

    LED.show(BOOT_COLOR_CONFIG);
    if (!this->reloadAllConfigs()) {
        abort();
    }

    LED.show(BOOT_COLOR_SERIAL);
    if (!this->impl->serial.begin()) {
        abort();
    }

    LED.show(BOOT_COLOR_WIFI);
    if (!this->impl->wifi.begin()) {
        abort();
    }

    LED.show(BOOT_COLOR_SSH);
    if (!this->impl->ssh.begin()) {
        abort();
    }

    LED.off();
}

void Runtime::loop() {
    this->impl->deferred.loop();
    this->impl->wifi.loop();
    yield();
}

// --- Configuration reload ---

bool Runtime::reloadDeviceConfig() {
    return DeviceConfigStorage::read(this->impl->deviceConfig);
}

bool Runtime::reloadWifiConfig() {
    return WifiConfigStorage::read(this->impl->wifiConfig);
}

bool Runtime::reloadSshConfig() {
    return SshConfigStorage::read(this->impl->sshConfig);
}

bool Runtime::reloadAllConfigs() {
    if (!this->reloadDeviceConfig()) {
        return false;
    }
    if (!this->reloadWifiConfig()) {
        return false;
    }
    if (!this->reloadSshConfig()) {
        return false;
    }

    return true;
}

// --- Configuration apply ---

bool Runtime::applyDeviceConfig() {
    if (!this->reloadDeviceConfig()) {
        return false;
    }
    if (!this->reloadWifiConfig()) {
        return false;
    }
    if (!this->restartWifi()) {
        return false;
    }

    return true;
}

bool Runtime::applyWifiConfig() {
    if (!this->reloadWifiConfig()) {
        return false;
    }
    if (!this->restartWifi()) {
        return false;
    }

    return true;
}

bool Runtime::applySshConfig() {
    if (!this->reloadSshConfig()) {
        return false;
    }
    if (!this->restartSsh()) {
        return false;
    }

    return true;
}

// --- Scheduling ---

bool Runtime::restartWifi() {
    static const Deferred::Entry WIFI_RESTART =
        +[](void* context) { (void)static_cast<Impl*>(context)->wifi.restart(); };

    return this->impl->deferred.schedule({WIFI_RESTART, this->impl.get()}, Firmware::WIFI_RESTART_DELAY_MS);
}

bool Runtime::restartSsh() {
    static const Deferred::Entry SSH_RESTART = +[](void* context) { (void)static_cast<Impl*>(context)->ssh.restart(); };

    return this->impl->deferred.schedule({SSH_RESTART, this->impl.get()}, Firmware::SSH_RESTART_DELAY_MS);
}

bool Runtime::scheduleReboot() {
    static const Deferred::Entry REBOOT = +[](void*) { ESP.restart(); };

    return this->impl->deferred.scheduleExclusive({REBOOT, nullptr}, Firmware::REBOOT_DELAY_MS);
}
