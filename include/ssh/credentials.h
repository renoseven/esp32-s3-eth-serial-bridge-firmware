// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Client authentication credentials and checks.

#pragma once

#include <WString.h>

// ============================================================
//                     Forward declarations
// ============================================================

struct ssh_key_struct;

using ssh_key = ssh_key_struct*;

// ============================================================
//                      SshCredentials
// ============================================================

// Client authentication credentials.
struct SshCredentials {
    String username;          // Allowed SSH username.
    String password;          // Allowed SSH password.
    String authorizedKey;     // Allowed OpenSSH public key line.
    bool allowNoAuth = false; // Whether none authentication is permitted.

    // --- Checks ---

    // Validate password authentication credentials.
    int checkPassword(const char* user, const char* password) const;

    // Validate public-key authentication credentials.
    int checkPubkey(const char* user, ssh_key pubkey, char signatureState) const;

    // Validate none authentication.
    int checkNone() const;
};
