#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEAdvertising.h>
#include "esp_bt.h"
#include "esp_mac.h"
#include "esp_sleep.h"

// =================================================================
// 【設定エリア】 ご自身の環境に合わせて以下の値を書き換えてください
// =================================================================

// 1. スキャンで取得した登録済みJoy-ConのBluetooth MACアドレス（正順）
// ※ハードウェア仕様による「+2」のズレは、setup()内のコードで自動的に "-2" 補正されます
static const char* SOURCE_JOYCON_MAC = "AA:BB:CC:DD:EE:FF";

// 2. スキャンで取得したSwitch 2本体のBluetooth MACアドレス（正順）
static const char* TARGET_SWITCH_MAC = "11:22:33:44:55:66";

// 3. 実際に取得したJoy-ConのPID（通常は 0x2066 または 0x2067 など）
static const uint16_t SOURCE_JOYCON_PID = 0x2066;

// 4. ピンおよび動作タイミングの設定
static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_4; // 復帰用タクトスイッチを接続するピン（GPIO 4 と GND の間）
static constexpr int LED_PIN = 8;                    // 送信中の演出用LEDピン（お使いのボードに合わせて変更）
static constexpr uint32_t BURST_MS = 3000;           // 電波の連続送信時間（3秒：受信タイミングを確実に捉える絶妙な長さ）

// =================================================================

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

// 正順のMAC文字列を、Switch用に「逆順（リトルエンディアン）」のバイト配列に変換する関数
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
  digitalWrite(LED_PIN, LOW); // 初期状態は消灯

  // 【重要】起動理由（Wakeup Cause）のチェック
  // USB挿入時などのコールドブート（電源ON時）であれば、誤発射を防ぐため電波を出さずに即スリープへ入る
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  if (wakeup_cause != ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println(">>> Power ON detected. Entering DEEP SLEEP immediately.");
    Serial.flush();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
    return;
  }

  // ボタン押下によるディープスリープからの復帰時の処理
  Serial.println(">>> Woke up by Button Press!");
  delay(50); // 安定化のためのウェイト
  Serial.println(">>> Sending Switch 2 wake signal...");

  // MACアドレスの自動補正（ESP32側のBluetooth仕様による "+2" のズレを自動で相殺）
  uint8_t sourceMacBytes[6];
  if (parseMac(SOURCE_JOYCON_MAC, sourceMacBytes)) {
    sourceMacBytes[5] -= 2; // 末尾バイトから2を引く（例: 4F -> 4D）
    esp_base_mac_addr_set(sourceMacBytes);
  }

  // ターゲットとなるSwitch 2のMACアドレスを逆順（リトルエンディアン）に変換
  uint8_t targetReversed[6];
  parseMacReverse(TARGET_SWITCH_MAC, targetReversed);

  // 本物のJoy-Conと完全に一致させる31バイトのRAWアドバタイジングペイロードを構築
  uint8_t raw_payload[31] = {
    0x02, 0x01, 0x06,   // BLE Flags (General Discoverable)
    0x1B, 0xFF,         // Length & Manufacturer Specific Data type
    0x53, 0x05,         // Nintendo Company ID (LSB, MSB)
    0x01, 0x00, 0x03,   // VID header
    0x7E, 0x05,         // USB VID (0x057E: Nintendo)
    0x00, 0x00,         // PID placeholder (ここにPIDが下位・上位の順で入る)
    0x00, 0x01, 0x81,   // State flag = 0x81 (スリープ復帰用ステータス)
    0, 0, 0, 0, 0, 0,   // Switch MAC reversed placeholder (ここに逆順Switch MACが入る)
    0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
  };

  // PIDをリトルエンディアンでペイロードに埋め込み
  raw_payload[12] = SOURCE_JOYCON_PID & 0xFF;
  raw_payload[13] = (SOURCE_JOYCON_PID >> 8) & 0xFF;

  // 逆順にしたSwitch 2本体のMACアドレスをペイロードに埋め込み
  for (int i = 0; i < 6; i++) {
    raw_payload[17 + i] = targetReversed[i];
  }

  // NimBLEライブラリによるBLE送信の初期化
  NimBLEDevice::init("");
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  
  NimBLEAdvertisementData advData;
  advData.addData(raw_payload, sizeof(raw_payload));
  adv->setAdvertisementData(advData);
  
  // 非接続型のアドバタイジング（ビーコン）モードに設定
  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  
  // 高速かつ確実な間隔で電波を放出
  adv->setMinInterval(0x20);
  adv->setMaxInterval(0x40);

  // 指定時間（4秒間）電波を途切れさせずに連続放射しつつ、LEDをチカチカ点滅させる演出
  adv->start();

  unsigned long startTime = millis();
  while (millis() - startTime < BURST_MS) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }

  adv->stop();
  digitalWrite(LED_PIN, LOW); // 送信完了後にLEDを確実に消灯
  
  Serial.println(">>> Wake burst complete. Going back to DEEP SLEEP.");
  Serial.flush();

  // 再びGPIO 4がLOW（ボタン押下）になったらスリープから復帰するように設定
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  // 超省電力のディープスリープへ移行
  esp_deep_sleep_start();
}

void loop() {
  // ディープスリープ運用のため、loop() 内の処理は使用しません
}
