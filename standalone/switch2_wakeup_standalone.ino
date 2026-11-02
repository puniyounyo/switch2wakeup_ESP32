// ============================================================================
//  Switch 2 Wakeup  -  Stand-alone 版 (ESP32-C3 / NimBLE-Arduino 2.x)
//
//  通常動作 : ディープスリープ待機 → HOME(GPIO4)で復帰 → 起動信号を2.5秒送信 → 再スリープ
//  設定動作 : GPIO5(Rボタン等)をLOWにした状態で「HOME押下」または「電源投入/リセット」
//             → スキャナーモード(30秒) → Joy-Con 2 のボタンを押す/本体から外す
//             → MAC/PIDをNVSへ保存 → ディープスリープ
//  初期値   : Joy-Con MAC = AA:BB:CC:DD:EE:FF / Switch 2 MAC = 11:22:33:44:55:66 (ダミー)
//
//  LEDパターン (GPIO8)
//    ・短く1回/秒              : 起動直後のサービス待機中（USB/シリアルが生きている）
//    ・高速点滅 (50ms周期)      : 起動信号を送信中
//    ・「ピピッ」を繰り返す      : スキャナーモード中
//    ・2秒間点灯               : スキャン成功（保存完了）
//    ・素早く5回点滅            : スキャン失敗（30秒タイムアウト、設定は変更なし）
//
//  Arduino IDE: ボード「ESP32C3 Dev Module」, USB CDC On Boot = Enabled
// ============================================================================
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertising.h>
#include <Preferences.h>
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "esp_mac.h"
#include "esp_sleep.h"

// ---------------------------- 設定 -----------------------------------------
static constexpr gpio_num_t PIN_HOME = GPIO_NUM_4;   // ホームボタン (GND短絡で押下)
static constexpr gpio_num_t PIN_SCAN = GPIO_NUM_5;   // スキャナーモード用ボタン (GND短絡で押下)
static constexpr int        PIN_LED  = 8;            // ESP32-C3 Super Mini の青LED
static constexpr bool       LED_ACTIVE_LOW = true;   // 点灯/消灯が逆なら false に

static constexpr uint32_t BURST_MS          = 2500;  // 起動信号の送信時間
static constexpr uint32_t SCAN_MODE_MS      = 30000; // スキャナーモードの動作時間
static constexpr uint32_t SCAN_SETTLE_MS    = 1500;  // 最初の検出後、より強い電波を待つ時間
static constexpr int      SCAN_MIN_RSSI     = -75;   // これより弱い機器は無視(他人のJoy-Con誤取得防止)
static constexpr uint32_t SERVICE_WINDOW_MS = 15000; // 電源投入/リセット後にUSBを生かしておく時間
static constexpr bool     WAIT_RELEASE_BEFORE_SLEEP = true; // ボタンを離すまでスリープしない
static constexpr uint32_t RELEASE_TIMEOUT_MS = 10000;

static const char* DEFAULT_JC_MAC  = "AA:BB:CC:DD:EE:FF";
static const char* DEFAULT_SW_MAC  = "11:22:33:44:55:66";
static const char* DEFAULT_PID     = "0x2066";

Preferences preferences;

// ---------------------------- 共通ヘルパー ---------------------------------
static void led(bool on) {
    digitalWrite(PIN_LED, (on != LED_ACTIVE_LOW) ? HIGH : LOW);
}

static bool parseMac(const char* s, uint8_t* out) {
    if (strlen(s) != 17) return false;
    unsigned int p[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x",
               &p[0], &p[1], &p[2], &p[3], &p[4], &p[5]) != 6) return false;
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)p[i];
    return true;
}

static void formatMac(const uint8_t* m, char* out, size_t n) {
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

// 48bit値から 2 を引く（桁借りあり）。BLE MAC = ベースMAC + 2 なので逆算に使う
static void macMinus2(uint8_t* mac) {
    int borrow = 2;
    for (int i = 5; i >= 0 && borrow; i--) {
        int v = (int)mac[i] - borrow;
        if (v >= 0) { mac[i] = (uint8_t)v; borrow = 0; }
        else        { mac[i] = (uint8_t)(v + 256); borrow = 1; }
    }
}

struct Config {
    String jc;
    String sw;
    String pid;
};

static Config loadConfig() {
    Config c;
    preferences.begin("settings", false);
    c.jc  = preferences.getString("jc_mac", DEFAULT_JC_MAC);
    c.sw  = preferences.getString("sw_mac", DEFAULT_SW_MAC);
    c.pid = preferences.getString("pid",    DEFAULT_PID);
    preferences.end();
    return c;
}

static void saveConfig(const String& jc, const String& sw, const String& pid) {
    preferences.begin("settings", false);
    preferences.putString("jc_mac", jc);
    preferences.putString("sw_mac", sw);
    preferences.putString("pid",    pid);
    preferences.end();
}

static void clearConfig() {
    preferences.begin("settings", false);
    preferences.clear();
    preferences.end();
}

// ボタン読み取り（LOW=押下）。20ms間隔で3回連続LOWなら押下とみなす
static bool isPressed(gpio_num_t pin) {
    for (int i = 0; i < 3; i++) {
        if (digitalRead(pin) != LOW) return false;
        delay(10);
    }
    return true;
}

static void waitRelease(gpio_num_t pin) {
    uint32_t t0 = millis();
    int highCount = 0;
    while (millis() - t0 < RELEASE_TIMEOUT_MS) {
        if (digitalRead(pin) == HIGH) {
            if (++highCount >= 5) break;   // 50ms連続HIGHで解放と判定（チャタリング対策）
        } else {
            highCount = 0;
        }
        delay(10);
    }
}

[[noreturn]] static void goDeepSleep() {
    if (WAIT_RELEASE_BEFORE_SLEEP) waitRelease(PIN_HOME);
    led(false);
    delay(20);

    // スリープ中もHOME端子のプルアップを維持する（LOWレベル復帰のため）
    gpio_set_direction(PIN_HOME, GPIO_MODE_INPUT);
    gpio_pullup_en(PIN_HOME);
    gpio_pulldown_dis(PIN_HOME);
#if SOC_GPIO_SUPPORT_HOLD_IO_IN_DSLP
    gpio_hold_en(PIN_HOME);
    gpio_deep_sleep_hold_en();
#endif

    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_HOME, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
    while (true) {}   // ここには来ない
}

// ---------------------------- サービス待機（シリアル設定） -----------------
// 電源投入/リセット直後だけ一定時間起きている。USBが生きているので
// ・ブラウザからの再書込み
// ・シリアルモニタからの CFG:/SHOW/CLR コマンド
// が使える。
static bool handleSerialLine(String line) {
    line.trim();
    if (line.length() == 0) return false;

    if (line.equalsIgnoreCase("SHOW")) {
        Config c = loadConfig();
        Serial.printf("JC=%s SW=%s PID=%s\n", c.jc.c_str(), c.sw.c_str(), c.pid.c_str());
        return false;
    }
    if (line.equalsIgnoreCase("CLR")) {
        clearConfig();
        Serial.println(">>> [OK] config cleared (back to dummy MAC)");
        return true;
    }
    if (line.startsWith("CFG:")) {
        // 形式: CFG:JoyConMAC,SwitchMAC,PID   例: CFG:AA:BB:CC:DD:EE:FF,11:22:33:44:55:66,0x2066
        int c1 = line.indexOf(',', 4);
        int c2 = (c1 >= 0) ? line.indexOf(',', c1 + 1) : -1;
        if (c1 < 0 || c2 < 0) { Serial.println(">>> [ERROR] format"); return false; }
        String jc  = line.substring(4, c1);      jc.trim();  jc.toUpperCase();
        String sw  = line.substring(c1 + 1, c2); sw.trim();  sw.toUpperCase();
        String pid = line.substring(c2 + 1);     pid.trim();
        uint8_t tmp[6];
        if (!parseMac(jc.c_str(), tmp) || !parseMac(sw.c_str(), tmp)) {
            Serial.println(">>> [ERROR] bad MAC");
            return false;
        }
        char* endp = nullptr;
        long pv = strtol(pid.c_str(), &endp, 16);
        if (endp == pid.c_str() || pv <= 0 || pv > 0xFFFF) {
            Serial.println(">>> [ERROR] bad PID");
            return false;
        }
        char pidNorm[8];
        snprintf(pidNorm, sizeof(pidNorm), "0x%04lX", pv);
        saveConfig(jc, sw, String(pidNorm));
        Serial.println(">>> [SUCCESS] CONFIG SAVED TO NVS!");
        return true;
    }
    Serial.printf("Received: [%s]\n", line.c_str());
    return false;
}

static void runServiceWindow() {
    Serial.println();
    Serial.println(">>> [SERVICE WINDOW] standby. CFG:JC,SW,PID / SHOW / CLR available.");
    Config c = loadConfig();
    Serial.printf("    JC=%s SW=%s PID=%s\n", c.jc.c_str(), c.sw.c_str(), c.pid.c_str());
    Serial.setTimeout(100);

    uint32_t t0 = millis();
    while (millis() - t0 < SERVICE_WINDOW_MS) {
        led((millis() % 1000) < 60);
        if (Serial.available()) {
            if (handleSerialLine(Serial.readStringUntil('\n'))) {
                delay(200);
                return;   // 設定変更したのでスリープへ
            }
        }
        // ホームが押されたら窓を待たずに通常動作へ移れるよう呼び出し元に任せる
        delay(10);
    }
}

// ---------------------------- 通常動作：起動信号の送信 ---------------------
static void runTransmitter() {
    Config c = loadConfig();

    uint16_t sourcePid = (uint16_t)strtol(c.pid.c_str(), NULL, 16);

    // 送信元MAC（BLEアドレス = ベースMAC + 2 なので -2 して設定）
    uint8_t srcMac[6];
    if (parseMac(c.jc.c_str(), srcMac)) {
        macMinus2(srcMac);
        esp_err_t e = esp_base_mac_addr_set(srcMac);
        if (e != ESP_OK) Serial.printf("esp_base_mac_addr_set failed: %d\n", (int)e);
    } else {
        Serial.println("JC MAC parse error (using chip default)");
    }

    // 宛先（Switch 2）MACは逆順でペイロードへ
    uint8_t swMac[6] = {0};
    if (!parseMac(c.sw.c_str(), swMac)) Serial.println("SW MAC parse error");

    uint8_t raw_payload[31] = {
        0x02, 0x01, 0x06,
        0x1B, 0xFF,
        0x53, 0x05,
        0x01, 0x00, 0x03,
        0x7E, 0x05,
        0x00, 0x00,
        0x00, 0x01, 0x81,
        0, 0, 0, 0, 0, 0,
        0x0F, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00
    };
    raw_payload[12] = sourcePid & 0xFF;
    raw_payload[13] = (sourcePid >> 8) & 0xFF;
    for (int i = 0; i < 6; i++) raw_payload[17 + i] = swMac[5 - i];

    NimBLEDevice::init("");
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

    NimBLEAdvertisementData advData;
    advData.addData(raw_payload, sizeof(raw_payload));
    adv->setAdvertisementData(advData);

    adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    adv->setMinInterval(0x20);
    adv->setMaxInterval(0x40);
    adv->start();

    uint32_t t0 = millis();
    while (millis() - t0 < BURST_MS) {
        led(true);  delay(50);
        led(false); delay(50);
    }
    adv->stop();
    led(false);
    Serial.println(">>> Wake burst complete. Going back to DEEP SLEEP.");
}

// ---------------------------- スキャナーモード -----------------------------
struct Found {
    bool     valid;
    uint8_t  jc[6];
    uint8_t  sw[6];
    uint16_t pid;
    int      rssi;
};

static Found        g_best = {};
static uint32_t     g_firstHitMs = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// 本家ペイロード形式（Flags + Nintendo製造元データ）かを確認
static bool isNintendoWakeFormat(const uint8_t* d, size_t len) {
    if (len < 23) return false;
    if (d[0] != 0x02 || d[1] != 0x01 || d[2] != 0x06) return false;
    if (d[3] != 0x1B || d[4] != 0xFF || d[5] != 0x53 || d[6] != 0x05) return false;
    if (d[7] != 0x01 || d[8] != 0x00 || d[9] != 0x03 || d[10] != 0x7E || d[11] != 0x05) return false;
    return true;
}

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        const std::vector<uint8_t>& payload = device->getPayload();
        const uint8_t* d = payload.data();
        const size_t len = payload.size();

        if (!isNintendoWakeFormat(d, len)) return;
        const int rssi = device->getRSSI();
        if (rssi < SCAN_MIN_RSSI) return;

        std::string addr = device->getAddress().toString();
        uint8_t jc[6];
        if (!parseMac(addr.c_str(), jc)) return;

        Found f;
        f.valid = true;
        memcpy(f.jc, jc, 6);
        for (int i = 0; i < 6; i++) f.sw[i] = d[22 - i];   // 17$301C22バイトは逆順
        f.pid  = (uint16_t)d[12] | ((uint16_t)d[13] << 8);
        f.rssi = rssi;

        portENTER_CRITICAL(&g_mux);
        if (!g_best.valid) g_firstHitMs = millis();
        if (!g_best.valid || rssi > g_best.rssi) g_best = f;   // 一番近い(強い)ものを採用
        portEXIT_CRITICAL(&g_mux);
    }
};
static ScanCallbacks g_scanCallbacks;

static void runScannerMode() {
    Serial.println();
    Serial.println(">>> [SCANNER MODE] 30s. Press a button on the Joy-Con 2 (or detach it).");

    g_best = {};
    g_firstHitMs = 0;

    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCallbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(96);
    scan->setWindow(96);
    scan->setMaxResults(0);
    if (!scan->start(0, false, true)) {
        Serial.println("ERROR: Failed to start BLE scan");
    }

    bool committed = false;
    Found result = {};
    uint32_t t0 = millis();
    while (millis() - t0 < SCAN_MODE_MS) {
        uint32_t ph = millis() % 1000;
        led(ph < 100 || (ph >= 200 && ph < 300));    // 「ピピッ」

        portENTER_CRITICAL(&g_mux);
        bool have = g_best.valid;
        uint32_t first = g_firstHitMs;
        Found snap = g_best;
        portEXIT_CRITICAL(&g_mux);

        if (have && (millis() - first) >= SCAN_SETTLE_MS) {
            result = snap;
            committed = true;
            break;
        }
        delay(10);
    }
    scan->stop();

    if (committed) {
        char jcs[18], sws[18], pids[8];
        formatMac(result.jc, jcs, sizeof(jcs));
        formatMac(result.sw, sws, sizeof(sws));
        snprintf(pids, sizeof(pids), "0x%04X", result.pid);
        saveConfig(String(jcs), String(sws), String(pids));
        Serial.printf(">>> [SCAN OK] JC=%s SW=%s PID=%s RSSI=%d -> saved\n",
                      jcs, sws, pids, result.rssi);
        led(true);
        delay(2000);
        led(false);
    } else {
        Serial.println(">>> [SCAN TIMEOUT] no Joy-Con 2 wake packet found. Config unchanged.");
        for (int i = 0; i < 5; i++) { led(true); delay(60); led(false); delay(60); }
    }
}

// ---------------------------- エントリポイント -----------------------------
void setup() {
    Serial.begin(115200);

    // 前回スリープ中に保持していたGPIO設定を解除
#if SOC_GPIO_SUPPORT_HOLD_IO_IN_DSLP
    gpio_deep_sleep_hold_dis();
#endif
    gpio_hold_dis(PIN_HOME);

    pinMode(PIN_LED, OUTPUT);
    led(false);
    pinMode(PIN_HOME, INPUT_PULLUP);
    pinMode(PIN_SCAN, INPUT_PULLUP);
    delay(30);   // プルアップが安定するまで待つ

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const bool scanHeld = isPressed(PIN_SCAN);

    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        // ホーム押下で復帰
        if (scanHeld) runScannerMode();   // ホーム + R 同時押し
        else          runTransmitter();   // 通常
    } else {
        // 電源投入 / リセット / 書込み直後
        if (scanHeld) {
            runScannerMode();             // Rを押したまま電源投入/リセット
        } else {
            runServiceWindow();           // USB/シリアルが使える猶予
        }
    }
    goDeepSleep();
}

void loop() {}
