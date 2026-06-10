// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshServer implementation: orchestrates SshHostKey, SshListener, and SshSession.
//
// Flow:
//   - begin(): hostKey.load + listener.start; end(): session.close + listener.stop + hostKey.destroy.
//   - Listener worker: accept while isRunning(); exit via listener.stop().
//   - Session worker: KEX -> negotiate -> io; exit via reset(); owner joins via session.close().

#include "ssh/server.h"

#include <libssh/libssh.h>
#include <libssh_esp32.h>

// ============================================================
//                         SshServer
// ============================================================

// --- Constructor ---

SshServer::SshServer() {
    libssh_begin();
}

// --- Lifecycle ---

bool SshServer::begin(const String& hostPrivateKey, const SshCredentials& credentials, ChannelIo io, void* userdata) {
    this->credentials = credentials;
    this->io = io;
    this->userdata = userdata;

    if (this->hostKey.load(hostPrivateKey) != SSH_OK) {
        this->end();
        return false;
    }

    if (this->listener.start(this->hostKey, SshServer::accept, this) != SSH_OK) {
        this->end();
        return false;
    }

    return true;
}

void SshServer::end() {
    // Session first so the bind (and imported host key) outlive KEX / bridge.
    // reset/stop each: stop() notifies and join via Worker.
    this->session.close();
    this->listener.stop();
    this->hostKey.destroy();
}

// --- Accept ---

int SshServer::accept(ssh_bind bind, void* userdata) {
    SshServer& server = *static_cast<SshServer*>(userdata);

    // A client is connected; listener rejectPending until the client disconnects.
    if (server.session.isAlive()) {
        return SSH_ACCEPT_BUSY;
    }

    const int ret = server.session.accept(bind, server.credentials, server.io, server.userdata);
    if (ret != SSH_OK && ret != SSH_AGAIN) {
        server.session.close();
    }

    return ret;
}
