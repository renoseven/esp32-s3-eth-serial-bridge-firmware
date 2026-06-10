// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshKey helpers and SshHostKey implementation.

#include "ssh/key.h"

#include <cstring>

#include <libssh/libssh.h>

#include "firmware.h"

namespace {
    // Import an OpenSSH public key line into key. Caller must ssh_key_free on success.
    // Format: "type base64 [comment]". Mutates a local copy only.
    int importPublicKey(const String& publicKey, ssh_key& key) {
        key = nullptr;

        if (publicKey.isEmpty() || publicKey.length() > Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX_LEN) {
            return SSH_ERROR;
        }

        String buffer = publicKey;
        buffer.trim();

        char* const typeName = strtok(buffer.begin(), " ");
        char* const base64 = strtok(nullptr, " ");
        if (typeName == nullptr || base64 == nullptr) {
            return SSH_ERROR;
        }

        if (strlen(typeName) > Firmware::LIMIT_SSH_KEY_TYPE_MAX_LEN) {
            return SSH_ERROR;
        }

        const enum ssh_keytypes_e type = ssh_key_type_from_name(typeName);
        if (type == SSH_KEYTYPE_UNKNOWN) {
            return SSH_ERROR;
        }

        return ssh_pki_import_pubkey_base64(base64, type, &key);
    }

    // Export a public key to OpenSSH authorized_keys line into publicKey.
    int exportPublicKey(ssh_key key, String& publicKey) {
        if (key == nullptr) {
            return SSH_ERROR;
        }

        const char* const typeName = ssh_key_type_to_char(ssh_key_type(key));
        if (typeName == nullptr) {
            return SSH_ERROR;
        }

        char* exported = nullptr;
        const int ret = ssh_pki_export_pubkey_base64(key, &exported);
        if (ret != SSH_OK) {
            return ret;
        }

        publicKey = typeName;
        publicKey += ' ';
        publicKey += exported;

        ssh_string_free_char(exported);
        return SSH_OK;
    }

    // Ensure the server host key exists and publish its OpenSSH-format public
    // key into hostPublicKey. When a new key is generated, hostPrivateKey is
    // updated with the exported private key (caller persists it).
    int resolveHostKey(String& hostPrivateKey, String& hostPublicKey, ssh_key& key) {
        key = nullptr;
        int ret = SSH_ERROR;

        do {
            // Load the provided host private key, if any.
            if (!hostPrivateKey.isEmpty() &&
                ssh_pki_import_privkey_base64(hostPrivateKey.c_str(), nullptr, nullptr, nullptr, &key) != SSH_OK) {
                key = nullptr;
            }

            // Generate and export a new key when import failed or none was provided.
            if (key == nullptr) {
                if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key) != SSH_OK) {
                    break;
                }

                char* exported = nullptr;
                if (ssh_pki_export_privkey_base64(key, nullptr, nullptr, nullptr, &exported) != SSH_OK) {
                    break;
                }

                hostPrivateKey = exported;
                ssh_string_free_char(exported);
            }

            // Publish the OpenSSH-format public key for the status page.
            if (exportPublicKey(key, hostPublicKey) != SSH_OK) {
                break;
            }

            ret = SSH_OK;
        } while (0);

        return ret;
    }
} // namespace

// ============================================================
//                           SshKey
// ============================================================

bool SshKey::isValidPublicKey(const String& str) noexcept {
    ssh_key key = nullptr;

    if (importPublicKey(str, key) != SSH_OK) {
        return false;
    }

    ssh_key_free(key);
    return true;
}

bool SshKey::isMatchedPublicKey(const String& publicKey, ssh_key key) noexcept {
    if (key == nullptr) {
        return false;
    }

    ssh_key imported = nullptr;
    if (importPublicKey(publicKey, imported) != SSH_OK) {
        return false;
    }

    const bool matched = ssh_key_cmp(key, imported, SSH_KEY_CMP_PUBLIC) == 0;
    ssh_key_free(imported);

    return matched;
}

// ============================================================
//                         SshHostKey
// ============================================================

SshHostKey::~SshHostKey() {
    this->destroy();
}

int SshHostKey::load(const String& hostPrivateKey) {
    this->destroy();
    this->privateKey = hostPrivateKey;
    return resolveHostKey(this->privateKey, this->publicKey, this->key);
}

void SshHostKey::destroy() {
    if (this->key != nullptr) {
        ssh_key_free(this->key);
        this->key = nullptr;
    }
    this->privateKey.clear();
    this->publicKey.clear();
}
