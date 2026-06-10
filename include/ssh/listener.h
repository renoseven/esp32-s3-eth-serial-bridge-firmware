// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH bind/listen and client accept.

#pragma once

#include "util/worker.h"

// ============================================================
//                     Forward declarations
// ============================================================

class SshHostKey;

struct ssh_bind_struct;

using ssh_bind = ssh_bind_struct*;

// ============================================================
//                         Constants
// ============================================================

// Accept callback return when busy.
inline constexpr int SSH_ACCEPT_BUSY = 1;

// ============================================================
//                      Callback types
// ============================================================

// Accept callback for incoming client connections.
using AcceptFn = int (*)(ssh_bind bind, void* userdata);

// ============================================================
//                         SshListener
// ============================================================

class SshListener {
  public:
    // --- Constructor ---

    SshListener();

    SshListener(const SshListener&) = delete;
    SshListener& operator=(const SshListener&) = delete;

    // --- Lifecycle ---

    // Start listening for client connections.
    int start(SshHostKey& key, AcceptFn accept, void* userdata);

    // Stop listening for client connections.
    void stop();

  private:
    // --- State ---

    ssh_bind bind = nullptr; // Listening bind.

    AcceptFn accept = nullptr; // Accept callback.
    void* userdata = nullptr;  // Userdata passed to accept.

    // --- Worker ---

    Worker worker; // Background worker running the listener.

    // --- Operation ---

    // Reject pending client connections.
    void rejectPending();

    // Reset state and release resources.
    void reset();

    // Listener entry point.
    static void entry(const TaskContext& ctx);
};
