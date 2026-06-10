// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH service API: USB serial port exposed over SSH.

#pragma once

#include <WString.h>

#include "ssh/server.h"

// ============================================================
//                     Forward declarations
// ============================================================

struct SshConfig;
class SerialService;

// ============================================================
//                         SshService
// ============================================================

class SshService {
  public:
    // --- Constructor ---

    explicit SshService(SerialService& serial, SshConfig& config);

    // --- Status ---

    // Return the SSH host public key (OpenSSH-format).
    const String& getHostKey() const noexcept {
        return this->server.getHostPublicKey();
    }

    // Return true while a client is connected.
    bool isConnected() const noexcept {
        return this->server.isConnected();
    }

    // --- Validation ---

    // Return true if str is a valid OpenSSH public key line.
    bool isValidOpenSshPublicKey(const String& str) const noexcept;

    // --- Lifecycle ---

    // Start the SSH serial gateway on port 22.
    bool begin();

    // Shut down the SSH serial gateway.
    void end();

    // Restart the SSH serial gateway.
    bool restart();

  private:
    // --- References ---

    SerialService& serial; // Serial byte stream for bridge.
    SshConfig& config;     // SSH configuration (auth strings + host key).

    // --- SSH ---

    SshServer server; // Host key, listener, and client session.

    // --- Bridge ---

    // Channel I/O: channel <-> serial until the channel closes.
    static void bridge(ssh_channel channel, void* userdata);
};
