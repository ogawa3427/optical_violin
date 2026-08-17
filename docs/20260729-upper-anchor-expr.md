# dB→expr を上端アンカーへ（本文 2.7.3 の実装反映）— 2026-07-29

## 動機

実演奏で「音が小さい・expr が 30 以上にほとんど行かない」という体感があり、
`public_release/scripts/4_note_traces/` の検証283ノートを現ファームの変換に通して裏を取った。

| | 旧マッピング |
|---|---|
| 全パルスの expr 中央値 | **19.1** |
| ノートピークの expr 中央値 | **30.1** |
| expr>60 の時間割合 | **0%** |

原因は `models_62.json` の `EXPR_INTERCEPT = −24.4678` が **50F 較正の旧 0dB 基準**
（expr≈4 が 0dB）をそのまま逆写像していたこと。$d_{50}=25.91$ dB がそのまま expr 19 に落ちるので、
「普通に弾いたときの音量」が設計上 expr 19 になっていた。expr 127 まで 32 dB 余らせて弾いていた計算。

本文 `writing/main.tex` 2.7.3「上限値から対応」（`%signed`）は既に**上端アンカー**を規定していた
（式 `eq:upper-anchor-expr`・表 `tab:upper-anchor-mapping`）のに、実装が追随していなかった。

## 変更内容

本文の規定:

- $L_{\max} = 50.86$ dB（表 `tab:output-range` のアタック上端 $p_{99}$、全 $i_A$ の最大）を新たな 0 dB と再定義
- $d' = d - L_{\max}$、$\mathrm{expr} = \mathrm{round}(127 \cdot 10^{d'/a})$

**ファームの C++ は 1 行も変えていない**。`m62.cpp::db_to_expr_one` の
`10^((db − EXPR_INTERCEPT)/EXPR_SLOPE)` という形のまま、焼き込む定数を置き換えるだけで等価になる:

```
127 · 10^((db − L_max)/a)  ==  10^((db − (L_max − a·log10 127))/a)
```

- `tools/export_for_mcu.py`: `EXPR_LMAX = 50.86` を導入、`db_to_expr` を上端アンカー式に変更、
  `EXPR_INTERCEPT` を `EXPR_LMAX − EXPR_SLOPE·log10(127)` の合成値として書き出す
- 再生成した定数: `EXPR_INTERCEPT −24.4678312 → −31.8142492`（`EXPR_SLOPE` は据え置き 39.2975107）
- 丸め `round()` は `src/main.cpp:145` の `(int)(expr + 0.5f)` が既に実施済み（変更不要）

## 検証

### 1. 本文の表 `tab:upper-anchor-mapping` の4点が完全再現

| 点 | $d'$ [dB] | 計算 expr | round | 本文 |
|---|---|---|---|---|
| アタック上端（$=L_{\max}$） | 0 | 127.000 | 127 | 127 |
| サステイン上端 $p_{99}$ | −1.99 | 113.022 | 113 | 113 |
| サステイン下端 $p_1$ | −50.61 | 6.545 | 7 | 7 |
| 旧 0 dB 基準 | −50.86 | 6.450 | 6 | 6 |

### 2. 変わったのは写像だけ（dB 段は不変）

新旧の `reference/*.expr.csv` / `*.causal.csv` 全 7,192 点で、
**新 expr / 旧 expr の比が min=max=1.537955**（＝ +7.3464 dB 相当、期待値と一致）。
`note_idx` と `t_ms` は完全一致、127 クランプの発生も 0 点。dB 段には一切触れていないことの証明。

### 3. parity / causal engine

- `tools/test_causal_engine.py`: 両ログとも **max_abs_expr_err = 0.0000（完全一致）**
- `run_parity.sh` batch: 両ログとも PASS（max err 0.0024 / 0.0032）
- `run_parity.sh` causal: shuttle は PASS。`dataset_...1-0` の note 3 のみ **CHECK**（max 5.36）
  → **これは本変更以前からある既知の不一致**。旧定数で走らせると同じ note 3 で 3.485 で、
  比は 5.36/3.485 = 1.538 とちょうど今回の倍率。float32/64 の丸め差が expr 倍率で拡大しただけで
  新規の退行ではない（ただし parity ゲートの絶対値しきい値 1.0 expr は今後さらに超えやすくなる）
- `pio run -e m5stack-atoms3r`: SUCCESS（RAM 7.2% / Flash 29.2%）

### 4. 検証283ノートでの効き

| | 旧 | 新 |
|---|---|---|
| 全パルス expr median | 19.1 | **29.4** |
| サステイン中央 median | 19.4 | **29.8** |
| ノートピーク median | 30.1 | **46.3** |
| ノートピーク p95 | 56.0 | **86.1** |
| ノートピーク max | 73.5 | **113.0** |
| expr>30 の時間割合 | 17% | **48%** |
| expr>60 の時間割合 | 0% | **5%** |
| 127 に張り付く時間 | 0.0% | **0.0%** |

クリップは起きず、一番強いノートで 113 に届く。副次的に**分解能も改善**する
（expr 19 付近は 1 ステップ = 0.88 dB、expr 60 付近なら 0.28 dB）。

## 残件

- **実機に焼いて音量を確認**（`pio run -e m5stack-atoms3r -t upload`）。ユーザ作業
- **$a$ の出所が公私で違う**: 本文の $a=39.21$ は公開 2.7.2 `expr_db_coeffs.json`（39.2116 /
  b_ref −24.3102）由来だが、ファームは私有 50F `coeffs_50F.json`（39.2975107 / −24.4678312）を焼いている。
  上端アンカー後の expr 差は全域 0.2 未満で実用上は無視できるが、「公開版が正典」の原則からは
  repoint すべき。別タスク（`PLAN.md`）
- **sharp のアタック直後 20 dB の穴はこれでは直らない**（相対量なので）。むしろ全体が上がる分
  目立ちやすくなる。詳細は `docs/findings.md` の 0729 エントリ
