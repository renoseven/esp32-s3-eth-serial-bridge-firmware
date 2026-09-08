// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web HTTP transport: the wrapper for one handler invocation.
//
// HttpRequest carries the inbound request/body context and produces the wire
// replies (assets, JSON, errors); it holds no routing knowledge.

#pragma once

#include <cstddef>
#include <cstdint>

#include <ArduinoJson.h>

// ============================================================
//                     Forward declarations
// ============================================================

class AsyncWebServerRequest;

struct Asset;
struct Route;

namespace Restful {
    struct Error;
}

// ============================================================
//                         HttpRequest
// ============================================================

// One HTTP exchange: request context and reply helpers.
class HttpRequest {
  public:
    explicit HttpRequest(AsyncWebServerRequest& req, const uint8_t* data = nullptr, size_t len = 0)
        : req(req), data(data), len(len) {}

    // Parse the POST body into JSON doc (body size already enforced by WebServer).
    const Restful::Error* parseJson(JsonDocument& doc) const noexcept;

    // Success replies send under the route's response policy.

    // Send 204.
    void replyEmpty(const Route& route) const;

    // Send 200 with a static asset payload.
    void replyAsset(const Route& route, const Asset& asset) const;

    // Send 200 with doc serialized as JSON.
    void replyJson(const Route& route, const JsonDocument& doc) const;

    // Send the error's status code and problem+json body (API policy).
    void replyError(const Restful::Error& err) const;

  private:
    AsyncWebServerRequest& req; // Outbound response is sent through this.
    const uint8_t* data;        // POST body bytes; nullptr when absent.
    size_t len;                 // POST body length; 0 when data is nullptr.
};
