# ESP32-C3 Nintendo Switch 2 BLE Remote Wakeup Device

ESP32-C3マイコンを使用して、Nintendo Switch 2をスリープ状態からリモートで遠隔復帰（ウェイクアップ）させるためのBLEビーコン送信デバイスです。タクトスイッチを押すと数秒間だけBLEアドバタイジング（起動用パケット）を連続放射し、即座に超省電力なディープスリープへ移行します。

---

## 📋 概要と特徴

- **スリープ復帰ビーコンの送信**: 本物のJoy-Conになりすまして、Switch 2がスリープ中に待ち受けている特定のアドバタイジングパケットを送信します。
- **コールドブート（誤動作）防止ガード**: USB電源挿入時などの意図しない電源投入時には、電波を発さずに即座にディープスリープへ入る安全設計です。
- **超低消費電力**: 普段はディープスリープ（数µA）で待機し、ボタン押下時のみ数秒間稼働するため、バッテリー駆動にも最適です。
- **専用スキャナー付属**: ペアリングに必要なJoy-ConおよびSwitch 2のMACアドレス、PIDを特定するための専用BLEスキャナーコードも同梱しています。

---

## 🛠️ 必要なハードウェア

- **ESP32-C3 開発ボード** (例: ESP32-C3-DevKitM-1 など)
- **タクトスイッチ** x 1（GPIO 4 と GND の間に接続）
- **LED** x 1（送信中の視覚的フィードバック用、内蔵LEDまたはGPIO 8等に接続）
- ジャンタクイヤ、ブレッドボード等

---

## ⚙️ 開発環境とボード設定 (Arduino IDE)

このプロジェクトをビルド・書き込みするためには、Arduino IDEにESP32ボードマネージャーと特定のBLEライブラリを追加する必要があります。

### 1. Arduino IDEの準備
1. Arduino IDEを開く。
2. **[ファイル] > [環境設定]** (Preferences) を開く。
3. 「追加のボードマネージャのURL」に以下を追加する：
   ```text
   [https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)
