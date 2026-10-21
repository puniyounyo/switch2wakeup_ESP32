#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertising.h>
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_mac.h"
#include "esp_sleep.h"

Preferences preferences;

static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_4; // タクトスイッチ
static constexpr int LED_PIN = 8;                     // LED
static constexpr uint32_t BURST_MS = 3000;

static bool parseMac(const char* macStr, uint8_t* outMac) {
    int parsed[6];
    if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
               &parsed[0], &parsed[1], &parsed[2],
               &parsed[3], &parsed[4], &parsed[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) outMac[i] = (uint8_t)parsed[i];
    return true;
}

static bool parseMacReverse(const char* macStr, uint8_t* outReversed) {
    uint8_t temp[6];
    if (!parseMac(macStr, temp)) return false;
    for (int i = 0; i < 6; i++) outReversed[i] = temp[5 - i];
    return true;
}

// シリアルから設定を受け取って保存する関数（改行・空白の揺れを完璧に吸収する堅牢版）
void checkAndSaveConfig() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        line.replace("\r", ""); // 改行コードの差異を排除

        if (line.length() > 0) {
            Serial.printf("Received: [%s]\n", line.c_str());
        }

        if (line.startsWith("CFG:")) {
            // 形式: CFG:JoyConMAC,SwitchMAC,PID
            // 例: CFG:AA:BB:CC:DD:EE:FF,11:22:33:44:55:66,0x2066
            String jc = line.substring(4, 21);
            String sw = line.substring(22, 39);
            String pid = line.substring(40);

            // 前後の空白やゴミをトリム
            jc.trim();
            sw.trim();
            pid.trim();

            preferences.begin("settings", false);
            preferences.putString("jc_mac", jc);
            preferences.putString("sw_mac", sw);
            preferences.putString("pid", pid);
            preferences.putInt("boot_flag", 1); // 設定完了フラグ
            preferences.end();

            Serial.println(">>> [SUCCESS] CONFIG SAVED TO NVS! Rebooting...");
            Serial.flush();
            delay(500);
            ESP.restart();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    preferences.begin("settings", false);
    int bootFlag = preferences.getInt("boot_flag", 0);
    preferences.end();

    // 未設定（または初回書き込み直後）の場合は設定待機モード
    if (bootFlag == 0) {
        Serial.println(">>> [CONFIG MODE] Please send CFG:JC_MAC,SW_MAC,PID from Web UI.");
        while (true) {
            checkAndSaveConfig();
            delay(20);
        }
    } else {
        // 通常送信機モード（ボタン押下によるウェイクアップ時のみ送信）
        esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
        if (wakeup_cause != ESP_SLEEP_WAKEUP_GPIO) {
            esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
            return;
        }

        preferences.begin("settings", true);
        String jcMacStr = preferences.getString("jc_mac", "AA:BB:CC:DD:EE:FF");
        String swMacStr = preferences.getString("sw_mac", "11:22:33:44:55:66");
        String pidStr = preferences.getString("pid", "0x2066");
        preferences.end();

        uint16_t sourcePid = (uint16_t)strtol(pidStr.c_str(), NULL, 16);

        // MACアドレスの適用 (-2オフセット補正)
        uint8_t sourceMacBytes[6];
        if (parseMac(jcMacStr.c_str(), sourceMacBytes)) {
            sourceMacBytes[5] -= 2;
            esp_base_mac_addr_set(sourceMacBytes);
        }

        uint8_t targetReversed[6];
        parseMacReverse(swMacStr.c_str(), targetReversed);

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
