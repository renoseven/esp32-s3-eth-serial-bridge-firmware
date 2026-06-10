# SerialBridge

USB Serial over SSH - firmware for the ESP32-S3.

English | [简体中文](README_zh.md)

SerialBridge turns an ESP32-S3 into a dual-access serial adapter: locally it
enumerates as a USB-CDC serial port (`/dev/ttyACM*` / `COMx`), and the same
serial channel is reachable over SSH via Wi-Fi. A built-in web UI on port 80
handles configuration. It is meant for headless lab benches, server rooms, and
embedded targets where you want both a local USB serial port and remote network
access without a dedicated PC tethered to the device.

## Contents

- [Features](#features)
- [Architecture](#architecture)
- [Hardware](#hardware)
- [Quick Start](#quick-start)
- [Host Setup (Linux)](#host-setup-linux)
- [Status LED](#status-led)
- [Networking](#networking)
- [SSH Access](#ssh-access)
- [Web UI & REST API](#web-ui--rest-api)
- [Configuration Reference](#configuration-reference)
- [Dependencies](#dependencies)
- [Installing Dependencies](#installing-dependencies)
- [Build & Flash](#build--flash)
- [Web Assets](#web-assets)
- [Project Layout](#project-layout)
- [License](#license)

## Features

- **USB-CDC serial** - the ESP32-S3 enumerates as a CDC device on the host.
  The same byte stream is bridged to SSH clients. A `SerialService` Worker on
  `Firmware::CORE_SERVICE` moves data through lock-free 8 KiB SPSC ring buffers
  (`RingBuf`) in each direction; peers use zero-copy `RingReader` / `RingWriter`
  views.
- **SSH server** - LibSSH-ESP32 on port 22, layered in `include/ssh/`
  (`SshServer` -> `SshListener` + `SshSession`). Password and public-key
  authentication, plus an optional no-auth mode toggled from the web UI. A
  persistent Ed25519 host key is generated on first SSH start.
- **Web UI** - single-page configuration and status on port 80: live
  device/network/SSH snapshot, network and SSH settings, English / 简体中文
  language switch, reboot, and factory reset.
- **Wi-Fi** - Off / Client (STA) / Access Point modes with independent STA and
  AP credentials. Unconfigured or unreachable STA falls back to a captive-portal
  AP for first-time setup.
- **RGB status LED** (`Firmware::LED_PIN`, GPIO 21) - boot progress colors, then off; USB TX/RX blink while traffic is present.
- **Persistent configuration** - device, Wi-Fi, and SSH settings in NVS survive
  reboots.

## Architecture

```
Entry (.ino)
  ├── hardware/        board peripherals (global `LED`, like USB)
  ├── Runtime          composition root: config mirrors + services + control API
  │     ├── config/    *Config / *ConfigStorage (device, wifi, ssh)
  │     ├── util/      deferred scheduler, Nvs, SPSC RingBuf, StringView, regex cache, Worker
  │     ├── ssh/       LibSSH server: host key, listener, session, auth
  │     └── service/   device status, USB serial bridge, Wi-Fi, SSH gateway
  └── WebServer        HTTP/REST adapter above Runtime
```

| Layer | Location | Role |
| ----- | -------- | ---- |
| Constants | `include/firmware.h` | Namespace `Firmware`: version, MCU layout (`CORE_*`, `CACHE_LINE_SIZE`), LED pins, network ports, bridge/Wi-Fi timing, deferred apply delays, factory defaults, validation limits |
| Config | `include/config/` | Value objects and ConfigStorage API; device name defaults from eFuse MAC; AP SSID defaults to device name (NVS key omitted when equal; read falls back to device namespace) |
| Util | `include/util/` | `DeferredScheduler` fixed-capacity deferred scheduler (`Deferred`, `Deferred::Entry`), mutex-protected `Nvs`, SPSC `RingBuf` with non-owning `RingReader` / `RingWriter` views (cache-line aligned cursors via `Firmware::CACHE_LINE_SIZE`), non-owning `StringView` (`string.h`), POSIX regex cache (`regex.h`), `Worker` task lifecycle (defaults to `Firmware::CORE_SERVICE`), mutex/semaphore primitives |
| Hardware | `include/hardware/` | Global `LED` (Arduino-style, like `USB`) |
| SSH | `include/ssh/` | `SshServer` orchestrates `SshHostKey`, `SshListener`, and `SshSession` on `Worker`; `SshCredentials`; `SshKey` validation helpers |
| Services | `include/service/` | Device status, USB-CDC serial bridge (`SerialService`), Wi-Fi, SSH gateway (`SshService` bridges serial rings to an SSH channel) |
| Runtime | `include/runtime.h` | Owns config mirrors and services; boots LED/USB -> config -> serial -> Wi-Fi -> SSH; compile-time AsyncTCP core check; exposes config queries, service getters, and bool save/reset/reboot with deferred restarts |
| Web | `include/web/` | `restful.h` wire contract, `model.h`/`domain.h` data model + per-domain Field/Route tables, `value.h` field read/validate/write, `request.h` HTTP transport, `handler.h` per-domain handlers, `server.h` route registration; handlers read live status from services and delegate persistence to Runtime |

**Dual-core layout** (`include/firmware.h`) - ESP32-S3 splits protocol
stack work on core 0 and application services on core 1:

| Core | Constant | Typical work |
| ---- | -------- | ------------ |
| 0 | `Firmware::CORE_PROTOCOL` | ESP-IDF Wi-Fi, lwIP, esp_timer; SSH `Worker`s (`SshListener`, `SshSession`); AsyncTCP / Web handlers (`build_opt.h`: `CONFIG_ASYNC_TCP_RUNNING_CORE=0`) |
| 1 | `Firmware::CORE_SERVICE` | Arduino `loopTask` and event task (`sketch.yaml`: `LoopCore=1`, `EventsCore=1`): deferred scheduler, Wi-Fi state machine; USB-CDC serial bridge `Worker` |

Pinned `Worker` tasks use `Firmware::CORE_*` in code; Arduino loop core comes from
the FQBN. Keep `LoopCore=1` — moving the loop to core 0 overloads the protocol core
and can trigger watchdog reboots. USB-CDC and SSH byte streams meet in lock-free
SPSC rings across the two cores.

`build_opt.h` sets `CONFIG_ASYNC_TCP_RUNNING_CORE=0` (Async TCP defaults to `-1`,
any core, when unset). `src/runtime.cpp` static_asserts it equals
`Firmware::CORE_PROTOCOL`. `Worker::Task` defaults to `Firmware::CORE_SERVICE`;
protocol-stack Workers (SSH) set `.core = Firmware::CORE_PROTOCOL` explicitly.

**Boot sequence** (`Runtime::begin()`) - each stage paints a color on the RGB
LED; when core services are up the LED turns off:

1. Start LED and USB hardware
2. Load device, Wi-Fi, and SSH config from NVS
3. Start USB-CDC and the serial bridge Worker (`CORE_SERVICE`)
4. Bring up Wi-Fi (STA or configuration AP; hostname = device name)
5. Bind SSH on port 22 (`SshListener` + optional `SshSession` on `CORE_PROTOCOL`) and bridge
   channel I/O to the serial rings

After the LED turns off, the web adapter registers routes and serves the UI.
It has no boot color of its own.

**Boot failures** - `Runtime::begin()` is fail-fast: if USB init, NVS reload, or
any core service `begin()` (serial, Wi-Fi, SSH) returns false, the firmware calls
`abort()`. The web UI never starts; the RGB LED may remain on the last boot color
(white / red / green / blue). Fix NVS corruption or hardware issues and reflash or
power-cycle. This is separate from runtime config errors, which are reported over
HTTP (see [`responseFailed`](#rest-routes) under REST routes).

**Runtime errors** - `Nvs`, ConfigStorage APIs, `DeferredScheduler`, `Runtime`,
and service `begin()` / `restart()` return `bool`. Web handlers propagate persistence, reload, or deferred scheduling
failures as HTTP 500 with `type: "responseFailed"`. Wi-Fi failover in
`maintainAp()` / `maintainSta()` is best-effort and intentionally ignores some
return values.

Configuration changes from the web UI are written to NVS and reloaded
immediately. Only the affected service restarts, after a short delay so the HTTP
response can finish first. Device-name save/reset also reloads the Wi-Fi mirror
and schedules Wi-Fi restart because the default AP SSID follows the device name.
Repeated saves before the delay elapses coalesce into one restart. Typical delays:
Wi-Fi restart **1.5 s**, SSH restart **500 ms**, reboot **2 s**. See
`include/util/deferred.h` for `DeferredScheduler`: schedule a `Deferred` (entry +
context) with `schedule()` / `scheduleExclusive()`, cancel with `cancel()`, and
dispatch due work from `loop()`. Re-scheduling an equal `Deferred` updates its
deadline in place (debounce). `schedule()` returns false when `entry` is null or the
queue is full; `scheduleExclusive()` when `entry` is null (it clears pending slots
first); `cancel()` when `entry` is null or no matching slot is pending. `loop()`
pops and runs each due deferred outside the mutex.

Factory reset clears all NVS namespaces and schedules a reboot without reloading
in-memory mirrors first.

**Runtime API** - `getDeviceConfig()` / `getWifiConfig()` / `getSshConfig()` return
in-memory mirrors; `getDeviceService()` / `getWifiService()` / `getSshService()`
expose live service state (used by `GET /status` and SSH key validation). There is
no separate status snapshot type.

**Code layout** - headers declare classes with public API first; `src/` mirrors
`include/` and follows the same member order in `.cpp` files. Paired headers
(`.h` + `.cpp`) document **role** in the header and **flow** in the
implementation; header-only modules (`deferred`, `nvs`, `ringbuf`, `string`,
`regex`, `worker`, `restful`) also document behavior in the header. Headers minimize
coupling via forward declarations where possible (`runtime.h`, service and web
headers). `.cpp` includes follow layer order: `firmware.h` -> `config/*` ->
`util/*` -> `hardware/*` -> `ssh/*` -> `service/*` -> `web/*` -> `runtime.h`.

**Memory and strings** - pick APIs by semantics, not a single style:

- Fixed-length byte equality (e.g. `StringView::operator==`) uses `__builtin_bcmp`.
- Null-terminated C strings and bulk memory use `<cstring>` (`strcmp`, `strlen`,
  `memset`, `memcpy`, `memmove`).
- Third-party and platform APIs stay native (`strtok`, `snprintf`, Arduino
  `String`, LibSSH, etc.).
- Do not use mem/str compare for constant-time secret checks.

## Hardware

- ESP32-S3 board with native USB (developed on **ESP32-S3-ETH**).
- Addressable RGB LED on **`Firmware::LED_PIN` (GPIO 21)** — see `include/firmware.h`.
- USB cable from the board to the host PC (CDC serial) and/or to the target
  device you want to bridge.

## Quick Start

1. **Install dependencies** (first time; see [Installing Dependencies](#installing-dependencies)), then **build and flash** (see [Build & Flash](#build--flash)):

```sh
make flash
```

2. **First boot** - with no saved STA network, join AP `SerialBridge-XXXX`
   (password `12345678`; `XXXX` is the device id from the MAC address). A
   captive portal opens the web UI; otherwise browse to the gateway IP.
3. **Configure Wi-Fi** in the web UI and save. Note the assigned IP on the
   Status tab or via your router / mDNS hostname.
4. **Plug USB** into the target - it appears as `/dev/ttyACM0` (`COMx` on
   Windows).
5. **Enable a login shell on the target** (Linux example):

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

6. **Connect over SSH** (default `admin` / `admin`):

```sh
ssh admin@<device-ip>
```

After the session banner, keystrokes go to the target serial port and output
streams back to your terminal.

## Host Setup (Linux)

When the bridge board is plugged into a Linux target, it shows up as
`/dev/ttyACM*`. To expose a login prompt on that port:

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

**Tips:**

- The TTY name may differ (`ttyACM1`, etc.). Check `ls /dev/ttyACM*` or
  `dmesg | grep ttyACM` after plugging in.
- Use a udev rule keyed on USB path or serial number for stable naming across
  reboots.
- Skip `serial-getty` if you only need raw serial I/O from your own app.

## Status LED

GPIO 21 (`Firmware::LED_PIN`) shows boot progress, then stays off. While the USB bridge is moving
bytes, TX/RX blink (25 ms lit / 25 ms dark) over that steady off state; both
directions round-robin when active together.

Blinking is **level-driven**: the serial bridge Worker calls `LED.blink(color,
bytes != 0)` each poll. When traffic stops, release is deferred by at least one
full blink cycle (2 ticks, 50 ms) so short bursts remain visible. See
`include/hardware/led.h` and `src/hardware/led.cpp` for the slot pool, timer
(`schedule()` / `tick()`), and pin output in `Led::write()`.

**Boot** (order matches `Runtime::begin()`)

| Color | Stage |
| ----- | ----- |
| White | Read config |
| Red | Serial / CDC bridge |
| Green | Wi-Fi |
| Blue | SSH |
| Off | Core services ready |

**Runtime** (blinks while traffic is present)

| Color | Meaning |
| ----- | ------- |
| Green | USB TX (toward target), while bytes move this poll |
| Orange | USB RX (from target), while bytes move this poll |

**Color model** (`include/hardware/led.h`, `src/hardware/led.cpp`)

- `LedColor` is packed RGBA (`0xRRGGBBAA`); alpha is compositing opacity for
  `over()`, not pin brightness. Default construct is transparent (`isTransparent()`).
  `rgb()` / `rgba()` define colors; `premulRed/Green/Blue()` project straight alpha
  over black for the pin.
- Constants in `led.cpp`: `COLOR_OFF` (off display state), `COLOR_BRIGHTNESS`
  (global brightness dimming overlay; black with alpha `255 - LED_BRIGHTNESS`),
  `BLINK_CYCLE_TICKS` (ticks per blink cycle, lit + dark), `TICK_MS` (blink phase
  duration, 25 ms).
- Display state: `baseColor` from `show()`, up to two blink slots, tick counters.
  Hardware dedup uses `lastColor`; `clear()` does not reset it. `blink()` ignores
  transparent colors; `COLOR_OFF` is the Led-layer off sentinel.
- `tick()` during blink: lit ticks write the slot color; dark ticks write
  `COLOR_OFF`. Idle restores `baseColor`.
- `Led::write()`: when `LED_BRIGHTNESS != 255`, `color.over(COLOR_BRIGHTNESS)`;
  then `premulRed/Green/Blue()`. No transparency check in `write()`.
- `Firmware::LED_BRIGHTNESS` in `include/firmware.h` (default `8`; `0` = off via
  opaque black overlay, `255` = skip overlay / full logical color).

## Networking

Three Wi-Fi modes (`WifiMode`), selectable in the web UI. STA and AP each have
their own SSID and password:

| Mode | Behavior |
| ---- | -------- |
| Client (STA) | Join the saved STA SSID - normal operation after setup |
| Access Point | WPA2 AP (default SSID = device name; password `12345678`) with captive-portal DNS |
| Off | Radio disabled (`WIFI_MODE_NULL`) |

Mode selection (`WifiService::switchMode()`): when `prefMode` is STA but
`staSsid` is empty, the radio starts in AP instead.

Failover timings (`src/service/wifi.cpp`, constants in `include/firmware.h`):

- First STA connect attempt times out after **10 s**, then falls back to AP.
- While STA link is down, retry `WiFi.begin()` every **5 s**; after **60 s**
  reopen AP for reconfiguration.
- In fallback AP with `prefMode` STA, probe STA again every **10 min**.

**Ports**: HTTP `80`, SSH `22`, captive-portal DNS `53` (AP only).

**IPv6** - enabled on STA and AP (`WiFi.enableIPv6()` / `WiFi.softAPenableIPv6()`).
Assigned addresses are exposed in `GET /status` as `wifiIpv6Addrs` (JSON string
array): STA reports link-local and global when present; AP reports the softAP
link-local address. Unassigned slots are omitted; when none are assigned (or Wi-Fi
is off), the array is `["::"]`.

## SSH Access

- TCP port **22**.
- **Host key** - Ed25519, generated on first start and stored in NVS. The
  OpenSSH-format public key is shown on the Status tab.
- **Authentication** (any configured method):
  - Password - default `admin` / `admin`
  - Public key - one OpenSSH `authorized_keys` line (RSA-8192 max, 1536 chars)
  - No-auth - optional; off by default
- One client session bridged to USB-CDC at a time. Extra TCP connections are
  rejected while a session is active (`SSH_ACCEPT_BUSY` -> `rejectPending`).
- **Implementation** - `SshService` owns `SshServer`, which loads or generates
  the host key (`SshHostKey`), runs a non-blocking listener (`SshListener`,
  `CORE_PROTOCOL`), and serves one client (`SshSession`, `CORE_PROTOCOL`).
  `isConnected()` reports whether a
  client is connected; `isActive()` reports whether the session is still running.
  Flow: `keyExchange` -> auth/channel/shell negotiate -> channel I/O.
  `SshService::bridge` moves bytes between the SSH channel and `SerialService`
  ring buffers (same byte stream as local USB-CDC). The bridge loop sleeps for
  `Firmware::BRIDGE_POLL_INTERVAL_MS` when idle.
- **Banner** after login:

```
This SSH session is bridged to the device serial port.
Escape sequence is '~' '.' (at line start)
```

> **Security** - defaults (`admin`/`admin`, AP password `12345678`) are for
> first-time setup. Change credentials before deploying on untrusted networks.
> Keep no-auth off unless you understand the exposure.

## Web UI & REST API

Sources live in `assets/`; handlers in `src/web/handler.cpp` (`src/web/server.cpp`
only registers routes). HTTP handlers run on the AsyncTCP task (`CORE_PROTOCOL`).
The wire contract - routes, MIME types, response headers,
JSON keys, error `type` ids, POST body limits - is defined in
`include/web/restful.h`. Handlers delegate persistence to `Runtime` and build
`GET /status` JSON directly from
`getDeviceService()` / `getWifiService()` / `getSshService()`. The `ConfigHandler`
base dispatches each config route to `DeviceConfigHandler`, `WifiConfigHandler`, or
`SshConfigHandler` (`handleQuery` / `handleSave` / `handleReset`); field read/validate/write is shared through `Value::*` in
`src/web/value.cpp`. The UI mirrors the contract in `assets/script.js`
(`DOMAIN_SPECS`, `ACTION_SPECS`, `LANG_SPECS.err`).

Each file under `assets/` is self-documented in its **file header**, standalone
per file. Headers describe structure only-ASCII trees for the page shell
(`index.html`), class layout (`style.css`), and domain tree (`script.js`). This
section covers runtime behavior and firmware alignment. Runtime relies on
structural parse; markup shape has no separate audit pass. **Structural parse
failures** log `console.error` and leave the affected tab or domain empty. Keep
`index.html` aligned with `DOMAIN_SPECS` / `TAB_SPECS` and `restful.h` during
development. After editing, run `make assets` (or let the build pipeline
regenerate when inputs change).

### Layers

| Layer | Responsibility | Documented in |
| ----- | -------------- | ------------- |
| HTML | Page shell (DOM nesting, `hidden`) | `index.html` header (document tree) |
| `class` | Appearance and layout only | `style.css` header (class model, class tree) |
| `data-*` | Roles, wire keys, actions, i18n keys, field error type ids | `index.html` header; `script.js` wire tables |

CSS uses class selectors only. JS discovers structure via `data-*` attributes
(`data-role`, `data-domain`, `data-field`, and related hooks). Classes such as
`.active` toggle styling. `class` hooks style; `data-*` hooks logic; each layer
stays within its scope.

### CSS model vs domain model

The same container often carries both a layout class and a domain role; the names
are independent:

```html
<form class="stack" data-role="block">
```

| Model | Hook | Meaning | ASCII tree in |
| ----- | ---- | ------- | ------------- |
| **CSS** | `class="stack"` | Vertical layout shell. Children may be `.status`, `.field`, `.group`, `.controls`, `.actions`. Layout only; logic uses `data-*`. | `style.css` header (class model, class tree) |
| **Domain** | `data-role="block"` | Region under a domain host: always-on fields, conditional child blocks (`data-group`), optional selector and hint. Parsed into `Block`. | `script.js` header (domain model, domain tree) |

Domain types: `Domain`, `Block`, `Field` (see **Domain model** below).
CSS layout types include `.section`, `.field`, `.group`-pairing with `data-*`
is by convention.

### Asset file headers

| File | Header sections |
| ---- | --------------- |
| `index.html` | Role, document tree, data-role, data-* |
| `style.css` | Role, class model, class tree, variables (:root), responsive |
| `script.js` | Role, domain model, domain tree, data-role, modules |

### Runtime

**Boot** - `DOMContentLoaded` constructs `Runtime` and calls `boot()`.

**Modules** - `UI` parses markup, binds events, and drives tabs, domain sync, i18n,
and status display. `Runtime` orchestrates tab loading, config/system actions,
and HTTP. `DOM`, `Dialog`, `HttpClient`, `Language`, and `Poller` support those layers
(see `script.js` header, Modules).

**Init** - `UI.init()` maps `[data-role=tab]` / `[data-role=tabpanel]`
by index to `TabRoute.getNames()`. For each domain,
`#resolveDomainBlocks` finds the host (`[data-domain]` on tabpanel or `.section`),
then `#collectTopLevelBlocks` gathers domain-root `[data-role=block]` nodes only
(nested `[data-role=block]` elements stay in parent `Block.blocks[]`). Each block is parsed by
`#parseBlock` into `Domain.blocks`; `[data-action]` buttons are collected into
`domain.actions`. Missing shell nodes, domain hosts, or blocks log
`console.error` and leave the affected tab or domain empty. `bind()` wires tabs, language, domain actions, and per-block selector
`change` -> `syncDomain`. All buttons use `type="button"` + `data-action`.
`Runtime.dispatchAction` routes `ACTION_SPECS` keys to save, reset, or system
handlers.

**Domain model** - `Domain { el, name, blocks[], actions }`. Each domain-root
`[data-role=block]` becomes a `Block` with `fields` (always-on), `blocks[]`
(optional child blocks), optional `selector` (`[data-role=group-selector]`), and
`hintEl` (`[data-role=group-hint]`). Conditional child blocks are direct
`[data-role=block][data-group]` nodes; `Block.name` reads `data-group`. Multiple
sibling blocks may share the same name. Nested blocks parse into parent
`Block.blocks[]`. Block-level fields live in `Block.fields` (direct children
only). `option[value]` on the selector matches child blocks by name (`off` shows
none); optional `option[data-hint]` supplies hint text in `hintEl`.
`Block.forEachField` / `forEachSelectedField` recurse through `blocks[]`
(selected traversal includes named child blocks that match the selector).
`Domain.forEachBlock` visits every block node for sync/bind.
`Domain.forEachSelectedField` / `getDirtyValues()` use block fields plus
selected child block fields; `forEachField` / `applyMeta` cover all fields. On
mode switch, `#resetHiddenValues` clears dirty values on hidden fields. Wi-Fi
`prefMode` (`"off"` / `"sta"` / `"ap"`) drives child blocks `sta` / `ap`.

**Config save/reset** - Save: `#buildConfigRequest` builds the POST body from
`getDirtyValues()`, then `#validateConfigRequest` runs client-side checks.
All config domains use `#validateFields` (`#isValidFieldValue` against field
meta: length, optional `pattern`; numeric range for `type="number"`; `allowNoAuth` is boolean). Rules mirror
`Value::isValidText` / `isValidNumber` in
`value.cpp` (strings use `RegexCache` for `pattern`; numbers use
`minValue` / `maxValue`; applied on POST by each handler's `mergeConfig` /
`validateConfig` in `handler.cpp`). On failure,
`#getFieldErrorMsg` returns
`field.errorMsg`; a missing `data-error` logs `Missing data-error: <domain>.<field>`
and still blocks save. Result type is
`RequestResult`. On `REQUEST_STATE.OK`, POST the payload; build/POST errors use
`Dialog.showError`. POST success -> `Dialog.showInform` -> `#finishConfigAction`
-> `reloadTabFields` for the tab (device + wifi on Network). Reload failure ->
`showPanelError` (same as `loadTabIfNeeded`). Superseded reload leaves the tab
`UNLOADED` and may retry when still active. Reset: confirm -> POST (no body) ->
inform -> reload.

Device and Wi-Fi save/reset defer `wifi.restart()` (~1.5 s in `runtime.cpp`) so
the POST response and the follow-up `GET /config/*/fields` can usually finish on
the same connection; on an unchanged network path the UI usually shows the
success dialog alone. Clients more often see both dialog and panel error when
they must reconnect elsewhere (e.g. new STA SSID) or when `disconnect: true`
treats a dropped POST as success before the response arrives.

`data-field` values must match `WIRE_KEY_*` in `restful.h`. Config fields
declare `data-error` (a `LANG_SPECS.err` / `ERR_*` type id); `Field.errorMsg`
reads it at parse time. `data-error` values must match `ERR_*` type ids and
`LANG_SPECS.err` keys. User-visible error text lives only in `LANG_SPECS.err`;
the firmware returns machine `type` ids.

### Page layout

Tabs in DOM order (must match `TAB_SPECS` / `TabRoute.getNames()`):
`status` | `network` | `ssh` | `system`. Each `[data-role=tabpanel]` starts with
`[data-role=tabpanel-error]`, then `.section` hosts (`[data-domain]` where needed).

| Tab | Contents |
| --- | -------- |
| Status | Read-only `data-field` cells from `GET /status` (polled every 10 s) |
| Network | Device name + Wi-Fi mode and STA/AP groups; save/reset via `data-action` |
| SSH | SSH credentials and allow-no-auth; save/reset via `data-action` |
| System | Reboot and factory-reset via `data-action` |

Structure ASCII trees: page shell in `index.html` header; domain tree in
`script.js` header (domain model, domain tree); class layout in `style.css`
header (class model, class tree).

### Style and variables

`:root` variables and responsive rules: `style.css` header (class model, class tree). Variable naming:
`--{entity}` and `--{entity}-{degree}` (`base` -> `muted` -> `faint` -> `subtle`
-> `strong`; `background` also has `-deep`). `--tab-count` must equal `.tab`
count in `.tablist`. Desktop-first base styles; `@media (max-width: 600px)`
overrides in the mobile section. Form inputs stay at 16px on mobile to avoid iOS
zoom-on-focus.

### HTTP responses

All responses include `X-Content-Type-Options: nosniff`. Policies in
`restful.h` (`RESPONSE_POLICY_*` tables applied by `HttpRequest` in `request.cpp`):

| Policy | Used for | Headers |
| ------ | -------- | ------- |
| `RESPONSE_POLICY_STATIC_PAGE` | `GET /` | `Cache-Control: public, max-age=180`, `Content-Encoding: gzip` |
| `RESPONSE_POLICY_STATIC_ASSET` | `GET /style.css`, `/script.js` | `Cache-Control: public, max-age=31536000, immutable`, `Content-Encoding: gzip` |
| `RESPONSE_POLICY_API` | JSON, 204, errors | `Cache-Control: no-cache` |

Reboot, factory-reset, and some config saves may add `Connection: close`.

### REST routes

| Method | Path | Success |
| ------ | ---- | ------- |
| GET | `/` | HTML (gzip) |
| GET | `/style.css` | CSS (gzip) |
| GET | `/script.js` | JavaScript (gzip) |
| GET | `/status` | JSON snapshot |
| GET | `/config/device/fields` | JSON + field metadata |
| POST | `/config/device/save` | 204 No Content |
| POST | `/config/device/reset` | 204 No Content |
| GET | `/config/wifi/fields` | JSON + field metadata |
| POST | `/config/wifi/save` | 204 No Content |
| POST | `/config/wifi/reset` | 204 No Content |
| GET | `/config/ssh/fields` | JSON + field metadata |
| POST | `/config/ssh/save` | 204 No Content |
| POST | `/config/ssh/reset` | 204 No Content |
| POST | `/reboot` | 204, then reboot |
| POST | `/factory-reset` | 204, then reboot |

**`GET /status` keys** - `deviceName`, `deviceFwVersion`, `deviceFreeMem`,
`deviceTotalMem`, `deviceUptime`, `deviceSdkVersion`,
`wifiMode` (`"off"` / `"sta"` / `"ap"`), `wifiRssi`, `wifiMacAddr`,
`wifiIpv4Addr`, `wifiIpv6Addrs`, `sshConnected`, `sshHostKey`.

`deviceFreeMem` / `deviceTotalMem` are bytes of available RAM (internal heap +
PSRAM when present).

`wifiIpv6Addrs` is a **string array** (deduplicated). The Status tab shows one
address per line (`fe80::...`, global, etc.). Example:

```json
"wifiIpv6Addrs": ["fe80::12:34ff:fe56:7890", "2001:db8::1"]
```

**`GET /config/*/fields`** - each field is a **fieldMeta** object:

| Key | Meaning |
| --- | ------- |
| `value` | Current value (may be omitted) |
| `minLength` | Optional. Minimum string length. When present, an empty string is invalid unless `allowEmpty` is `true`. Omitted means no minimum. |
| `maxLength` | Optional. Maximum string length. Omitted means no maximum. |
| `minValue` | Optional. Minimum numeric value (e.g. `type="number"` inputs). Omitted means no minimum. |
| `maxValue` | Optional. Maximum numeric value (e.g. `type="number"` inputs). Omitted means no maximum. |
| `pattern` | Optional. POSIX extended regex (`REG_EXTENDED`) for non-empty string values; must also be valid JavaScript `RegExp` syntax. Omitted means no pattern check. |
| `allowEmpty` | `true` when an empty string (text) or empty number input (`null` on save) is valid |

Constraint keys are omitted when not applicable. Example:

```json
{
  "prefMode": { "value": "ap", "minLength": 2, "maxLength": 3, "pattern": "^(off|sta|ap)$" },
  "name": {
    "value": "SerialBridge-ABCD",
    "minLength": 1,
    "maxLength": 32,
    "pattern": "^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?$"
  },
  "staPassword": { "value": "{keep}", "minLength": 8, "maxLength": 63, "allowEmpty": true }
}
```

| Domain | Field keys |
| ------ | ---------- |
| device | `name` |
| wifi | `prefMode`, `staSsid`, `staPassword`, `apSsid`, `apPassword` |
| ssh | `username`, `password`, `authorizedKey`, `allowNoAuth` |

**Partial saves** - `POST /config/*/save` merges only keys present in the body.
`WifiConfigHandler::mergeConfig` merges present fields onto the current config, then
`validateConfig` enforces the cross-field mode/SSID rule; only a non-empty,
validated `prefMode` is applied.

**Passwords** - when stored, `GET .../fields` returns `"value": "{keep}"`
(`WIRE_VALUE_PASSWORD_KEEP`). The UI shows that placeholder in the password input;
replace or clear it to submit a new value. Omit the key on POST to keep the
secret unchanged; sending `"{keep}"` is rejected. Empty string clears where
`allowEmpty` applies.

**Errors** - HTTP 4xx/5xx with `application/problem+json` body
`{"type":"<typeId>"}`:

| `type` | Typical cause |
| ------ | ------------- |
| `requestEmpty` | Missing POST body |
| `requestTooSmall` | Body below route minimum |
| `requestTooLarge` | Body exceeds route limit |
| `requestInvalidFormat` | Body is not a JSON object |
| `requestOverflow` | JSON too large to parse |
| `responseOverflow` | Response JSON too large to build |
| `responseFailed` | NVS persistence, config reload, or deferred command scheduling failed |
| `configDeviceInvalidName` | Invalid device name |
| `configWifiInvalidPrefMode` | `prefMode` must be `"off"` / `"sta"` / `"ap"` |
| `configWifiInvalidSsid` | Invalid SSID for active mode |
| `configWifiInvalidPassword` | Invalid Wi-Fi password |
| `configSshInvalidUsername` | Invalid SSH username |
| `configSshInvalidPassword` | Invalid SSH password |
| `configSshInvalidAuthorizedKey` | Invalid public key |
| `configSshInvalidAllowNoAuth` | `allowNoAuth` not a boolean |

`responseFailed` is returned after request validation passes but `Runtime` reports
NVS persistence, in-memory reload, or deferred restart/reboot scheduling failure
(for example `POST /config/*/save`, `POST /config/*/reset`, `POST /reboot`,
`POST /factory-reset`).

Validation limits: string lengths in `include/firmware.h`; wire patterns in
`include/web/restful.h` (`WIRE_PATTERN_DEVICE_NAME`, `WIRE_PATTERN_WIFI_MODE`).
Echoed via fieldMeta (`minLength` / `maxLength` for strings, `minValue` /
`maxValue` for numbers, `pattern` for regex). The UI checks constraints in
`#isValidFieldValue` (`#isValidFieldLength`, `#isValidFieldPattern`,
`#isValidNumberRange`) and pre-validates in
`#validateConfigRequest` before POST (mirrors `Value::isValidText` /
`isValidNumber` in `value.cpp`; pattern matching uses `RegexCache`).

**Wi-Fi mode wire values** - `prefMode` and `wifiMode` use `"off"`, `"sta"`, and
`"ap"` (`WIRE_VALUE_WIFI_MODE_*` in `restful.h`) on `GET /status`,
`GET /config/wifi/fields`, and `POST /config/wifi/save`. When adding or renaming
modes, keep these in sync:

| Location | What to update |
| -------- | -------------- |
| `include/web/restful.h` | `WIRE_VALUE_WIFI_MODE_OFF` / `_STA` / `_AP`, `WIRE_PATTERN_WIFI_MODE` |
| `include/firmware.h` | `LIMIT_WIFI_MODE_MIN_LEN`, `LIMIT_WIFI_MODE_MAX_LEN` |
| `src/web/handler.cpp` | `wifiModeToWire()`, `wifiModeFromWire()` |
| `assets/script.js` + `assets/index.html` | `WIFI_MODE` constants, `<option value="...">` on `prefMode` |

### Keep in sync

| Source | Align with |
| ------ | ---------- |
| `include/web/restful.h` | Routes, `WIRE_KEY_*`, `WIRE_VALUE_*`, `WIRE_PATTERN_*`, `ERR_*` type ids, password `{keep}` |
| `assets/script.js` | `TAB_SPECS`, `DOMAIN_SPECS`, `ACTION_SPECS`, `LANG_SPECS.err`; file header (domain model, domain tree) |
| `assets/index.html` | document tree, data-role, data-* (`data-field`, `data-error`, ...) |
| `assets/style.css` | class model, class tree, variables (:root), `--tab-count` |
| `src/web/handler.cpp` | `DeviceConfigHandler` / `WifiConfigHandler` / `SshConfigHandler` (`handleQuery` / `handleSave` / `handleReset`), per-domain `mergeConfig` / `validateConfig`, `GET /status` JSON, `wifiModeToWire` / `wifiModeFromWire` |
| `src/web/value.cpp` | `Value::isValid*` / `read*` / `write*`, `RegexCache` pattern matching |
| `include/web/domain.h` | `Field` / `Route` tables and `FieldMeta` constraints |

## Configuration Reference

Defaults and limits are in `include/firmware.h`.

**Defaults**

| Setting | Default |
| ------- | ------- |
| Device name | `SerialBridge-XXXX` (`XXXX` = low 16 bits of eFuse MAC) |
| Wi-Fi mode | AP (`Firmware::DEFAULT_WIFI_PREF_MODE` = `2`, matches `WifiMode::Ap`) |
| STA SSID / password | empty |
| AP SSID | device name (stored in NVS only when different; otherwise derived on read) |
| AP password | `12345678` |
| SSH username / password | `admin` / `admin` |
| SSH authorized key | none |
| SSH allow no-auth | off |
| SSH host key | Ed25519, generated on first start |
| LED brightness | `8` (`Firmware::LED_BRIGHTNESS`; black overlay alpha `255 - value`, `0`-`255`) |

**Limits**

| Field | Range |
| ----- | ----- |
| Device name | 1-32 chars (letters, digits, hyphens; no leading/trailing hyphen) |
| Wi-Fi mode (`prefMode`) | 2-3 chars; `"off"` / `"sta"` / `"ap"` |
| Wi-Fi SSID | 1-32 chars |
| Wi-Fi password | 8-63 chars, or empty for open network |
| SSH username | 1-32 chars |
| SSH password | up to 64 chars |
| SSH authorized key | up to 1536 chars (OpenSSH line, RSA-8192 max) |

## Dependencies

Arduino libraries (pinned in `sketch.yaml`):

| Library | Version |
| ------- | ------- |
| [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | 3.12.0 |
| [Async TCP](https://github.com/ESP32Async/AsyncTCP) | 3.5.0 |
| [ArduinoJson](https://arduinojson.org/) | 7.4.3 |
| [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32) | 5.9.0 |

Board package: `esp32:esp32` **3.3.11**.

Build tooling:

- [`arduino-cli`](https://arduino.github.io/arduino-cli/)
- Python 3.10+ (`scripts/`)
- Node.js + npm for asset minification (`scripts/package.json`)

`embed_assets.py` invokes `minify.mjs` via Node. The build fails with a clear
prompt if `node_modules/` is missing.

## Installing Dependencies

Run once on a new machine (or after bumping versions in `sketch.yaml`). Requires
[`arduino-cli`](https://arduino.github.io/arduino-cli/) on `PATH`.

**ESP32 board package**

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
```

**Arduino libraries**

Library names must match the Arduino Library Manager entries (including spaces).
`ESP Async WebServer@3.12.0` depends on `Async TCP@3.5.0` — install both.

```sh
arduino-cli lib install \
  "ArduinoJson@7.4.3" \
  "Async TCP@3.5.0" \
  "ESP Async WebServer@3.12.0" \
  "LibSSH-ESP32@5.9.0"
```

**Web asset tooling (npm)**

```sh
cd scripts && npm install
```

**Verify**

```sh
arduino-cli core list
arduino-cli lib list | grep -E 'ArduinoJson|Async TCP|ESP Async|LibSSH'
cd scripts && npm ls
```

Expected: platform `esp32:esp32` **3.3.11**; libraries per the table above;
`@minify-html/node` and `esbuild` under `scripts/node_modules/`.

Then compile with `make build` (see [Build & Flash](#build--flash)).

The `Makefile` wraps `scripts/build.py`: embed web assets, inject
`FW_VERSION_ID` from git, compile with arduino-cli, optionally upload. On WSL,
upload auto-calls `attach_wsl_usb.py` when no serial port is visible.

```sh
make                  # show help (default)
make build            # compile
make flash            # compile and upload
make assets           # regenerate embedded web assets
make boards           # list boards and serial ports
make clean            # remove build cache
```

```sh
make build PROFILE=debug
make flash PORT=/dev/ttyACM0
make build FORCE_ASSETS=1
```

**Profiles** (`sketch.yaml`):

| Profile | Description |
| ------- | ----------- |
| `release` | Normal build/upload (default) |
| `release-full` | Release + erase entire flash on upload |
| `debug` | Verbose debug logging |
| `debug-full` | Debug logging + erase entire flash |

**Sketch compile flags** (`build_opt.h`):

| Flag | Purpose |
| ---- | ------- |
| `-Iinclude` | Header search path for `#include "..."` |
| `-fno-exceptions` | Disable C++ exceptions |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=0` | Pin Async TCP to `Firmware::CORE_PROTOCOL` |

**Make variables**

| Variable | Default | Used by |
| -------- | ------- | ------- |
| `PROFILE` | `release` | `build`, `flash`, `clean` |
| `FORCE_ASSETS` | - | `build`, `flash` (`FORCE_ASSETS=1`) |
| `VERBOSE` | - | `build`, `flash` (`VERBOSE=1`) |
| `NO_GIT_HASH` | - | `build`, `flash` (`NO_GIT_HASH=1`) |
| `PORT` | auto-detect | `flash` |

`make flash` passes Espressif USB IDs to `build.py` (`VID`/`PID`/`AUTO_ATTACH`,
default `303a:1001`).

Git HEAD is injected as `FW_VERSION_ID` (e.g. `v0.1a (a63d8ff)`). Use
`NO_GIT_HASH=1` to skip.

**Serial ports** - native ESP32-S3 USB is usually `/dev/ttyACM*`; USB-UART
adapters use `/dev/ttyUSB*`. `make flash` auto-detects both when `PORT` is unset.

### WSL USB passthrough

Build in WSL with the board plugged into Windows requires
[`usbipd-win`](https://github.com/dorssel/usbipd-win). After attach, the port
appears as `/dev/ttyACM*` or `/dev/ttyUSB*`.

**Automatic** - `make flash` / `build.py -u` calls `attach_wsl_usb.py` when no
matching port is found in WSL.

**Manual** (from WSL):

```sh
./scripts/attach_wsl_usb.py
./scripts/attach_wsl_usb.py --vid 303a --pid 1001 --auto-attach 1
```

Run the `.py` entry point from WSL; it invokes `attach_wsl_usb.ps1` on Windows
via `powershell.exe -ExecutionPolicy Bypass`.

**First-time setup** (Administrator PowerShell):

```powershell
usbipd list
usbipd bind --busid <BUSID>
```

If bind is still needed, the attach script prints the command (exit code `2`).

**IDE / clangd** - generate a compile database:

```sh
cd scripts && python generate_compile_commands.py
```

## Web Assets

Edit source files in `assets/`; the build generates `include/web/assets.h` and
`src/web/assets.cpp` (gitignored; produced by `make assets` or `make build`).

Pipeline (`scripts/embed_assets.py`):

1. Minify via `scripts/minify.mjs` (`esbuild` for JS, `@minify-html/node` for
   HTML/CSS; JS identifier mangling on by default)
2. Gzip-compress
3. Emit sized `extern const uint8_t NAME[N] PROGMEM` declarations and definitions

Callers use `sizeof(Assets::NAME)` for byte length (no separate `_LEN`
constants). `build.py` runs `embed_assets()` when inputs change; force with
`make assets` or `make build FORCE_ASSETS=1`. Pass `--no-strip` to skip
minification.

| Script | Role |
| ------ | ---- |
| `build.py` | Compile / upload orchestration |
| `embed_assets.py` | Minify + gzip + emit assets |
| `minify.mjs` | Minification entry |
| `attach_wsl_usb.py` / `.ps1` | WSL usbipd attach |
| `generate_compile_commands.py` | clangd compile database |

## Project Layout

```
assets/              Web UI sources (index.html, style.css, script.js)
include/
  firmware.h         Compile-time constants (Firmware: MCU, LED, ports, timing, defaults, limits)
  config/            *Config / *ConfigStorage pairs
  util/              deferred (DeferredScheduler), nvs (Nvs), ringbuf (SPSC RingBuf / RingReader / RingWriter), string (StringView), regex (RegexCache), worker (Worker), mutex, semaphore
  hardware/          led.h (global `LED`)
  ssh/               SshServer, SshListener, SshSession, SshHostKey, SshCredentials, SshKey
  service/           device, serial, Wi-Fi, ssh (SshService gateway) APIs
  web/               restful.h (contract), model.h/domain.h (data model + tables), value.h, request.h, handler.h, server.h; assets.h generated at build
  runtime.h          Composition root (config mirrors + services + deferred apply)
src/                 Mirrors include/; implementation flow documented in file headers
*.ino                Entry: owns Runtime + WebServer adapter
build_opt.h          Sketch-wide compile flags (see [Build & Flash](#build--flash))
scripts/             build.py, embed_assets.py, minify.mjs, attach_wsl_usb.*
Makefile             Wraps build.py; bare make shows help
sketch.yaml          arduino-cli profiles, FQBN, pinned libraries
```

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 RenoSeven.
