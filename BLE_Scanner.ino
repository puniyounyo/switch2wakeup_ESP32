
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <vector>

// RSSIがこの値以上の機器を表示。
// 近くの機器を探す場合は -90、絞る場合は -70 などに調整。
static constexpr int RSSI_THRESHOLD = -90;

// 元プロジェクトで想定されている広告データの形式を確認。
// 配列の位置は0始まり。
static bool isExpectedNintendoFormat(
    const uint8_t* data, size_t len) {

  if (len < 23) return false;

  // BLE Flags
  if (data[0] != 0x02 ||
      data[1] != 0x01 ||
      data[2] != 0x06) {
    return false;
  }

  // Manufacturer Specific Data + Nintendo company ID
  if (data[3] != 0x1B ||
      data[4] != 0xFF ||
      data[5] != 0x53 ||
      data[6] != 0x05) {
    return false;
  }

  // Nintendo広告ヘッダー、VID部分
  if (data[7]  != 0x01 ||
      data[8]  != 0x00 ||
      data[9]  != 0x03 ||
      data[10] != 0x7E ||
      data[11] != 0x05) {
    return false;
  }

  return true;
}

static bool hasNintendoMarker(
    const uint8_t* data, size_t len) {

  for (size_t i = 0; i + 1 < len; i++) {
    if ((data[i] == 0x53 && data[i + 1] == 0x05) ||
        (data[i] == 0x7E && data[i + 1] == 0x05)) {
      return true;
    }
  }

  return false;
}

// MACアドレスを読みやすい形式にする。
static void formatMac(
    const uint8_t* mac, char* out, size_t outSize) {

  snprintf(out, outSize,
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5]);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {

    const std::vector<uint8_t>& payload = device->getPayload();
    const uint8_t* data = payload.data();
    const size_t len = payload.size();

    const int rssi = device->getRSSI();
    const bool nintendoMarker = hasNintendoMarker(data, len);
    const bool validFormat = isExpectedNintendoFormat(data, len);

    // Nintendoマーカーがある機器、またはRSSIが強い機器を表示。
    if (!nintendoMarker && rssi < RSSI_THRESHOLD) {
      return;
    }

    // 送信元アドレス。Nintendo形式に一致した場合も、
    // これだけでペアリング済みJoy-Conと断定はしない。
    const std::string address = device->getAddress().toString();

    Serial.println();
    Serial.println("==================================================");

    if (validFormat) {
      Serial.println("[Nintendo広告形式に一致]");
    } else if (nintendoMarker) {
      Serial.println("[Nintendoマーカー検出・形式未確認]");
    } else {
      Serial.println("[近くのBLE機器]");
    }

    // 1. Joy-Con MAC候補
    Serial.printf("Joy-Con MAC候補 : %s\n", address.c_str());

    // 2. Switch 2 MAC、VID、PID
    if (validFormat) {
      uint8_t switchMac[6];

      // RAWの17～22バイトを逆順に戻す。
      for (int i = 0; i < 6; i++) {
        switchMac[i] = data[22 - i];
      }

      char switchMacText[18];
      formatMac(switchMac, switchMacText, sizeof(switchMacText));

      // PIDは12～13バイト、リトルエンディアン。
      const uint16_t pid =
          static_cast<uint16_t>(data[12]) |
          (static_cast<uint16_t>(data[13]) << 8);

      const uint16_t vid =
          static_cast<uint16_t>(data[5]) |
          (static_cast<uint16_t>(data[6]) << 8);

      Serial.printf("Switch 2 MAC    : %s\n", switchMacText);
      Serial.printf("VID候補         : 0x%04X\n", vid);
      Serial.printf("PID             : 0x%04X\n", pid);
      Serial.printf("State flag      : 0x%02X\n", data[16]);
    } else {
      Serial.println("Switch 2 MAC    : 解析対象外（形式不一致）");
      Serial.println("VID / PID       : 未解析");
    }

    // 3. その他の情報
    Serial.println("--- その他のデータ ---");
    Serial.printf("RSSI            : %d dBm\n", rssi);
    Serial.printf("Adv type        : %u\n", device->getAdvType());
    Serial.printf("Payload length  : %u bytes\n",
                  static_cast<unsigned>(len));
    Serial.printf("Nintendo marker : %s\n",
                  nintendoMarker ? "あり" : "なし");
    Serial.printf("Format match    : %s\n",
                  validFormat ? "YES" : "NO");

    if (device->haveName()) {
      Serial.printf("Name            : %s\n",
                    device->getName().c_str());
    }

    // 4. 最後にRAWデータを表示
    Serial.println("--- RAW Advertisement Data ---");
    Serial.print("RAW: ");

    for (size_t i = 0; i < len; i++) {
      Serial.printf("%02X", data[i]);
      if (i + 1 < len) Serial.print(" ");
    }

    Serial.println();
    Serial.println("==================================================");
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
  Serial.println("==================================================");
  Serial.println(" ESP32-C3 BLE Scanner");
  Serial.println(" NimBLE-Arduino 2.x");
  Serial.println("==================================================");
  Serial.println("Joy-Con MAC / Switch 2 MAC / PID / RAW");
  Serial.println("Scanning continuously...");

  NimBLEDevice::init("");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(96);
  scan->setWindow(96);
  scan->setMaxResults(0);

  if (!scan->start(0, false, true)) {
    Serial.println("ERROR: Failed to start BLE scan");
  }
}

void loop() {
  delay(1000);
}
