
#include <Arduino.h>
#include <NimBLEDevice.h>

// ===== 設定：スキャン結果に合わせて変更 =====

// 起動対象Switch 2のBluetooth MAC（通常の表記順）
static const char* TARGET_SWITCH_MAC = "AA:BB:CC:DD:EE:FF";

// そのSwitch 2に登録済みのJoy-Con MAC
static const char* SOURCE_JOYCON_MAC = "E0:EF:BF:00:11:22";

// 実際に取得したPIDに変更
static const uint16_t SOURCE_JOYCON_PID = 0x2066;

// ESP32-C3のBOOTボタン。ボードによって異なる場合あり
static constexpr int BUTTON_PIN = 9;
static constexpr uint32_t BURST_MS = 2000;

// ============================================

static bool busy = false;
static bool lastButton = HIGH;

static void sendWakeBurst() {
  if (busy) return;
  busy = true;

  uint8_t target[6];
  int parsed[6];

  if (sscanf(TARGET_SWITCH_MAC, "%x:%x:%x:%x:%x:%x",
             &parsed[0], &parsed[1], &parsed[2],
             &parsed[3], &parsed[4], &parsed[5]) != 6) {
    Serial.println("ERROR: Invalid target MAC");
    busy = false;
    return;
  }

  for (int i = 0; i < 6; i++) {
    target[i] = (uint8_t)parsed[i];
  }

  uint8_t payload[31] = {
    0x02, 0x01, 0x06,
    0x1B, 0xFF, 0x53, 0x05,
    0x01, 0x00, 0x03,
    0x7E, 0x05,
    0x00, 0x00,       // PID
    0x00, 0x01, 0x81, // state flag = 0x81
    0, 0, 0, 0, 0, 0, // Switch MAC reversed
    0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
  };

  payload[12] = SOURCE_JOYCON_PID & 0xFF;
  payload[13] = (SOURCE_JOYCON_PID >> 8) & 0xFF;

  for (int i = 0; i < 6; i++) {
    payload[17 + i] = target[5 - i];
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();

  // 生の31バイトをそのまま広告データとして設定
  NimBLEAdvertisementData data;
  data.addData(std::string(
      reinterpret_cast<const char*>(payload), sizeof(payload)));

  if (!adv->setAdvertisementData(data)) {
    Serial.println("ERROR: setAdvertisementData failed");
    busy = false;
    return;
  }

  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  adv->setMinInterval(0x30);
  adv->setMaxInterval(0x50);

  Serial.println("Wake burst starting...");
  Serial.print("Payload: ");

  for (int i = 0; i < sizeof(payload); i++) {
    Serial.printf("%02X ", payload[i]);
  }
  Serial.println();

  if (!adv->start()) {
    Serial.println("ERROR: Advertising start failed");
    busy = false;
    return;
  }

  delay(BURST_MS);
  adv->stop();

  Serial.println("Wake burst complete.");
  busy = false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  NimBLEDevice::init("");

  // 指定Joy-Conのアドレスをランダムアドレスとして設定
  NimBLEAddress sourceAddress(SOURCE_JOYCON_MAC, BLE_ADDR_RANDOM);

  if (!NimBLEDevice::setOwnAddr(sourceAddress)) {
    Serial.println("ERROR: setOwnAddr failed");
  } else if (!NimBLEDevice::setOwnAddrType(BLE_ADDR_RANDOM)) {
    Serial.println("ERROR: setOwnAddrType failed");
  } else {
    Serial.println("Source address configured as random.");
  }

  Serial.println("ESP32-C3 Switch 2 wake transmitter ready.");
  Serial.println("Press BOOT to send a 2-second burst.");
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
