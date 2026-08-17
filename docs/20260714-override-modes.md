# BtnA 4モード切替のパラメータ乗っ取り（joystick Y→iA / fader→p）

> 日付: 2026-07-14 / 関連: `src/main.cpp`, `src/infer/causal.h`, `src/io/telemetry.{h,cpp}`, env `m5stack-atoms3r`

## やったこと

演奏中に外部コントローラで推論パラメータを直接乗っ取るデモ/検証機能。
AtomS3R の BtnA を押すたびに 4 モードを巡回:

| mode | タグ表示 | 乗っ取り | タグ色 |
|---|---|---|---|
| 0 | `--` | なし（iA も音量も弓から） | 暗灰 |
| 1 | `iA` | joystick Y → i(t) | マゼンタ |
| 2 | `p` | fader → 音量パーセンタイル | オレンジ |
| 3 | `iA+p` | 両方 | 緑 |

### 乗っ取り点（`causal.h::feed()`、public フィールド `override_i/i_ext/override_pct/pct_ext`）

- **iA**: `i_now = trailing(raw_i_, LPF_I_WIN)` の直後で `i_ext` [-1,1] に置換。
  i_now は t_end / アタック形状 / ベース dB 全段の入力なので 1 箇所で演奏全体に効く。
- **p**: **両区間**を置換（ogawa 指示: アタック含め全区間）。
  - サステイン: `pct`（`base_db = percentile_to_db_i(pct, i_now)` の入力）
  - アタック: `p_atk`（`atk_dev` の入力）

### 入力読取（`main.cpp`、M62_LIVE_HID 時のみ）

- **15ms スロットル**で loop() から読む（毎ループ I2C/ADC を叩くと HID→expr レイテンシに乗る）。
  そのモードで必要な入力だけ読む。
- joystick: `Wire`（**I2C ポート0**）、SDA=**G39** / SCL=**G38**、100kHz、アドレス 0x52（初期型）。
  X,Y,Btn の 3 バイト連続読み → **Y** を `(y-128)/127` で [-1,1] に正規化。
- fader: `analogRead(7)`（**G7**=ADC1_CH6）12bit → `raw/4095` で [0,1]。
- BtnA: `M5.update()` + `wasPressed()`。

### LCD（`telemetry`）

`setStatus(mode, i_ext, pct_ext)`（volatile 単純代入、Core1→Core0）。
- 乗っ取りタグ（右下・右寄せ、モード別 3 色）
- iA 乗っ取り中: 現在 i_ext を iA 累積ヒスト上に 3px マゼンタ縦マーカーで重畳（10fps 追従）
- p 乗っ取り中: expr バー上に新設した p バーをオレンジで塗る
- レイアウト再配分: d/t カウント size2→1、expr バー高さ 12→15px、iA ヒスト高さ 42→34px

## ハマった点（重要、findings 行き）

1. **`Wire1` が M5Unified と衝突して全 read が Error -1**。
   `M5.begin()`（telemetry 側）が内蔵 IMU 用の内部 I2C を **ESP32 ハードウェア I2C ポート NUM_1** で
   初期化済み（ログ: `i2cInit(): sda=45 scl=0`）。後から `Wire1.begin(38,39)` すると同じ NUM_1 を
   取り合って `i2cRead returned Error -1` を連発。→ **外部 I2C は `Wire`（NUM_0）を使う**。
2. **Grove ペア線の役割取り違え × 2**。実機で切り分けた結果:
   - joystick: SDA/SCL が想定と逆（両向きスキャンで SDA=G39/SCL=G38 のみ 0x52 応答）
   - fader: アナログ出力は **G7** 側の線（G8 は 4095 張り付き）。Unit Fader は 2 信号線の
     片方がアナログ出力、もう片方が SK6812 LED データ入力
3. 初期型 joystick は **1 バイトだけの requestFrom だと応答しない可能性**があるため
   3 バイト（X,Y,Btn）連続読みにした。

デバッグ手段（`M62_OVERRIDE_DEBUG`、現在 0 で無効）: 起動時に SDA/SCL 両向き I2C スキャン、
500ms ごとに mode/joy_n/joy_x/adc7/adc8/i_ext/pct を CDC へダンプ。再ハマり時は 1 に戻す。

## 結果

- 実機で joystick（iA マーカー追従）・fader（p バー追従）とも動作確認済み
- ビルド: `pio run -e m5stack-atoms3r` SUCCESS（Flash ~29%）

## 未解決・次にやること

- 乗っ取り中の演奏音の聴感評価（iA 固定 vs 弓由来、fader 音量の応答感）
- fader の白線（SK6812 LED）は未使用。視覚フィードバックに使える
