// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WebServer: serves the embedded UI and the configuration REST API over HTTP.
//
// Owns the async HTTP server and, on begin(), registers every domain's routes
// against its handler.

#pragma once

#include <ESPAsyncWebServer.h>

// ============================================================
//                     Forward declarations
// ============================================================

class Runtime;

// ============================================================
//                         WebServer
// ============================================================

class WebServer {
  public:
    // --- Constructor ---

    explicit WebServer(Runtime& runtime);

    // --- Lifecycle ---

    // Register assets and domains, then start listening.
    void begin();

  private:
    // --- References ---

    Runtime& runtime;      // Composition root for config and services.
    AsyncWebServer server; // HTTP async server.
};
