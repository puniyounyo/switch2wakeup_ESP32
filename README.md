https://puniyounyo.github.io/switch2wakeup_ESP32/web_flasher.html

https://puniyounyo.github.io/switch2wakeup_ESP32/web_flasher/index.html

# Nintendo Switch 2 Wake Button
### ESP32-C3 Deep Sleep版 超低消費電力 起動リモコン

Nintendo Switch 2 を物理ボタンひとつでスリープ解除するための ESP32-C3 用プロジェクトです。

ボタンを押すと ESP32-C3 が Deep Sleep から復帰し、Joy-Con 互換の Wake Beacon を送信して Switch 2 を起動します。送信完了後は再び Deep Sleep に戻るため、非常に低消費電力で運用できます。

---

## 特徴

- Nintendo Switch 2 をワンボタンで起動
- ESP32-C3対応
- Deep Sleep対応
- 超低消費電力
- ボタン電池運用可能
- USB給電運用可能
- LEDによる送信状態表示
- 将来的な複数台対応も容易

---

## システム構成

本プロジェクトは以下の2つのスケッチで構成されています。

### 1. BLE_Scanner.ino

Nintendoデバイスのみを抽出して検出する専用スキャナです。

取得できる情報

- Joy-Con MACアドレス
- Switch 2 MACアドレス
- PID
- RSSI
- Advertisement Data


---

### 2. switch2wakeup_ESP32_Deepsleep.ino

Switch 2 の Wake Beacon を送信する本体プログラムです。

機能

- Deep Sleep
- GPIO起床
- Wake Beacon送信
- LED通知
- 自動スリープ復帰


---

# 必要なもの

## ハードウェア

- ESP32-C3 SuperMini
- タクトスイッチ × 1
- USBケーブル

### 推奨

- LED × 1
- 220Ω抵抗 × 1
- ケース

---

## ソフトウェア

- Arduino IDE 2.x
- ESP32 Board Package
- NimBLE-Arduino

---

# インストール

## ESP32ボード追加

Arduino IDEの

```text
設定
↓
Additional Board Manager URLs
```

へ追加

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

---

## ESP32ボードインストール

```text
ツール
↓
ボードマネージャ
↓
ESP32
```

をインストール

---

## NimBLEライブラリ

ライブラリマネージャで

```text
NimBLE-Arduino
```

をインストール

---

# Step 1 : Switch2情報取得

まず BLE_Scanner.ino.txt をESP32へ書き込みます。 【2-294418】

シリアルモニタを

```text
115200bps
```

で開きます。

---

Switch2と登録済みJoy-Conを近くに置くと以下のような情報が表示されます。

```text
Joy-Con MAC候補 :
AA:BB:CC:DD:EE:FF

Switch 2 MAC :
11:22:33:44:55:66

PID :
0x2066
```

【2-294418】

---

## メモする情報

```text
Joy-Con MAC

Switch2 MAC

PID
```

---

# Step 2 : 起動プログラム設定

switch2wakeup_ESP32_Deepsleep.ino.txt を開きます。 【1-24e8fa】

以下3ヶ所を書き換えます。

---

## Joy-Con MAC

```cpp
static const char* SOURCE_JOYCON_MAC =
"AA:BB:CC:DD:EE:FF";
```

↓

```cpp
取得したJoy-Con MAC
```

---

## Switch2 MAC

```cpp
static const char* TARGET_SWITCH_MAC =
"AA:BB:CC:DD:EE:FF";
```

↓

```cpp
取得したSwitch2 MAC
```

---

## PID

```cpp
static const uint16_t SOURCE_JOYCON_PID =
0x2066;
```

↓

```cpp
取得したPID
```

---

# Step 3 : 配線

## ボタン

デフォルト設定

```cpp
BUTTON_PIN = GPIO4
```

【1-24e8fa】

配線

```text
GPIO4 ── ボタン ── GND
```

内部プルアップ利用のため外部抵抗不要です。 【1-24e8fa】

---

## LED（任意）

デフォルト

```cpp
LED_PIN = GPIO8
```

【1-24e8fa】

配線

```text
GPIO8
 ↓
220Ω
 ↓
LED
 ↓
GND
```

---

# Step 4 : 書き込み

switch2wakeup_ESP32_Deepsleep.ino.txt をESP32-C3へ書き込みます。 【1-24e8fa】

---

# 動作確認

USB接続後リセットすると

```text
>>> Power ON detected.
Entering DEEP SLEEP immediately.
```

と表示されます。 【1-24e8fa】

これは正常動作です。

---

Switch2をスリープ状態にし、

```text
ボタン押下
```

すると

```text
ESP32起床
↓
Wake Beacon送信
↓
Switch2起動
↓
Deep Sleepへ復帰
```

します。 【1-24e8fa】

---

# 消費電力

通常時は Deep Sleep 動作です。

```text
待機
↓
Deep Sleep
↓
数μAレベル
```

ボタン押下時のみ送信処理を行います。 【1-24e8fa】

---

# ボタン電池運用

推奨電池

```text
CR2450
CR2477
```

安定動作のため

```text
470μF〜1000μF
```

程度の電解コンデンサ追加を推奨します。

---

# 複数台対応

Switch2を複数台所有している場合は、

- Joy-Con MAC
- Switch MAC
- PID

を台数分保持することで拡張可能です。

例

```text
ボタン1 → Switch2-A

ボタン2 → Switch2-B

ボタン3 → Switch2-C
```

---

# トラブルシューティング

## Switch2が起動しない

確認項目

- Joy-Con MACが正しいか
- Switch2 MACが正しいか
- PIDが正しいか
- Switch2がスリープ中か

---

## BLE_Scannerで検出できない

確認項目

- 距離が遠すぎないか
- 登録済みJoy-Conが近くにあるか
- シリアル速度が115200bpsか

---

## 電池運用で不安定

原因

```text
起動時の電流不足
```

対策

```text
大容量コンデンサ追加
```

---

# 謝辞

本プロジェクトは Nintendo Switch 2 の BLE Wake Beacon の解析結果を参考に実装されています。

解析コミュニティおよび先行研究者の方々に感謝します。

---

# 免責

本ソフトウェアおよび回路の利用は自己責任で行ってください。

作者は本プロジェクトの利用によって発生した損害について一切責任を負いません。
