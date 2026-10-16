#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// 内蔵LEDのピン（ESP32-C3の定番はGPIO 2または8などですが、ボードに合わせて変更してください）
#define LED_PIN 2

int scanTime = 5; // スキャン時間（秒）
BLEScan* pBLEScan;

// Nintendo特有のマーカー（カンパニーID 0x0553 または VID 0x057E）をバイト列から探す
bool hasNintendoMarkers(const uint8_t *data, size_t length) {
  for (size_t i = 0; i + 1 < length; i++) {
    // 0x0553 (little-endian: 53 05)
    if (data[i] == 0x53 && data[i + 1] == 0x05) return true;
    // 0x057E (little-endian: 7E 05)
    if (data[i] == 0x7E && data[i + 1] == 0x05) return true;
  }
  return false;
}

// デバイスが見つかったときのコールバック
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // MACアドレスの取得
    std::string macStr = advertisedDevice.getAddress().toString();
    
    // メーカーデータの取得
    std::string mfgData = advertisedDevice.getManufacturerData();
    bool nintendoMarker = false;

    if (!mfgData.empty()) {
      nintendoMarker = hasNintendoMarkers((const uint8_t*)mfgData.data(), mfgData.length());
    }

    // NintendoのOUI（E0:EF:BF）またはマーカーが含まれている場合のみ強力にログ出力
    // ※ESP32のシリアルに分かりやすく表示します
    if (macStr.rfind("e0:ef:bf", 0) == 0 || nintendoMarker) {
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("★ [NINTENDO FOUND!] MAC: %s, RSSI: %d\n", macStr.c_str(), advertisedDevice.getRSSI());
      if (advertisedDevice.haveName()) {
        Serial.printf("    Name: %s\n", advertisedDevice.getName().c_str());
      }
      digitalWrite(LED_PIN, LOW);
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("Initializing BLE Scanner...");
  BLEDevice::init("");
  
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // アクティブスキャン（詳細情報を取得）
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("Scanning started!");
  Serial.println("Tip: Switch 2 settings -> Controllers -> Change Grip/Order, and hold ESP32 close to it.");
}

void loop() {
  // 5秒間スキャンを実行
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  // 結果をクリアして次のスキャンへ
  pBLEScan->clearResults(); 
  delay(1000);
}
