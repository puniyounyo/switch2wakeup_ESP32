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

[ツール] > [ボード] > [ボードマネージャー] から esp32 (Espressif Systems) を検索し、最新バージョンをインストールする。2. 必須ライブラリのインストールESP32の標準Bluetoothライブラリではなく、細かいパケット制御やアドバタイジング操作が可能な NimBLE-Arduino ライブラリが必須です。Arduino IDEの [ツール] > [ライブラリを管理...] を開く。検索窓に NimBLE-Arduino と入力する。h2zero/NimBLE-Arduino を選択してインストールする。3. ボード設定の選択[ツール] メニューから、お使いのESP32-C3ボードに合わせて以下のように設定します：項目設定値の例BoardESP32C3 Dev Module (またはお使いのC3互換ボード)USB CDC On BootEnabled (シリアルモニタを使用する場合)Flash ModeQIO / DIOUpload Speed921600📥 導入・セットアップ手順ステップ 1: MACアドレスとPIDの調査 (BLE Scannerの利用)まず、お使いのSwitch 2とペアリング済みのJoy-Conの情報を取得します。同梱の BLE_Scanner.ino スケッチをESP32-C3に書き込みます。Arduino IDEのシリアルモニタを開き（ボーレート 115200）、SwitchやJoy-Conの近くで起動します。スキャン結果に表示される以下の情報を控えます：Joy-Con MAC候補 (例: A4:C1:E8:92:34:4F)Switch 2 MAC (例: A4:C1:E8:6F:E7:23)PID (例: 0x2066)💡 ESP32のMACアドレス仕様に関する補正についてESP32ハードウェアの仕様上、BluetoothのベースMACアドレスは設定値に対して自動的に +2 される挙動があります。本プロジェクトのメインスケッチには、これを相殺するために自動で -2 補正するロジックが組み込まれていますので、スキャンで得た正順のMACアドレスをそのまま記入して問題ありません。ステップ 2: 送信プログラムの書き込みメインの switch2wakeup_ESP32_Deepsleep.ino スケッチをArduino IDEで開きます。コード上部の【設定エリア】にご自身で調査した値を書き換えて入力します：C++// 1. スキャンで取得した登録済みJoy-ConのBluetooth MACアドレス（正順）
static const char* SOURCE_JOYCON_MAC = "あなたのJoy-ConのMAC";

// 2. スキャンで取得したSwitch 2本体のBluetooth MACアドレス（正順）
static const char* TARGET_SWITCH_MAC = "あなたのSwitch 2のMAC";

// 3. 実際に取得したJoy-ConのPID
static const uint16_t SOURCE_JOYCON_PID = 0x2066; // 例
ESP32-C3ボードをPCに接続し、対応するポートを選択してスケッチを書き込みます。🕹️ 使い方配線確認: GPIO 4 と GND の間にタクトスイッチが接続されていることを確認します。通常動作:普段、ESP32-C3は超省電力のディープスリープ状態で待機しています。タクトスイッチを押すとスリープから復帰し、LEDが点滅しながら約4秒間、Switch 2の起動用BLEパケットを連続放射します。送信が完了すると自動的に再びディープスリープへ戻ります。安全機能: USBケーブルを抜き差しした際などのコールドブート（電源投入時）では、ボタンが押されていない限り電波を出さずに即座にスリープへ入るため、意図しない誤作動を防ぎます。
