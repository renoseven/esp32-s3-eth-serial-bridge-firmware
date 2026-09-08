// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshListener implementation.
//
// Flow: start (bind + import key + listen + spawn) -> accept worker -> stop.
// Worker context (entry): poll ctx.worker.isRunning().
// Owner context (stop): stop() notifies via Worker::stop().
// Busy: accept callback returns SSH_ACCEPT_BUSY (session isAlive) -> rejectPending.
// Idle: accept callback may ssh_bind_accept; listener sleeps between polls.

#include "ssh/listener.h"

#include <cerrno>

#include <fcntl.h>

#include <libssh/libssh.h>
#include <libssh/server.h>

#include "firmware.h"

#include "ssh/key.h"

namespace {
    // --- Bind ---

    // ssh_bind_set_blocking(): 0 = non-blocking.
    constexpr int SSH_NON_BLOCKING = 0;

    // Skip system-wide libssh server config (not present on device).
    constexpr bool SSH_PROCESS_CONFIG_ENABLED = false;

    // --- Timeout ---

    constexpr uint32_t ACCEPT_POLL_INTERVAL_MS = 100;

    // --- Worker ---

    constexpr const char* TASK_NAME = "ssh-listener";
    constexpr uint32_t TASK_STACK = 8192;
    constexpr uint8_t TASK_PRIORITY = 4;
} // namespace

// ============================================================
//                         SshListener
// ============================================================

// --- Lifecycle ---

SshListener::SshListener() = default;

// Create bind, import key, listen, spawn accept worker.
// On IMPORT_KEY success ownership moves to bind and key is cleared.
// On failure bind may be partially created; fail path calls reset().
int SshListener::start(SshHostKey& key, AcceptFn accept, void* userdata) {
    static constexpr Task LISTENER_TASK{
        .name = TASK_NAME,
        .stack = TASK_STACK,
        .priority = TASK_PRIORITY,
        .core = Firmware::CORE_PROTOCOL,
        .entry = SshListener::entry,
    };

    if (!key.isValid() || accept == nullptr) {
        return SSH_ERROR;
    }

    do {
        // Create the bind.
        this->bind = ssh_bind_new();
        if (this->bind == nullptr) {
            break;
        }

        // Set the bind options.
        if (ssh_bind_options_set(this->bind, SSH_BIND_OPTIONS_PROCESS_CONFIG, &SSH_PROCESS_CONFIG_ENABLED) != SSH_OK) {
            break;
        }
        if (ssh_bind_options_set(this->bind, SSH_BIND_OPTIONS_BINDPORT, &Firmware::NET_PORT_SSH) != SSH_OK) {
            break;
        }
        if (ssh_bind_options_set(this->bind, SSH_BIND_OPTIONS_IMPORT_KEY, key.get()) != SSH_OK) {
            break;
        }
        key.clear();

        // Listen for incoming connections.
        if (ssh_bind_listen(this->bind) != SSH_OK) {
            break;
        }

        // Set the bind to non-blocking mode.
        ssh_bind_set_blocking(this->bind, SSH_NON_BLOCKING);

        // Workaround: LibSSH-ESP32 ignores bind->blocking in ssh_bind_accept();
        // O_NONBLOCK on the listen socket is required.
        {
            const socket_t fd = ssh_bind_get_fd(this->bind);
            if (fd == SSH_INVALID_SOCKET) {
                break;
            }

            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0) {
                break;
            }

            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
                break;
            }
        }

        // Set the accept callback.
        this->accept = accept;
        this->userdata = userdata;

        // Spawn the accept worker.
        if (!this->worker.start(LISTENER_TASK, this)) {
            break;
        }

        return SSH_OK;
    } while (0);

    this->reset();
    return SSH_ERROR;
}

// stop -> join -> reset.
void SshListener::stop() {
    this->worker.stop();
    this->worker.join();
    this->reset();
}

// --- Operation ---

void SshListener::rejectPending() {
    if (this->bind == nullptr) {
        return;
    }

    // Reject extra clients through libssh.
    for (;;) {
        ssh_session pending = ssh_new();
        if (pending == nullptr) {
            break;
        }

        const int ret = ssh_bind_accept(this->bind, pending);
        if (ret == SSH_OK) {
            if (ssh_get_fd(pending) != SSH_INVALID_SOCKET) {
                ssh_disconnect(pending);
            }
            ssh_free(pending);
            continue;
        }

        ssh_free(pending);
        if (ret == SSH_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        break;
    }
}

void SshListener::reset() {
    if (this->bind != nullptr) {
        ssh_bind_free(this->bind);
        this->bind = nullptr;
    }

    this->accept = nullptr;
    this->userdata = nullptr;
}

// --- Worker ---

void SshListener::entry(const TaskContext& ctx) {
    SshListener* listener = static_cast<SshListener*>(ctx.userdata);

    // Worker context: poll ctx.worker.isRunning().
    while (ctx.worker.isRunning()) {
        if (listener->accept(listener->bind, listener->userdata) == SSH_ACCEPT_BUSY) {
            listener->rejectPending();
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ACCEPT_POLL_INTERVAL_MS));
    }
}
