#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEAdvertising.h>
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_mac.h"
#include "esp_sleep.h"

Preferences preferences;

// ピン設定
static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_4; // タクトスイッチ
static constexpr int LED_PIN = 8;                     // LED

// RSSIフィルタ
static constexpr int RSSI_THRESHOLD = -90;
static constexpr uint32_t BURST_MS = 3000;

// Nintendo広告フォーマット判定
static bool isExpectedNintendoFormat(const uint8_t* data, size_t len) {
    if (len < 23) return false;
    if (data[0] != 0x02 || data[1] != 0x01 || data[2] != 0x06) return false;
    if (data[3] != 0x1B || data[4] != 0xFF || data[5] != 0x53 || data[6] != 0x05) return false;
    if (data[7] != 0x01 || data[8] != 0x00 || data[9] != 0x03 || data[10] != 0x7E || data[11] != 0x05) return false;
    return true;
}

static bool hasNintendoMarker(const uint8_t* data, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if ((data[i] == 0x53 && data[i + 1] == 0x05) || (data[i] == 0x7E && data[i + 1] == 0x05)) {
            return true;
        }
    }
    return false;
}

// MAC文字列をバイト配列に変換
static bool parseMac(const char* macStr, uint8_t* outMac) {
    int parsed[6];
    if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
               &parsed[0], &parsed[1], &parsed[2],
               &parsed[3], &parsed[4], &parsed[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        outMac[i] = (uint8_t)parsed[i];
    }
    return true;
}

// Switch用にリトルエンディアン（逆順）に変換
static bool parseMacReverse(const char* macStr, uint8_t* outReversed) {
    uint8_t temp[6];
    if (!parseMac(macStr, temp)) return false;
    for (int i = 0; i < 6; i++) {
        outReversed[i] = temp[5 - i];
    }
    return true;
}

// スキャンコールバッククラス
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        const std::vector<uint8_t>& payload = device->getPayload();
        const uint8_t* data = payload.data();
        const size_t len = payload.size();
        const int rssi = device->getRSSI();

        if ((!hasNintendoMarker(data, len) && !isExpectedNintendoFormat(data, len)) || rssi < RSSI_THRESHOLD) {
            return;
        }

        String address = device->getAddress().toString().c_str();
        address.toUpperCase();

        // Webダッシュボードやシリアルモニターで認識しやすいフォーマットで出力
        Serial.println("--- DEVICE FOUND ---");
        Serial.printf("MAC:%s\n", address.c_str());
        if (isExpectedNintendoFormat(data, len)) {
            uint8_t switchMac[6];
            for (int i = 0; i < 6; i++) {
                switchMac[i] = data[22 - i];
            }
            char switchMacText[18];
            snprintf(switchMacText, sizeof(switchMacText), "%02X:%02X:%02X:%02X:%02X:%02X",
                     switchMac[0], switchMac[1], switchMac[2], switchMac[3], switchMac[4], switchMac[5]);
            
            uint16_t pid = static_cast<uint16_t>(data[12]) | (static_cast<uint16_t>(data[13]) << 8);
            Serial.printf("SWITCH_MAC:%s\n", switchMacText);
            Serial.printf("PID:0x%04X\n", pid);
        }
        Serial.println("--------------------");
    }
};

static ScanCallbacks scanCallbacks;

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    preferences.begin("settings", false);
    
    // 起動直後のシリアルからの設定書き込み監視（0.5秒間待機）
    unsigned long startWait = millis();
    while (millis() - startWait < 500) {
        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            if (line.startsWith("CFG:")) {
                // 形式: CFG:JoyConMAC,SwitchMAC,PID
                preferences.putString("jc_mac", line.substring(4, 21));
                preferences.putString("sw_mac", line.substring(22, 39));
                preferences.putString("pid", line.substring(40));
                preferences.putInt("boot_flag", 1); // 次回から送信機モードへ移行
                Serial.println(">>> CONFIG SAVED SUCCESSFULLY! Rebooting...");
                ESP.restart();
            }
        }
    }

    int bootFlag = preferences.getInt("boot_flag", 0);

    if (bootFlag == 0) {
        // === 初回起動時: 強制的にスキャンモード ===
        Serial.println(">>> [初回起動] BLEスキャンモードを開始します...");
        
        // スキャンに入った段階でフラグを1に更新（次回以降は通常送信機モードになる）
        preferences.putInt("boot_flag", 1);

        NimBLEDevice::init("");
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(&scanCallbacks, false);
        scan->setActiveScan(true);
        scan->setInterval(96);
        scan->setWindow(96);
        scan->start(0, false, true);

        // スキャン中はシリアル経由の設定コマンド受信を常時待ち受け
        while (true) {
            if (Serial.available()) {
                String line = Serial.readStringUntil('\n');
                line.trim();
                if (line.startsWith("CFG:")) {
                    preferences.putString("jc_mac", line.substring(4, 21));
                    preferences.putString("sw_mac", line.substring(22, 39));
                    preferences.putString("pid", line.substring(40));
                    Serial.println(">>> CONFIG SAVED! Rebooting...");
                    ESP.restart();
                }
            }
            delay(10);
        }
    } else {
        // === 2回目以降: 通常送信機モード ===
        esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
        if (wakeup_cause != ESP_SLEEP_WAKEUP_GPIO) {
            // コールドブート時は誤動作防止のため即座にディープスリープへ
            esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
            return;
        }

        // 保存された設定の読み込み
        String jcMacStr = preferences.getString("jc_mac", "AA:BB:CC:DD:EE:FF");
        String swMacStr = preferences.getString("sw_mac", "AA:BB:CC:DD:EE:FF");
        String pidStr = preferences.getString("pid", "0x2066");

        uint16_t sourcePid = (uint16_t)strtol(pidStr.c_str(), NULL, 16);

        // MACアドレス自動補正（ESP32のハードウェア仕様による +2 のズレを相殺）
        uint8_t sourceMacBytes[6];
        if (parseMac(jcMacStr.c_str(), sourceMacBytes)) {
            sourceMacBytes[5] -= 2;
            esp_base_mac_addr_set(sourceMacBytes);
        }

        uint8_t targetReversed[6];
        parseMacReverse(swMacStr.c_str(), targetReversed);

        // アドバタイジングペイロードの構築
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

        for (int i = 0; i < 6; i++) {
            raw_payload[17 + i] = targetReversed[i];
        }

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

        // 3秒間のバースト送信 ＆ LEDチカチカ点滅
        unsigned long startTime = millis();
        while (millis() - startTime < BURST_MS) {
            digitalWrite(LED_PIN, HIGH);
            delay(50);
            digitalWrite(LED_PIN, LOW);
            delay(50);
        }

        adv->stop();
        digitalWrite(LED_PIN, LOW);
        
        Serial.println(">>> Wake burst complete. Going back to DEEP SLEEP.");
        Serial.flush();

        esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
        esp_deep_sleep_start();
    }
}

void loop() {}
