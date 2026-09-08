// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Device configuration value object and storage API.

#pragma once

#include <WString.h>

// ============================================================
//                        DeviceConfig
// ============================================================

// Device configuration plain value object.
struct DeviceConfig {
    String name; // Device name.
};

// ============================================================
//                          DeviceConfigStorage
// ============================================================

namespace DeviceConfigStorage {
    // Load from storage into config.
    bool read(DeviceConfig& config);

    // Replace persisted config.
    bool update(const DeviceConfig& config);

    // Erase all persisted keys.
    bool clear();
} // namespace DeviceConfigStorage
