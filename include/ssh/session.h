// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH client session.

#pragma once

#include <atomic>
#include <cstdint>

#include <libssh/callbacks.h>
#include <libssh/server.h>

#include "ssh/credentials.h"

#include "util/worker.h"

// ============================================================
//                     Forward declarations
// ============================================================

struct ssh_session_struct;
struct ssh_bind_struct;

using ssh_session = ssh_session_struct*;
using ssh_bind = ssh_bind_struct*;

// ============================================================
//                      Callback types
// ============================================================

// Channel I/O callback for an established client session.
using ChannelIo = void (*)(ssh_channel channel, void* userdata);

// ============================================================
//                         SshSession
// ============================================================

class SshSession {
  public:
    // --- Constructor ---

    SshSession();

    SshSession(const SshSession&) = delete;
    SshSession& operator=(const SshSession&) = delete;

    // --- Status ---

    // Return true while a client is connected.
    bool isAlive() const noexcept {
        return this->state.load(std::memory_order_relaxed) != State::Idle;
    }

    // Return true while the client session is active.
    bool isActive() const noexcept {
        return this->worker.isRunning();
    }

    // --- Lifecycle ---

    // Accept a client connection.
    int accept(ssh_bind bind, const SshCredentials& credentials, ChannelIo io, void* userdata);

    // Close the client session.
    void close();

  private:
    // --- Types ---

    enum class State : uint8_t {
        Idle,           // No client connected.
        Authenticating, // Client authentication in progress.
        OpeningChannel, // Waiting for a session channel.
        StartingShell,  // Waiting for a shell request.
        Established,    // Ready for channel I/O.
    };

    // libssh callback handlers.
    struct Callbacks;

    // --- State ---

    SshCredentials credentials;             // Client authentication credentials.
    std::atomic<State> state = State::Idle; // Current session state.

    ssh_session session = nullptr; // Connected client session.
    ssh_channel channel = nullptr; // Established session channel.

    ssh_server_callbacks_struct serverCallbacks{};   // libssh server callbacks.
    ssh_channel_callbacks_struct channelCallbacks{}; // libssh channel callbacks.

    ChannelIo io = nullptr;   // Channel I/O callback.
    void* userdata = nullptr; // Userdata passed to io.

    // --- Worker ---

    Worker worker; // Background worker running the session.

    // --- Operation ---

    // Perform key exchange with the connected client.
    int keyExchange(const TaskContext& ctx);

    // Complete authentication and channel setup.
    int negotiate(const TaskContext& ctx);

    // Reset state and release resources.
    void reset();

    // Session entry point.
    static void entry(const TaskContext& ctx);
};
