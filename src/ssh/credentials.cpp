// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshCredentials checks for libssh auth callbacks.

#include "ssh/credentials.h"

#include <libssh/libssh.h>
#include <libssh/server.h>

#include "ssh/key.h"

// ============================================================
//                      SshCredentials
// ============================================================

int SshCredentials::checkPassword(const char* user, const char* password) const {
    if (!user || !password || this->username != user || this->password != password) {
        return SSH_AUTH_DENIED;
    }
    return SSH_AUTH_SUCCESS;
}

int SshCredentials::checkPubkey(const char* user, ssh_key pubkey, char signatureState) const {
    if (!user || this->username != user || !SshKey::isMatchedPublicKey(this->authorizedKey, pubkey)) {
        return SSH_AUTH_DENIED;
    }

    // Probe only: client asks whether this key would be accepted (no signature yet).
    if (signatureState == SSH_PUBLICKEY_STATE_NONE) {
        return SSH_AUTH_SUCCESS;
    }

    // Signed request: only a verified signature completes auth.
    if (signatureState != SSH_PUBLICKEY_STATE_VALID) {
        return SSH_AUTH_DENIED;
    }

    return SSH_AUTH_SUCCESS;
}

int SshCredentials::checkNone() const {
    return this->allowNoAuth ? SSH_AUTH_SUCCESS : SSH_AUTH_DENIED;
}
