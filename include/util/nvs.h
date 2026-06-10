// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Typed read/write storage in NVS.
//
// Implementation:
//   - Serializer and Deserializer function pointers wire T to Preferences keys.
//   - Mutex serializes every storage operation on the namespace.
//   - modify() read-modify-writes in memory under one lock for partial updates.
//   - Mutator must not block or re-enter the same Nvs instance during modify().
//   - read() opens read-only; write() and clear() open read-write; all return false on failure.

#pragma once

#include <utility>

#include <Preferences.h>

#include "util/mutex.h"

// ============================================================
//                          Nvs
// ============================================================

// Typed read/write storage in NVS.
template <typename T> class Nvs {
  public:
    // --- Types ---

    using Serializer = bool (*)(Preferences&, const T&);
    using Deserializer = bool (*)(Preferences&, T&);

    // --- Constructor ---

    Nvs(const char* ns, Serializer serializer, Deserializer deserializer);

    // --- Storage ---

    // Load from storage into out (defaults for missing keys).
    bool read(T& out) const;

    // Read-modify-write under lock; mutator must not block or re-enter storage.
    template <typename Mutator> bool modify(Mutator&& mutator);

    // Replace persisted config under lock.
    bool write(const T& config);

    // Erase all persisted keys.
    bool clear();

  private:
    // --- Preferences ---

    const char* ns;      // Preferences namespace.
    mutable Mutex mutex; // Lock for nvs operations.

    // --- Serializers ---

    Serializer serializer;     // Serialize T into Preferences.
    Deserializer deserializer; // Deserialize T from Preferences.
};

// ============================================================
//                       Nvs impl
// ============================================================

// --- Constructor ---

template <typename T>
Nvs<T>::Nvs(const char* ns, Serializer serializer, Deserializer deserializer)
    : ns(ns), serializer(serializer), deserializer(deserializer) {}

// --- Storage ---

template <typename T> bool Nvs<T>::read(T& out) const {
    Preferences prefs;

    bool ret = false;
    {
        MutexGuard guard(this->mutex);

        if (!prefs.begin(this->ns, true)) {
            return false;
        }

        ret = this->deserializer(prefs, out);
        prefs.end();
    }

    return ret;
}

template <typename T> template <typename Mutator> bool Nvs<T>::modify(Mutator&& mutator) {
    Preferences prefs;

    bool ret = false;
    {
        MutexGuard guard(this->mutex);

        if (!prefs.begin(this->ns, false)) {
            return false;
        }

        T config;
        if (this->deserializer(prefs, config)) {
            std::forward<Mutator>(mutator)(config);
            ret = this->serializer(prefs, config);
        }

        prefs.end();
    }

    return ret;
}

template <typename T> bool Nvs<T>::write(const T& config) {
    Preferences prefs;

    bool ret = false;
    {
        MutexGuard guard(this->mutex);

        if (!prefs.begin(this->ns, false)) {
            return false;
        }

        ret = this->serializer(prefs, config);
        prefs.end();
    }

    return ret;
}

template <typename T> bool Nvs<T>::clear() {
    Preferences prefs;

    bool ret = false;
    {
        MutexGuard guard(this->mutex);

        if (!prefs.begin(this->ns, false)) {
            return false;
        }

        ret = prefs.clear();
        prefs.end();
    }

    return ret;
}
