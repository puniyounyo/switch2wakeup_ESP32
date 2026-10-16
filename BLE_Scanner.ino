
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>

// RSSIがこの値以上のデバイスを表示。
// 近くの機器を探す場合は -90、絞る場合は -70 などに調整。
static constexpr int RSSI_THRESHOLD = -90;

static bool hasNintendoMarker(const uint8_t* data, size_t len) {
  for (size_t i = 0; i + 1 < len; i++) {
    if ((data[i] == 0x53 && data[i + 1] == 0x05) ||
        (data[i] == 0x7E && data[i + 1] == 0x05)) {
      return true;
    }
  }
  return false;
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const std::vector<uint8_t>& payload = device->getPayload();
    const uint8_t* data = payload.data();
    const size_t len = payload.size();

    const int rssi = device->getRSSI();
    const bool nintendo = hasNintendoMarker(data, len);

    // Nintendoマーカーがある機器、またはRSSIが強い機器を表示
    if (!nintendo && rssi < RSSI_THRESHOLD) {
      return;
    }

    Serial.println();
    Serial.println(nintendo ? "===== NINTENDO MARKER ====="
                            : "===== NEARBY DEVICE =====");

    Serial.printf("MAC: %s\n",
                  device->getAddress().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", rssi);
    Serial.printf("Adv type: %u\n", device->getAdvType());
    Serial.printf("Payload length: %u\n", (unsigned)len);

    if (device->haveName()) {
      Serial.printf("Name: %s\n", device->getName().c_str());
    }

    Serial.print("RAW: ");
    for (size_t i = 0; i < len; i++) {
      Serial.printf("%02X", data[i]);
      if (i + 1 < len) Serial.print(" ");
    }
    Serial.println();

    // Nintendoの広告フォーマットと思われる場合にPID候補を表示
    // 元のプロジェクトの仕様：RAWの12～13バイト
    if (len >= 14 &&
        data[0] == 0x02 &&
        data[1] == 0x01) {
      Serial.printf("PID candidate (offset 12-13): 0x%02X%02X\n",
                    data[13], data[12]);
    }
  }

  void onScanEnd(const NimBLEScanResults&, int reason) override {
    Serial.printf("Scan ended, reason=%d. Restarting...\n", reason);
    NimBLEDevice::getScan()->start(0, false, true);
  }
};

static ScanCallbacks scanCallbacks;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP32-C3 BLE Scanner");
  Serial.println("Install NimBLE-Arduino 2.x");

  NimBLEDevice::init("");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(96);
  scan->setWindow(96);
  scan->setMaxResults(0);

  if (!scan->start(0, false, true)) {
    Serial.println("ERROR: Failed to start BLE scan");
  } else {
    Serial.println("Scanning continuously...");
  }
}

void loop() {
  delay(1000);
}
