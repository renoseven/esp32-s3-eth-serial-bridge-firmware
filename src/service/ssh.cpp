// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshService implementation: serial bridge over an SshServer session channel.

#include "service/ssh.h"

#include <libssh/libssh.h>

#include "firmware.h"

#include "config/ssh.h"

#include "service/serial.h"

#include "ssh/key.h"

namespace {
    // Written after login, before bridging. CRLF for raw (non-PTY) terminals.
    constexpr const char SSH_MOTD[] = "This SSH session is bridged to the device serial port.\r\n"
                                      "Escape sequence is '~' '.' (at line start)\r\n";
} // namespace

// ============================================================
//                         SshService
// ============================================================

// --- Constructor ---

SshService::SshService(SerialService& serial, SshConfig& cfg) : serial(serial), config(cfg) {}

// --- Validation ---

bool SshService::isValidOpenSshPublicKey(const String& str) const noexcept {
    return SshKey::isValidPublicKey(str);
}

// --- Lifecycle ---

bool SshService::begin() {
    const SshCredentials credentials{this->config.username, this->config.password, this->config.authorizedKey,
                                     this->config.allowNoAuth};
    if (!this->server.begin(this->config.hostKey, credentials, SshService::bridge, this)) {
        return false;
    }

    const String& hostKey = this->server.getHostPrivateKey();
    if (hostKey != this->config.hostKey) {
        return SshConfigStorage::updateHostKey(this->config, hostKey);
    }

    return true;
}

void SshService::end() {
    this->server.end();
}

bool SshService::restart() {
    this->end();
    return this->begin();
}

// --- Bridge ---

void SshService::bridge(ssh_channel channel, void* userdata) {
    SshService& service = *static_cast<SshService*>(userdata);

    if (channel == nullptr) {
        return;
    }

    ssh_channel_write(channel, SSH_MOTD, sizeof(SSH_MOTD) - 1);

    RingReader reader = service.serial.reader(); // physical serial RX -> channel
    RingWriter writer = service.serial.writer(); // channel -> physical serial TX

    while (service.server.isActive()) {
        bool didWork = false;

        if (!ssh_channel_is_open(channel)) {
            return;
        }

        if (ssh_channel_is_eof(channel)) {
            return;
        }

        // SSH channel -> txBuf: drain available channel data into the ring buffer.
        for (;;) {
            const size_t len = writer.len();
            if (len == 0) {
                break;
            }

            const int read = ssh_channel_read_nonblocking(channel, writer.data(), len, 0);
            if (read < 0) {
                return;
            }
            if (read == 0) {
                break;
            }

            writer.commit(static_cast<size_t>(read));
            didWork = true;
        }

        // rxBuf -> SSH channel: forward buffered serial data to the client.
        for (;;) {
            const size_t len = reader.len();
            if (len == 0) {
                break;
            }

            const int written = ssh_channel_write(channel, reader.data(), len);
            if (written < 0) {
                return;
            }
            if (written == 0) {
                break;
            }

            reader.consume(static_cast<size_t>(written));
            didWork = true;
        }

        if (!didWork) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
        }
    }
}
