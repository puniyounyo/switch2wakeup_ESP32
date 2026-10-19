#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEAdvertising.h>
#include "esp_bt.h"
#include "esp_mac.h"
#include "esp_sleep.h"

// =================================================================
// 【設定エリア】
// =================================================================

static const char* SOURCE_JOYCON_MAC = "AA:BB:CC:DD:EE:FF";
static const char* TARGET_SWITCH_MAC = "AA:BB:CC:DD:EE:FF";
static const uint16_t SOURCE_JOYCON_PID = 0x2066;

static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_4; 
static constexpr int LED_PIN = 8;      
static constexpr uint32_t MAX_BURST_MS = 6000; // 安全のための最大タイムアウト（6秒）

// =================================================================

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

static bool parseMacReverse(const char* macStr, uint8_t* outReversed) {
  uint8_t temp[6];
  if (!parseMac(macStr, temp)) return false;
  for (int i = 0; i < 6; i++) {
    outReversed[i] = temp[5 - i];
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 1. 起動理由のチェック（USB挿入などのコールドブートなら即スリープ）
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  if (wakeup_cause != ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println(">>> Power ON detected. Entering DEEP SLEEP immediately.");
    Serial.flush();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
    return;
  }

  // 2. 復帰直後のチャタリング・誤検知防止ウェイト
  delay(150);

  // もし起動直後にすでにボタンが離されている（誤爆・短押し）なら、信号を出さずに即スリープへ戻す
  if (digitalRead(BUTTON_PIN) == HIGH) {
    Serial.println(">>> Short press or accidental trigger detected. Skipping wake signal.");
    Serial.flush();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
    return;
  }

  Serial.println(">>> Button held. Starting wake signal transmission...");

  // 3. MACアドレス設定
  uint8_t sourceMacBytes[6];
  if (parseMac(SOURCE_JOYCON_MAC, sourceMacBytes)) {
    sourceMacBytes[5] -= 2;
    esp_base_mac_addr_set(sourceMacBytes);
  }

  uint8_t targetReversed[6];
  parseMacReverse(TARGET_SWITCH_MAC, targetReversed);

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

  raw_payload[12] = SOURCE_JOYCON_PID & 0xFF;
  raw_payload[13] = (SOURCE_JOYCON_PID >> 8) & 0xFF;

  for (int i = 0; i < 6; i++) {
    raw_payload[17 + i] = targetReversed[i];
  }

  // 4. BLEアドバタイジング開始
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

  // 5. ★「ボタンが押されている間（LOWの間）」だけ送信＆LEDチカチカを継続するループ
  unsigned long startTime = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);

    // 万が一押しっぱなしにされた場合の安全装置（最大タイムアウト）
    if (millis() - startTime > MAX_BURST_MS) {
      Serial.println(">>> Timeout reached. Stopping transmission.");
      break;
    }
  }

  // 6. ボタンが離された（あるいはタイムアウトした）ので信号停止
  adv->stop();
  digitalWrite(LED_PIN, LOW);
  
  Serial.println(">>> Button released. Stopping signal & entering DEEP SLEEP.");
  Serial.flush();

  // 7. ディープスリープへ移行
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void loop() {
}
