// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH server endpoint.

#pragma once

#include <WString.h>

#include "ssh/credentials.h"
#include "ssh/key.h"
#include "ssh/listener.h"
#include "ssh/session.h"

// ============================================================
//                         SshServer
// ============================================================

class SshServer {
  public:
    // --- Constructor ---

    SshServer();

    SshServer(const SshServer&) = delete;
    SshServer& operator=(const SshServer&) = delete;

    // --- Status ---

    // Return the OpenSSH-format host public key.
    const String& getHostPublicKey() const noexcept {
        return this->hostKey.getPublicKey();
    }

    // Return the OpenSSH-format host private key.
    const String& getHostPrivateKey() const noexcept {
        return this->hostKey.getPrivateKey();
    }

    // Return true while a client is connected.
    bool isConnected() const noexcept {
        return this->session.isAlive();
    }

    // Return true while a client session is active.
    bool isActive() const noexcept {
        return this->session.isActive();
    }

    // --- Lifecycle ---

    // Start the SSH server with the given host key, credentials, and channel I/O callback.
    bool begin(const String& hostPrivateKey, const SshCredentials& credentials, ChannelIo io, void* userdata);

    // Stop the SSH server and release all resources.
    void end();

  private:
    // --- Begin args ---

    SshCredentials credentials; // Client authentication credentials.
    ChannelIo io = nullptr;     // Channel I/O callback for an established client.
    void* userdata = nullptr;   // Userdata passed to io.

    // --- Components ---

    SshHostKey hostKey;   // Server host key.
    SshListener listener; // Listens for incoming client connections.
    SshSession session;   // Serves one connected client.

    // Accept callback for incoming client connections.
    static int accept(ssh_bind bind, void* userdata);
};
