# migration — 62 パイプラインの AtomS3R(C++) 移植

notebook 62 の expr 波形パイプラインを実機 (AtomS3R + Grove Unit Synth) に移植したもの。
推論コアは Arduino 非依存の純 C++ (`src/infer/`)。焼き込みテーブルは Python から生成。

## モデル/定数の生成

`include/models_62.h`（KRR バンク・各種定数）は **自動生成・直接編集しない**。

```bash
cd projects/migration
python -m pip install -r tools/requirements.txt   # 初回のみ
python tools/export_for_mcu.py                    # 窓幅等を変えたら毎回
```

- `python` = pyenv 3.12.2。システムの `python3` は numpy 無しで不可。
- scikit-learn は **1.8.0 固定**（モデル pickle 保存時と一致させる。ズレると警告＋非保証）。
- チューニングつまみは `tools/export_for_mcu.py` 先頭の「★ チューニングつまみ」ブロックに集約。
  窓幅 (`LPF_*`)・ハンドオフ・`DELTA_RATIO_CUT` 等はここだけ編集。
  `WINDOW_MS` / `T_CAP` は回帰モデルに焼かれた値なので変えるなら notebook 62 側で再学習。
- 学習データ (`reg_test.joblib`) は 43A の shuttle 3 本（i_target = 0 / +1 / −1）。

## 出力 (`src/io/output.cpp`)

- 音色: program 42 (チェロ)、CC#73 Attack=1、CC#74 Release=50、リバーブ系=0。ch0/ch1 両方に設定。
- **2 ch トグル**: ノートごとに ch を交互に使い、隣接ノートの expr/リリースを分離（デカップリング）。
  `noteOff` は両 ch を探して off、`expr` は現ノートの ch へ。

## 因果エンジン (`src/infer/causal.h`)

m_event 1 個ごとに expr 1 個を吐く逐次推論。ノート境界の扱い:

- **セグメント分割**: `transition_distance > SEG.threshold`（同音・同弓向きで Δt≈166ms 相当）。
- **Δ比カット**: `Δ[k]/Δ[k-1] > DELTA_RATIO_CUT(=1.8)`。`tick(now_ms)`（`main.cpp` の live ループが毎回呼ぶ）で
  最後のパルスから `Δ[k]×1.8` 超の無音中に切る。次パルス到達で気づく feed 内チェックは保険。
- **expr CC=0 のタイミング**: ノート終了時ではなく **次ノートのオンセット**（`beginNote`、noteOn 直後）で
  新 ch へ送出。前ノートのリリースは自然減衰させる。
- 移動平均（`LPF_*`/`ATK_SMOOTH`）の履歴はノート頭で全クリア（前ノートの値は漏れない）。

## parity 検証

`tools/run_parity.sh`（または手動）で C++ 推論コアと Python 参照 (`reference/*.csv`) を突合。
batch は notebook 62 と数値一致、causal は C++↔Python 因果リファレンスで一致を確認。

```bash
g++ -std=c++17 -O2 -I include -I src/infer test/main_native.cpp src/infer/m62.cpp -o /tmp/m62
/tmp/m62 reference/<stem>.raw.csv --causal | python tools/compare_parity.py - reference/<stem>.causal.csv
```

- 既知: dataset short の note3 で 1 サンプルのみ expr ズレ（境界の float32(実機) vs float64(参照) が
  別分岐を選ぶため。論理バグではない、shuttle causal は max 0.0003 で完全一致、実機は float32 なので
  これが真の挙動）。2026-07-10 時点では `frac == HANDOFF_REMAIN 開始境界`（t=210ms, t_end=300ms,
  frac=0.70）で発生、max abs expr ≈3.48（旧: i_to_node5 の節点境界 ±0.25/±0.75 で ≈1.84〜2.2、
  同種の境界フリップ。HANDOFF_REMAIN 0.20→0.30 で境界位置が動き別ノートで顕在化）。
- **2026-07-10: d50 クラス別化・アタック偏差 乗算→加算・弓速参照の隣接ブレンド化**。
  public_release 側の進展（サステイン中央値のクラス別 d50、(i,t) 音量グリッド由来の加算 dev、
  弓速パーセンタイル参照の隣接2ノードブレンド）に private62/migration を同期。
  `db_vals`(3クラス私有計算)→`DB_ALL`(pooled, public_release 直読み), `_fac`(乗算)→`_dev`(加算,
  新設 `VOL_GRID`/`vol_db_grid`/`atk_dev`), `atk_bow_percentile`(最近傍)→`atk_bow_percentile_i`
  (隣接2ノードブレンド), t_end テーブルは d50_by_class との交点で再計算, HANDOFF_REMAIN 0.20→0.30
  （本文§2.6.2と一致）。詳細: `projects/dataAnalysis/docs/20260710-62-align-with-public.md`
- **2026-07-03: 思念モデルを Ridge → KRR(RBF) に差し替え**。48 の train_kkr と同一手順で
  export 時に学習し、生 Δt 行列＋per-k scale/dual coef/gamma を焼き込む（mean はカーネル距離で
  相殺されるため不要）。あわせて db_vals バケットキー不整合
  （0701 の "11/pA"→"pA" 変更の残骸で全クラス n=0 になっていた）を修正。
  詳細: `docs/20260703-krr-bake.md`
