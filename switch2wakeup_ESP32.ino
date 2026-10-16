#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEAdvertising.h>
#include "esp_bt.h"
#include "esp_mac.h"

// =================================================================
// 【設定エリア】普段通りの「正規の順（正順）」で入力してください
// =================================================================

// 1. スキャンで取得した登録済みJoy-ConのBluetooth MACアドレス（正順）
static const char* SOURCE_JOYCON_MAC = "AA:BB:CC:DD:EE:FF";

// 2. Switch 2本体のBluetooth MACアドレス（正順）
static const char* TARGET_SWITCH_MAC = "AA:BB:CC:DD:EE:FF";

// 3. 実際に取得したJoy-ConのPID（通常は 0x2066 または 0x2067）
static const uint16_t SOURCE_JOYCON_PID = 0x2066;

// ESP32-C3等のBOOTボタンピン（通常は GPIO 9）
static constexpr int BUTTON_PIN = 9;
static constexpr uint32_t BURST_MS = 2000;

// =================================================================

static bool busy = false;
static bool lastButton = HIGH;

// 正順のMAC文字列をバイト配列に変換する関数
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

// 正順のMAC文字列を「逆順（リトルエンディアン）」に変換する関数
static bool parseMacReverse(const char* macStr, uint8_t* outReversed) {
  uint8_t temp[6];
  if (!parseMac(macStr, temp)) return false;
  for (int i = 0; i < 6; i++) {
    outReversed[i] = temp[5 - i];
  }
  return true;
}

static void sendWakeBurst() {
  if (busy) return;
  busy = true;

  Serial.println(">>> Sending Switch 2 Raw Wake Beacon...");

  uint8_t targetReversed[6];
  if (!parseMacReverse(TARGET_SWITCH_MAC, targetReversed)) {
    Serial.println("ERROR: Invalid TARGET_SWITCH_MAC format");
    busy = false;
    return;
  }

  // 本物のジョイコンと完全に一致させる31バイトのRAWペイロード
  uint8_t raw_payload[31] = {
    0x02, 0x01, 0x06,   // BLE Flags
    0x1B, 0xFF,         // Length & Manufacturer Specific Data type
    0x53, 0x05,         // Nintendo Company ID (LSB, MSB)
    0x01, 0x00, 0x03,   // VID header
    0x7E, 0x05,         // USB VID (0x057E)
    0x00, 0x00,         // PID placeholder
    0x00, 0x01, 0x81,   // State flag = 0x81
    0, 0, 0, 0, 0, 0,   // Switch MAC reversed placeholder
    0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
  };

  // PIDをリトルエンディアンで埋め込み
  raw_payload[12] = SOURCE_JOYCON_PID & 0xFF;
  raw_payload[13] = (SOURCE_JOYCON_PID >> 8) & 0xFF;

  // 逆順にしたSwitch 2本体MACを埋め込み
  for (int i = 0; i < 6; i++) {
    raw_payload[17 + i] = targetReversed[i];
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();

  // ライブラリによる変形を防ぎ、完全な31バイトのRAWデータをそのままアドバタイジングに乗せる
  NimBLEAdvertisementData advData;
  advData.addData(raw_payload, sizeof(raw_payload));
  
  adv->setAdvertisementData(advData);
  
  // 非接続アドバタイジングモード
  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  adv->setMinInterval(0x30);
  adv->setMaxInterval(0x50);

  Serial.print("Payload: ");
  for (size_t i = 0; i < sizeof(raw_payload); i++) {
    Serial.printf("%02X ", raw_payload[i]);
  }
  Serial.println();

  // 2秒間バースト送信
  unsigned long startTime = millis();
  while (millis() - startTime < BURST_MS) {
    adv->start();
    delay(20);
    adv->stop();
  }

  Serial.println(">>> Wake burst complete.");
  busy = false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 【自動補正】設定されたジョイコンMACの末尾バイトから "-2" を引き、
  // ESP32ハードウェア側の "+2" 増加仕様を完璧に相殺して狙い通りのMACで飛ばす
  uint8_t sourceMacBytes[6];
  if (parseMac(SOURCE_JOYCON_MAC, sourceMacBytes)) {
    // 末尾バイトから2を引く（例: 4F -> 4D）
    sourceMacBytes[5] -= 2;

    esp_err_t err = esp_base_mac_addr_set(sourceMacBytes);
    if (err == ESP_OK) {
      Serial.println(">>> Controller MAC spoofed & auto-corrected successfully!");
    } else {
      Serial.printf(">>> Failed to spoof MAC address! Error: %d\n", err);
    }
  }

  NimBLEDevice::init("");

  Serial.println("ESP32-C3 Switch 2 wake transmitter ready.");
  Serial.println("Press BOOT to send a 2-second wake burst.");
}

void loop() {
  bool button = digitalRead(BUTTON_PIN);

  if (lastButton == HIGH && button == LOW) {
    delay(25); // 簡易デバウンス
    if (digitalRead(BUTTON_PIN) == LOW) {
      sendWakeBurst();
    }
  }

  lastButton = digitalRead(BUTTON_PIN);
  delay(10);
}
