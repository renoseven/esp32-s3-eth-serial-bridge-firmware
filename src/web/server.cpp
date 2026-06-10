// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// WebServer implementation: route wiring only.
//
// begin() walks a route-table/HttpHandler pairing for every domain and
// registers each route on the async server. GET dispatches straight to the
// handler; POST rejects empty/undersized/oversized or fragmented bodies and
// dispatches only when the body arrives in a single chunk. Handlers run on
// the AsyncTCP task.

#include "web/server.h"

#include "firmware.h"

#include "runtime.h"

#include "web/domain.h"
#include "web/handler.h"
#include "web/model.h"
#include "web/request.h"
#include "web/restful.h"

namespace {

    struct Domain {
        const Route* routes;        // ROUTE_END-terminated route table.
        const HttpHandler& handler; // Domain handler.
    };

} // namespace

// ============================================================
//                         WebServer
// ============================================================

// --- Constructor ---

WebServer::WebServer(Runtime& runtime) : runtime(runtime), server(Firmware::NET_PORT_HTTP) {}

// --- Lifecycle ---

void WebServer::begin() {
    static const AssetHandler ASSET_HANDLER(this->runtime);
    static const StatusHandler STATUS_HANDLER(this->runtime);
    static const DeviceConfigHandler DEVICE_HANDLER(this->runtime);
    static const WifiConfigHandler WIFI_HANDLER(this->runtime);
    static const SshConfigHandler SSH_HANDLER(this->runtime);
    static const SystemHandler SYSTEM_HANDLER(this->runtime);

    static const Domain DOMAINS[] = {
        {ASSET_ROUTES, ASSET_HANDLER}, {STATUS_ROUTES, STATUS_HANDLER}, {DEVICE_ROUTES, DEVICE_HANDLER},
        {WIFI_ROUTES, WIFI_HANDLER},   {SSH_ROUTES, SSH_HANDLER},       {SYSTEM_ROUTES, SYSTEM_HANDLER},
    };

    for (const Domain& domain : DOMAINS) {
        for (const Route* it = domain.routes; it->path != nullptr; it++) {
            // Both referents have static storage, so the reference captures
            // below outlive the registration loop.
            const Route& route = *it;
            const HttpHandler& handler = domain.handler;
            const WebRequestMethod method = static_cast<WebRequestMethod>(route.method);

            switch (method) {
            case Restful::HTTP_GET:
                this->server.on(route.path, method, [&route, &handler](AsyncWebServerRequest* req) { //
                    handler.handle(route, HttpRequest{*req});
                });
                break;
            case Restful::HTTP_POST:
                this->server.on(
                    route.path, method,
                    [&route, &handler](AsyncWebServerRequest* req) {
                        // Content-Length 0 never reaches the body callback.
                        if (req->contentLength() != 0) {
                            return;
                        }

                        const HttpRequest request{*req};
                        if (route.minPostBodyLen > 0) {
                            request.replyError(Restful::ERR_REQUEST_EMPTY);
                            return;
                        }

                        handler.handle(route, request);
                    },
                    nullptr,
                    [&route, &handler](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
                                       size_t total) {
                        if (index != 0) {
                            return;
                        }

                        const HttpRequest request{*req, data, len};

                        if (total < route.minPostBodyLen) {
                            request.replyError(Restful::ERR_REQUEST_TOO_SMALL);
                            return;
                        }
                        if (total > route.maxPostBodyLen) {
                            request.replyError(Restful::ERR_REQUEST_TOO_LARGE);
                            return;
                        }
                        if (len != total) {
                            request.replyError(Restful::ERR_REQUEST_INVALID_FORMAT);
                            return;
                        }

                        handler.handle(route, request);
                    });
                break;
            default:
                break;
            }
        }
    }

    this->server.onNotFound([](AsyncWebServerRequest* req) { req->redirect(Restful::ROUTE_INDEX); });

    this->server.begin();
}
