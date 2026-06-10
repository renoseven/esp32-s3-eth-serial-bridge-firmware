// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// OpenSSH public-key validation and host key management.

#pragma once

#include <WString.h>

// ============================================================
//                     Forward declarations
// ============================================================

struct ssh_key_struct;

using ssh_key = ssh_key_struct*;

// ============================================================
//                           SshKey
// ============================================================

namespace SshKey {
    // Return true if str is a valid OpenSSH public key line.
    bool isValidPublicKey(const String& str) noexcept;

    // Return true if key matches the OpenSSH public key line.
    bool isMatchedPublicKey(const String& publicKey, ssh_key key) noexcept;
} // namespace SshKey

// ============================================================
//                         SshHostKey
// ============================================================

class SshHostKey {
  public:
    // --- Constructor ---

    SshHostKey() = default;

    ~SshHostKey();

    SshHostKey(const SshHostKey&) = delete;
    SshHostKey& operator=(const SshHostKey&) = delete;

    // --- Status ---

    // Return true while a host key is loaded.
    bool isValid() const noexcept {
        return this->key != nullptr;
    }

    // Return the OpenSSH-format host private key.
    const String& getPrivateKey() const noexcept {
        return this->privateKey;
    }

    // Return the OpenSSH-format host public key.
    const String& getPublicKey() const noexcept {
        return this->publicKey;
    }

    // Return the native host private key handle.
    ssh_key get() const noexcept {
        return this->key;
    }

    // --- Lifecycle ---

    // Load or generate the host key.
    int load(const String& hostPrivateKey);

    // Release the host key and clear stored strings.
    void destroy();

    // Detach the native key handle without freeing it.
    void clear() {
        this->key = nullptr;
    }

  private:
    // --- State ---

    String privateKey;     // OpenSSH-format host private key.
    String publicKey;      // OpenSSH-format host public key.
    ssh_key key = nullptr; // Native host private key handle.
};
