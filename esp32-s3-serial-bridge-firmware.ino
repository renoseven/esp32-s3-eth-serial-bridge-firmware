// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SerialBridge - ESP32-S3 USB-CDC serial port over SSH.
//
// Arduino sketch entry point: setup()/loop() compose Runtime and WebServer,
// then forward lifecycle calls.

#include "runtime.h"
#include "web/server.h"

static Runtime g_runtime;
static WebServer g_web_server{g_runtime};

void setup() {
    g_runtime.begin();
    g_web_server.begin();
}

void loop() {
    g_runtime.loop();
}
