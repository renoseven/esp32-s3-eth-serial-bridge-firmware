// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH configuration value object and storage API.

#pragma once

#include <WString.h>

// ============================================================
//                          SshConfig
// ============================================================

// SSH configuration plain value object.
struct SshConfig {
    // --- Server authentication ---

    String hostKey; // SSH host private key.

    // --- Client authentication ---

    String username;      // Client login name.
    String password;      // Client password.
    String authorizedKey; // Client public key.
    bool allowNoAuth;     // Allow login without password or public key.
};

// ============================================================
//                          SshConfigStorage
// ============================================================

namespace SshConfigStorage {
    // Load from storage into config.
    bool read(SshConfig& config);

    // Replace persisted config.
    bool update(const SshConfig& config);

    // Partial update: host key only.
    bool updateHostKey(SshConfig& config, const String& hostKey);

    // Erase all persisted keys.
    bool clear();
} // namespace SshConfigStorage
