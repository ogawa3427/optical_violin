# 用語集 — migration 移植で導入した名前

この移植作業 (`projects/migration`) で**新しく付けた名前**の一覧。
notebook 62 / plan から引き継いだ既存用語は「継承」と明記し、私の造語と区別する。

---

## エンジン / 全体概念

| 用語 | 意味 | 場所 |
|---|---|---|
| **CausalEngine** | 因果(causal)エンジン。m_event 1 個ごとに expr を 1 個即出力する本体。実機が使う。 | `src/infer/causal.h` |
| **バッチ版 / 因果版** | バッチ版=notebook と同じく「ノート全体を見てから」描く版 (`m62.cpp`)。因果版=逐次の `CausalEngine`。 | — |
| **i 走査化 (running i)** | バッチが末尾確定 `i_tend` を使う所を、走査中の i(t) に置き換える因果化。私の造語。 | `causal.h` |
| **trailing LPF / 中央 LPF** | trailing=後方移動平均(因果)。中央=両側 edge pad の移動平均(notebook の `lpf`)。 | `causal.h` / `m62.cpp` |
| **リプレイ自己検証** | 実機起動時に焼き込みサンプル入力を流し、CDC へ expr を吐いて PC 参照と突合する検証モード。私の造語。 | `src/main.cpp` |
| **parity ゲート** | C++ 出力と Python 参照を突合し合否を出す検証。 *(駆動概念は plan §6 継承)* | `tools/run_parity.sh` |

## C++ 型 / クラス / 関数 (新規)

| 名前 | 意味 | 場所 |
|---|---|---|
| **`m62`** | 本移植の名前空間 (62 パイプライン)。 | 全 `src/infer` |
| **StreamSegmenter** | 逐次セグメンタ。イベントを push しノート境界でコールバック。バッチ検証用。 | `stream.h` |
| **Event / MEvent / Note / NoteData / ExprPoint** | 生イベント / ノート内イベント / ノート / 中間生成物 / (時刻,expr) 点。 | `m62.h` |
| **`make_note()`** | 1 セグメント行列 → Note (note_num!=0 / min_m_events フィルタ込み)。 | `m62.cpp` |
| **`transition_distance()`** | 2 イベント間の重み付き遷移距離。 *(計算式は pipeline.py 継承)* | `m62.cpp` |
| **`ridge_score_prefix()`** | prefix k の Ridge スコアを Δt 列から直接計算 (因果用に公開)。 | `m62.cpp` |
| **`db_to_expr_one()`** | 50F 逆変換 1 点版 (notebook `db_to_expr` の単値版)。 | `m62.cpp` |
| **`onHidEvent()`** | live モードで dataSender の HID パース結果を流し込むフック。 | `main.cpp` |
| **drop-first / row0 drop** | 先頭ノートでは ev0 を「start マーカー」として捨てる規約の実装名。 | `stream.h` (`drop_first_`) |

## 焼き込みテーブル名 (`include/models_62.h`)

| 名前 | 中身 |
|---|---|
| **RIDGE_OFFSET / RIDGE_MEAN / RIDGE_SCALE / RIDGE_COEF / RIDGE_TOTAL** | Ridge **三角バンク**を flat 配列化。`RIDGE_OFFSET[k]=k(k+1)/2`。 |
| **Ridge 三角バンク (triangular bank)** | prefix k ごとに k+1 特徴の Ridge を 104 個並べた構造の呼称。 *(構造は reg_test.joblib 継承、命名は私)* |
| **TEND_I / TEND_T** | t_end(i) アンカー (i 配列 / t_end[ms] 配列)。 |
| **SP_P0 / SP_P1 / SP_P2 / SP_SIGMA / SP_I_NODES** | softplus 5 クラスの制御点とσ、i ノード。 |
| **DB_DULL / DB_NORMAL / DB_SHARP** | 57 の dB CDF (i=-1/0/+1)。`percentile_to_db_i` 用。 |
| **PCT_GRID / PCT_PRESENT / PCT_I_NODES / PCT_T_GRID / PCT_P_AXIS** | 弓速パーセンタイルグリッド (5 node×101 pct×61 time) と各軸。 |
| **NAN_SENT** | NaN を表す番兵値 (9.99e9)。constexpr float 配列に NaN を置けないため。私の造語。 |
| **SEG** | seg_params (11 定数) を収めた `SegParams` 構造体のインスタンス。 |

## ツール / ファイル名 (新規)

| 名前 | 役割 |
|---|---|
| **export_for_mcu.py** | 焼き込みテーブル + Python 参照波形 (バッチ/因果) を生成。 |
| **compare_parity.py** | C++ 出力と Python 参照を突合 (max abs / RMS)。 |
| **run_parity.sh** | ビルド→両 CSV を batch/causal で突合する parity ゲート。 |
| **models_62.h / models_62.json** | 焼き込みテーブルの C ヘッダ / JSON。 |
| **sample_events.h** | リプレイ自己検証用の埋め込みサンプル入力。 |
| **`*.raw.csv` / `*.expr.csv` / `*.causal.csv` / `*.notes.csv` / `*.events.csv`** | 参照: 生入力 / バッチ参照 / 因果参照 / ノートメタ / ノート別イベント。 |

## PlatformIO env 名

| 名前 | 意味 |
|---|---|
| **atoms3r** | 既存 dataSender と同じ `platform=espressif32` 版 (この環境ではビルド不可)。 |
| **atoms3r_pioarduino** | pioarduino プラットフォーム版 (ビルド成功・推奨)。 |
| **native** | PC parity ハーネス用 (`test/main_native.cpp`)。 |

---

### 継承用語 (私の造語ではない・notebook 62 / plan 由来)

`i_tend`, `t_end`, `dynamic handoff`, `softplus`, `vol_base`, `i_node`,
`build_note_data`, `note_expr_waveform`, `atk_bow_percentile`, `percentile_to_db_i`,
`atk_softplus_db_i`, `reg_sk`, `offs_win`, `seg_params`, `焼き込み(む)`, `parity 駆動`,
`LPF_I_WIN`/`LPF_SPEED_WIN`/`HANDOFF_REMAIN` 等の定数名。
