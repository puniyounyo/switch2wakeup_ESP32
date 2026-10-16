#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define LED_PIN 2

int scanTime = 5; // スキャン時間（秒）
BLEScan* pBLEScan;

// Nintendo特有のマーカー（カンパニーID 0x0553 または VID 0x057E）をバイト列から探す
bool hasNintendoMarkers(const uint8_t *data, size_t length) {
  for (size_t i = 0; i + 1 < length; i++) {
    if (data[i] == 0x53 && data[i + 1] == 0x05) return true;
    if (data[i] == 0x7E && data[i + 1] == 0x05) return true;
  }
  return false;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Arduinoの String 型に変更して受け取る
    String macStr = advertisedDevice.getAddress().toString().c_str();
    String mfgData = advertisedDevice.getManufacturerData();
    bool nintendoMarker = false;

    if (mfgData.length() > 0) {
      nintendoMarker = hasNintendoMarkers((const uint8_t*)mfgData.c_str(), mfgData.length());
    }

    // NintendoのOUI（e0:ef:bf）またはマーカーが含まれている場合
    if (macStr.startsWith("e0:ef:bf") || nintendoMarker) {
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
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("Scanning started!");
}

void loop() {
  // ポインタとして受け取るように修正
  BLEScanResults* foundDevices = pBLEScan->start(scanTime, false);
  pBLEScan->clearResults(); 
  delay(1000);
}
