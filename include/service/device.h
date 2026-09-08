// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Device service API: device identity and platform status.

#pragma once

#include <cstdint>

#include <WString.h>

// ============================================================
//                     Forward declarations
// ============================================================

class DeviceConfig;

// ============================================================
//                        DeviceService
// ============================================================

class DeviceService {
  public:
    // --- Constructor ---

    explicit DeviceService(const DeviceConfig& config);

    // --- Status ---

    // Return the configured device name.
    const String& getName() const noexcept;

    // Return the firmware version string (e.g. "v0.1a (badbeef)").
    const char* getFwVersion() const noexcept;

    // Return ESP-IDF SDK version string.
    const char* getSdkVersion() const noexcept;

    // Return free RAM in bytes (internal heap + PSRAM).
    uint32_t getFreeMemory() const noexcept;

    // Return total RAM in bytes (internal heap + PSRAM).
    uint32_t getTotalMemory() const noexcept;

    // Return uptime in seconds.
    uint32_t getUptime() const noexcept;

  private:
    const DeviceConfig& config; // Device configuration.
};
