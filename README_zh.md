# SerialBridge

基于 SSH 的 USB 串口 - 运行于 ESP32-S3 的固件。

[English](README.md) | 简体中文

SerialBridge 将 ESP32-S3 变成双通道串口适配器：本地以 USB CDC 设备出现（`/dev/ttyACM*` /
`COMx`），同一串口通道也可经 Wi-Fi 通过 SSH 远程访问，并内置 Web 配置界面。适用于
无显示器的实验台、机房，以及需要本地 USB 串口与网络远程串口、又不想为每台设备单独接 PC
的嵌入式场景。

## 目录

- [功能](#功能)
- [架构](#架构)
- [硬件](#硬件)
- [快速上手](#快速上手)
- [目标主机配置（Linux）](#目标主机配置linux)
- [状态指示灯](#状态指示灯)
- [网络](#网络)
- [SSH 访问](#ssh-访问)
- [Web 界面与 REST API](#web-界面与-rest-api)
- [配置参考](#配置参考)
- [依赖](#依赖)
- [安装依赖](#安装依赖)
- [构建与烧录](#构建与烧录)
- [Web 资源](#web-资源)
- [工程结构](#工程结构)
- [许可证](#许可证)

## 功能

- **USB-CDC 串口** - ESP32-S3 在主机上枚举为 CDC 设备，同一数据流桥接到 SSH 客户端。
  `Firmware::CORE_SERVICE` 上的 `SerialService` Worker 经无锁 8 KiB SPSC
  环形缓冲（`RingBuf`）双向搬运数据；两端通过零拷贝的 `RingReader` / `RingWriter`
  视图访问。
- **SSH 服务** - 基于 LibSSH-ESP32，端口 22，分层实现在 `include/ssh/`
  （`SshServer` -> `SshListener` + `SshSession`）。支持密码与公钥认证，Web 界面可切换
  免认证模式；首次启动 SSH 时生成并持久化 Ed25519 主机密钥。
- **Web 界面** - 端口 80 的单页配置与状态：实时设备/网络/SSH 快照，网络与 SSH 设置，
  语言切换（English / 简体中文），重启与恢复出厂。
- **Wi-Fi** - 关闭 / 客户端（STA）/ 接入点（AP）三种模式，STA 与 AP 各自独立 SSID
  与密码。未配置或无法连接已保存网络时，回退为带强制门户的 AP，便于首次配置。
- **RGB 状态灯**（`Firmware::LED_PIN`，GPIO 21） - 启动阶段分色指示，就绪后熄灭；有 USB 收发时 TX/RX 持续闪烁。
- **配置持久化** - 设备、Wi-Fi、SSH 配置保存在 NVS，重启后保留。

## 架构

```
入口 (.ino)
  ├── hardware/        板级外设（全局 `LED`，用法类似 USB）
  ├── Runtime          组合根：配置镜像 + 服务 + 控制接口
  │     ├── config/    *Config / *ConfigStorage（device / wifi / ssh）
  │     ├── util/      延期调度、Nvs、SPSC RingBuf、StringView、正则缓存、Worker
  │     ├── ssh/       LibSSH 服务端：主机密钥、监听、会话、认证
  │     └── service/   设备状态、USB 串口桥接、Wi-Fi、SSH 网关
  └── WebServer        位于 Runtime 之上的 HTTP/REST 适配器
```

| 层级 | 位置 | 职责 |
| ---- | ---- | ---- |
| 常量 | `include/firmware.h` | 命名空间 `Firmware`：版本、MCU 布局（`CORE_*`、`CACHE_LINE_SIZE`）、LED 引脚、网络端口、桥接/Wi-Fi 时序、延期生效延迟、出厂默认值、校验限制 |
| 配置 | `include/config/` | 值对象与 ConfigStorage API；设备名默认来自 eFuse MAC；AP SSID 默认跟随设备名（与设备名相同时省略 NVS 键，读取时回退到 device 命名空间） |
| 工具 | `include/util/` | `DeferredScheduler` 固定容量延期调度器（`Deferred`、`Deferred::Entry`）、互斥 `Nvs`、SPSC `RingBuf`（非拥有的 `RingReader` / `RingWriter` 视图；游标按 `Firmware::CACHE_LINE_SIZE` 缓存行对齐）、非拥有的 `StringView`（`string.h`）、POSIX 正则缓存（`regex.h`）、`Worker` 任务生命周期（默认 `Firmware::CORE_SERVICE`）、mutex/semaphore 原语 |
| 硬件 | `include/hardware/` | 全局 `LED`（用法类似 `USB`） |
| SSH | `include/ssh/` | `SshServer` 在 `Worker` 上编排 `SshHostKey`、`SshListener`、`SshSession`；`SshCredentials`；`SshKey` 校验辅助 |
| 服务 | `include/service/` | 设备状态、USB-CDC 串口桥接（`SerialService`）、Wi-Fi、SSH 网关（`SshService` 将串口环形缓冲桥接到 SSH 通道） |
| 组合根 | `include/runtime.h` | 持有配置镜像与服务；按 LED/USB -> 配置 -> 串口 -> Wi-Fi -> SSH 启动；AsyncTCP 绑核编译期校验；提供配置查询、服务 getter，以及返回 bool 的 save/reset/reboot（延期生效） |
| Web | `include/web/` | `restful.h` 契约、`model.h`/`domain.h` 数据模型与各域 Field/Route 表、`value.h` 字段读取/校验/写出、`request.h` HTTP 传输、`handler.h` 各域处理器、`server.h` 路由注册；处理器从服务读取实时状态并委托 Runtime 持久化 |

**双核布局**（`include/firmware.h`）— ESP32-S3 将协议栈放在核心 0、应用服务放在核心 1：

| 核心 | 常量 | 典型工作负载 |
| ---- | ---- | ------------ |
| 0 | `Firmware::CORE_PROTOCOL` | ESP-IDF Wi-Fi、lwIP、esp_timer；SSH `Worker`（`SshListener`、`SshSession`）；AsyncTCP / Web 处理器（`build_opt.h`：`CONFIG_ASYNC_TCP_RUNNING_CORE=0`） |
| 1 | `Firmware::CORE_SERVICE` | Arduino `loopTask` 与事件任务（`sketch.yaml`：`LoopCore=1`、`EventsCore=1`）：延期调度、Wi-Fi 状态机；USB-CDC 串口桥 `Worker` |

代码中显式绑核的 `Worker` 使用 `Firmware::CORE_*`；Arduino loop 核心由 FQBN 决定。请保持
`LoopCore=1` — 将 loop 挪到核心 0 会与协议栈争抢 CPU，可能触发看门狗重启。USB-CDC 与
SSH 字节流经无锁 SPSC 环形缓冲跨核交换。

`build_opt.h` 设置 `CONFIG_ASYNC_TCP_RUNNING_CORE=0`（未定义时 Async TCP 默认为 `-1`，
任意核心）。`src/runtime.cpp` 通过 `static_assert` 校验其与 `Firmware::CORE_PROTOCOL`
一致。`Worker::Task` 默认绑定 `Firmware::CORE_SERVICE`；协议栈 Worker（SSH）显式设置
`.core = Firmware::CORE_PROTOCOL`。

**启动顺序**（`Runtime::begin()`）- 各阶段在 RGB LED 上显示对应颜色，核心服务就绪后熄灭：

1. 启动 LED 与 USB 硬件
2. 从 NVS 加载设备、Wi-Fi、SSH 配置
3. 启动 USB-CDC 与串口桥接 Worker（`CORE_SERVICE`）
4. 启动 Wi-Fi（STA 或配置 AP；主机名 = 设备名）
5. 绑定 SSH 22 端口（`CORE_PROTOCOL` 上的 `SshListener` 与可选 `SshSession`），并将通道 I/O
   桥接到串口环形缓冲

LED 熄灭后，Web 适配器注册路由并提供界面；它没有专属启动色。

**启动失败** - `Runtime::begin()` 采用 fail-fast：USB 初始化、NVS 重载或任一核心服务
`begin()`（串口、Wi-Fi、SSH）返回 false 时，固件调用 `abort()`。Web 界面不会启动；
RGB LED 可能停留在最后一次启动色（白 / 红 / 绿 / 蓝）。请修复 NVS 损坏或硬件问题后
重新烧录或断电重启。这与运行期配置错误不同——后者通过 HTTP 上报（[`responseFailed`](#rest-路由) 见 REST 路由一节）。

**运行期错误** - `Nvs`、各 ConfigStorage、`DeferredScheduler`、`Runtime` 以及
service 的 `begin()` / `restart()` 均返回 `bool`。
Web 处理器将持久化、内存 mirror reload 或延期调度失败传播为 HTTP 500，
`type: "responseFailed"`。Wi-Fi 在 `maintainAp()` / `maintainSta()` 中的故障转移
为尽力而为，部分返回值有意忽略。

Web 界面的配置更改写入 NVS 并立即重载。仅受影响的服务会在短延迟后重启，以便 HTTP
响应先完成。设备名 save/reset 还会重载 Wi-Fi 镜像并调度 Wi-Fi 重启，因为默认 AP SSID
跟随设备名。延迟到期前多次保存会合并为一次重启。典型延迟：
Wi-Fi 重启 **1.5 s**、SSH 重启 **500 ms**、整机重启 **2 s**。延期调度见
`include/util/deferred.h` 中的 `DeferredScheduler`：以 `Deferred`（entry + context）
调用 `schedule()` / `scheduleExclusive()`，`cancel()` 取消，`loop()` 执行到期项。
相同 `Deferred` 再次调度时原地刷新 deadline（防抖）。`schedule()` 在 `entry` 为空
或队列已满时返回 false；`scheduleExclusive()` 在 `entry` 为空时返回 false（会先清空
pending 槽位）；`cancel()` 在 `entry` 为空或找不到匹配项时返回 false。`loop()` 在
mutex 外运行每个到期 deferred。

恢复出厂会清空全部 NVS 命名空间并调度重启，不会先 reload 内存中的配置镜像。

**Runtime API** - `getDeviceConfig()` / `getWifiConfig()` / `getSshConfig()` 返回
内存中的配置镜像；`getDeviceService()` / `getWifiService()` / `getSshService()`
提供各服务的实时状态（供 `GET /status` 与 SSH 公钥校验使用）。不再单独定义状态快照类型。

**代码布局** - 头文件以 public API 在前声明类；`src/` 与 `include/` 镜像，`.cpp`
实现顺序与头文件成员顺序一致。配对头文件（`.h` + `.cpp`）在头文件中写**作用**、
在实现文件中写**流程**；头文件内实现的模块（`deferred`、`nvs`、`ringbuf`、
`string`、`regex`、`worker`、`restful`）也在头文件中说明行为。头文件尽量用前向声明降低
耦合（`runtime.h`、各 service 与 web 头文件）。`.cpp` 的 `#include` 按层级排序：
`firmware.h` -> `config/*` -> `util/*` -> `hardware/*` -> `ssh/*` ->
`service/*` -> `web/*` -> `runtime.h`。

**内存与字符串** — 按语义选择 API，而非统一风格：

- 定长字节相等比较（如 `StringView::operator==`）使用 `__builtin_bcmp`。
- 以 null 结尾的 C 字符串与批量内存操作使用 `<cstring>`（`strcmp`、`strlen`、
  `memset`、`memcpy`、`memmove`）。
- 第三方与平台 API 保持原生用法（`strtok`、`snprintf`、Arduino `String`、LibSSH 等）。
- 不要对密钥等敏感数据使用 mem/str 比较做常数时间校验。

## 硬件

- 具备原生 USB 的 ESP32-S3 开发板（在 **ESP32-S3-ETH** 上开发验证）
- **`Firmware::LED_PIN`（GPIO 21）** 上的可寻址 RGB LED — 见 `include/firmware.h`
- USB 线连接开发板与主机（CDC 串口）和/或需要桥接的目标设备

## 快速上手

1. **安装依赖**（首次；见[安装依赖](#安装依赖)），然后**构建并烧录**（见[构建与烧录](#构建与烧录)）：

```sh
make flash
```

2. **首次启动** - 无已保存 STA 网络时，连接 AP `SerialBridge-XXXX`（密码
   `12345678`；`XXXX` 为 MAC 派生的设备 id）。强制门户会自动打开 Web 界面；
   否则在浏览器访问网关 IP。
3. **配置 Wi-Fi** - 在 Web 界面保存后，记下「运行状态」页显示的 IP，或通过路由器 /
   mDNS 主机名查看。
4. **连接目标** - 将 USB 接到目标机器，目标侧识别为 `/dev/ttyACM0`（Windows 为
   `COMx`）。
5. **在目标上启用串口登录**（Linux 示例）：

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

6. **SSH 连接**（默认 `admin` / `admin`）：

```sh
ssh admin@<设备IP>
```

显示会话提示后，输入转发至目标串口，输出回传到终端。

## 目标主机配置（Linux）

桥接板接入 Linux 目标后识别为 `/dev/ttyACM*`。要在该串口上开放登录提示符：

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

**提示：**

- 设备名可能不同（`ttyACM1` 等）。插入后用 `ls /dev/ttyACM*` 或
  `dmesg | grep ttyACM` 确认。
- 可用 udev 规则按 USB 路径或序列号固定设备名。
- 若只需原始串口 I/O，可跳过 serial-getty，由应用直接读写 TTY。

## 状态指示灯

GPIO 21（`Firmware::LED_PIN`）显示启动进度，就绪后保持熄灭。USB 桥有字节收发时，TX/RX 在该熄灭
底色上以 25 ms 亮 / 25 ms 暗闪烁；两向同时活跃时轮流显示。

闪烁为**电平驱动**：串口桥 Worker 每轮 poll 调用 `LED.blink(color, bytes != 0)`。
流量停止后，release 至少延迟一个完整闪烁周期（2 tick、50 ms），保证短 burst 仍可见。
实现见 `include/hardware/led.h` 与 `src/hardware/led.cpp`（槽位池、定时器
`schedule()` / `tick()`、引脚输出在 `Led::write()`）。

**启动**（顺序与 `Runtime::begin()` 一致）

| 颜色 | 阶段 |
| ---- | ---- |
| 白 | 读取配置 |
| 红 | 串口 / CDC 桥接 |
| 绿 | Wi-Fi |
| 蓝 | SSH |
| 灭 | 核心服务就绪 |

**运行时**（有流量时持续闪烁）

| 颜色 | 含义 |
| ---- | ---- |
| 绿 | USB 发送（到目标），本轮 poll 有字节 |
| 橙 | USB 接收（来自目标），本轮 poll 有字节 |

**颜色模型**（`include/hardware/led.h`、`src/hardware/led.cpp`）

- `LedColor` 为 RGBA（`0xRRGGBBAA`）；alpha 用于 `over()` 合成，不是引脚亮度。
  默认构造为全透明（`isTransparent()`）。`rgb()` / `rgba()` 定义颜色；
  `premulRed/Green/Blue()` 将 straight alpha 投影到黑底供引脚使用。
- `led.cpp` 常量：`COLOR_OFF`（off 显示状态）、`COLOR_BRIGHTNESS`（全局亮度
  dimming overlay；黑色，alpha 为 `255 - LED_BRIGHTNESS`）、`BLINK_CYCLE_TICKS`
  （一次 blink 的 tick 数，亮 + 暗）、`TICK_MS`（blink 相位时长，25 ms）。
- 显示状态：`show()` 设置的 `baseColor`、最多两个 blink 槽位、tick 计数。
  硬件去重用 `lastColor`，`clear()` 不重置。`blink()` 忽略透明色；`COLOR_OFF`
  为 Led 层 off 哨兵。
- blink 期间 `tick()`：亮相写 slot 颜色，暗相写 `COLOR_OFF`；idle 时恢复 `baseColor`。
- `Led::write()`：当 `LED_BRIGHTNESS != 255` 时 `color.over(COLOR_BRIGHTNESS)`；
  再 `premulRed/Green/Blue()`。`write()` 内不单独判断透明度。
- `include/firmware.h` 中的 `Firmware::LED_BRIGHTNESS`（默认 `8`；`0` 经不透明黑
  overlay 灭灯，`255` 跳过 overlay / 使用逻辑色全亮）。

## 网络

三种 Wi-Fi 模式（`WifiMode`），可在 Web 界面选择。STA 与 AP 各自拥有独立 SSID 与密码：

| 模式 | 行为 |
| ---- | ---- |
| 客户端（STA） | 加入已保存的 STA SSID - 配置完成后的正常工作模式 |
| 接入点（AP） | WPA2 AP（默认 SSID = 设备名，密码 `12345678`）+ 强制门户 DNS |
| 关闭 | 关闭射频（`WIFI_MODE_NULL`） |

模式选择（`WifiService::switchMode()`）：`prefMode` 为 STA 但 `staSsid`
为空时，实际以 AP 启动。

故障转移（`src/service/wifi.cpp`，常量见 `include/firmware.h`）：

- 首次 STA 连接 **10 秒**超时后回退为 AP。
- STA 链路断开时每 **5 秒**重试 `WiFi.begin()`；断开 **60 秒**后重新开启 AP
  以便重新配置。
- 回退 AP 且 `prefMode` 为 STA 时，每 **10 分钟**再次尝试加入 STA。

**端口**：HTTP `80`，SSH `22`，强制门户 DNS `53`（仅 AP 模式）。

**IPv6** - STA 与 AP 均启用（`WiFi.enableIPv6()` / `WiFi.softAPenableIPv6()`）。
已分配地址通过 `GET /status` 的 `wifiIpv6Addrs`（JSON 字符串数组）上报：STA
包含 link-local 与 global（若已分配）；AP 上报 softAP link-local。未分配的
地址项会被省略；若无一分配（或 Wi-Fi 关闭），数组为 `["::"]`。

## SSH 访问

- TCP 端口 **22**。
- **主机密钥** - Ed25519，首次启动时生成并存入 NVS。「运行状态」页显示 OpenSSH 格式
  公钥，便于核对指纹。
- **认证方式**（任一已配置方式均可）：
  - 密码 - 默认 `admin` / `admin`
  - 公钥 - 一行 OpenSSH `authorized_keys`（RSA-8192 上限，最多 1536 字符）
  - 免认证 - 可选，默认关闭
- 同一时间桥接一个客户端会话到 USB-CDC。已有客户端连接时，额外 TCP 连接会被
  拒绝（`SSH_ACCEPT_BUSY` -> `rejectPending`）。
- **实现** - `SshService` 持有 `SshServer`，后者加载或生成主机密钥（`SshHostKey`）、
  在 `CORE_PROTOCOL` 运行非阻塞监听（`SshListener`）并服务单个客户端（`SshSession`）。
  `isConnected()` 表示是否有客户端连接；`isActive()` 表示会话是否仍在进行。流程：
  `keyExchange` -> 认证/通道/shell 协商 -> 通道 I/O。`SshService::bridge` 在 SSH
  通道与 `SerialService` 环形缓冲之间搬运字节（与本地 USB-CDC 为同一数据流）。桥接
  循环在无数据时休眠 `Firmware::BRIDGE_POLL_INTERVAL_MS`。
- **登录后提示**：

```
This SSH session is bridged to the device serial port.
Escape sequence is '~' '.' (at line start)
```

> **安全提示**：默认值（`admin`/`admin`、AP 密码 `12345678`）仅为首次配置方便。
> 在不受信任的网络部署前请更改凭据，并保持免认证模式关闭，除非你清楚其风险。

## Web 界面与 REST API

界面源码位于 `assets/`，处理器在 `src/web/handler.cpp`（`src/web/server.cpp`
仅负责注册路由）。HTTP 处理器在 AsyncTCP 任务（`CORE_PROTOCOL`）上运行。契约 - 路由、MIME 类型、响应头、JSON 字段名、错误 `type` 标识、
POST 体限制 - 定义在 `include/web/restful.h`。处理器将持久化委托给 `Runtime`，并通过
`getDeviceService()` / `getWifiService()` / `getSshService()` 直接组装
`GET /status` JSON。`ConfigHandler` 基类将每个配置路由分派到
`DeviceConfigHandler`、`WifiConfigHandler` 或 `SshConfigHandler` 的
`handleQuery` / `handleSave` / `handleReset`；字段的读取/校验/写出经由
`src/web/value.cpp` 的 `Value::*` 共享。
`assets/script.js` 与之对应（`DOMAIN_SPECS`、`ACTION_SPECS`、`LANG_SPECS.err`）。

`assets/` 下各文件在**文件头注释**中各自独立自描述。文件头只描述结构--页面壳
（`index.html`）、class 布局（`style.css`）、领域树（`script.js`）各含 model 与
ASCII 树。本节覆盖运行时行为与固件对齐。运行时依赖结构性 parse，markup 形状无单独
审计步骤。**结构性 parse 失败**会 `console.error` 并令对应 tab 或 domain 保持为空。开发时须保持 `index.html` 与 `DOMAIN_SPECS` / `TAB_SPECS` 及
`restful.h` 一致。修改后执行 `make assets`（或等构建流程在输入变化时自动重新生成）。

### 分层

| 层 | 职责 | 文档位置 |
| -- | ---- | -------- |
| HTML | 页面壳（DOM 嵌套、`hidden`） | `index.html` 文件头（document tree） |
| `class` | 仅外观与布局 | `style.css` 文件头（class tree） |
| `data-*` | 角色、wire key、操作、i18n key、字段错误 type id | `index.html` 文件头；`script.js` wire 表 |

CSS 仅使用 class 选择器。JS 通过 `data-*` 属性（`data-role`、`data-domain`、
`data-field` 等）发现结构；`.active` 等 class 仅用于切换样式。`class` 挂钩样式，
`data-*` 挂钩逻辑；各层职责分离。

### CSS 模型与领域模型

同一容器常同时带布局 class 与领域 role，二者命名独立：

```html
<form class="stack" data-role="block">
```

| 模型 | 挂钩 | 含义 | ASCII 树位置 |
| ---- | ---- | ---- | ------------ |
| **CSS** | `class="stack"` | 竖向布局壳。子节点可为 `.status`、`.field`、`.group`、`.controls`、`.actions`。仅负责布局；逻辑走 `data-*`。 | `style.css` 文件头（class model、class tree） |
| **领域** | `data-role="block"` | domain host 下的区域：常驻字段、条件子 block（`data-group`）、可选 selector 与 hint。解析为 `Block`。 | `script.js` 文件头（领域模型、领域树） |

领域类型：`Domain`、`Block`、`Field`（见下文**领域模型**）。CSS 布局类型含
`.section`、`.field`、`.group` 等；与 `data-*` 按约定配对。

### 文件头结构

| 文件 | 文件头章节 |
| ---- | ---------- |
| `index.html` | Role、document tree、data-role、data-* |
| `style.css` | Role、class model、class tree、变量 (:root)、响应式 |
| `script.js` | Role、领域模型、领域树、data-role、modules |

### 运行时

**启动** - `DOMContentLoaded` 构造 `Runtime` 并调用 `boot()`。

**模块** - `UI` 解析 markup、绑定事件，驱动 tab、领域同步、i18n 与状态展示。
`Runtime` 编排 tab 加载、配置/系统操作与 HTTP。`DOM`、`Dialog`、`HttpClient`、
`Language`、`Poller` 支撑上述层（见 `script.js` 文件头 Modules）。

**初始化** - `UI.init()` 按索引将 `[data-role=tab]` /
`[data-role=tabpanel]` 对齐 `TabRoute.getNames()`。对每个 domain，
`#resolveDomainBlocks` 定位 host（tabpanel 或 `.section` 上的 `[data-domain]`），
再由 `#collectTopLevelBlocks` 收集 domain 根级 `[data-role=block]`（仅根级；
嵌套 block 留在父级 `Block.blocks[]`）。`#parseBlock` 解析为 `Domain.blocks`；`[data-action]`
归入 `domain.actions`。shell 节点、domain host 或 block 缺失时 `console.error`，
对应 tab 或 domain 保持为空。`bind()` 绑定 tab、语言、域操作及每个 block 的 selector
`change` -> `syncDomain`。
所有按钮均为 `type="button"` + `data-action`。`Runtime.dispatchAction` 按
`ACTION_SPECS` 路由。

**领域模型** - `Domain { el, name, blocks[], actions }`。每个 domain 根级
`[data-role=block]` 解析为 `Block`，含 `fields`（常驻）、`blocks[]`（子 block）、
可选 `selector`（`[data-role=group-selector]`）与 `hintEl`（`[data-role=group-hint]`）。
条件子 block 为直接子节点 `[data-role=block][data-group]`；`Block.name` 读取
`data-group`。允许多个同名兄弟 block。嵌套 block 解析进父级 `Block.blocks[]`。
block 级字段在 `Block.fields`（仅直接子字段）。selector 的 `option[value]`
按名称匹配子 block（`off` 不显示子 block）；可选 `option[data-hint]` 在选中时写入 `hintEl`。
`Block.forEachField` / `forEachSelectedField` 递归 `blocks[]`（selected 遍历
仅包含 selector 选中的具名子 block）。`Domain.forEachBlock` 用于 sync/bind。
`Domain.forEachSelectedField` / `getDirtyValues()` 含 block 级字段与选中子 block
字段；`forEachField` / `applyMeta` 覆盖全部字段。模式切换时 `#resetHiddenValues`
清除隐藏字段脏值。Wi-Fi `prefMode`（`"off"` / `"sta"` / `"ap"`）对应子 block `sta` / `ap`。

**配置保存/重置** - 保存：`#buildConfigRequest` 从 `getDirtyValues()` 组 POST 体，
再经 `#validateConfigRequest` 做客户端校验。各配置域均走 `#validateFields`
（`#isValidFieldValue` 对照 field meta：长度、可选 `pattern`、number 的数值范围；`allowNoAuth` 为布尔）。规则与
`value.cpp` 中 `Value::isValidText` / `isValidNumber` 一致（字符串
`pattern` 由 `RegexCache` 校验；数字用 `minValue` / `maxValue`；POST 时由各处理器的
`mergeConfig` / `validateConfig`（`handler.cpp`）执行）。失败时
`#getFieldErrorMsg` 返回 `field.errorMsg`；漏配时记录
`Missing data-error: <domain>.<field>` 并仍阻止保存。结果为 `RequestResult`。
`REQUEST_STATE.OK` 时 POST；组包/POST 失败用 `Dialog.showError`。POST 成功 ->
`Dialog.showInform` -> `#finishConfigAction` -> `reloadTabFields`（Network tab
同时拉 device 与 wifi）。reload 失败 -> `showPanelError`（与 `loadTabIfNeeded`
相同）。reload 被 supersede 时 tab 保持 `UNLOADED`，仍在前台时可重试。重置：
确认 -> POST（无 body）-> 成功框 -> reload。

device / wifi 的 save|reset 在 `runtime.cpp` 里延迟约 1.5 s 再 `wifi.restart()`，
以便 POST 响应和随后的 `GET /config/*/fields` 在常见情况下于同一连接上完成；
网络路径不变时，界面通常只显示成功框。客户端改连别处（如换了 STA SSID），或
`disconnect: true` 在读完响应前把断连当作 POST 成功时，更常见「成功框 + 错误条」。

`data-field` 必须与 `restful.h` 的 `WIRE_KEY_*` 一致。配置字段通过 `data-error`
声明错误 type id（对应 `LANG_SPECS.err` / `ERR_*`）；`Field.errorMsg` 在解析时
读取。`data-error` 必须与 `ERR_*` type id 及 `LANG_SPECS.err` 键一致。用户可见
错误文案仅在 `LANG_SPECS.err` 中维护；固件只返回机器可读的 `type` 标识。

### 页面布局

四个 tab 按 DOM 顺序（须与 `TAB_SPECS` / `TabRoute.getNames()` 一致）：
`status` | `network` | `ssh` | `system`。每个 `[data-role=tabpanel]` 以
`[data-role=tabpanel-error]` 开头，后跟卡片区块（按需 `[data-domain]`）。

| Tab | 内容 |
| --- | ---- |
| 运行状态 | 只读 `data-field` 单元格，来自 `GET /status`（每 10 秒轮询） |
| 网络配置 | 设备名 + Wi-Fi 模式与 STA/AP 分组；经 `data-action` 保存/重置 |
| SSH | SSH 凭据与免认证开关；经 `data-action` 保存/重置 |
| 系统管理 | 重启与恢复出厂，经 `data-action` |

结构 ASCII 树：页面壳见 `index.html` 文件头；领域树见 `script.js` 文件头
（领域模型、领域树）；class 布局见 `style.css` 文件头（class model、class tree）。

### 样式与变量

`:root` 变量与响应式规则见 `style.css` 文件头（class model、class tree）。变量命名：`--{实体}` 与
`--{实体}-{程度}`（`base` -> `muted` -> `faint` -> `subtle` -> `strong`；
`background` 另有 `-deep`）。`--tab-count` 须等于 `.tablist` 内 `.tab` 数量。
默认面向桌面；mobile 段中 `@media (max-width: 600px)` 覆盖。移动端表单输入保持
16px，避免 iOS 聚焦时自动缩放。

### HTTP 响应

所有响应包含 `X-Content-Type-Options: nosniff`。`RESPONSE_POLICY_*` 策略表在
`restful.h`（由 `request.cpp` 中 `HttpRequest` 的 `applyResponsePolicy` 套用）：

| 策略 | 用于 | 响应头 |
| ---- | ---- | ------ |
| `RESPONSE_POLICY_STATIC_PAGE` | `GET /` | `Cache-Control: public, max-age=180`、`Content-Encoding: gzip` |
| `RESPONSE_POLICY_STATIC_ASSET` | `GET /style.css`、`/script.js` | `Cache-Control: public, max-age=31536000, immutable`、`Content-Encoding: gzip` |
| `RESPONSE_POLICY_API` | JSON、204、错误 | `Cache-Control: no-cache` |

重启、恢复出厂及部分配置保存可能附加 `Connection: close`。

### REST 路由

| 方法 | 路径 | 成功响应 |
| ---- | ---- | -------- |
| GET | `/` | HTML（gzip） |
| GET | `/style.css` | CSS（gzip） |
| GET | `/script.js` | JavaScript（gzip） |
| GET | `/status` | JSON 运行快照 |
| GET | `/config/device/fields` | JSON + 字段元数据 |
| POST | `/config/device/save` | 204 No Content |
| POST | `/config/device/reset` | 204 No Content |
| GET | `/config/wifi/fields` | JSON + 字段元数据 |
| POST | `/config/wifi/save` | 204 No Content |
| POST | `/config/wifi/reset` | 204 No Content |
| GET | `/config/ssh/fields` | JSON + 字段元数据 |
| POST | `/config/ssh/save` | 204 No Content |
| POST | `/config/ssh/reset` | 204 No Content |
| POST | `/reboot` | 204，随后重启 |
| POST | `/factory-reset` | 204，随后重启 |

**`GET /status` 字段** - `deviceName`、`deviceFwVersion`、`deviceFreeMem`、
`deviceTotalMem`、`deviceUptime`、`deviceSdkVersion`、
`wifiMode`（`"off"` / `"sta"` / `"ap"`）、`wifiRssi`、`wifiMacAddr`、
`wifiIpv4Addr`、`wifiIpv6Addrs`、`sshConnected`、`sshHostKey`。

`deviceFreeMem` / `deviceTotalMem` 为可用 RAM 字节数（内部堆 + PSRAM，若存在）。

`wifiIpv6Addrs` 为**字符串数组**（已去重）。「运行状态」页按行显示各地址
（`fe80::...`、global 等）。示例：

```json
"wifiIpv6Addrs": ["fe80::12:34ff:fe56:7890", "2001:db8::1"]
```

**`GET /config/*/fields`** - 每个字段为 **fieldMeta** 对象：

| 键 | 含义 |
| -- | ---- |
| `value` | 当前值（部分字段可省略） |
| `minLength` | 可选。字符串最小长度。存在时，空字符串非法，除非 `allowEmpty` 为 `true`。省略表示无下限。 |
| `maxLength` | 可选。字符串最大长度。省略表示无上限。 |
| `minValue` | 可选。数值下限（如 `type="number"` 输入）。省略表示无下限。 |
| `maxValue` | 可选。数值上限（如 `type="number"` 输入）。省略表示无上限。 |
| `pattern` | 可选。非空字符串的 POSIX 扩展正则（`REG_EXTENDED`）；须同时兼容 JavaScript `RegExp` 语法。省略表示不做正则校验。 |
| `allowEmpty` | 为 `true` 时，空字符串（文本）或空数字框（保存时为 `null`）合法 |

约束键在不适用时省略。示例：

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

| 域 | 字段名 |
| -- | ------ |
| device | `name` |
| wifi | `prefMode`、`staSsid`、`staPassword`、`apSsid`、`apPassword` |
| ssh | `username`、`password`、`authorizedKey`、`allowNoAuth` |

**部分保存** - `POST /config/*/save` 仅合并请求体中出现的键。`WifiConfigHandler::mergeConfig`
先把出现的字段合并到当前配置，再由 `validateConfig` 执行模式/SSID 的跨字段规则；
仅非空且校验通过的 `prefMode` 会写入配置。

**密码** - 已保存时 `GET .../fields` 返回 `"value": "{keep}"`
（`WIRE_VALUE_PASSWORD_KEEP`）。界面会在密码框中显示该占位符；要修改须替换或
清空后再输入。POST 时省略该键以保持原密码；发送 `"{keep}"` 会被拒绝。在允许
`allowEmpty` 的字段上，空字符串表示清除密码。

**错误** - HTTP 4xx/5xx，响应体为 `application/problem+json`：
`{"type":"<typeId>"}`：

| `type` | 常见原因 |
| ------ | -------- |
| `requestEmpty` | POST 体缺失 |
| `requestTooSmall` | 低于路由体下限 |
| `requestTooLarge` | 超过路由体上限 |
| `requestInvalidFormat` | 请求体不是 JSON 对象 |
| `requestOverflow` | JSON 过大无法解析 |
| `responseOverflow` | 响应 JSON 过大无法构建 |
| `responseFailed` | NVS 持久化、配置 reload 或延期调度失败 |
| `configDeviceInvalidName` | 设备名无效 |
| `configWifiInvalidPrefMode` | `prefMode` 须为 `"off"` / `"sta"` / `"ap"` |
| `configWifiInvalidSsid` | 当前模式下 SSID 无效 |
| `configWifiInvalidPassword` | Wi-Fi 密码无效 |
| `configSshInvalidUsername` | SSH 用户名无效 |
| `configSshInvalidPassword` | SSH 密码无效 |
| `configSshInvalidAuthorizedKey` | 公钥无效 |
| `configSshInvalidAllowNoAuth` | `allowNoAuth` 不是布尔值 |

`responseFailed` 在请求校验通过后、但 `Runtime` 报告 NVS 持久化、内存 mirror
reload 或延期 restart/reboot 调度失败时返回（例如 `POST /config/*/save`、
`POST /config/*/reset`、`POST /reboot`、`POST /factory-reset`）。

校验限制：字符串长度见 `include/firmware.h`；wire pattern 见
`include/web/restful.h`（`WIRE_PATTERN_DEVICE_NAME`、`WIRE_PATTERN_WIFI_MODE`）。
通过 fieldMeta 下发（字符串用 `minLength` / `maxLength`，数字用 `minValue` / `maxValue`，另含 `pattern`）。
界面在 `#isValidFieldValue`（`#isValidFieldLength`、`#isValidFieldPattern`、
`#isValidNumberRange`）中对照 meta 检查，并于 POST 前经 `#validateConfigRequest` 预校验（与 `value.cpp` 中
`Value::isValidText` / `isValidNumber` 一致；`pattern` 经 `RegexCache` 处理）。

**Wi-Fi 模式 wire 值** - `prefMode` 与 `wifiMode` 使用 `"off"`、`"sta"`、`"ap"`
（`restful.h` 中 `WIRE_VALUE_WIFI_MODE_*`），用于 `GET /status`、`GET /config/wifi/fields`、
`POST /config/wifi/save`。新增或重命名模式时须同步更新：

| 位置 | 须更新内容 |
| ---- | ---------- |
| `include/web/restful.h` | `WIRE_VALUE_WIFI_MODE_OFF` / `_STA` / `_AP`，`WIRE_PATTERN_WIFI_MODE` |
| `include/firmware.h` | `LIMIT_WIFI_MODE_MIN_LEN`、`LIMIT_WIFI_MODE_MAX_LEN` |
| `src/web/handler.cpp` | `wifiModeToWire()`、`wifiModeFromWire()` |
| `assets/script.js` + `assets/index.html` | `WIFI_MODE` 常量、`prefMode` 的 `<option value="...">` |

### 保持一致

| 源文件 | 应对齐的固件内容 |
| ------ | ---------------- |
| `include/web/restful.h` | 路由、`WIRE_KEY_*`、`WIRE_VALUE_*`、`WIRE_PATTERN_*`、`ERR_*` type、密码 `{keep}` |
| `assets/script.js` | `TAB_SPECS`、`DOMAIN_SPECS`、`ACTION_SPECS`、`LANG_SPECS.err`；文件头（领域模型、领域树） |
| `assets/index.html` | document tree、data-role、data-*（`data-field`、`data-error` 等） |
| `assets/style.css` | class model、class tree、变量 (:root)、`--tab-count` |
| `src/web/handler.cpp` | `DeviceConfigHandler` / `WifiConfigHandler` / `SshConfigHandler`（`handleQuery` / `handleSave` / `handleReset`）、各域 `mergeConfig` / `validateConfig`、`GET /status` JSON、`wifiModeToWire` / `wifiModeFromWire` |
| `src/web/value.cpp` | `Value::isValid*` / `read*` / `write*`、`RegexCache` 模式匹配 |
| `include/web/domain.h` | `Field` / `Route` 表与 `FieldMeta` 约束 |

## 配置参考

默认值与限制见 `include/firmware.h`。

**默认值**

| 配置项 | 默认值 |
| ------ | ------ |
| 设备名 | `SerialBridge-XXXX`（`XXXX` = eFuse MAC 低 16 位） |
| Wi-Fi 模式 | AP（`Firmware::DEFAULT_WIFI_PREF_MODE` = `2`，对应 `WifiMode::Ap`） |
| STA SSID / 密码 | 空 |
| AP SSID | 设备名（仅在与设备名不同时写入 NVS；否则读取时派生） |
| AP 密码 | `12345678` |
| SSH 用户名 / 密码 | `admin` / `admin` |
| SSH 授权公钥 | 无 |
| SSH 允许免认证 | 关闭 |
| SSH 主机密钥 | Ed25519，首次启动 SSH 时生成 |
| LED 亮度 | `8`（`Firmware::LED_BRIGHTNESS`；黑 overlay 的 alpha 为 `255 - 值`，`0`-`255`） |

**限制**

| 字段 | 范围 |
| ---- | ---- |
| 设备名 | 1-32 字符（字母、数字、连字符；不能以连字符开头/结尾） |
| Wi-Fi 模式（`prefMode`） | 2-3 字符；`"off"` / `"sta"` / `"ap"` |
| Wi-Fi SSID | 1-32 字符 |
| Wi-Fi 密码 | 8-63 字符，或留空使用开放网络 |
| SSH 用户名 | 1-32 字符 |
| SSH 密码 | 最多 64 字符 |
| SSH 授权公钥 | 最多 1536 字符（OpenSSH 行，RSA-8192 上限） |

## 依赖

Arduino 库（版本固定在 `sketch.yaml`）：

| 库 | 版本 |
| -- | ---- |
| [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | 3.12.0 |
| [Async TCP](https://github.com/ESP32Async/AsyncTCP) | 3.5.0 |
| [ArduinoJson](https://arduinojson.org/) | 7.4.3 |
| [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32) | 5.9.0 |

开发板支持包：`esp32:esp32` **3.3.11**。

构建工具：

- [`arduino-cli`](https://arduino.github.io/arduino-cli/)
- Python 3.10+（`scripts/`）
- Node.js + npm（资源压缩，`scripts/package.json`）

`embed_assets.py` 通过 Node 调用 `minify.mjs`。缺少 `node_modules/` 时构建会失败并
提示安装命令。

## 安装依赖

新环境首次克隆仓库，或 `sketch.yaml` 升级版本后执行一次。需要
[`arduino-cli`](https://arduino.github.io/arduino-cli/) 已在 `PATH` 中。

**ESP32 开发板支持包**

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
```

**Arduino 库**

库名须与 Arduino Library Manager 一致（含空格）。
`ESP Async WebServer@3.12.0` 依赖 `Async TCP@3.5.0`，需成对安装。

```sh
arduino-cli lib install \
  "ArduinoJson@7.4.3" \
  "Async TCP@3.5.0" \
  "ESP Async WebServer@3.12.0" \
  "LibSSH-ESP32@5.9.0"
```

**Web 资源构建工具（npm）**

```sh
cd scripts && npm install
```

**确认版本**

```sh
arduino-cli core list
arduino-cli lib list | grep -E 'ArduinoJson|Async TCP|ESP Async|LibSSH'
cd scripts && npm ls
```

期望：平台 `esp32:esp32` **3.3.11**；库版本与上表一致；
`scripts/node_modules/` 下已有 `@minify-html/node` 与 `esbuild`。

完成后执行 `make build` 验证（见[构建与烧录](#构建与烧录)）。

## 构建与烧录

`Makefile` 封装 `scripts/build.py`：嵌入 Web 资源、注入 git 短哈希 `FW_VERSION_ID`、
arduino-cli 编译、可选上传。WSL 下若找不到串口，上传时会自动调用 `attach_wsl_usb.py`。

```sh
make                  # 显示帮助（默认）
make build            # 编译
make flash            # 编译并上传
make assets           # 重新生成嵌入的 Web 资源
make boards           # 列出开发板与串口
make clean            # 清除构建缓存
```

```sh
make build PROFILE=debug
make flash PORT=/dev/ttyACM0
make build FORCE_ASSETS=1
```

**Profile**（`sketch.yaml`）：

| Profile | 说明 |
| ------- | ---- |
| `release` | 普通构建/上传（默认） |
| `release-full` | release，上传时擦除整片 flash |
| `debug` | 详细调试日志 |
| `debug-full` | 调试日志 + 擦除整片 flash |

**草图编译选项**（`build_opt.h`）：

| 选项 | 用途 |
| ---- | ---- |
| `-Iinclude` | `#include "..."` 头文件搜索路径 |
| `-fno-exceptions` | 禁用 C++ 异常 |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=0` | 将 Async TCP 绑定到 `Firmware::CORE_PROTOCOL` |

**Make 变量**

| 变量 | 默认值 | 用于 |
| ---- | ------ | ---- |
| `PROFILE` | `release` | `build`、`flash`、`clean` |
| `FORCE_ASSETS` | - | `build`、`flash`（`FORCE_ASSETS=1`） |
| `VERBOSE` | - | `build`、`flash`（`VERBOSE=1`） |
| `NO_GIT_HASH` | - | `build`、`flash`（`NO_GIT_HASH=1`） |
| `PORT` | 自动检测 | `flash` |

`make flash` 向 `build.py` 传递 Espressif USB ID（`VID`/`PID`/`AUTO_ATTACH`，默认
`303a:1001`）。

git HEAD 注入为 `FW_VERSION_ID`（如 `v0.1a (a63d8ff)`）。`NO_GIT_HASH=1` 可跳过。

**串口** - 原生 USB 一般为 `/dev/ttyACM*`；USB-UART 芯片为 `/dev/ttyUSB*`。未指定
`PORT` 时 `make flash` 自动检测两者。

### WSL USB 透传

在 Windows 插板、WSL 内构建需用 [`usbipd-win`](https://github.com/dorssel/usbipd-win)
转发设备，之后出现 `/dev/ttyACM*` 或 `/dev/ttyUSB*`。

**自动** - `make flash` / `build.py -u` 在 WSL 内找不到串口时调用
`attach_wsl_usb.py`。

**手动**（WSL 中）：

```sh
./scripts/attach_wsl_usb.py
./scripts/attach_wsl_usb.py --vid 303a --pid 1001 --auto-attach 1
```

请从 WSL 运行 `.py` 入口；它通过 `powershell.exe -ExecutionPolicy Bypass` 调用 Windows
上的 `attach_wsl_usb.ps1`。

**首次配置**（管理员 PowerShell）：

```powershell
usbipd list
usbipd bind --busid <BUSID>
```

若尚未 bind，attach 脚本会打印命令（退出码 `2`）。

**IDE / clangd** - 生成编译数据库：

```sh
cd scripts && python generate_compile_commands.py
```

## Web 资源

请编辑 `assets/` 源文件；构建会生成 `include/web/assets.h` 与 `src/web/assets.cpp`
（已 gitignore；由 `make assets` 或 `make build` 生成）。

流程（`scripts/embed_assets.py`）：

1. 经 `scripts/minify.mjs` 压缩（JS 用 `esbuild`，HTML/CSS 用 `@minify-html/node`；
   JS 默认缩短标识符）
2. gzip 压缩
3. 生成带长度的 `extern const uint8_t NAME[N] PROGMEM` 声明与定义

调用方用 `sizeof(Assets::NAME)` 获取字节长度（无单独 `_LEN` 常量）。`build.py` 在输入
变化时调用 `embed_assets()`；可用 `make assets` 或 `make build FORCE_ASSETS=1` 强制生成。
`embed_assets.py --no-strip` 可跳过压缩。

| 脚本 | 作用 |
| ---- | ---- |
| `build.py` | 编译 / 上传编排 |
| `embed_assets.py` | 压缩 + gzip + 生成 assets |
| `minify.mjs` | 压缩入口 |
| `attach_wsl_usb.py` / `.ps1` | WSL usbipd attach |
| `generate_compile_commands.py` | clangd 编译数据库 |

## 工程结构

```
assets/              Web 界面源码（index.html、style.css、script.js）
include/
  firmware.h         编译期常量（Firmware：MCU、LED、端口、时序、默认值、限制）
  config/            *Config / *ConfigStorage 配对
  util/              deferred（DeferredScheduler）、nvs（Nvs）、ringbuf（SPSC RingBuf / RingReader / RingWriter）、string（StringView）、regex（RegexCache）、worker（Worker）、mutex、semaphore
  hardware/          led.h（全局 `LED`）
  ssh/               SshServer、SshListener、SshSession、SshHostKey、SshCredentials、SshKey
  service/           device、serial、Wi-Fi、ssh（SshService 网关）API
  web/               restful.h（契约）、model.h/domain.h（数据模型与表）、value.h、request.h、handler.h、server.h；assets.h 构建时生成
  runtime.h          组合根（配置镜像 + 服务 + 延期生效）
src/                 与 include/ 镜像；实现流程见各文件头注释
*.ino                入口：持有 Runtime + WebServer 适配器
build_opt.h          草图级编译选项（见[构建与烧录](#构建与烧录)）
scripts/             build.py、embed_assets.py、minify.mjs、attach_wsl_usb.*
Makefile             封装 build.py；裸 make 显示帮助
sketch.yaml          arduino-cli profile、FQBN、固定版本库
```

## 许可证

基于 [MIT 许可证](LICENSE) 发布。Copyright (c) 2026 RenoSeven。
