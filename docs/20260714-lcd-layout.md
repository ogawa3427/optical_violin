# 2026-07-14 AtomS3R LCD 画面構成の差し替え

手書きワイヤに合わせて `Telemetry::draw()` のレイアウトだけを組み替えた。
演奏パス・因果エンジンの判定ロジックは触っていない（`onExpr` に `i_a` を同梱した
配線変更のみ）。

## 新レイアウト (128×128、上→下)

| 段 | 内容 |
|---|---|
| 1 | **音名** (例 `G4#`) \| **弓方向** (`UP` ピンク / `DN` 水色)。無音時は両方グレー `--` |
| 2 | **expr メーター** CC#11 [0,127] 横バー + 数値 |
| 3 | **iA 累積** 左=-1 … 右=+1。パルスごとに 1px 縦線を**明るいシアン**で加算、重なりで白寄り。noteOff でリセット |
| 4 | `p` / `n` カウンタ（**1行**） |
| 5 | end reason size2: `d`/`t` と直前理由（dist=橙 / delta・flush=紫） |

## 配線変更

- `CausalEngine::onExpr` シグネチャに `float i_a` を追加（走査中の trailing LPF 済み思念）。
  オンセットの expr=0 送出時は `NAN`（LCD 累積には載せない）。
- `PulseRecord::i_a` 追加。CSV は末尾列として追記
  (`P,...,infer_ns,send_ns,i_a`)。旧パーサは列 0..7 のまま動く。

## 音名表記

シャープはスケッチ準拠で `G4#` 形式（`G#4` ではない）。

## 関連ファイル

- `src/io/telemetry.{h,cpp}` — LCD 描画
- `src/infer/causal.h` — onExpr に i_a
- `src/main.cpp` — PulseRecord へ橋渡し
