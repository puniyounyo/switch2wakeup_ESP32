#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>

#define BUTTON_PIN 9
#define LED_PIN 2

// ==========================================
// 🛠️ 【設定エリア】取得したアドレスを入力
// ==========================================
// ターゲットのSwitch 2本体のBT MACアドレス
uint8_t TARGET_SWITCH_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; 

// ペアリング済みジョイコンのPID (通常は 0x2066 または 0x2067)
uint16_t SOURCE_JOYCON_PID   = 0x2066;
// ==========================================

// 31バイトのウェイクパケットを構築する関数
void buildWakePayload(const uint8_t target_mac[6], uint16_t pid, uint8_t out[31]) {
  const uint8_t template_data[] = {
    0x02, 0x01, 0x06,                         // BLE Flags
    0x1B, 0xFF, 0x53, 0x05,                   // Mfg data, Nintendo company ID
    0x01, 0x00, 0x03,                         // header
    0x7E, 0x05,                               // Nintendo VID
    0x00, 0x00,                               // PID (filled)
    0x00, 0x01, 0x00,                         // bytes 14-16 (byte 16 = 0x81 for sleep-wake)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // target MAC reversed (filled)
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // pad
  };
  memcpy(out, template_data, sizeof(template_data));
  
  // PIDの設定
  out[12] = pid & 0xFF;
  out[13] = (pid >> 8) & 0xFF;
  // 状態フラグ (0x81 = スリープ復帰用)
  out[16] = 0x81;
  // ターゲット本体のMACアドレスを逆順にしてセット
  for (int i = 0; i < 6; i++) {
    out[17 + i] = target_mac[5 - i];
  }
}

void sendWakeBurst() {
  Serial.println(">>> Sending Switch 2 Wake Beacon...");
  
  uint8_t payload[31];
  buildWakePayload(TARGET_SWITCH_MAC, SOURCE_JOYCON_PID, payload);

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setManufacturerData(std::string((char*)payload, sizeof(payload)));
  pAdvertising->setAdvertisementData(advData);

  // 2秒間アドバタイジングを連射
  pAdvertising->start();
  delay(2000);
  pAdvertising->stop();
  
  Serial.println(">>> Beacon sent!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // BLE初期化
  BLEDevice::init("Switch2_Wake_Beacon");
  Serial.println("Single-Switch 2 Wake Beacon Ready!");
}

void loop() {
  // ボタンが押されたら（LOWになったら）
  if (digitalRead(BUTTON_PIN) == LOW) {
    digitalWrite(LED_PIN, HIGH);

    // ウェイクパケットを送信
    sendWakeBurst();

    digitalWrite(LED_PIN, LOW);
    
    // 連続トリガー防止のクールダウン
    delay(3000);
  }
  delay(50);
}
