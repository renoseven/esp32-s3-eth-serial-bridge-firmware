// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SerialBridge - device configuration script
//
// Role
//   Wire tables, infra, domain model, HTTP transport, UI runtime.
//
// Domain model
//   Domain  el, name, blocks[], actions
//   Block   el, name, selector, hintEl, fields, blocks[]
//   Field   el, name, errorMsg, meta
//
//   Block.name <- data-group on conditional child blocks
//   group-selector option.value matches child Block.name (except "off")
//
// Domain tree
//   [data-domain] host                        .tabpanel | .section
//   +-- [data-role=block] ...                 domain-root -> Domain.blocks[]
//   |   +-- [data-role=group-selector]        Block.selector (optional)
//   |   |   +-- option[value][data-hint]
//   |   +-- [data-role=group-hint]            Block.hintEl (optional)
//   |   +-- [data-field] ...                  Block.fields
//   |   +-- [data-role=block][data-group]     nested Block (optional; same name allowed)
//   |   |   +-- [data-field] ...              child Block.fields
//   |   |   +-- [data-role=block] ...         nested child Block.blocks[] (optional)
//   |   +-- [data-action] ...                 Domain.actions
//
// data-role
//   lang-switch, tablist, tab-indicator, tab, tabpanels, tabpanel, tabpanel-error
//   block, group-selector, group-hint, dialog, dialog-message
//
// Modules
//   DOM, Language, Poller, HttpClient, Dialog, UI, Runtime

// ============================================================
//  Wire
// ============================================================

// --- enums ---

const DOMAIN_TYPE = { POLL: 0, CONFIG: 1, ACTION: 2 };
const WIFI_MODE = { OFF: 'off', STA: 'sta', AP: 'ap' };
const TAB_STATE = { UNLOADED: 0, FETCHING: 1, READY: 2 };
const REQUEST_STATE = { OK: 0, FAIL: 1 };
const RELOAD_STATE = { OK: 0, FAIL: 1, SUPERSEDED: 2 };
const DIALOG_TYPE = { INFORM: 0, CONFIRM: 1, ERROR: 2 };
const DIALOG_STATE = { DISMISSED: 0, ACCEPTED: 1 };

// --- data-role ---

// Keys: SCREAMING_SNAKE_CASE. Values: DOM data-role strings (kebab-case).
const DATA_ROLE = Object.freeze({
    // header
    LANG_SWITCH: 'lang-switch',

    // tablist
    TABLIST: 'tablist',
    TAB_INDICATOR: 'tab-indicator',
    TAB: 'tab',

    // tabpanels
    TABPANELS: 'tabpanels',
    TABPANEL: 'tabpanel',
    TABPANEL_ERROR: 'tabpanel-error',

    // block
    BLOCK: 'block',
    GROUP_HINT: 'group-hint',
    GROUP_SELECTOR: 'group-selector',

    // dialog
    DIALOG: 'dialog',
    DIALOG_MESSAGE: 'dialog-message'
});

// --- limits ---

const HTTP_NETWORK_ERROR = 0;
const HTTP_LOCAL_ERROR = -1;
const HTTP_NO_CONTENT = 204;

const HTTP_GET_TIMEOUT_MS = 3000;
const HTTP_POST_TIMEOUT_MS = 5000;
const STATUS_POLL_INTERVAL_MS = 10000;

// --- specs ---

const DOMAIN_SPECS = Object.freeze({
    status: {
        type: DOMAIN_TYPE.POLL,
        interval: STATUS_POLL_INTERVAL_MS,
        fetch: {
            route: '/status'
        }
    },
    device: {
        type: DOMAIN_TYPE.CONFIG,
        fields: {
            route: '/config/device/fields'
        },
        save: {
            route: '/config/device/save',
            disconnect: true,
            successMsg: 'deviceSaveOk'
        },
        reset: {
            route: '/config/device/reset',
            disconnect: true,
            confirmMsg: 'deviceConfirmReset',
            successMsg: 'deviceResetOk'
        }
    },
    wifi: {
        type: DOMAIN_TYPE.CONFIG,
        fields: {
            route: '/config/wifi/fields'
        },
        save: {
            route: '/config/wifi/save',
            disconnect: true,
            successMsg: 'wifiSaveOk'
        },
        reset: {
            route: '/config/wifi/reset',
            disconnect: true,
            confirmMsg: 'wifiConfirmReset',
            successMsg: 'wifiResetOk'
        }
    },
    ssh: {
        type: DOMAIN_TYPE.CONFIG,
        fields: {
            route: '/config/ssh/fields'
        },
        save: {
            route: '/config/ssh/save',
            successMsg: 'sshSaveOk'
        },
        reset: {
            route: '/config/ssh/reset',
            confirmMsg: 'sshConfirmReset',
            successMsg: 'sshResetOk'
        }
    },
    system: {
        type: DOMAIN_TYPE.ACTION,
        reboot: {
            actionKey: 'reboot-system',
            route: '/reboot',
            disconnect: true,
            confirmMsg: 'systemConfirmReboot',
            successMsg: 'systemRebooting'
        },
        factoryReset: {
            actionKey: 'reset-factory',
            route: '/factory-reset',
            disconnect: true,
            confirmMsg: 'systemConfirmFactoryReset',
            successMsg: 'systemFactoryRebooting'
        }
    }
});

const TAB_SPECS = Object.freeze({
    status: ['status'],
    network: ['device', 'wifi'],
    ssh: ['ssh'],
    system: ['system']
});

const ACTION_NAMES = Object.freeze(['save', 'reset', 'reboot', 'factoryReset']);

const ACTION_SPECS = Object.freeze(
    Object.fromEntries(
        Object.entries(DOMAIN_SPECS).flatMap(function ([domainName, domainSpec]) {
            return ACTION_NAMES.filter(function (actionName) {
                return domainSpec[actionName]?.route;
            }).map(function (actionName) {
                const endpoint = domainSpec[actionName];
                const actionKey = endpoint.actionKey ?? `${actionName}-${domainName}`;

                return [actionKey, { domainName: domainName, actionName: actionName }];
            });
        })
    )
);

const LANG_SPECS = Object.freeze({
    'en-US': {
        ui: {
            // header
            productName: 'SerialBridge',
            productTagline: 'Serial over SSH',
            langSwitch: '中',

            // tabs
            tabStatus: 'Status',
            tabNetwork: 'Network',
            tabSsh: 'SSH',
            tabSystem: 'System',

            // status
            statusDeviceTitle: 'Device',
            statusDeviceName: 'Name',
            statusDeviceFwVersion: 'Firmware Version',
            statusDeviceSdkVersion: 'SDK Version',
            statusDeviceMemory: 'Free Memory',
            statusDeviceUptime: 'Uptime',
            statusWifiTitle: 'Wi-Fi',
            statusWifiMode: 'Mode',
            statusWifiModeOff: 'Off',
            statusWifiModeSta: 'Client',
            statusWifiModeAp: 'Access Point',
            statusWifiRssi: 'RSSI',
            statusWifiMacAddr: 'MAC Address',
            statusWifiIpv4Addr: 'IPv4 Address',
            statusWifiIpv6Addrs: 'IPv6 Address',
            statusSshTitle: 'SSH',
            statusSshHostKey: 'SSH Host Key',
            statusSshHostKeyPending: 'Generating on first SSH startup...',
            statusSshConnection: 'SSH Connection',
            statusSshConnected: 'Connected',
            statusSshDisconnected: 'Disconnected',

            // device
            deviceTitle: 'Device',
            deviceName: 'Name',
            deviceSave: 'Save',
            deviceReset: 'Reset',
            deviceConfirmReset: 'Reset device config to defaults?',
            deviceSaveOk: 'Device config saved.',
            deviceResetOk: 'Device config reset to defaults.',

            // wifi
            wifiTitle: 'Wi-Fi',
            wifiPrefMode: 'Mode',
            wifiModeOff: 'Off',
            wifiModeSta: 'Client',
            wifiModeAp: 'Access Point',
            wifiModeStaHint: 'Connect this device to an existing Wi-Fi network.',
            wifiModeApHint: 'Create a Wi-Fi hotspot for other devices to connect.',
            wifiApSsid: 'SSID',
            wifiApPassword: 'Password',
            wifiStaSsid: 'SSID',
            wifiStaPassword: 'Password',
            wifiSave: 'Save',
            wifiReset: 'Reset',
            wifiConfirmReset: 'Reset Wi-Fi config to defaults?',
            wifiSaveOk: 'Wi-Fi config saved.',
            wifiResetOk: 'Wi-Fi config reset to defaults.',

            // ssh
            sshTitle: 'Authentication',
            sshUsername: 'Username',
            sshPassword: 'Password',
            sshAuthorizedKey: 'Authorized Key (optional)',
            sshAllowNoAuth: 'Allow unauthenticated SSH',
            sshSave: 'Save',
            sshReset: 'Reset',
            sshConfirmReset: 'Reset SSH config to defaults?',
            sshSaveOk: 'SSH config saved.',
            sshResetOk: 'SSH config reset to defaults.',

            // system
            systemTitle: 'System',
            systemReboot: 'Reboot Device',
            systemRebootDescription: 'Restart the device without changing any saved settings.',
            systemRebootAction: 'Reboot',
            systemConfirmReboot: 'Reboot the device?',
            systemRebooting: 'Rebooting...',
            systemFactoryReset: 'Factory Config Reset',
            systemFactoryResetDescription: 'Erase all saved configuration and restart with defaults.',
            systemFactoryResetAction: 'Reset',
            systemConfirmFactoryReset: 'Factory config reset and reboot? All saved config will be erased.',
            systemFactoryRebooting: 'Factory config reset started. Rebooting...',

            // dialog
            dialogCancel: 'Cancel',
            dialogConfirm: 'OK'
        },
        err: {
            // request
            requestEmpty: 'Request body is required.',
            requestTooSmall: 'Request body too small.',
            requestTooLarge: 'Request body too large.',
            requestInvalidFormat: 'Invalid request format.',
            requestOverflow: 'Request overflow.',
            requestNetworkError: 'Network error.',
            requestFailed: 'Request failed.',

            // response
            responseOverflow: 'Response overflow.',
            responseFailed: 'Operation failed.',
            responseInvalidFormat: 'Invalid response format.',

            // config
            configNotReady: 'Config not loaded yet. Please wait and try again.',
            configNothingToSave: 'No changes to save.',

            // device config
            configDeviceInvalidName: 'Invalid device name.',

            // wifi config
            configWifiInvalidPrefMode: 'Invalid Wi-Fi mode.',
            configWifiInvalidSsid: 'Invalid Wi-Fi SSID.',
            configWifiInvalidPassword: 'Invalid Wi-Fi password.',

            // ssh config
            configSshInvalidUsername: 'Invalid SSH username.',
            configSshInvalidPassword: 'Invalid SSH password.',
            configSshInvalidAuthorizedKey: 'Invalid public key.',
            configSshInvalidAllowNoAuth: 'Invalid allow-no-auth setting.'
        }
    },
    'zh-CN': {
        ui: {
            // header
            productName: 'SerialBridge',
            productTagline: 'SSH 远程串口服务器',
            langSwitch: 'EN',

            // tabs
            tabStatus: '运行状态',
            tabNetwork: '网络配置',
            tabSsh: 'SSH',
            tabSystem: '系统管理',

            // status
            statusDeviceTitle: '设备',
            statusDeviceName: '名称',
            statusDeviceFwVersion: '固件版本',
            statusDeviceSdkVersion: 'SDK 版本',
            statusDeviceMemory: '剩余内存',
            statusDeviceUptime: '运行时长',
            statusWifiTitle: '无线网络',
            statusWifiMode: '模式',
            statusWifiModeOff: '关闭',
            statusWifiModeSta: '客户端',
            statusWifiModeAp: '接入点',
            statusWifiRssi: '信号',
            statusWifiMacAddr: 'MAC 地址',
            statusWifiIpv4Addr: 'IPv4 地址',
            statusWifiIpv6Addrs: 'IPv6 地址',
            statusSshTitle: 'SSH',
            statusSshHostKey: 'SSH 主机公钥',
            statusSshHostKeyPending: '首次启动 SSH 服务后生成',
            statusSshConnection: 'SSH 连接',
            statusSshConnected: '已连接',
            statusSshDisconnected: '未连接',

            // device
            deviceTitle: '设备配置',
            deviceName: '名称',
            deviceSave: '保存',
            deviceReset: '重置',
            deviceConfirmReset: '重置设备配置为默认值？',
            deviceSaveOk: '设备配置已保存。',
            deviceResetOk: '设备配置已恢复为默认值。',

            // wifi
            wifiTitle: '无线网络配置',
            wifiPrefMode: '模式',
            wifiModeOff: '关闭',
            wifiModeSta: '客户端',
            wifiModeAp: '接入点',
            wifiModeStaHint: '将设备连接到现有的无线网络。',
            wifiModeApHint: '创建无线网络热点，供其他设备连接。',
            wifiApSsid: '接入点名称',
            wifiApPassword: '密码',
            wifiStaSsid: '网络名称',
            wifiStaPassword: '密码',
            wifiSave: '保存',
            wifiReset: '重置',
            wifiConfirmReset: '重置无线网络配置为默认值？',
            wifiSaveOk: '无线网络配置已保存。',
            wifiResetOk: '无线网络配置已恢复为默认值。',

            // ssh
            sshTitle: '认证配置',
            sshUsername: '用户名',
            sshPassword: '密码',
            sshAuthorizedKey: '授权公钥 (可选)',
            sshAllowNoAuth: '允许免认证连接',
            sshSave: '保存',
            sshReset: '重置',
            sshConfirmReset: '重置 SSH 配置为默认值？',
            sshSaveOk: 'SSH 配置已保存。',
            sshResetOk: 'SSH 配置已恢复为默认值。',

            // system
            systemTitle: '系统管理',
            systemReboot: '重启设备',
            systemRebootDescription: '重启设备，不会更改任何已保存的配置。',
            systemRebootAction: '重启',
            systemConfirmReboot: '确定要重启设备吗？',
            systemRebooting: '正在重启...',
            systemFactoryReset: '恢复出厂配置',
            systemFactoryResetDescription: '清除所有已保存的配置，并以默认设置重启。',
            systemFactoryResetAction: '恢复',
            systemConfirmFactoryReset: '恢复出厂配置并重启？所有已保存的配置将被清除。',
            systemFactoryRebooting: '正在恢复出厂配置并重启...',

            // dialog
            dialogCancel: '取消',
            dialogConfirm: '确定'
        },
        err: {
            // request
            requestEmpty: '缺少请求体。',
            requestTooSmall: '请求体过小。',
            requestTooLarge: '请求体过大。',
            requestInvalidFormat: '请求格式无效。',
            requestOverflow: '请求溢出。',
            requestNetworkError: '网络错误。',
            requestFailed: '请求失败。',

            // response
            responseOverflow: '响应溢出。',
            responseFailed: '操作失败。',
            responseInvalidFormat: '响应格式无效。',

            // config
            configNotReady: '配置尚未加载，请稍后再试。',
            configNothingToSave: '没有可保存的更改。',

            // device config
            configDeviceInvalidName: '设备名无效。',

            // wifi config
            configWifiInvalidPrefMode: '无线网络模式无效。',
            configWifiInvalidSsid: '无线网络名称无效。',
            configWifiInvalidPassword: '无线网络密码无效。',

            // ssh config
            configSshInvalidUsername: 'SSH 用户名无效。',
            configSshInvalidPassword: 'SSH 密码无效。',
            configSshInvalidAuthorizedKey: '公钥无效。',
            configSshInvalidAllowNoAuth: '免认证设置无效。'
        }
    }
});

// --- routing ---

class TabRoute {
    static #NAMES = Object.freeze(Object.keys(TAB_SPECS));
    static #ROUTES = Object.freeze(
        Object.fromEntries(
            Object.entries(TAB_SPECS).flatMap(function ([tabName, domains]) {
                return domains.map(function (domainName) {
                    return [domainName, tabName];
                });
            })
        )
    );

    static getNames() {
        return TabRoute.#NAMES;
    }

    static getDomains(tabName) {
        return TAB_SPECS[tabName] ?? [];
    }

    static getConfigDomains(tabName) {
        return TabRoute.getDomains(tabName).filter(function (domainName) {
            return DOMAIN_SPECS[domainName]?.type === DOMAIN_TYPE.CONFIG;
        });
    }

    static getTabName(domainName) {
        return TabRoute.#ROUTES[domainName];
    }

    static hasDomainType(tabName, domainType) {
        for (const domainName of TabRoute.getDomains(tabName)) {
            if (DOMAIN_SPECS[domainName]?.type === domainType) {
                return true;
            }
        }

        return false;
    }

    static isFetchable(tabName) {
        return TabRoute.hasDomainType(tabName, DOMAIN_TYPE.POLL) || TabRoute.hasDomainType(tabName, DOMAIN_TYPE.CONFIG);
    }
}

// ============================================================
//  Infra
// ============================================================

class DOM {
    static hasDataRole(el, role) {
        return el?.getAttribute('data-role') === role;
    }

    static queryByDataRole(root, role) {
        return root?.querySelector(`[data-role="${role}"]`) ?? null;
    }

    static queryAllByDataRole(root, role) {
        return root ? [...root.querySelectorAll(`[data-role="${role}"]`)] : [];
    }
}

class Generation {
    #seq = 0;

    next() {
        return ++this.#seq;
    }

    isStale(expected) {
        return expected !== this.#seq;
    }
}

class AsyncScope {
    #controller = null;
    #generation = new Generation();

    begin() {
        this.#clearController();

        const controller = new AbortController();
        this.#controller = controller;
        const gen = this.#generation.next();

        return {
            signal: controller.signal,
            gen,
            end: () => {
                if (this.#controller === controller) {
                    this.#controller = null;
                }
            }
        };
    }

    abort() {
        this.#clearController();
        this.#generation.next();
    }

    isStale(gen) {
        return this.#generation.isStale(gen);
    }

    #clearController() {
        this.#controller?.abort();
        this.#controller = null;
    }
}

class Poller {
    #intervalMs;
    #timer = null;
    #paused = false;
    #inFlight = null;
    #generation = new Generation();

    constructor(intervalMs) {
        this.#intervalMs = intervalMs;
    }

    start(pollFn, refreshNow) {
        this.stop();

        if (refreshNow) {
            void this.#onPoll(pollFn).catch((err) => console.error('Status poll failed:', err));
        }

        this.#timer = setInterval(() => {
            void this.#onPoll(pollFn).catch((err) => console.error('Status poll failed:', err));
        }, this.#intervalMs);
    }

    stop() {
        if (this.#timer !== null) {
            clearInterval(this.#timer);
            this.#timer = null;
        }
    }

    setPaused(paused) {
        this.#paused = paused;
    }

    isStale(gen) {
        return this.#generation.isStale(gen);
    }

    invalidate() {
        this.#generation.next();
    }

    #onPoll(pollFn) {
        if (this.#paused) {
            return Promise.resolve();
        }

        if (!this.#inFlight) {
            const gen = this.#generation.next();
            this.#inFlight = Promise.resolve(pollFn(gen)).finally(() => {
                this.#inFlight = null;
            });
        }

        return this.#inFlight;
    }
}

class Language {
    static #LANGS = Object.freeze(Object.keys(LANG_SPECS));
    static #STORAGE_KEY = 'language';

    static #load() {
        try {
            const saved = localStorage.getItem(Language.#STORAGE_KEY);
            if (saved && LANG_SPECS[saved]) {
                return saved;
            }
        } catch (err) {
            console.error('Failed to read language preference:', err);
        }

        return Language.#LANGS[0];
    }

    #current;

    constructor() {
        this.#current = Language.#load();
    }

    getCurrent() {
        return this.#current;
    }

    getUiText(name) {
        return LANG_SPECS[this.#current]?.ui?.[name] ?? name;
    }

    getErrText(name) {
        return LANG_SPECS[this.#current]?.err?.[name] ?? name;
    }

    switchNext() {
        const langs = Language.#LANGS;

        this.#current = langs[(langs.indexOf(this.#current) + 1) % langs.length];
        this.#save();
    }

    #save() {
        try {
            localStorage.setItem(Language.#STORAGE_KEY, this.#current);
        } catch (err) {
            console.error('Failed to persist language preference:', err);
        }
    }
}

// ============================================================
//  Domain model
// ============================================================

class Domain {
    constructor(name) {
        this.el = null;
        this.name = name;
        this.blocks = [];
        this.actions = Object.create(null);
    }

    // --- traverse ---

    forEachField(fn) {
        for (const block of this.blocks) {
            block.forEachField(fn);
        }
    }

    forEachSelectedField(fn) {
        for (const block of this.blocks) {
            block.forEachSelectedField(fn);
        }
    }

    forEachBlock(fn) {
        const pending = [...this.blocks];

        while (pending.length) {
            const block = pending.pop();
            fn(block);

            for (let i = block.blocks.length - 1; i >= 0; i--) {
                pending.push(block.blocks[i]);
            }
        }
    }

    // --- fields ---

    getField(fieldName) {
        let found = null;

        this.forEachSelectedField((field) => {
            if (field.name === fieldName) {
                found = field;
            }
        });

        return found;
    }

    // --- meta ---

    applyMeta(data) {
        this.forEachField((field) => {
            const meta = data[field.name];

            field.setMeta(meta);
            if (!meta) {
                return;
            }

            field.applyMeta(meta);
            if (field.hasMetaValue()) {
                field.setValue(meta.value);
            }
        });
    }

    isMetaReady() {
        if (this.blocks.length === 0) {
            return true;
        }

        let ready = true;

        this.forEachSelectedField((field) => {
            if (!field.meta) {
                ready = false;
            }
        });

        return ready;
    }

    // --- save ---

    getDirtyValues() {
        const values = {};

        this.forEachSelectedField((field) => {
            if (field.isDirty()) {
                values[field.name] = field.readValue();
            }
        });

        return values;
    }

    getSaveErrorMsg() {
        if (!this.isMetaReady()) {
            return 'configNotReady';
        }

        let ready = true;

        this.forEachBlock((block) => {
            if (!block.isSelectorReady()) {
                ready = false;
            }
        });

        if (!ready) {
            return 'configNotReady';
        }

        return null;
    }
}

class Block {
    constructor({ el, name, selector, hintEl, fields, blocks }) {
        this.el = el;
        this.name = name ?? null;
        this.selector = selector ?? null;
        this.hintEl = hintEl ?? null;
        this.fields = fields;
        this.blocks = blocks ?? [];
    }

    // --- selector ---

    getSelectedOption() {
        const field = this.selector;
        if (!field?.el) {
            return null;
        }

        const needle = String(field.readValue() ?? '');

        return [...field.el.options].find((opt) => opt.value === needle) ?? null;
    }

    getSelectedNames() {
        const value = this.selector?.readValue();

        if (!value || value === WIFI_MODE.OFF) {
            return [];
        }

        return [value];
    }

    getSelectedHintKey() {
        return this.getSelectedOption()?.getAttribute('data-hint') ?? null;
    }

    isSelectorReady() {
        const hasConditional = this.blocks.some((block) => block.name);

        if (!hasConditional) {
            return true;
        }

        return !!this.selector?.el;
    }

    // --- traverse ---

    forEachField(fn) {
        for (const field of Object.values(this.fields)) {
            fn(field);
        }

        for (const block of this.blocks) {
            block.forEachField(fn);
        }
    }

    forEachSelectedField(fn) {
        for (const field of Object.values(this.fields)) {
            fn(field);
        }

        const selected = new Set(this.getSelectedNames());

        for (const block of this.blocks) {
            if (block.name && !selected.has(block.name)) {
                continue;
            }

            block.forEachSelectedField(fn);
        }
    }
}

class Field {
    constructor({ el, name }) {
        this.el = el;
        this.name = name;
        this.errorMsg = el?.getAttribute('data-error') ?? null;
        this.meta = null;
    }

    // --- value ---

    readValue() {
        const el = this.el;

        if (!el) {
            return undefined;
        }

        switch (el.tagName) {
            case 'INPUT': {
                switch (el.type) {
                    case 'checkbox':
                        return el.checked;
                    case 'number':
                        return el.value === '' ? null : el.valueAsNumber;
                    default:
                        return el.value.trim();
                }
            }
            case 'SELECT':
            case 'TEXTAREA':
                return el.value.trim();
        }

        return (el.textContent ?? '').trim();
    }

    setValue(value) {
        const el = this.el;

        if (!el) {
            return;
        }

        switch (el.tagName) {
            case 'INPUT': {
                switch (el.type) {
                    case 'checkbox':
                        if (el.checked !== value) {
                            el.checked = value;
                        }
                        return;
                    case 'number': {
                        if (value === null || value === undefined || value === '') {
                            if (el.value !== '') {
                                el.value = '';
                            }
                            return;
                        }
                        const n = Number(value);
                        if (!Number.isFinite(n)) {
                            if (el.value !== '') {
                                el.value = '';
                            }
                            return;
                        }
                        if (el.valueAsNumber !== n) {
                            el.valueAsNumber = n;
                        }
                        return;
                    }
                    default: {
                        const strVal = String(value ?? '').trim();
                        if (el.value !== strVal) {
                            el.value = strVal;
                        }
                        return;
                    }
                }
            }
            case 'SELECT':
            case 'TEXTAREA': {
                const strVal = String(value ?? '').trim();
                if (el.value !== strVal) {
                    el.value = strVal;
                }
                return;
            }
        }

        const text = value === undefined || value === null ? '' : String(value).trim();
        if (el.textContent !== text) {
            el.textContent = text;
        }
    }

    resetValue() {
        if (!this.hasMetaValue()) {
            return;
        }

        this.setValue(this.meta.value);
    }

    // --- meta ---

    hasMetaValue() {
        const meta = this.meta;

        return meta != null && 'value' in meta;
    }

    setMeta(meta) {
        this.meta = meta ?? null;
    }

    applyMeta(meta) {
        const el = this.el;

        if (!el || !meta) {
            return;
        }

        if (el.tagName === 'INPUT' && el.type === 'checkbox') {
            return;
        }

        const updateAttribute = (attr, value) => {
            if (value !== undefined) {
                el.setAttribute(attr, value);
            } else {
                el.removeAttribute(attr);
            }
        };

        if (el.tagName === 'INPUT' && el.type === 'number') {
            updateAttribute('min', meta.minValue);
            updateAttribute('max', meta.maxValue);
        } else {
            updateAttribute('minlength', meta.allowEmpty === true ? undefined : meta.minLength);
            updateAttribute('maxlength', meta.maxLength);
        }
    }

    isDirty() {
        if (!this.hasMetaValue()) {
            return true;
        }

        return this.readValue() !== this.meta.value;
    }
}

// ============================================================
//  Transport
// ============================================================

class HttpResult {
    #status;
    #value;
    #errorMsg;

    constructor(status, value, errorMsg) {
        this.#status = Number.isFinite(status) ? status : HTTP_LOCAL_ERROR;
        this.#value = value;
        this.#errorMsg = errorMsg;
    }

    isError() {
        return this.#status < 200 || this.#status >= 300;
    }

    isNetworkError() {
        return this.#status === HTTP_NETWORK_ERROR;
    }

    getValue() {
        return this.#value;
    }

    getErrorMsg() {
        return this.#errorMsg;
    }
}

class HttpClient {
    static #REQUEST_FAILED = 'requestFailed';
    static #REQUEST_NETWORK_ERROR = 'requestNetworkError';
    static #RESPONSE_INVALID_FORMAT = 'responseInvalidFormat';

    static #fetchWithTimeout(url, opts, signal, ms) {
        const timeout = AbortSignal.timeout(ms);
        const fetchSignal = signal ? AbortSignal.any([timeout, signal]) : timeout;

        return fetch(url, { ...opts, signal: fetchSignal });
    }

    static async #parseResponse(response) {
        if (response.status === HTTP_NO_CONTENT) {
            return null;
        }

        try {
            const data = await response.json();

            return data ?? null;
        } catch {
            return null;
        }
    }

    #getTimeoutMs;
    #postTimeoutMs;

    constructor(getTimeoutMs, postTimeoutMs) {
        this.#getTimeoutMs = getTimeoutMs;
        this.#postTimeoutMs = postTimeoutMs;
    }

    async get(domainName, actionName, signal) {
        const endpoint = DOMAIN_SPECS[domainName][actionName];
        const opts = {};

        return this.#request(endpoint.route, opts, signal, this.#getTimeoutMs);
    }

    async post(domainName, actionName, signal, body) {
        const endpoint = DOMAIN_SPECS[domainName][actionName];
        const opts = { method: 'POST', headers: {} };

        if (body !== undefined) {
            opts.headers['Content-Type'] = 'application/json';
            opts.body = JSON.stringify(body);
        }

        const result = await this.#request(endpoint.route, opts, signal, this.#postTimeoutMs);
        if (endpoint.disconnect && result.isNetworkError()) {
            // Expected drop on disconnect actions: treat network error as success.
            return new HttpResult(HTTP_NO_CONTENT, null, null);
        }

        return result;
    }

    async #request(url, opts, signal, ms) {
        try {
            const response = await HttpClient.#fetchWithTimeout(url, opts, signal, ms);
            const body = await HttpClient.#parseResponse(response);

            const isSuccess = response.status >= 200 && response.status < 300;
            if (!isSuccess) {
                const errorMsg = typeof body?.type === 'string' ? body.type : HttpClient.#REQUEST_FAILED;

                return new HttpResult(response.status, null, errorMsg);
            }

            if (body === null && response.status !== HTTP_NO_CONTENT) {
                return new HttpResult(HTTP_LOCAL_ERROR, null, HttpClient.#RESPONSE_INVALID_FORMAT);
            }

            return new HttpResult(response.status, body, null);
        } catch (err) {
            if (err?.name === 'AbortError') {
                throw err;
            }

            return new HttpResult(HTTP_NETWORK_ERROR, null, HttpClient.#REQUEST_NETWORK_ERROR);
        }
    }
}

// ============================================================
//  UI
// ============================================================

class Dialog {
    #language;
    #callback = null;

    constructor(language) {
        this.#language = language;
        this.el = null;
        this.messageEl = null;
        this.confirmEl = null;
        this.cancelEl = null;
    }

    // --- public ---

    init() {
        const el = DOM.queryByDataRole(document, DATA_ROLE.DIALOG);

        if (!el) {
            console.error(`Parse failed: [data-role="${DATA_ROLE.DIALOG}"] missing`);
            return;
        }

        this.el = el;
        this.messageEl = DOM.queryByDataRole(el, DATA_ROLE.DIALOG_MESSAGE);
        this.confirmEl = el.querySelector('[data-action="dialog-confirm"]') ?? null;
        this.cancelEl = el.querySelector('[data-action="dialog-cancel"]') ?? null;

        if (!this.messageEl || !this.confirmEl) {
            console.error('Parse failed: dialog message or confirm control missing');
        }

        this.confirmEl?.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.#close(DIALOG_STATE.ACCEPTED);
        });
        this.cancelEl?.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.#close(DIALOG_STATE.DISMISSED);
        });

        el?.addEventListener('cancel', (e) => {
            e.preventDefault();
            e.stopPropagation();
            const isConfirm = this.cancelEl && !this.cancelEl.hidden;

            this.#close(isConfirm ? DIALOG_STATE.DISMISSED : DIALOG_STATE.ACCEPTED);
        });
    }

    showInform(uiName) {
        return this.#open(DIALOG_TYPE.INFORM, this.#language.getUiText(uiName));
    }

    showConfirm(confirmMsg) {
        return this.#open(DIALOG_TYPE.CONFIRM, this.#language.getUiText(confirmMsg));
    }

    showError(errorMsg) {
        return this.#open(DIALOG_TYPE.ERROR, this.#language.getErrText(errorMsg));
    }

    // --- private ---

    #open(dialogType, messageText) {
        return new Promise((callback) => {
            if (!this.el || !this.messageEl || !this.confirmEl) {
                callback(dialogType === DIALOG_TYPE.CONFIRM ? DIALOG_STATE.DISMISSED : DIALOG_STATE.ACCEPTED);
                return;
            }

            this.messageEl.textContent = messageText;
            this.confirmEl.textContent = this.#language.getUiText('dialogConfirm');
            if (this.cancelEl) {
                this.cancelEl.textContent = this.#language.getUiText('dialogCancel');
                this.cancelEl.hidden = dialogType !== DIALOG_TYPE.CONFIRM;
            }
            this.#callback = callback;

            this.el.showModal();
        });
    }

    #close(state) {
        if (this.#callback) {
            const fn = this.#callback;
            this.#callback = null;

            fn(state);
        }

        if (this.el?.open) {
            this.el.close();
        }
    }
}

class UI {
    static #TAB_NAV_KEYS = Object.freeze({
        ArrowRight: 1,
        ArrowDown: 1,
        ArrowLeft: -1,
        ArrowUp: -1
    });

    // wifiMode wire value -> status* i18n key (#formatWifiMode)
    static #WIFI_MODE_UI_KEY = Object.freeze({
        [WIFI_MODE.OFF]: 'statusWifiModeOff',
        [WIFI_MODE.STA]: 'statusWifiModeSta',
        [WIFI_MODE.AP]: 'statusWifiModeAp'
    });

    // --- static ---

    static #isNestedDomainBlock(blockEl) {
        let el = blockEl.parentElement;

        while (el) {
            if (DOM.hasDataRole(el, DATA_ROLE.BLOCK)) {
                return true;
            }

            el = el.parentElement;
        }

        return false;
    }

    static #collectTopLevelBlocks(root) {
        return DOM.queryAllByDataRole(root, DATA_ROLE.BLOCK).filter(function (blockEl) {
            return !UI.#isNestedDomainBlock(blockEl);
        });
    }

    static #resolveDomainHost(host, name) {
        const panelDomain = host.getAttribute('data-domain');
        if (panelDomain) {
            return panelDomain === name ? host : null;
        }

        return host.querySelector(`[data-domain="${name}"]`);
    }

    static #resolveDomainBlocks(host, name) {
        if (!host) {
            return { el: null, blockEls: [] };
        }

        const domainHost = UI.#resolveDomainHost(host, name);
        if (!domainHost) {
            return { el: null, blockEls: [] };
        }

        return {
            el: domainHost,
            blockEls: UI.#collectTopLevelBlocks(domainHost)
        };
    }

    static #isContainedBy(el, roots) {
        for (const root of roots) {
            if (root !== el && root.contains(el)) {
                return true;
            }
        }

        return false;
    }

    static #collectChildBlockEls(blockEl) {
        const childBlockEls = [];

        for (const child of blockEl.children) {
            if (DOM.hasDataRole(child, DATA_ROLE.BLOCK)) {
                childBlockEls.push(child);
            }
        }

        return childBlockEls;
    }

    static #collectFields(scopeEl, excludedRoots) {
        const fields = Object.create(null);

        scopeEl.querySelectorAll('[data-field]').forEach(function (fieldEl) {
            if (UI.#isContainedBy(fieldEl, excludedRoots)) {
                return;
            }

            const fieldName = fieldEl.getAttribute('data-field');
            if (!fieldName) {
                return;
            }

            fields[fieldName] = new Field({ el: fieldEl, name: fieldName });
        });

        return fields;
    }

    static #collectActions(scopeEl) {
        const actions = Object.create(null);

        scopeEl.querySelectorAll('[data-action]').forEach(function (el) {
            const actionKey = el.getAttribute('data-action');
            if (!actionKey || actionKey.startsWith('dialog-')) {
                return;
            }

            actions[actionKey] = el;
        });

        return actions;
    }

    static #resolveSelector(fields) {
        let selector = null;

        for (const field of Object.values(fields)) {
            if (!DOM.hasDataRole(field.el, DATA_ROLE.GROUP_SELECTOR)) {
                continue;
            }

            if (selector) {
                continue;
            }

            if (!field.el.getAttribute('data-field')) {
                continue;
            }

            selector = field;
        }

        return selector;
    }

    static #parseBlock(blockEl) {
        const blockByEl = new Map();
        const stack = [blockEl];

        while (stack.length) {
            const el = stack[stack.length - 1];
            const childBlockEls = UI.#collectChildBlockEls(el);
            const pendingChild = childBlockEls.find((childEl) => !blockByEl.has(childEl));

            if (pendingChild) {
                stack.push(pendingChild);
                continue;
            }

            stack.pop();

            const fields = UI.#collectFields(el, childBlockEls);

            blockByEl.set(
                el,
                new Block({
                    el: el,
                    name: el.getAttribute('data-group') || null,
                    selector: UI.#resolveSelector(fields),
                    hintEl: DOM.queryByDataRole(el, DATA_ROLE.GROUP_HINT),
                    fields: fields,
                    blocks: childBlockEls.map((childEl) => blockByEl.get(childEl))
                })
            );
        }

        return blockByEl.get(blockEl);
    }

    static #createPanelState(tabName) {
        const entry = {
            el: null,
            errorEl: null,
            state: TAB_STATE.UNLOADED,
            errorMsg: null
        };

        if (TabRoute.hasDomainType(tabName, DOMAIN_TYPE.POLL)) {
            entry.data = null;
        }

        return entry;
    }

    static #syncChildBlocks(root, language) {
        const pending = [root];

        while (pending.length) {
            const block = pending.pop();

            if (block.selector) {
                const selected = new Set(block.getSelectedNames());

                for (const child of block.blocks) {
                    if (child.name && child.el) {
                        child.el.hidden = !selected.has(child.name);
                    }
                }
            }

            if (block.hintEl) {
                const hintKey = block.getSelectedHintKey();
                block.hintEl.textContent = hintKey ? language.getUiText(hintKey) : '';
            }

            for (let i = block.blocks.length - 1; i >= 0; i--) {
                pending.push(block.blocks[i]);
            }
        }
    }

    static #hasValue(v) {
        return v !== undefined && v !== null && v !== '';
    }

    static #formatBytes(n) {
        if (!UI.#hasValue(n) || isNaN(n)) {
            return '';
        }

        const kb = n / 1024;
        if (kb >= 1024) {
            return `${(kb / 1024).toFixed(1)} MB`;
        }

        return `${Math.round(kb)} KB`;
    }

    static #formatUptime(sec) {
        if (!UI.#hasValue(sec) || isNaN(sec)) {
            return '';
        }
        sec = Math.floor(sec);

        const totalMinutes = Math.floor(sec / 60);
        const hour = Math.floor(totalMinutes / 60);
        const minutes = totalMinutes % 60;
        const parts = [];

        if (hour) {
            parts.push(`${hour}h`);
        }
        if (minutes || !hour) {
            parts.push(`${minutes}m`);
        }

        return parts.join(' ');
    }

    static #formatWifiRssi(rssi) {
        return UI.#hasValue(rssi) ? `${rssi} dBm` : '';
    }

    static #formatIpv6Addrs(addrs) {
        if (Array.isArray(addrs)) {
            return addrs.filter((addr) => UI.#hasValue(addr)).join('\n');
        }

        return UI.#hasValue(addrs) ? String(addrs) : '';
    }

    #language = new Language();
    #domains = {};
    #tabs = {
        nav: null,
        activeTabName: '',
        tabEls: {},
        panels: {}
    };
    dialog = new Dialog(this.#language);

    #onTabSelect = null;
    #onActionTrigger = null;

    constructor() {
        for (const domainName of Object.keys(DOMAIN_SPECS)) {
            this.#domains[domainName] = new Domain(domainName);
        }
    }

    // --- public: lifecycle ---

    init() {
        this.#tabs.nav = DOM.queryByDataRole(document, DATA_ROLE.TABLIST);
        const tabpanelsEl = DOM.queryByDataRole(document, DATA_ROLE.TABPANELS);
        const tabs = DOM.queryAllByDataRole(this.#tabs.nav, DATA_ROLE.TAB);
        const panels = DOM.queryAllByDataRole(tabpanelsEl, DATA_ROLE.TABPANEL);
        const tabNames = TabRoute.getNames();
        const expected = tabNames.length;

        if (!this.#tabs.nav) {
            console.error(`Parse failed: [data-role="${DATA_ROLE.TABLIST}"] missing`);
        }

        if (!tabpanelsEl) {
            console.error(`Parse failed: [data-role="${DATA_ROLE.TABPANELS}"] missing`);
        }

        if (tabs.length !== expected) {
            console.error(`Parse failed: tab count ${tabs.length}, expected ${expected}`);
        }

        if (panels.length !== expected) {
            console.error(`Parse failed: tabpanel count ${panels.length}, expected ${expected}`);
        }

        for (let i = 0; i < expected; i++) {
            const tabName = tabNames[i];

            if (!tabs[i]) {
                console.error(`Parse failed: tab missing for "${tabName}" at index ${i}`);
            }

            if (!panels[i]) {
                console.error(`Parse failed: tabpanel missing for "${tabName}" at index ${i}`);
            }

            this.#tabs.panels[tabName] = UI.#createPanelState(tabName);
            this.#tabs.tabEls[tabName] = tabs[i] ?? null;

            if (panels[i]) {
                this.#initTabpanel(tabName, panels[i]);
            }
        }

        for (const domain of Object.values(this.#domains)) {
            this.#initDomain(domain);
        }

        this.dialog.init();
        this.#syncLanguage();
    }

    bind({ onTabSelect, onActionTrigger }) {
        this.#onTabSelect = onTabSelect;
        this.#onActionTrigger = onActionTrigger;
        this.#bindEvents();
    }

    // --- public: domain ---

    getDomain(domainName) {
        return this.#domains[domainName] ?? null;
    }

    // --- public: tabs ---

    setActiveTab(tabName) {
        for (const name of TabRoute.getNames()) {
            const active = name === tabName;
            const tabEl = this.#tabs.tabEls[name];

            tabEl?.classList.toggle('active', active);
            if (tabEl) {
                tabEl.tabIndex = active ? 0 : -1;
            }

            if (this.#tabs.panels[name]?.el) {
                this.#tabs.panels[name].el.hidden = !active;
            }
        }

        this.#tabs.activeTabName = tabName;
    }

    isActiveTab(tabName) {
        return this.#tabs.activeTabName === tabName;
    }

    getTabState(tabName) {
        return this.#tabs.panels[tabName]?.state ?? TAB_STATE.UNLOADED;
    }

    setTabState(tabName, state) {
        const panel = this.#tabs.panels[tabName];

        if (panel) {
            panel.state = state;
        }
    }

    setTabReady(tabName) {
        const panel = this.#tabs.panels[tabName];

        if (!panel) {
            return;
        }

        panel.state = TAB_STATE.READY;
        this.hidePanelError(tabName);
        this.syncTabDomains(tabName);
    }

    setTabFetching(tabName) {
        const panel = this.#tabs.panels[tabName];

        if (panel?.state === TAB_STATE.UNLOADED) {
            panel.state = TAB_STATE.FETCHING;
        }
    }

    setTabData(tabName, data) {
        const panel = this.#tabs.panels[tabName];

        if (panel) {
            panel.data = data;
        }
    }

    getTabData(tabName) {
        return this.#tabs.panels[tabName]?.data ?? null;
    }

    // --- public: panels ---

    showPanelError(tabName, errorMsg) {
        const panel = this.#tabs.panels[tabName];

        if (!panel) {
            return;
        }

        panel.state = TAB_STATE.UNLOADED;
        panel.errorMsg = errorMsg;

        if (panel.errorEl) {
            panel.errorEl.hidden = false;
            panel.errorEl.textContent = this.#language.getErrText(errorMsg);
        }

        this.syncTabDomains(tabName);
    }

    hidePanelError(tabName) {
        const panel = this.#tabs.panels[tabName];

        if (!panel) {
            return;
        }

        panel.errorMsg = null;

        if (panel.errorEl) {
            panel.errorEl.hidden = true;
        }
    }

    hideAllPanelErrors() {
        for (const tabName of TabRoute.getNames()) {
            if (this.#tabs.panels[tabName]?.errorMsg) {
                this.hidePanelError(tabName);
            }
        }
    }

    // --- public: domain sync ---

    syncTabDomains(tabName) {
        const panel = this.#tabs.panels[tabName];

        if (!panel) {
            return;
        }

        const ready = panel.state === TAB_STATE.READY;

        for (const domainName of TabRoute.getConfigDomains(tabName)) {
            const domain = this.#domains[domainName];

            if (domain) {
                this.syncDomain(domain, ready);
            }
        }
    }

    syncAllDomains() {
        for (const tabName of TabRoute.getNames()) {
            if (TabRoute.getConfigDomains(tabName).length > 0) {
                this.syncTabDomains(tabName);
            }
        }
    }

    syncDomain(domain, resetHidden = true) {
        if (!domain?.blocks?.length) {
            return;
        }

        for (const block of domain.blocks) {
            UI.#syncChildBlocks(block, this.#language);
        }

        if (resetHidden) {
            this.#resetHiddenValues(domain);
        }
    }

    // --- public: i18n ---

    switchLanguage() {
        this.#language.switchNext();
        this.#syncLanguage();
    }

    // --- public: status ---

    renderStatus(status) {
        if (!status) {
            return;
        }

        this.getDomain('status').forEachField((field) => {
            field.setValue(this.#formatStatusText(field.name, status));
        });
    }

    // --- private: init ---

    #initTabpanel(tabName, rootEl) {
        const entry = this.#tabs.panels[tabName];

        if (!rootEl) {
            return;
        }

        entry.el = rootEl;
        entry.errorEl = DOM.queryByDataRole(rootEl, DATA_ROLE.TABPANEL_ERROR);
    }

    #initDomain(domain) {
        const tabName = TabRoute.getTabName(domain.name);
        const panelEl = this.#tabs.panels[tabName]?.el;

        domain.el = null;
        domain.blocks = [];
        domain.actions = Object.create(null);

        if (!panelEl) {
            console.error(`Parse failed: tabpanel missing for domain "${domain.name}"`);
            return;
        }

        const { el, blockEls } = UI.#resolveDomainBlocks(panelEl, domain.name);

        if (!el) {
            console.error(`Parse failed: domain host missing for "${domain.name}"`);
            return;
        }

        if (blockEls.length === 0) {
            console.error(`Parse failed: [data-role=block] missing for "${domain.name}"`);
            return;
        }

        domain.el = el;
        domain.blocks = blockEls.map((blockEl) => UI.#parseBlock(blockEl));

        const domainType = DOMAIN_SPECS[domain.name]?.type;
        if (domainType === DOMAIN_TYPE.CONFIG || domainType === DOMAIN_TYPE.ACTION) {
            for (const block of domain.blocks) {
                Object.assign(domain.actions, UI.#collectActions(block.el));
            }
        }
    }

    // --- private: events ---

    #activeTabIndex() {
        const i = TabRoute.getNames().indexOf(this.#tabs.activeTabName);

        return i < 0 ? 0 : i;
    }

    #selectTabAt(index) {
        const tabName = TabRoute.getNames()[Math.max(0, Math.min(index, TabRoute.getNames().length - 1))];

        this.#onTabSelect?.(tabName);
        this.#tabs.tabEls[tabName]?.focus();
    }

    #bindDomainActions(domain) {
        for (const [actionKey, button] of Object.entries(domain.actions)) {
            if (!actionKey) {
                continue;
            }

            button.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.#onActionTrigger?.(actionKey);
            });
        }
    }

    #bindEvents() {
        // language switch
        DOM.queryByDataRole(document, DATA_ROLE.LANG_SWITCH)?.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.switchLanguage();
        });

        // tabbar click navigation
        for (const tabName of TabRoute.getNames()) {
            this.#tabs.tabEls[tabName]?.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.#onTabSelect?.(tabName);
            });
        }

        // tabbar keyboard navigation
        const tabBar = this.#tabs.nav;
        if (tabBar) {
            tabBar.addEventListener('keydown', (e) => {
                const step = UI.#TAB_NAV_KEYS[e.key];
                if (step !== undefined) {
                    e.preventDefault();
                    this.#selectTabAt(this.#activeTabIndex() + step);
                    return;
                }

                if (e.key === 'Home' || e.key === 'End') {
                    e.preventDefault();
                    this.#selectTabAt(e.key === 'Home' ? 0 : TabRoute.getNames().length - 1);
                }
            });
        }

        // domain actions
        for (const domain of Object.values(this.#domains)) {
            this.#bindDomainActions(domain);

            domain.forEachBlock((block) => {
                block.selector?.el?.addEventListener('change', () => {
                    this.syncDomain(domain);
                });

                if (block.el?.tagName === 'FORM') {
                    block.el.addEventListener('submit', (e) => {
                        e.preventDefault();
                    });
                }
            });
        }
    }

    // --- private: domain sync ---

    #resetHiddenValues(domain) {
        const visible = new Set();

        domain.forEachSelectedField((field) => {
            visible.add(field.name);
        });

        domain.forEachField((field) => {
            if (visible.has(field.name) || !field.isDirty()) {
                return;
            }

            field.resetValue();
        });
    }

    // --- private: i18n ---

    #syncLanguage() {
        const lang = this.#language.getCurrent();
        const dict = LANG_SPECS[lang].ui;

        document.documentElement.lang = lang;
        document.title = dict.productName;

        document.querySelectorAll('[data-i18n]').forEach((el) => {
            const key = el.getAttribute('data-i18n');

            if (dict[key] !== undefined) {
                el.textContent = dict[key];
            }
        });

        const statusData = this.getTabData(TabRoute.getTabName('status'));
        if (statusData) {
            this.renderStatus(statusData);
        }

        this.#syncPanelErrors();
        for (const tabName of TabRoute.getNames()) {
            if (this.getTabState(tabName) !== TAB_STATE.READY) {
                continue;
            }

            for (const domainName of TabRoute.getConfigDomains(tabName)) {
                this.syncDomain(this.getDomain(domainName));
            }
        }
    }

    #syncPanelErrors() {
        for (const tabName of TabRoute.getNames()) {
            const panel = this.#tabs.panels[tabName];

            if (TabRoute.isFetchable(tabName) && panel?.errorEl) {
                const errorMsg = panel.errorMsg;

                if (errorMsg && !panel.errorEl.hidden) {
                    panel.errorEl.textContent = this.#language.getErrText(errorMsg);
                }
            }
        }
    }

    // --- private: status ---

    #formatWifiMode(mode) {
        const uiName = UI.#WIFI_MODE_UI_KEY[mode];

        return uiName ? this.#language.getUiText(uiName) : '';
    }

    #formatSshHostKey(sshHostKey) {
        return sshHostKey || this.#language.getUiText('statusSshHostKeyPending');
    }

    #formatSshConnected(connected) {
        return connected
            ? this.#language.getUiText('statusSshConnected')
            : this.#language.getUiText('statusSshDisconnected');
    }

    #formatStatusText(fieldName, status) {
        switch (fieldName) {
            case 'deviceFreeMem':
            case 'deviceTotalMem':
                return UI.#formatBytes(status[fieldName]);
            case 'deviceUptime':
                return UI.#formatUptime(status[fieldName]);
            case 'wifiMode':
                return this.#formatWifiMode(status[fieldName]);
            case 'wifiRssi':
                return UI.#formatWifiRssi(status[fieldName]);
            case 'wifiIpv6Addrs':
                return UI.#formatIpv6Addrs(status[fieldName]);
            case 'sshHostKey':
                return this.#formatSshHostKey(status[fieldName]);
            case 'sshConnected':
                return this.#formatSshConnected(status[fieldName]);
            default: {
                const value = status[fieldName];

                return value === undefined || value === null ? '' : String(value);
            }
        }
    }
}

// ============================================================
//  Orchestration - results
// ============================================================

class RequestResult {
    #state;
    #payload;
    #successMsg;
    #errorMsg;

    constructor(state, { payload = null, successMsg = null, errorMsg = null } = {}) {
        this.#state = state;
        this.#payload = payload;
        this.#successMsg = successMsg;
        this.#errorMsg = errorMsg;
    }

    getState() {
        return this.#state;
    }

    getPayload() {
        return this.#payload;
    }

    getSuccessMsg() {
        return this.#successMsg;
    }

    getErrorMsg() {
        return this.#errorMsg;
    }
}

class ReloadResult {
    #state;
    #errorMsg;

    constructor(state, errorMsg = null) {
        this.#state = state;
        this.#errorMsg = errorMsg;
    }

    getState() {
        return this.#state;
    }

    getErrorMsg() {
        return this.#errorMsg;
    }
}

// ============================================================
//  Runtime
// ============================================================

class Runtime {
    static #ACTION_DISPATCH = {
        save: (runtime, action) => runtime.#saveConfig(action),
        reset: (runtime, action) => runtime.#resetConfig(action),
        system: (runtime, action) => runtime.#runSystemAction(action)
    };

    #ui;
    #http;
    #poller;
    #asyncScopes = {};

    constructor() {
        this.#ui = new UI();
        this.#http = new HttpClient(HTTP_GET_TIMEOUT_MS, HTTP_POST_TIMEOUT_MS);
        this.#poller = new Poller(DOMAIN_SPECS.status.interval);
    }

    // --- boot ---

    boot() {
        this.#ui.init();
        this.#ui.bind({
            onTabSelect: (tabName) => this.activateTab(tabName),
            onActionTrigger: (actionKey) => this.dispatchAction(actionKey)
        });
        this.activateTab(TabRoute.getNames()[0]);
        this.#ui.syncAllDomains();
    }

    // --- public: tabs ---

    activateTab(tabName) {
        if (!(tabName in TAB_SPECS)) {
            tabName = TabRoute.getNames()[0];
        }

        if (this.#ui.isActiveTab(tabName)) {
            if (this.#ui.getTabState(tabName) === TAB_STATE.UNLOADED) {
                this.#ensureTabData(tabName, true);
            }
            return;
        }

        this.#ui.setActiveTab(tabName);
        this.#ensureTabData(tabName, this.#ui.getTabState(tabName) !== TAB_STATE.READY);
    }

    invalidateTabs() {
        for (const tabName of TabRoute.getNames()) {
            if (!TabRoute.isFetchable(tabName)) {
                continue;
            }

            this.#asyncScopes[tabName]?.abort();

            if (TabRoute.hasDomainType(tabName, DOMAIN_TYPE.POLL)) {
                this.#ui.setTabData(tabName, null);
                this.#poller.invalidate();
            }

            this.#ui.setTabState(tabName, TAB_STATE.UNLOADED);
        }

        this.#ui.syncAllDomains();
    }

    async reloadTabFields(tabName) {
        const configDomains = TabRoute.getConfigDomains(tabName);

        if (configDomains.length === 0) {
            return new ReloadResult(RELOAD_STATE.OK);
        }

        const scope = this.#asyncScopes[tabName] ?? (this.#asyncScopes[tabName] = new AsyncScope());
        const run = scope.begin();

        this.#ui.setTabState(tabName, TAB_STATE.FETCHING);

        let result;
        try {
            result = await this.#fetchConfigFields(tabName, run.signal);
        } catch (err) {
            if (err?.name === 'AbortError') {
                if (!scope.isStale(run.gen)) {
                    this.#ui.setTabState(tabName, TAB_STATE.UNLOADED);
                }
                return new ReloadResult(RELOAD_STATE.SUPERSEDED);
            }

            throw err;
        } finally {
            run.end();
        }

        if (scope.isStale(run.gen) || !this.#ui.isActiveTab(tabName)) {
            if (!scope.isStale(run.gen)) {
                this.#ui.setTabState(tabName, TAB_STATE.UNLOADED);
            }
            return new ReloadResult(RELOAD_STATE.SUPERSEDED);
        }

        if (result.isError()) {
            this.#ui.setTabState(tabName, TAB_STATE.UNLOADED);
            return new ReloadResult(RELOAD_STATE.FAIL, result.getErrorMsg());
        }

        const fields = result.getValue();

        if (fields) {
            for (const domainName of configDomains) {
                this.#ui.getDomain(domainName).applyMeta(fields[domainName]);
            }
        }

        this.#ui.setTabReady(tabName);
        return new ReloadResult(RELOAD_STATE.OK);
    }

    async loadTabIfNeeded(tabName) {
        const state = this.#ui.getTabState(tabName);
        if (state === TAB_STATE.READY || state === TAB_STATE.FETCHING) {
            return;
        }

        const result = await this.reloadTabFields(tabName);
        if (result.getState() === RELOAD_STATE.FAIL) {
            this.#ui.showPanelError(tabName, result.getErrorMsg());
        }
    }

    // --- public: dispatch ---

    dispatchAction(actionKey) {
        const action = ACTION_SPECS[actionKey] ?? null;

        if (!action) {
            console.warn('Unknown action key:', actionKey);
            return;
        }

        const handlerKey = DOMAIN_SPECS[action.domainName]?.type === DOMAIN_TYPE.ACTION ? 'system' : action.actionName;
        const handler = Runtime.#ACTION_DISPATCH[handlerKey];

        if (!handler) {
            console.warn('Unknown action handler:', actionKey);
            return;
        }

        void handler(this, action).catch((err) => console.error('Action failed:', err));
    }

    // --- private: tab data ---

    #ensureTabData(tabName, refreshNow) {
        if (TabRoute.hasDomainType(tabName, DOMAIN_TYPE.POLL)) {
            this.#poller.start((gen) => this.#fetchStatus(gen), refreshNow);
            return;
        }

        this.#poller.stop();

        if (TabRoute.hasDomainType(tabName, DOMAIN_TYPE.CONFIG)) {
            void this.loadTabIfNeeded(tabName).catch((err) => console.error('Tab field load failed:', err));
        }
    }

    // --- fetch ---

    async #fetchStatus(gen) {
        const tabName = TabRoute.getTabName('status');

        if (this.#poller.isStale(gen)) {
            return;
        }

        this.#ui.setTabFetching(tabName);

        const result = await this.#http.get('status', 'fetch');

        if (this.#poller.isStale(gen)) {
            return;
        }

        if (result.isError()) {
            this.#ui.showPanelError(tabName, result.getErrorMsg());
            return;
        }

        this.#ui.setTabData(tabName, result.getValue());
        this.#ui.setTabReady(tabName);
        this.#ui.renderStatus(result.getValue());
    }

    async #fetchConfigFields(tabName, signal) {
        const domains = TabRoute.getConfigDomains(tabName);

        const results = await Promise.all(domains.map((domainName) => this.#http.get(domainName, 'fields', signal)));

        const failedIdx = results.findIndex((result) => result.isError());
        if (failedIdx >= 0) {
            return results[failedIdx];
        }

        const fields = {};
        domains.forEach((domainName, index) => {
            fields[domainName] = results[index].getValue();
        });

        return new HttpResult(200, fields, null);
    }

    async #postPaused(domainName, actionName, body) {
        this.#poller.setPaused(true);
        try {
            return await this.#http.post(domainName, actionName, undefined, body);
        } finally {
            this.#poller.setPaused(false);
        }
    }

    // --- validation ---

    #isValidFieldLength(value, meta) {
        const v = String(value ?? '').trim();

        if (v.length === 0) {
            if (meta?.allowEmpty === true) {
                return true;
            }
            if (meta?.minLength !== undefined) {
                return false;
            }

            return true;
        }

        if (meta?.minLength !== undefined && v.length < meta.minLength) {
            return false;
        }
        if (meta?.maxLength !== undefined && v.length > meta.maxLength) {
            return false;
        }

        return true;
    }

    #isValidNumberRange(value, meta) {
        if (meta?.minValue !== undefined && value < meta.minValue) {
            return false;
        }
        if (meta?.maxValue !== undefined && value > meta.maxValue) {
            return false;
        }

        return true;
    }

    #isValidFieldPattern(value, meta) {
        const pattern = meta?.pattern;
        if (!pattern) {
            return true;
        }

        const v = String(value ?? '').trim();
        if (v.length === 0) {
            return true;
        }

        try {
            return new RegExp(pattern).test(v);
        } catch {
            console.error(`Invalid field pattern: ${pattern}`);
            return false;
        }
    }

    #isValidFieldValue(field, value) {
        if (!field?.meta) {
            return true;
        }

        if (value === null) {
            return field.meta.allowEmpty === true;
        }

        if (field.el?.type === 'checkbox') {
            return typeof value === 'boolean';
        }

        if (typeof value === 'number') {
            return Number.isFinite(value) && this.#isValidNumberRange(value, field.meta);
        }

        if (!this.#isValidFieldLength(value, field.meta)) {
            return false;
        }

        return this.#isValidFieldPattern(value, field.meta);
    }

    #getFieldErrorMsg(domain, field) {
        if (!field?.errorMsg) {
            console.error(`Missing data-error: ${domain.name}.${field?.name ?? '?'}`);
        }

        return field?.errorMsg;
    }

    #validateFields(domain, body) {
        for (const fieldName of Object.keys(body)) {
            const field = domain.getField(fieldName);
            if (!field || this.#isValidFieldValue(field, body[fieldName])) {
                continue;
            }

            return this.#getFieldErrorMsg(domain, field);
        }

        return null;
    }

    #validateConfigRequest(domain, body) {
        switch (domain.name) {
            case 'device':
            case 'wifi':
            case 'ssh':
                return this.#validateFields(domain, body);
            default:
                return null;
        }
    }

    // --- config request ---

    #buildConfigRequest(domain) {
        const domainName = domain.name;
        const saveSpec = DOMAIN_SPECS[domainName]?.save;
        if (!saveSpec) {
            console.error(`Save spec missing: ${domainName}`);
            return new RequestResult(REQUEST_STATE.FAIL);
        }

        const saveErrorMsg = domain.getSaveErrorMsg();
        if (saveErrorMsg) {
            return new RequestResult(REQUEST_STATE.FAIL, { errorMsg: saveErrorMsg });
        }

        const dirtyValues = domain.getDirtyValues();
        if (Object.keys(dirtyValues).length === 0) {
            return new RequestResult(REQUEST_STATE.FAIL, { errorMsg: 'configNothingToSave' });
        }

        const fieldErrorMsg = this.#validateConfigRequest(domain, dirtyValues);
        if (fieldErrorMsg) {
            return new RequestResult(REQUEST_STATE.FAIL, { errorMsg: fieldErrorMsg });
        }

        return new RequestResult(REQUEST_STATE.OK, {
            payload: dirtyValues,
            successMsg: saveSpec.successMsg
        });
    }

    async #finishConfigAction(tabName) {
        const result = await this.reloadTabFields(tabName);

        if (result.getState() === RELOAD_STATE.SUPERSEDED) {
            this.#ui.setTabState(tabName, TAB_STATE.UNLOADED);
            this.#ui.syncTabDomains(tabName);
            if (this.#ui.isActiveTab(tabName)) {
                await this.loadTabIfNeeded(tabName);
            }
            return;
        }

        if (result.getState() === RELOAD_STATE.FAIL) {
            this.#ui.showPanelError(tabName, result.getErrorMsg());
        }
    }

    // --- action handlers ---

    async #saveConfig(action) {
        const endpoint = DOMAIN_SPECS[action.domainName][action.actionName];
        const tabName = TabRoute.getTabName(action.domainName);
        const domain = this.#ui.getDomain(action.domainName);

        if (!domain) {
            return;
        }

        const result = this.#buildConfigRequest(domain);
        if (result.getState() === REQUEST_STATE.FAIL) {
            const errorMsg = result.getErrorMsg();
            if (errorMsg) {
                await this.#ui.dialog.showError(errorMsg);
            }
            return;
        }

        const postResult = await this.#postPaused(action.domainName, action.actionName, result.getPayload());
        if (postResult.isError()) {
            await this.#ui.dialog.showError(postResult.getErrorMsg());
            return;
        }

        const successMsg = result.getSuccessMsg() ?? endpoint.successMsg;
        await this.#ui.dialog.showInform(successMsg);
        await this.#finishConfigAction(tabName);
    }

    async #resetConfig(action) {
        const endpoint = DOMAIN_SPECS[action.domainName][action.actionName];
        const tabName = TabRoute.getTabName(action.domainName);

        if ((await this.#ui.dialog.showConfirm(endpoint.confirmMsg)) !== DIALOG_STATE.ACCEPTED) {
            return;
        }

        const result = await this.#postPaused(action.domainName, action.actionName);
        if (result.isError()) {
            await this.#ui.dialog.showError(result.getErrorMsg());
            return;
        }

        await this.#ui.dialog.showInform(endpoint.successMsg);
        await this.#finishConfigAction(tabName);
    }

    async #runSystemAction(action) {
        const endpoint = DOMAIN_SPECS[action.domainName][action.actionName];

        if (!endpoint?.route || !endpoint.confirmMsg) {
            return;
        }

        if ((await this.#ui.dialog.showConfirm(endpoint.confirmMsg)) !== DIALOG_STATE.ACCEPTED) {
            return;
        }

        const result = await this.#postPaused(action.domainName, action.actionName);
        if (result.isError()) {
            await this.#ui.dialog.showError(result.getErrorMsg());
            return;
        }

        this.invalidateTabs();
        this.#ui.hideAllPanelErrors();
        await this.#ui.dialog.showInform(endpoint.successMsg);
    }
}

// ============================================================
//  Boot
// ============================================================

const runtime = new Runtime();

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () {
        runtime.boot();
    });
} else {
    runtime.boot();
}
