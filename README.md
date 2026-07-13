# migration — 62 パイプライン AtomS3R 実機移植

`projects/dataAnalysis/62_all_all_dev.ipynb` の最終出力（ノートごと CC#11 Expression
波形 [0,127]）を、PC・Python なしで **AtomS3R 単体** で再現するための移植実装。

計画書: [`docs/20260626_atoms3r_port_plan.md`](../../docs/20260626_atoms3r_port_plan.md)

> このディレクトリは移植作業用。実機に焼くファームの元は別途 `projects/firmware`。

---

## 何ができたか（現状）

- **因果(causal)・逐次エンジンを実装** (`src/infer/causal.h`)。**m_event が 1 個来るたびに
  expr を 1 個確定して即出力**（plan §0 の本来のゴール）。ノート完成を待たない。
  - バッチ版との違いは 2 点だけ: ①末尾確定 i_tend ではなく走査中の i(t)(trailing LPF) を
    毎ステップ使う ②中央 LPF → trailing LPF。dynamic handoff は各ステップの仮 t_end で動く。
- **推論コアを純 C++ に移植** (`src/infer/`)。notebook 62 の Cell14/18/20 を忠実再現（バッチ版）。
- **焼き込みテーブルを生成** (`include/models_62.h`)。Ridge 三角バンク・t_end アンカー・
  softplus・dB CDF・弓速パーセンタイルグリッド・50F 較正を C 配列化。
- **数値 parity を検証**（下表）。バッチ版は notebook 62 と一致、因果版は C++↔Python で一致。
- **PlatformIO 構成**: `native`（PC parity）/ `atoms3r_pioarduino`（実機ファーム）。
- **実機ファーム** (`src/main.cpp`)。CausalEngine をそのまま使い、各パルスで CC#11 を
  即送出（リアルタイム）。リプレイ自己検証モードも内蔵。**firmware.bin ビルド成功**。

### 残課題（plan §2 / M5・実機チューニング）
- **LPF 窓の実機調整**: 因果化に伴う遅延 vs 平滑のトレードオフ（`LPF_I_WIN=10`/
  `LPF_SPEED_WIN=50` 等）を実弾で詰める。聴感確認も併用（plan §6）。
- **ライブ HID 入力の結線**（`onHidEvent()` のフック先）。`projects/dataSender` の
  USB-HID ホスト解析を移植する（`-D M62_LIVE_HID`）。
- **出力先の最終決定**（plan §7-1）。CDC デバッグ(C)・Grove Unit Synth(A) 実装済み、USB-MIDI(B) 未。

---

## ディレクトリ

```
include/
  models_62.h        焼き込みテーブル (export_for_mcu.py 生成・編集禁止)
  sample_events.h    オンデバイス自己検証用サンプル入力 (自動生成)
src/
  infer/             Arduino 非依存の純 C++ 推論コア
    m62.{h,cpp}      ②セグメンテーション〜⑧expr の全段 (バッチ版)
    causal.h         因果エンジン CausalEngine (1 パルス 1 expr・実機の本体)
    stream.h         逐次セグメンタ (StreamSegmenter, バッチ検証用)
  io/
    output.{h,cpp}   出力 (CDC デバッグ / Grove Unit Synth MIDI)
  main.cpp           実機 setup/loop
test/
  main_native.cpp    PC parity ハーネス (raw.csv → expr CSV)
tools/
  export_for_mcu.py  焼き込みテーブル + Python 参照波形の生成
  compare_parity.py  C++ 出力と参照の突合
  run_parity.sh      ↑をまとめて回す parity ゲート
reference/           Python 参照 (models_62.json, *.raw/expr/notes/events.csv)
```

---

## 使い方

### 1. 焼き込みテーブル & 参照を再生成（データ更新時のみ）

```bash
cd projects/dataAnalysis
.venv/bin/python ../migration/tools/export_for_mcu.py
```

`include/models_62.h`・`reference/*` を上書きする。⑥の弓速↔dB写像は `realData`
collected cache 由来なので、データセット更新時は再 dump が要る（plan §8）。

### 2. PC で parity 検証

```bash
cd projects/migration
./tools/run_parity.sh          # g++ でビルドして全 CSV を突合
# or PlatformIO 経由:
pio run -e native && .pio/build/native/program reference/shuttle_60_S_1.raw.csv \
  | python tools/compare_parity.py /dev/stdin reference/shuttle_60_S_1.expr.csv
```

完了条件: 全 CSV で note 数一致・点数一致・**max abs expr error ≤ 1.0**。

### 3. 実機ファームをビルド／書き込み

```bash
cd projects/migration
pio run -e atoms3r_pioarduino              # ビルド (推奨・検証済み)
pio run -e atoms3r_pioarduino -t upload    # 書き込み
pio device monitor -b 115200               # 起動時のリプレイ自己検証出力を見る
```

> **ビルド env の注意**: 既存 dataSender/soundCard と同じ `[env:atoms3r]`
> (`platform = espressif32` + git arduino core #3.2.0) は、この環境では
> framework の `adc_continuous_data_t` 型衝突でビルドできない（dataSender でも同一エラー）。
> arduino-esp32 3.x はコアと IDF libs が整合する **pioarduino** プラットフォーム
> (`[env:atoms3r_pioarduino]`) でビルドすること。**検証済み: firmware.bin 生成成功**
> (Flash 16.1% / 539KB, RAM 6.2%)。

既定（CDC デバッグ）では起動時に `note_idx,t_ms,expr` を吐く。これを保存して
`reference/shuttle_60_S_1.expr.csv`（先頭 400 イベント分）と比べれば、実機上でも
Python と同じ数値が出ることを確認できる。

Grove Unit Synth で単体発音する場合は `-D M62_OUTPUT_MIDI` を build_flags に追加。
ライブ HID 入力で動かす場合は `-D M62_LIVE_HID` を追加し、`onHidEvent()` に
dataSender のパース結果を結線する。

---

## 検証結果（最新: 2026-07-03, KRR 焼き込み後）

バッチ版（C++ vs Python 参照）:

| 入力 CSV | ノート | 点数 | max abs expr 誤差 |
|---|---|---|---|
| shuttle_60_S_1 | 41/41 | 2638 | 0.000458 |
| dataset 4-0-short_1-0 | 18/18 | 1045 | 0.000861 |

因果版（C++ CausalEngine vs Python 因果リファレンス, **1 パルス 1 expr**）:

| 入力 CSV | ノート | 点数 | max abs expr 誤差 |
|---|---|---|---|
| shuttle_60_S_1 | 45/45 | 2517 | 0.000348 |
| dataset 4-0-short_1-0 | 25/25 | 992 | 1.84（下記の既知 1 点のみ。それ以外は < 0.01） |

誤差は float(C++) vs double(Python) の丸めのみ。因果版の点数=パルス数（各 m_event で 1 expr）。
既知: dataset の note 3 の 1 サンプルのみ、`i_to_node5` の節点境界（±0.25/±0.75）で
float32(実機) と float64(参照) が別ノードを選び expr ≈1.84 ズレる（Ridge 時代の ≈2.2 と同種）。
論理バグではなく、実機は float32 なのでこちらが真の挙動。
因果版は notebook バッチとは意図的にズレる（i 走査化・trailing LPF）— plan §6 の通り別枠管理。

---

## plan §7 未決事項への現状回答

1. **出力先**: first target は (C) CDC デバッグ（検証容易）。(A) Grove 発音も実装済み。
2. **ボード定義**: 暫定 `m5stack-atoms3`（dataSender と同じ）。pioarduino env で
   ビルド成功（firmware.bin 生成）。AtomS3R で実際に焼けるかは実機で要確認。
3. **新規 firmware**: この `projects/migration` を新規ファームとし、推論コアを `src/infer`
   に分離（ライブラリ化容易）。`dataSender` は入力解析の流用元。
4. **LPF 因果化の許容遅延**: 因果化は実装済み（`causal.h` の trailing LPF）。
   窓幅 `LPF_I_WIN=10`/`LPF_SPEED_WIN=50` の実機チューニングは M5 で残課題。
