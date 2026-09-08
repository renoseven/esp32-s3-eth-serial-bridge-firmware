// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshConfigStorage implementation.
//
// Flow:
//   - read(): Nvs::read() with per-key defaults.
//   - updateHostKey(): Nvs::modify() for host-key bootstrap; sync mirror hostKey only.
//   - update(): Nvs::write().
//   - clear(): erase namespace.
// Serialize: fail-fast on first NVS write failure.
// Deserialize: read all keys with defaults.

#include "config/ssh.h"

#include <Preferences.h>

#include "firmware.h"

#include "util/nvs.h"

namespace {
    // --- NVS ---

    constexpr const char NVS_NS[] = "ssh";

    constexpr const char NVS_KEY_HOST_KEY[] = "hostKey";
    constexpr const char NVS_KEY_USERNAME[] = "username";
    constexpr const char NVS_KEY_PASSWORD[] = "password";
    constexpr const char NVS_KEY_AUTHORIZED_KEY[] = "authorizedKey";
    constexpr const char NVS_KEY_ALLOW_NO_AUTH[] = "allowNoAuth";

    // --- Serializer ---

    // Write SshConfig fields into open writable Preferences.
    bool serializeConfig(Preferences& prefs, const SshConfig& config) {
        if (prefs.putString(NVS_KEY_HOST_KEY, config.hostKey) != config.hostKey.length()) {
            return false;
        }
        if (prefs.putString(NVS_KEY_USERNAME, config.username) != config.username.length()) {
            return false;
        }
        if (prefs.putString(NVS_KEY_PASSWORD, config.password) != config.password.length()) {
            return false;
        }
        if (prefs.putString(NVS_KEY_AUTHORIZED_KEY, config.authorizedKey) != config.authorizedKey.length()) {
            return false;
        }
        if (prefs.putBool(NVS_KEY_ALLOW_NO_AUTH, config.allowNoAuth) != sizeof(uint8_t)) {
            return false;
        }

        return true;
    }

    // --- Deserializer ---

    // Load SshConfig fields from open Preferences; missing keys use firmware defaults.
    bool deserializeConfig(Preferences& prefs, SshConfig& config) {
        config.hostKey = prefs.getString(NVS_KEY_HOST_KEY, Firmware::DEFAULT_SSH_HOST_KEY);
        config.username = prefs.getString(NVS_KEY_USERNAME, Firmware::DEFAULT_SSH_USERNAME);
        config.password = prefs.getString(NVS_KEY_PASSWORD, Firmware::DEFAULT_SSH_PASSWORD);
        config.authorizedKey = prefs.getString(NVS_KEY_AUTHORIZED_KEY, Firmware::DEFAULT_SSH_AUTHORIZED_KEY);
        config.allowNoAuth = prefs.getBool(NVS_KEY_ALLOW_NO_AUTH, Firmware::DEFAULT_SSH_ALLOW_NO_AUTH);

        return true;
    }

    // --- Nvs ---

    Nvs<SshConfig> nvs(NVS_NS, serializeConfig, deserializeConfig);
} // namespace

// ============================================================
//                            SshConfigStorage
// ============================================================

bool SshConfigStorage::read(SshConfig& config) {
    return nvs.read(config);
}

bool SshConfigStorage::update(const SshConfig& config) {
    return nvs.write(config);
}

bool SshConfigStorage::updateHostKey(SshConfig& config, const String& hostKey) {
    if (!nvs.modify([&hostKey](SshConfig& stored) { stored.hostKey = hostKey; })) {
        return false;
    }

    config.hostKey = hostKey;
    return true;
}

bool SshConfigStorage::clear() {
    return nvs.clear();
}
