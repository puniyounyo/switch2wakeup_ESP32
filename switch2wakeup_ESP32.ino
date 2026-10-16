#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "esp_bt.h"
#include "esp_mac.h"

// =================================================================
// 【設定エリア】
// =================================================================

// 1. スキャナーで取得した「右ジョイコン」のBluetooth MACアドレス（正順・左からそのまま）
uint8_t CONTROLLER_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; // ← 取得したジョイコンのMACに変更

// 2. 「Switch 2本体」のBluetooth MACアドレス（※Wi-Fi MAC末尾+1等にしてから「逆順」にしたもの）
uint8_t TARGET_SWITCH_MAC_REVERSED[6] = {0x97, 0x1F, 0x99, 0x55, 0xE2, 0x98}; // ← ターゲットの逆順MACに変更

// 送信トリガー用のボタンピン (Seeed Studio XIAO ESP32-C3 / SuperMini の BOOTボタンは GPIO 9)
const int BUTTON_PIN = 9; 

// =================================================================
// ウェイクパケット送信関数
// =================================================================
void sendWakeBeacon() {
  Serial.println(">>> Sending Switch 2 Wake Beacon...");

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->stop();

  BLEAdvertisementData advData;
  
  uint8_t raw_payload[31];
  memset(raw_payload, 0, sizeof(raw_payload));
  
  // Nintendo特有のフォーマットヘッダ
  raw_payload[0] = 0x1E; // Length
  raw_payload[1] = 0xFF; // Manufacturer Specific Data Type
  raw_payload[2] = 0x53; // Nintendo Company ID (LSB)
  raw_payload[3] = 0x05; // Nintendo Company ID (MSB)
  raw_payload[4] = 0x7E; // Vendor ID (LSB)
  raw_payload[5] = 0x05; // Vendor ID (MSB)
  raw_payload[6] = 0x66; // Product / Source flag
  raw_payload[7] = 0x81; // Status flag
  
  // ターゲットSwitch 2のMACアドレスをコピー（逆順配列）
  for (int i = 0; i < 6; i++) {
    raw_payload[8 + i] = TARGET_SWITCH_MAC_REVERSED[i]; 
  }

  // ArduinoのStringにバイナリデータを正しく格納する
  String mfgData = "";
  for (int i = 0; i < sizeof(raw_payload); i++) {
    mfgData += (char)raw_payload[i];
  }

  advData.setManufacturerData(mfgData);
  pAdvertising->setAdvertisementData(advData);
  
  // 約2秒間バースト送信
  pAdvertising->start();
  delay(2000); 
  pAdvertising->stop();

  Serial.println(">>> Beacon sent!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. ESP32自体のベースMACアドレスを、ジョイコンのものに偽装する
  esp_err_t err = esp_base_mac_addr_set(CONTROLLER_MAC);
  if (err == ESP_OK) {
    Serial.println(">>> Controller MAC spoofed successfully!");
  } else {
    Serial.printf(">>> Failed to spoof MAC address! Error: %d\n", err);
  }

  // 2. ピン設定 (BOOTボタン: 内部プルアップ、押すとLOW)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 3. BLE初期化
  BLEDevice::init("Switch2WakeDongle");
  
  Serial.println("Single-Switch 2 Wake Beacon Ready! Press BOOT button to send.");
}

void loop() {
  // BOOTボタンが押されたら送信
  if (digitalRead(BUTTON_PIN) == LOW) {
    sendWakeBeacon();
    delay(1000); // チャタリング防止
  }
  
  delay(50);
}
