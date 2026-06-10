// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Web HTTP transport implementation: HttpRequest body parsing and replies.

#include "web/request.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "web/model.h"
#include "web/restful.h"

// ============================================================
//                         HttpRequest
// ============================================================

namespace {

    void applyResponsePolicy(AsyncWebServerResponse& response, const Restful::ResponseHeader* policy) {
        for (const Restful::ResponseHeader* entry = policy; entry->key != nullptr; entry++) {
            response.addHeader(entry->key, entry->value);
        }
    }

} // namespace

const Restful::Error* HttpRequest::parseJson(JsonDocument& doc) const noexcept {
    // Body size bounds are enforced in WebServer before dispatch.
    if (this->data == nullptr || this->len == 0) {
        return &Restful::ERR_REQUEST_INVALID_FORMAT;
    }

    const DeserializationError err = deserializeJson(doc, this->data, this->len);
    if (err == DeserializationError::NoMemory || doc.overflowed()) {
        return &Restful::ERR_REQUEST_OVERFLOW;
    }
    if (err || !doc.is<JsonObjectConst>()) {
        return &Restful::ERR_REQUEST_INVALID_FORMAT;
    }

    return nullptr;
}

void HttpRequest::replyEmpty(const Route& route) const {
    AsyncWebServerResponse* response = this->req.beginResponse(Restful::HTTP_NO_CONTENT);

    applyResponsePolicy(*response, route.policy);
    this->req.send(response);
}

void HttpRequest::replyAsset(const Route& route, const Asset& asset) const {
    AsyncWebServerResponse* response = this->req.beginResponse(Restful::HTTP_OK, asset.mime, asset.data, asset.len);

    applyResponsePolicy(*response, route.policy);
    this->req.send(response);
}

void HttpRequest::replyJson(const Route& route, const JsonDocument& doc) const {
    if (doc.overflowed()) {
        this->replyError(Restful::ERR_RESPONSE_OVERFLOW);
        return;
    }

    AsyncResponseStream* response = this->req.beginResponseStream(Restful::MIME_JSON);
    applyResponsePolicy(*response, route.policy);
    serializeJson(doc, *response);
    this->req.send(response);
}

void HttpRequest::replyError(const Restful::Error& err) const {
    AsyncWebServerResponse* response = this->req.beginResponse(err.status, Restful::MIME_PROBLEM_JSON, err.body);

    applyResponsePolicy(*response, Restful::RESPONSE_POLICY_API);
    this->req.send(response);
}
