// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// DeviceService implementation.
//
// Flow: name from the in-memory DeviceConfig mirror; firmware version from
// Firmware::VERSION; RAM and uptime from ESP Arduino APIs.

#include "service/device.h"

#include <Arduino.h>

#include "firmware.h"

#include "config/device.h"

// ============================================================
//                        DeviceService
// ============================================================

// --- Constructor ---

DeviceService::DeviceService(const DeviceConfig& cfg) : config(cfg) {}

// --- Status ---

const String& DeviceService::getName() const noexcept {
    return this->config.name;
}

const char* DeviceService::getFwVersion() const noexcept {
    return Firmware::VERSION;
}

const char* DeviceService::getSdkVersion() const noexcept {
    return ESP.getSdkVersion();
}

uint32_t DeviceService::getFreeMemory() const noexcept {
    return ESP.getFreeHeap() + ESP.getFreePsram();
}

uint32_t DeviceService::getTotalMemory() const noexcept {
    return ESP.getHeapSize() + ESP.getPsramSize();
}

uint32_t DeviceService::getUptime() const noexcept {
    return static_cast<uint32_t>(millis() / 1000);
}
