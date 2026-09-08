// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshSession implementation.
//
// Flow: accept (bind accept + spawn) -> keyExchange -> negotiate -> io -> reset.
// isAlive(): client connected (state != Idle), independent of worker.isAlive().
// Worker context (entry, KEX, negotiate): poll ctx.worker.isRunning().
// Owner context (close, io): poll isActive() -> worker.isRunning().
// accept() is only called when !isAlive(); server.isConnected() drives listener BUSY.
// Worker path: stages -> reset(); worker self-deletes (no self-join).
// Owner path: close() = stop -> join -> reset (idempotent if entry already reset).
// ChannelIo must return when isActive() becomes false.

#include "ssh/session.h"

#include <cerrno>

#include <Arduino.h>
#include <libssh/libssh.h>
#include <lwip/tcp.h>

#include "firmware.h"

namespace {
    // --- Transport ---

    // ssh_set_blocking(): 0 = non-blocking.
    constexpr int SSH_NON_BLOCKING = 0;

    // Disable Nagle for lower latency on small writes.
    constexpr int SSH_TCP_NODELAY = 1;

    // --- Timeout ---

    constexpr uint32_t KEX_TIMEOUT_MS = 15000;
    constexpr uint32_t KEX_POLL_INTERVAL_MS = 10;
    constexpr uint32_t NEGOTIATE_TIMEOUT_MS = 60000;
    constexpr uint32_t NEGOTIATE_POLL_INTERVAL_MS = 10;

    // --- Worker ---

    constexpr const char* TASK_NAME = "ssh-session";
    constexpr uint32_t TASK_STACK = 16384;
    constexpr uint8_t TASK_PRIORITY = 6;
} // namespace

// ============================================================
//                   SshSession::Callbacks
// ============================================================

struct SshSession::Callbacks {
    static int channelPtyRequest(ssh_session, ssh_channel, const char*, int, int, int, int, void*) {
        return SSH_OK;
    }

    static int channelShellRequest(ssh_session, ssh_channel, void* userdata) {
        static_cast<SshSession*>(userdata)->state.store(State::Established, std::memory_order_relaxed);
        return SSH_OK;
    }

    static ssh_channel channelOpenSession(ssh_session session, void* userdata) {
        SshSession* self = static_cast<SshSession*>(userdata);

        ssh_channel channel = ssh_channel_new(session);
        if (channel == nullptr) {
            return nullptr;
        }

        ssh_set_channel_callbacks(channel, &self->channelCallbacks);

        self->channel = channel;
        self->state.store(State::StartingShell, std::memory_order_relaxed);

        return channel;
    }

    static int authPassword(ssh_session, const char* user, const char* password, void* userdata) {
        SshSession* self = static_cast<SshSession*>(userdata);

        const int ret = self->credentials.checkPassword(user, password);
        if (ret == SSH_AUTH_SUCCESS) {
            self->state.store(State::OpeningChannel, std::memory_order_relaxed);
        }

        return ret;
    }

    static int authPubkey(ssh_session, const char* user, struct ssh_key_struct* pubkey, char signatureState,
                          void* userdata) {
        SshSession* self = static_cast<SshSession*>(userdata);

        const int ret = self->credentials.checkPubkey(user, pubkey, signatureState);
        if (ret == SSH_AUTH_SUCCESS && signatureState == SSH_PUBLICKEY_STATE_VALID) {
            self->state.store(State::OpeningChannel, std::memory_order_relaxed);
        }

        return ret;
    }

    static int authNone(ssh_session, const char*, void* userdata) {
        SshSession* self = static_cast<SshSession*>(userdata);

        const int ret = self->credentials.checkNone();
        if (ret == SSH_AUTH_SUCCESS) {
            self->state.store(State::OpeningChannel, std::memory_order_relaxed);
        }

        return ret;
    }
};

// ============================================================
//                         SshSession
// ============================================================

// --- Lifecycle ---

SshSession::SshSession() = default;

// Accept callback returns SSH_ACCEPT_BUSY / SSH_AGAIN / SSH_ERROR (libssh codes).
int SshSession::accept(ssh_bind bind, const SshCredentials& credentials, ChannelIo io, void* userdata) {
    static constexpr Task SESSION_TASK{
        .name = TASK_NAME,
        .stack = TASK_STACK,
        .priority = TASK_PRIORITY,
        .core = Firmware::CORE_PROTOCOL,
        .entry = SshSession::entry,
    };

    // Join a finishing worker (reset() done, task handle not yet cleared).
    if (this->worker.isAlive()) {
        this->worker.join();
    }

    if (this->isAlive() || this->worker.isAlive()) {
        return SSH_ERROR;
    }

    this->session = ssh_new();
    if (this->session == nullptr) {
        return SSH_ERROR;
    }

    const int ret = ssh_bind_accept(bind, this->session);
    if (ret != SSH_OK) {
        this->reset();
        return (ret == SSH_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK) ? SSH_AGAIN : SSH_ERROR;
    }

    // Claim the slot before configuring / spawning.
    this->state.store(State::Authenticating, std::memory_order_relaxed);
    this->io = io;
    this->userdata = userdata;
    this->channel = nullptr;
    this->credentials = credentials;

    const socket_t fd = ssh_get_fd(this->session);
    if (fd != SSH_INVALID_SOCKET) {
        // Best-effort latency tweak; connection works without TCP_NODELAY.
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &SSH_TCP_NODELAY, sizeof(SSH_TCP_NODELAY));
    }

    const int authMethods = this->credentials.allowNoAuth
                                ? (SSH_AUTH_METHOD_NONE | SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY)
                                : (SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
    ssh_set_auth_methods(this->session, authMethods);
    ssh_set_blocking(this->session, SSH_NON_BLOCKING);

    if (!this->worker.start(SESSION_TASK, this)) {
        this->reset();
        return SSH_ERROR;
    }

    return SSH_OK;
}

// close -> join -> reset. Safe if entry already reset.
void SshSession::close() {
    this->worker.stop();
    this->worker.join();
    this->reset();
}

// --- Operation ---

// Poll ctx.worker.isRunning() until OK, error, timeout, or stop.
int SshSession::keyExchange(const TaskContext& ctx) {
    const uint32_t start = millis();

    while (ctx.worker.isRunning()) {
        const int ret = ssh_handle_key_exchange(this->session);
        if (ret == SSH_OK) {
            return SSH_OK;
        }
        if (ret != SSH_AGAIN) {
            return SSH_ERROR;
        }
        if (millis() - start >= KEX_TIMEOUT_MS) {
            return SSH_ERROR;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(KEX_POLL_INTERVAL_MS));
    }

    return SSH_ERROR;
}

// Poll ctx.worker.isRunning() until Established (or error / timeout).
int SshSession::negotiate(const TaskContext& ctx) {
    ssh_callbacks_init(&this->serverCallbacks);
    this->serverCallbacks.userdata = this;
    this->serverCallbacks.auth_password_function = Callbacks::authPassword;
    this->serverCallbacks.auth_pubkey_function = Callbacks::authPubkey;
    this->serverCallbacks.auth_none_function = Callbacks::authNone;
    this->serverCallbacks.channel_open_request_session_function = Callbacks::channelOpenSession;
    ssh_set_server_callbacks(this->session, &this->serverCallbacks);

    ssh_callbacks_init(&this->channelCallbacks);
    this->channelCallbacks.userdata = this;
    this->channelCallbacks.channel_pty_request_function = Callbacks::channelPtyRequest;
    this->channelCallbacks.channel_shell_request_function = Callbacks::channelShellRequest;

    const uint32_t start = millis();

    while (ctx.worker.isRunning() && this->state.load(std::memory_order_relaxed) != State::Established) {
        if (!ssh_is_connected(this->session)) {
            return SSH_ERROR;
        }
        if (millis() - start >= NEGOTIATE_TIMEOUT_MS) {
            return SSH_ERROR;
        }

        ssh_execute_message_callbacks(this->session);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NEGOTIATE_POLL_INTERVAL_MS));
    }

    if (this->state.load(std::memory_order_relaxed) != State::Established) {
        return SSH_ERROR;
    }

    return SSH_OK;
}

void SshSession::reset() {
    if (this->channel != nullptr) {
        ssh_channel_close(this->channel);
        ssh_channel_free(this->channel);
        this->channel = nullptr;
    }

    if (this->session != nullptr) {
        if (ssh_get_fd(this->session) != SSH_INVALID_SOCKET) {
            ssh_disconnect(this->session);
        }
        ssh_free(this->session);
        this->session = nullptr;
    }

    this->io = nullptr;
    this->userdata = nullptr;
    this->credentials = {};
    this->state.store(State::Idle, std::memory_order_relaxed);
}

// --- Worker ---

void SshSession::entry(const TaskContext& ctx) {
    SshSession* session = static_cast<SshSession*>(ctx.userdata);

    // Worker context: poll ctx.worker.isRunning(); stages do the same.
    do {
        if (!ctx.worker.isRunning()) {
            break;
        }
        if (session->keyExchange(ctx) != SSH_OK) {
            break;
        }
        if (session->negotiate(ctx) != SSH_OK) {
            break;
        }
        if (session->io == nullptr) {
            break;
        }
        session->io(session->channel, session->userdata);
    } while (0);

    // Reset client state only. Do not close()/join here — self-join is a no-op and
    // Worker::taskEntry clears the task handle after this returns.
    session->reset();
}
