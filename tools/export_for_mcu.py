#!/usr/bin/env python3
"""
62 パイプラインの「焼き込みテーブル」を生成し、Python 参照波形も書き出す。

- 入力: projects/dataAnalysis 配下の各モデル/データ (notebook 62_all_all_dev.ipynb と同一ソース)
- 出力:
    projects/migration/include/models_62.h   ... C++(PROGMEM) 用ヘッダ
    projects/migration/reference/models_62.json ... 同じ値の JSON (デバッグ/突合用)
    projects/migration/reference/<csv stem>.events.csv ... 入力イベント (m,note_num,offset_ms,note_idx)
    projects/migration/reference/<csv stem>.expr.csv    ... Python 参照 expr 波形 (note_idx,t_ms,expr)
    projects/migration/reference/<csv stem>.notes.csv   ... ノートメタ (note_idx,i_tend,t_end,dur_ms,n_pts)

このスクリプトは notebook の Cell1(57/softplus) / Cell3(atk_spd_pct_grid) /
Cell14(build_note_data) / Cell18(handoff) / Cell20(expr) のロジックを忠実に再現する。
バッチ版(中央 LPF)で計算しており、C++ 側もまず同じ値を出すことを parity の基準とする。
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

# --- パス解決: dataAnalysis を作業ディレクトリにする -------------------------
THIS = Path(__file__).resolve()
MIGRATION_ROOT = THIS.parent.parent              # projects/migration
DATA_ANALYSIS = (MIGRATION_ROOT.parent / "dataAnalysis").resolve()
assert DATA_ANALYSIS.exists(), f"dataAnalysis not found: {DATA_ANALYSIS}"

import os
os.chdir(DATA_ANALYSIS)
sys.path.insert(0, str(DATA_ANALYSIS))
REALDATA_SCRIPTS = (DATA_ANALYSIS.parent / "realData" / "myScripts").resolve()
sys.path.insert(0, str(REALDATA_SCRIPTS))

import pipeline as pl
import attack_reg as ar
from intVec import load_tend_table, t_end_for_i

HERE = DATA_ANALYSIS
PUBLIC_SCRIPTS = MIGRATION_ROOT.parent.parent / "public_release" / "scripts"
assert PUBLIC_SCRIPTS.exists(), f"public_release not found: {PUBLIC_SCRIPTS}"
OUT_INCLUDE = MIGRATION_ROOT / "include" / "models_62.h"
OUT_JSON = MIGRATION_ROOT / "reference" / "models_62.json"
REF_DIR = MIGRATION_ROOT / "reference"
REF_DIR.mkdir(parents=True, exist_ok=True)

# #########################################################################
# ★ チューニングつまみ — ここだけ編集して再実行すれば models_62.h が更新される
#   (窓幅・平滑・ハンドオフ等。notebook 62 の Cell1/3/14/18 由来の定数を集約)
#   ※ WINDOW_MS / T_CAP は回帰モデル(attack_reg bundle)に焼かれた値なので
#     ここでは変えられない (bundle.W_ms / bundle.T_cap がそのまま出力される)。
# #########################################################################
# --- 移動平均の窓幅 [サンプル数] ---
LPF_I_WIN      = 5     # i(t) KRR スコアの中央移動平均
LPF_SPEED_WIN  = 6     # ベース弓速の平滑
LPF_ATK_WIN    = 3     # アタック用大窓弓速の平滑
ATK_SMOOTH_WIN = 3      # アタック dB の平滑 (Cell14)
ATK_PCT_LPF    = 3      # アタック％グリッドの行方向 LPF (Cell3)
# --- アタック％グリッドの時間軸 [ms] ---
ATK_PCT_T_MAX  = 600.0  # グリッド最大時刻
ATK_PCT_DT     = 10.0   # グリッド刻み
# --- 形状/ゲイン ---
# 2026-07-10: アタック偏差は乗算fac→加算dev((i,t)音量グリッド由来)に変更したため
# ATK_DEV_GAIN は不使用 (後方互換のため定数・models_62.h への焼き込みは残置)。
ATK_DEV_GAIN   = 1.0    # 旧: アタック偏差倍率の指数 (未使用)
MIN_DUR_MS     = 150.0  # これ未満のノートは学習/出力から除外
# --- 動的ハンドオフ (アタック→ベース) (Cell18) ---
HANDOFF_REMAIN = 0.30   # t_end 手前のクロスフェード区間 (末尾割合 0..1)。本文§2.6.2「70%→100%」に合わせる
HANDOFF_SMOOTH = True   # True=smoothstep / False=線形
# --- Δt 比による即時ノートカット (causal 専用) ---
DELTA_RATIO_CUT = 2   # Δ[k]/Δ[k-1] がこれを超えたらノートを切る (0=無効)
# #########################################################################

# --- 入力パス (通常いじらない) ---
CSV_DIR       = Path("../datasetBuilder/csv")
DATASET_ROOT  = Path("../../dataset")
SKIP_DIR_NAMES = {"trash", "docs"}


def lpf(arr, win):
    arr = np.asarray(arr, dtype=float)
    if win <= 0 or len(arr) < 2:
        return arr
    w = min(int(win), len(arr))
    pad_l = (w - 1) // 2
    pad_r = w - 1 - pad_l
    padded = np.pad(arr, (pad_l, pad_r), mode="edge")
    return np.convolve(padded, np.ones(w) / w, mode="valid")


# =========================================================================
# softplus アタック曲線 (Cell1)
# =========================================================================
def _sp(x):
    return np.where(x > 30.0, x, np.log1p(np.exp(np.minimum(x, 30.0))))


def softplus_blend(t_arr, p0, p1, p2, sigma):
    t0, y0 = float(p0[0]), float(p0[1])
    t1, y1 = float(p1[0]), float(p1[1])
    t2, y2 = float(p2[0]), float(p2[1])
    s1 = (y1 - y0) / (t1 - t0) if abs(t1 - t0) > 1e-6 else 0.0
    s2 = (y2 - y1) / (t2 - t1) if abs(t2 - t1) > 1e-6 else 0.0
    sig = max(float(sigma), 0.01)
    t = np.asarray(t_arr, dtype=float)
    sp = _sp((t - t1) / sig)
    return y0 + s1 * (t - t0) + (s2 - s1) * sig * sp


_SP_FITS_PATH = HERE / "output" / "52A_merged" / "softplus_fits.json"
sp_fits = json.loads(_SP_FITS_PATH.read_text(encoding="utf-8"))
ATK_I_NODES = np.array([-1.0, -0.75, 0.0, +0.75, +1.0])
ATK_ASD_KEYS = ["pD", "D", "S", "A", "pA"]


def _softplus_db_one(t_ms, asd_key):
    vt = sp_fits[asd_key]
    p0, p1, p2, sig = vt["p0"], vt["p1"], vt["p2"], vt["sigma_ms"]
    return softplus_blend(np.asarray(t_ms, dtype=float), p0, p1, p2, sig)


def atk_softplus_db_i(t_ms, i_val):
    t = np.asarray(t_ms, dtype=float)
    ys = np.stack([_softplus_db_one(t, k) for k in ATK_ASD_KEYS], axis=0)
    return np.array([float(np.interp(i_val, ATK_I_NODES, ys[:, j])) for j in range(len(t))])


# =========================================================================
# public_release 由来の正典データ読み込み (2026-07-10)
#   private 側で db_buckets / d50 / (i,t)音量グリッド を再計算せず、public_release の
#   出力(json/npz)をそのまま焼き込む。理由: private dataAnalysis と public_release の
#   元データパスが分岐しており、同じフィルタを再実装しても数値一致は保証できない。
#   数値そのものを正典として読み込めばズレが構造的に発生しない
#   (詳細: projects/dataAnalysis/docs/20260710-62-align-with-public.md)。
#   - DB_ALL_PUBLIC : サステイン出力用 pooled dB CDF
#                     (2.5.3_percentile_mapping, y0<-55dB 除外済み, 3クラス n=189)
#   - D50_BY_CLASS  : t_end 交点計算用のクラス別 d50
#                     (2.5.2_sustain_volume/sustain_d50.json, 5クラス, y0除外済み)
#   - VOLGRID_*     : アタック偏差 dev(p,i,t) 用の (i,t) 音量パーセンタイルグリッド
#                     (2.5.2_sustain_volume/volume_timegrid_5class.npz, 2.5.2B)
# =========================================================================
print("[1/6] loading public_release data (db pooled / d50_by_class / vol grid)...")

_pctmap_pub = json.loads(
    (PUBLIC_SCRIPTS / "2.5.3_percentile_mapping" / "output" / "percentile_mapping.json")
    .read_text(encoding="utf-8"))
DB_ALL_PUBLIC = np.asarray(_pctmap_pub["volume_db_pooled_sorted"], dtype=float)

_sustain_d50_pub = json.loads(
    (PUBLIC_SCRIPTS / "2.5.2_sustain_volume" / "output" / "sustain_d50.json")
    .read_text(encoding="utf-8"))
D50_BY_CLASS = {k: float(v) for k, v in _sustain_d50_pub["d50_by_class"].items()}

_volgrid_pub = np.load(
    PUBLIC_SCRIPTS / "2.5.2_sustain_volume" / "output" / "volume_timegrid_5class.npz")
VOLGRID_T_GRID = _volgrid_pub["t_grid"].astype(float)
VOLGRID_P_AXIS = _volgrid_pub["p_axis"].astype(float)
VOLGRID_I_NODES = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])
VOLGRID_ASD_BY_I = {-1.0: "pD", -0.5: "D", 0.0: "S", 0.5: "A", 1.0: "pA"}
VOLGRID_BY_ASD = {c: _volgrid_pub[f"grid_{c}"].astype(float) for c in ["pD", "D", "S", "A", "pA"]}

print(f"    db_all pooled n={len(DB_ALL_PUBLIC)}")
print("    d50_by_class:", {k: round(v, 2) for k, v in D50_BY_CLASS.items()})
print(f"    vol grid: t 0..{VOLGRID_T_GRID[-1]:.0f}ms  p_axis n={len(VOLGRID_P_AXIS)}")


# =========================================================================
# 61 — attack bow-speed percentile grid (Cell3) -> atk_spd_pct_grid
# =========================================================================
print("[2/6] building atk_spd_pct_grid (61)...")
_atk_bundle = ar.load_attack_reg(ar.REG_TEST_MODEL_PATH)
seg_params = pl.load_seg_params()

ATK_PCT_T_GRID = np.arange(0.0, ATK_PCT_T_MAX + ATK_PCT_DT, ATK_PCT_DT)  # 窓幅は先頭ブロック参照
_ATK_P_AXIS = np.linspace(0.0, 1.0, 101)
ATK_I_NODES5 = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])

LABELED_CSVS = [
    (CSV_DIR / "20260610155734-rec-shuttle_60_S_1.csv",  0.0),
    (CSV_DIR / "20260610155341-rec-shuttle_60_A_1.csv",  1.0),
    (CSV_DIR / "20260610155141-rec-shuttle_60_A_1.csv", -1.0),
]


# =========================================================================
# KRR（仇親仇）学習 — notebook 48 の train_kkr と同一手順
#   prefix k ごとに StandardScaler + KernelRidge(RBF) を GridSearchCV で学習。
#   焼き込みは「生 pad 済み Δt 行列 + per-k scale + per-k dual coef + per-k gamma」。
#   StandardScaler の mean はカーネル距離で相殺されるため保存不要:
#     ||(x-m)/s - (X_i-m)/s||^2 = Σ_j ((x_j - X_ij)/s_j)^2
# =========================================================================
print("[0/6] training KRR bank (48 と同一手順)...")
from sklearn.kernel_ridge import KernelRidge
from sklearn.model_selection import GridSearchCV
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

KRR_CONFIG = dict(ar.DEFAULT_CONFIG)
_train_notes = ar.load_labeled_notes_from_csvs(LABELED_CSVS)
_W_TRAIN = int(KRR_CONFIG["window_ms"])
T_cap_krr = ar._compute_t_cap(_train_notes, _W_TRAIN, int(KRR_CONFIG["max_prefix_steps"]))
_pairs = ar.build_training_pairs(
    _train_notes, T_cap=T_cap_krr, window_ms=_W_TRAIN,
    feature_mode=str(KRR_CONFIG["feature_mode"]), use_log1p=bool(KRR_CONFIG["use_log1p"]))
_KRR_GRID = {"kkr__alpha": [0.01, 0.1, 1.0, 10.0], "kkr__gamma": [0.001, 0.01, 0.1, 1.0]}

krr_scale, krr_alpha, krr_gamma = [], [], []
krr_X = None          # (n_train, T_cap) 生 pad 済み Δt 行列（全 k で共通の行順）
_krr_pipes = []
for _k, (_X, _y) in enumerate(_pairs):
    pipe = Pipeline([("scaler", StandardScaler()), ("kkr", KernelRidge(kernel="rbf"))])
    if len(_y) < 5:
        pipe.set_params(kkr__alpha=1.0, kkr__gamma=0.1)
        pipe.fit(_X if len(_y) > 0 else np.zeros((1, max(_X.shape[1], 1))),
                 _y if len(_y) > 0 else np.array([0.0]))
    else:
        gs = GridSearchCV(pipe, _KRR_GRID, cv=min(3, len(_y)),
                          scoring="neg_mean_absolute_error", n_jobs=-1)
        gs.fit(_X, _y)
        pipe = gs.best_estimator_
    sc = pipe.named_steps["scaler"]
    kk = pipe.named_steps["kkr"]
    krr_scale.append([float(v) for v in sc.scale_])
    krr_alpha.append([float(v) for v in np.ravel(kk.dual_coef_)])
    krr_gamma.append(float(kk.gamma))
    _krr_pipes.append(pipe)
    if _k == T_cap_krr - 1:
        krr_X = np.asarray(_X, dtype=float)

KRR_N_TRAIN = krr_X.shape[0]
assert all(len(a) == KRR_N_TRAIN for a in krr_alpha), "prefix ごとの学習行数が不一致"
print(f"    n_train={KRR_N_TRAIN}  T_cap={T_cap_krr}")


def krr_score_prefix(deltas, k):
    """prefix k の KRR スコア（C++ score_at_k と同一の式・float64）。"""
    k = int(np.clip(k, 0, T_cap_krr - 1))
    d = np.asarray(deltas, dtype=float)[: k + 1]
    s = np.asarray(krr_scale[k], dtype=float)
    diff = (d[None, :] - krr_X[:, : k + 1]) / s[None, :]
    kern = np.exp(-krr_gamma[k] * np.einsum("ij,ij->i", diff, diff))
    return float(np.clip(kern @ np.asarray(krr_alpha[k]), -1.0, 1.0))


def krr_prefix_scores(obs):
    """obs (T_cap,1) → 全 prefix のスコア列（prefix_scores_reg 互換）。"""
    d = np.asarray(obs, dtype=float).ravel()
    return np.array([krr_score_prefix(d, k) for k in range(len(d))])


# 焼き込み用の式が sklearn pipeline と一致するか検証
for _k in (0, 4, T_cap_krr // 2, T_cap_krr - 1):
    _x = krr_X[0, : _k + 1]
    _ref = float(np.clip(_krr_pipes[_k].predict(_x.reshape(1, -1))[0], -1.0, 1.0))
    _got = krr_score_prefix(_x, _k)
    assert abs(_ref - _got) < 1e-9, f"KRR 再構成不一致 k={_k}: {_ref} vs {_got}"
print("    KRR 再構成チェック OK")


def i_to_node5(i_val):
    iv = float(np.clip(i_val, -1.0, 1.0))
    return float(ATK_I_NODES5[int(np.argmin(np.abs(ATK_I_NODES5 - iv)))])


def _note_i_tend(note):
    obs, offs_win = ar.note_to_obs(note, _atk_bundle)
    if len(offs_win) < 2:
        return None
    reg_sk = lpf(krr_prefix_scores(obs), LPF_I_WIN)
    k_last = min(obs.shape[0] - 1, len(offs_win) - 2)
    return float(reg_sk[k_last])


def bow_speed_curve(note):
    evs = sorted([e for e in note.get("m_events", []) if int(e.get("note_num", 0)) != 0],
                 key=lambda e: int(e["offset_ms"]))
    if len(evs) < 2:
        return None, None
    offs = np.array([int(e["offset_ms"]) for e in evs], dtype=float)
    spd = 1000.0 / np.maximum(np.diff(offs), 1e-3)
    return (offs[:-1] + offs[1:]) / 2.0, spd


def _interp_curve(tmid, spd):
    if tmid is None or len(tmid) < 2:
        return None
    return np.interp(ATK_PCT_T_GRID, tmid, spd, left=np.nan, right=np.nan)


atk_spd_buckets = {n: [] for n in ATK_I_NODES5}


def _add_note(note):
    if float(note["end_ms"] - note["start_ms"]) < MIN_DUR_MS:
        return
    c = _interp_curve(*bow_speed_curve(note))
    if c is None:
        return
    it = _note_i_tend(note)
    if it is None:
        return
    atk_spd_buckets[i_to_node5(it)].append(c)


def _notes_from_csv(csv_path):
    vec_df = pl.preprocess_csv_to_m_event_vectors_df(csv_path)
    labs = pl.detect_start_labels(vec_df, seg_params)
    notes, _ = pl.segment_notes_from_labels(
        vec_df, labs, csv_path.name, min_m_events=int(seg_params["min_m_events_per_note"]))
    return notes


for cp, _it in LABELED_CSVS:
    for note in _notes_from_csv(cp):
        _add_note(note)

for csv_path in sorted(DATASET_ROOT.rglob("*.csv")):
    if any(s in SKIP_DIR_NAMES for s in csv_path.parts):
        continue
    anno_path = csv_path.with_suffix(".anno.json")
    if not anno_path.exists():
        continue
    if not json.loads(anno_path.read_text(encoding="utf-8")).get("attack"):
        continue
    try:
        notes = _notes_from_csv(csv_path)
    except Exception:
        continue
    for note in notes:
        _add_note(note)

atk_spd_pct_grid = {}
for n, arrs in atk_spd_buckets.items():
    if not arrs:
        atk_spd_pct_grid[n] = None
        continue
    stack = np.stack(arrs, axis=0)
    valid = np.isfinite(stack).sum(axis=0) >= 2
    g = np.full((len(_ATK_P_AXIS), stack.shape[1]), np.nan)
    if valid.any():
        g[:, valid] = np.nanpercentile(stack[:, valid], _ATK_P_AXIS * 100.0, axis=0)
    if ATK_PCT_LPF > 0:
        g = np.apply_along_axis(lambda r: lpf(r, ATK_PCT_LPF), 1, g)
    atk_spd_pct_grid[n] = g
    print(f"    i={n:+.1f}  n={len(arrs)}")


def atk_bow_percentile(i_node, t_ms, speed):
    g = atk_spd_pct_grid.get(i_node)
    if g is None:
        return float("nan")
    tt = float(np.clip(t_ms, ATK_PCT_T_GRID[0], ATK_PCT_T_GRID[-1]))
    jf = np.interp(tt, ATK_PCT_T_GRID, np.arange(len(ATK_PCT_T_GRID)))
    j0 = int(np.floor(jf)); j1 = min(j0 + 1, g.shape[1] - 1); w = jf - j0
    col = g[:, j0] * (1 - w) + g[:, j1] * w
    if not np.isfinite(col).all():
        return float("nan")
    return float(np.interp(speed, col, _ATK_P_AXIS, left=0.0, right=1.0))


def atk_bow_percentile_i(i_val, t_ms, speed):
    """連続 i 版: 隣接2ノードの分布でそれぞれ p を求め、i で線形ブレンド (0-1)。
    最近傍割当 (i_to_node5) だとクラス境界 ±0.25/±0.75 で参照分布が不連続に切り替わり、
    同じ弓速でも p が跳ぶ問題を解消する (private 62 と同一, 2026-07-07 決定)。"""
    iv = float(np.clip(i_val, -1.0, 1.0))
    k = int(np.clip(np.searchsorted(ATK_I_NODES5, iv, side="right") - 1,
                    0, len(ATK_I_NODES5) - 2))
    n0, n1 = float(ATK_I_NODES5[k]), float(ATK_I_NODES5[k + 1])
    w = (iv - n0) / (n1 - n0)
    p0 = atk_bow_percentile(n0, t_ms, speed)
    p1 = atk_bow_percentile(n1, t_ms, speed)
    if not np.isfinite(p0):
        return p1
    if not np.isfinite(p1):
        return p0
    return float((1.0 - w) * p0 + w * p1)


# =========================================================================
# 音量パーセンタイル写像 (build_note_data が使う)
#   - percentile_to_db_i: サステイン用。pooled CDF (DB_ALL_PUBLIC, y0除外済み) 直読み。
#     2026-07-07 決定でクラス依存を撤廃済み (i_val は互換のため引数だけ残す, 未使用)。
#   - vol_db_grid / atk_dev: アタック用。(i,t) 音量グリッド (VOLGRID_*, 2.5.2B) から
#     dev(p,i,t) = vol(p,i,t) - vol(0.5,i,t) を返す (加算オフセット、2026-07-10 決定)。
# =========================================================================
def percentile_to_db_i(p, i_val=None):
    arr = DB_ALL_PUBLIC
    idx = float(np.clip(p, 0.0, 1.0)) * (len(arr) - 1)
    lo = int(idx); hi = min(lo + 1, len(arr) - 1)
    return float(arr[lo] + (idx - lo) * (arr[hi] - arr[lo]))


def _vol_db_node(asd_short, p, t_ms):
    g = VOLGRID_BY_ASD.get(asd_short)
    if g is None:
        return float("nan")
    tt = float(np.clip(t_ms, VOLGRID_T_GRID[0], VOLGRID_T_GRID[-1]))
    jf = np.interp(tt, VOLGRID_T_GRID, np.arange(len(VOLGRID_T_GRID)))
    j0 = int(np.floor(jf)); j1 = min(j0 + 1, g.shape[1] - 1); w = jf - j0
    col = g[:, j0] * (1 - w) + g[:, j1] * w
    if not np.isfinite(col).all():
        return float("nan")
    return float(np.interp(p, VOLGRID_P_AXIS, col))


def vol_db_grid(p, i_val, t_ms):
    """(p, i, t) -> 実演奏音量 dB。(i,t) グリッド、i隣接2ノードブレンド。"""
    iv = float(np.clip(i_val, -1.0, 1.0))
    k = int(np.clip(np.searchsorted(VOLGRID_I_NODES, iv, side="right") - 1,
                    0, len(VOLGRID_I_NODES) - 2))
    n0, n1 = float(VOLGRID_I_NODES[k]), float(VOLGRID_I_NODES[k + 1])
    w = (iv - n0) / (n1 - n0)
    d0 = _vol_db_node(VOLGRID_ASD_BY_I[n0], p, t_ms)
    d1 = _vol_db_node(VOLGRID_ASD_BY_I[n1], p, t_ms)
    if not np.isfinite(d0):
        return d1
    if not np.isfinite(d1):
        return d0
    return float((1.0 - w) * d0 + w * d1)


def atk_dev(p, i_val, t_ms):
    """アタック偏差 dev(t) = vol(p,i,t) - vol(0.5,i,t)。p が nan なら偏差なし(0.0)。"""
    if not np.isfinite(p):
        return 0.0
    d = vol_db_grid(p, i_val, t_ms)
    d50 = vol_db_grid(0.5, i_val, t_ms)
    if not np.isfinite(d) or not np.isfinite(d50):
        return 0.0
    return d - d50


# =========================================================================
# t_end/t_sw 交点 (Cell3, 2026-07-10 改訂): アタックテンプレートとクラス別 d50(i) の交点。
# d50 は D50_BY_CLASS (public_release, y0<-55dB除外済み)。旧 t_end (seed の手動クリップ値) は
# 交点探索の区間限定と上限にのみ使う (min(交点, 旧値))。private 62 の Cell3 と同一アルゴリズム。
# =========================================================================
_tend_table_orig = load_tend_table()


def _tend_crossing_for_class(asd_key, i_val, t_end_orig):
    v = sp_fits[asd_key]
    t1, y1 = v["p1"]; t2, y2 = v["p2"]
    d50 = D50_BY_CLASS[asd_key]
    lo, hi = (y1, y2) if y1 <= y2 else (y2, y1)
    if not (lo <= d50 <= hi):
        return float(t_end_orig)
    tg = np.linspace(t1, t2, max(int(abs(t2 - t1)) * 4, 200))
    yg = _softplus_db_one(tg, asd_key)
    tc = float(tg[np.argmin(np.abs(yg - d50))])
    return min(tc, float(t_end_orig))


_tend_i_crossing = np.array(ATK_I_NODES, dtype=float)
_tend_t_crossing = np.array([
    _tend_crossing_for_class(k, i, t_end_for_i(i, _tend_table_orig))
    for k, i in zip(ATK_ASD_KEYS, ATK_I_NODES)
], dtype=float)
tend_table = (_tend_i_crossing, _tend_t_crossing)
print("[2.5/6] t_end: orig vs crossing (d50_by_class 版)")
for i, k in zip(ATK_I_NODES, ATK_ASD_KEYS):
    te0 = t_end_for_i(i, _tend_table_orig)
    tec = t_end_for_i(i, tend_table)
    print(f"    i={i:+.1f} ({k}):  orig={te0:6.1f}ms  crossing={tec:6.1f}ms  delta={tec - te0:+6.1f}ms")


# =========================================================================
# build_note_data (Cell14)
# =========================================================================
print("[3/6] preparing build_note_data / handoff / expr ...")
bundle = ar.load_attack_reg(ar.REG_TEST_MODEL_PATH)


def build_note_data(note, gt_label):
    obs, offs_win = ar.note_to_obs(note, bundle)
    if len(offs_win) < 2:
        return None
    reg_sk_raw = krr_prefix_scores(obs)
    reg_sk = lpf(reg_sk_raw, LPF_I_WIN)
    k_last = min(obs.shape[0] - 1, len(offs_win) - 2)
    i_tend = float(reg_sk[k_last])
    t_end = t_end_for_i(i_tend, tend_table)
    dur_ms = float(note["end_ms"] - note["start_ms"])

    _kmax = min(len(offs_win) - 1, len(reg_sk))
    t_i = np.array([float(offs_win[k + 1]) for k in range(_kmax) if offs_win[k + 1] <= t_end])
    i_ts = np.array([float(reg_sk[k]) for k in range(len(t_i))])
    if len(t_i) > 0 and t_i[-1] < t_end:
        t_i = np.append(t_i, t_end); i_ts = np.append(i_ts, i_tend)

    evs = sorted([e for e in note.get("m_events", []) if int(e.get("note_num", 0)) != 0],
                 key=lambda e: int(e["offset_ms"]))

    if len(evs) >= 2:
        all_offs = np.array([int(e["offset_ms"]) for e in evs], dtype=float)
        all_spd_raw = 1000.0 / np.maximum(np.diff(all_offs), 1e-3)
        all_spd = lpf(all_spd_raw, LPF_SPEED_WIN)
        t_base = all_offs[1:]
        pct_base = np.array([atk_bow_percentile_i(i_tend, float(t), float(s))
                             for t, s in zip(t_base, all_spd)])
        vol_base = np.array([percentile_to_db_i(p, i_tend) if np.isfinite(p) else np.nan
                             for p in pct_base])
    else:
        t_base = vol_base = np.array([])

    if len(evs) >= 2:
        _spd_atk = lpf(1000.0 / np.maximum(np.diff(all_offs), 1e-3), LPF_ATK_WIN)
        _t_spd = (all_offs[:-1] + all_offs[1:]) / 2.0
    else:
        _spd_atk, _t_spd = np.array([]), np.array([])

    def _spd_at(t):
        if len(_spd_atk) < 1:
            return float("nan")
        return float(np.interp(t, _t_spd, _spd_atk, left=_spd_atk[0], right=_spd_atk[-1]))

    atk_ks = [k for k in range(min(len(offs_win), len(reg_sk))) if offs_win[k] <= t_end]
    t_atk = np.array([float(offs_win[k]) for k in atk_ks]) if atk_ks else np.array([])
    i_atk = np.array([float(reg_sk[k]) for k in atk_ks]) if atk_ks else np.array([])
    if len(t_atk) > 0 and t_atk[-1] < t_end:
        t_atk = np.append(t_atk, t_end); i_atk = np.append(i_atk, i_tend)

    db_atk_raw = np.array([float(atk_softplus_db_i([t], ik)[0]) for t, ik in zip(t_atk, i_atk)])

    # 2026-07-10: 旧・乗算 fac (percentile_to_db_i の pooled 分布比) を廃止し、
    # public_release 2.5.3B と同じ加算式 out=center+dev に統一 (dev は (i,t) 音量グリッド由来)。
    def _dev(t, ik):
        p = atk_bow_percentile_i(ik, t, _spd_at(t))
        return atk_dev(p, ik, t)

    dev_atk = np.array([_dev(float(t), float(ik)) for t, ik in zip(t_atk, i_atk)])
    db_atk = lpf(db_atk_raw + dev_atk, ATK_SMOOTH_WIN)
    if len(t_atk) > 0:
        if t_atk[0] > 0:
            t_atk = np.concatenate([[0.0], t_atk])
            db_atk = np.concatenate([[0.0], db_atk])
            db_atk_raw = np.concatenate([[0.0], db_atk_raw])
        else:
            db_atk[0] = 0.0; db_atk_raw[0] = 0.0

    base_tend = (float(np.interp(t_end, t_base, vol_base)) if len(t_base) >= 2 else np.nan)
    sust_mask = t_base > t_end if len(t_base) else np.array([], dtype=bool)
    t_sust = np.concatenate([[t_end], t_base[sust_mask], [dur_ms]]) if len(t_base) else np.array([t_end, dur_ms])
    db_sust = np.concatenate([[base_tend if np.isfinite(base_tend) else 0.0],
                              vol_base[sust_mask], [0.0]]) if len(t_base) else np.array([0.0, 0.0])

    return dict(gt_label=gt_label, i_tend=i_tend, t_end=t_end, dur_ms=dur_ms,
                t_i=t_i, i_ts=i_ts, t_base=t_base, vol_base=vol_base,
                t_atk=t_atk, db_atk=db_atk, t_sust=t_sust, db_sust=db_sust)


# =========================================================================
# 動的ハンドオフ (Cell18)  ※ HANDOFF_REMAIN / HANDOFF_SMOOTH は先頭ブロック参照
# =========================================================================
def _handoff_step(w):
    w = np.clip(w, 0.0, 1.0)
    return w * w * (3.0 - 2.0 * w) if HANDOFF_SMOOTH else w


def apply_dynamic_handoff(nd, remain=HANDOFF_REMAIN):
    t_atk = nd["t_atk"]; db_atk = nd["db_atk"]
    if len(t_atk) < 2 or len(nd["t_base"]) < 2:
        return db_atk.copy()
    i_at = np.interp(t_atk, nd["t_i"], nd["i_ts"], left=nd["i_ts"][0], right=nd["i_ts"][-1])
    base_at = np.interp(t_atk, nd["t_base"], nd["vol_base"])
    out = db_atk.copy()
    start = 1.0 - remain
    for k, t in enumerate(t_atk):
        te = t_end_for_i(float(i_at[k]), tend_table)
        if te <= 1.0:
            continue
        frac = t / te
        if frac <= start:
            continue
        w = _handoff_step((frac - start) / max(remain, 1e-6))
        if np.isfinite(base_at[k]):
            out[k] = (1.0 - w) * db_atk[k] + w * base_at[k]
    return out


# =========================================================================
# expr (Cell20)
# =========================================================================
_coef_path = Path("output/50F_fit/coeffs_50F.json")
_c = json.loads(_coef_path.read_text(encoding="utf-8"))
EXPR_SLOPE = float(_c["slope"])
EXPR_MIN, EXPR_MAX = 0.0, 127.0

# --- 上端アンカー（本文 2.7.3「上限値から対応」, 式 eq:upper-anchor-expr）-------------
# システム値域の上限 L_max（表 tab:output-range のアタック上端 p99, 全 i_A の最大）を
# 新たな 0 dB と再定義し、expr=127 を対応させる:
#     d' = db - L_max,   expr = round(127 * 10^(d'/a))
# 旧実装は 50F 較正の 0dB 基準（expr≈4 が 0dB）をそのまま逆写像していたため、
# d50=25.91dB が expr≈19 にしか届かず全体が過小音量だった（PLAN.md 2026-07-29）。
#
# C++ 側 `db_to_expr_one` は `10^((db - EXPR_INTERCEPT)/EXPR_SLOPE)` の形のままで等価に
# なるので、焼き込む EXPR_INTERCEPT を下式で置き換える（ファームのコード変更は不要）:
#     127 * 10^((db - L_max)/a) == 10^((db - (L_max - a*log10(127)))/a)
EXPR_LMAX = 50.86   # dB, public_release 2.7.3 output_range.json: range_summary.attack_p99_worst.max
EXPR_INTERCEPT = EXPR_LMAX - EXPR_SLOPE * np.log10(EXPR_MAX)


def db_to_expr(db):
    e = EXPR_MAX * np.power(10.0, (np.asarray(db, float) - EXPR_LMAX) / EXPR_SLOPE)
    return np.clip(e, EXPR_MIN, EXPR_MAX)


def note_expr_waveform(nd):
    db_atk_h = apply_dynamic_handoff(nd)
    t_all = np.concatenate([nd["t_atk"], nd["t_sust"]])
    db_all = np.concatenate([db_atk_h, nd["db_sust"]])
    order = np.argsort(t_all, kind="stable")
    return t_all[order], db_to_expr(db_all[order])


# =========================================================================
# 焼き込みテーブル dump
# =========================================================================
print("[4/6] packing KRR bank ...")
T_cap = T_cap_krr
assert int(bundle.T_cap) == T_cap, f"bundle.T_cap={bundle.T_cap} != KRR T_cap={T_cap}"
assert int(bundle.W_ms) == _W_TRAIN, f"bundle.W_ms={bundle.W_ms} != KRR window={_W_TRAIN}"

tend_i, tend_t = tend_table

# softplus 5 クラス (ATK_ASD_KEYS 順)
softplus_p1 = [[float(sp_fits[k]["p1"][0]), float(sp_fits[k]["p1"][1])] for k in ATK_ASD_KEYS]
softplus_p2 = [[float(sp_fits[k]["p2"][0]), float(sp_fits[k]["p2"][1])] for k in ATK_ASD_KEYS]
softplus_p0 = [[float(sp_fits[k]["p0"][0]), float(sp_fits[k]["p0"][1])] for k in ATK_ASD_KEYS]
softplus_sigma = [float(sp_fits[k]["sigma_ms"]) for k in ATK_ASD_KEYS]

# db_all: pooled サステイン出力用 CDF (public_release, y0<-55dB除外済み)
db_all_pooled = [float(x) for x in DB_ALL_PUBLIC]

# atk_spd_pct_grid: 5 nodes x 101 x 61, NaN sentinel -> 9.99e9
NAN_SENT = 9.99e9
N_T = len(ATK_PCT_T_GRID)
N_P = len(_ATK_P_AXIS)
pct_grid_flat = []          # node-major: [node][p][t]
pct_grid_present = []
for n in ATK_I_NODES5:
    g = atk_spd_pct_grid[float(n)]
    if g is None:
        pct_grid_present.append(0)
        pct_grid_flat.append([[NAN_SENT] * N_T for _ in range(N_P)])
    else:
        pct_grid_present.append(1)
        pct_grid_flat.append([[float(v) if np.isfinite(v) else NAN_SENT for v in g[p]] for p in range(N_P)])

# volgrid: (i,t) 音量パーセンタイルグリッド (2.5.2B, アタック偏差 dev(p,i,t) 用)
# node-major: [node(5)][p(101)][t(81)]
VOL_N_T = len(VOLGRID_T_GRID)
VOL_N_P = len(VOLGRID_P_AXIS)
volgrid_flat = []
for n in VOLGRID_I_NODES:
    g = VOLGRID_BY_ASD[VOLGRID_ASD_BY_I[float(n)]]
    volgrid_flat.append([[float(v) if np.isfinite(v) else NAN_SENT for v in g[p]] for p in range(VOL_N_P)])

models = dict(
    meta=dict(source="export_for_mcu.py", T_cap=int(T_cap), window_ms=int(bundle.W_ms),
              feature_mode=bundle.feature_mode, use_log1p=bool(bundle.use_log1p),
              clip_output=bool(bundle.clip_output)),
    consts=dict(
        LPF_I_WIN=LPF_I_WIN, LPF_SPEED_WIN=LPF_SPEED_WIN, LPF_ATK_WIN=LPF_ATK_WIN,
        ATK_SMOOTH_WIN=ATK_SMOOTH_WIN, ATK_DEV_GAIN=ATK_DEV_GAIN, ATK_PCT_LPF=ATK_PCT_LPF,
        HANDOFF_REMAIN=HANDOFF_REMAIN, HANDOFF_SMOOTH=int(HANDOFF_SMOOTH),
        DELTA_RATIO_CUT=DELTA_RATIO_CUT,
        MIN_DUR_MS=MIN_DUR_MS, ATK_PCT_T_MAX=ATK_PCT_T_MAX, ATK_PCT_DT=ATK_PCT_DT,
        N_T=N_T, N_P=N_P, NAN_SENT=NAN_SENT,
        EXPR_SLOPE=EXPR_SLOPE, EXPR_INTERCEPT=float(EXPR_INTERCEPT),
        EXPR_LMAX=EXPR_LMAX, EXPR_MIN=EXPR_MIN, EXPR_MAX=EXPR_MAX,
    ),
    seg_params={k: float(v) for k, v in seg_params.items()},
    krr=dict(T_cap=int(T_cap), n_train=int(KRR_N_TRAIN), gamma=krr_gamma,
             scale=krr_scale, alpha=krr_alpha,
             X=[[float(v) for v in row] for row in krr_X]),
    tend=dict(i=[float(x) for x in tend_i], t=[float(x) for x in tend_t]),
    softplus=dict(keys=ATK_ASD_KEYS, i_nodes=[float(x) for x in ATK_I_NODES],
                  p0=softplus_p0, p1=softplus_p1, p2=softplus_p2, sigma=softplus_sigma),
    db_all=db_all_pooled,
    pct_grid=dict(i_nodes=[float(x) for x in ATK_I_NODES5], present=pct_grid_present,
                  t_grid=[float(x) for x in ATK_PCT_T_GRID],
                  p_axis=[float(x) for x in _ATK_P_AXIS], data=pct_grid_flat),
    vol_grid=dict(i_nodes=[float(x) for x in VOLGRID_I_NODES],
                  t_grid=[float(x) for x in VOLGRID_T_GRID],
                  p_axis=[float(x) for x in VOLGRID_P_AXIS], data=volgrid_flat),
)

OUT_JSON.write_text(json.dumps(models), encoding="utf-8")
print(f"    wrote {OUT_JSON} ({OUT_JSON.stat().st_size/1024:.0f} KB)")


# =========================================================================
# C ヘッダ生成
# =========================================================================
print("[5/6] writing models_62.h ...")


def cf(v):
    """有効な C++ float リテラル文字列。小数点/指数が無ければ .0 を足す。"""
    s = f"{float(v):.9g}"
    if not any(ch in s for ch in (".", "e", "E", "n", "i")):  # nan/inf 対策含む
        s += ".0"
    return s + "f"


def c_arr_1d(values, per_line=8):
    out = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join(cf(v) for v in values[i:i + per_line])
        out.append("  " + chunk + ",")
    return "\n".join(out)


lines = []
lines.append("// AUTO-GENERATED by projects/migration/tools/export_for_mcu.py — DO NOT EDIT")
lines.append("// 62 パイプライン 焼き込みテーブル (AtomS3R 実機移植用)")
lines.append("#pragma once")
lines.append("#include <stdint.h>")
lines.append("")
lines.append("namespace m62 {")
lines.append("")
# scalar consts
c = models["consts"]
lines.append(f"constexpr int   T_CAP          = {int(T_cap)};")
lines.append(f"constexpr int   WINDOW_MS      = {int(bundle.W_ms)};")
lines.append(f"constexpr int   LPF_I_WIN      = {LPF_I_WIN};")
lines.append(f"constexpr int   LPF_SPEED_WIN  = {LPF_SPEED_WIN};")
lines.append(f"constexpr int   LPF_ATK_WIN    = {LPF_ATK_WIN};")
lines.append(f"constexpr int   ATK_SMOOTH_WIN = {ATK_SMOOTH_WIN};")
lines.append(f"constexpr int   ATK_PCT_LPF    = {ATK_PCT_LPF};")
lines.append(f"constexpr float ATK_DEV_GAIN   = {cf(ATK_DEV_GAIN)};")
lines.append(f"constexpr float HANDOFF_REMAIN = {cf(HANDOFF_REMAIN)};")
lines.append(f"constexpr int   HANDOFF_SMOOTH = {int(HANDOFF_SMOOTH)};")
lines.append(f"constexpr float DELTA_RATIO_CUT = {cf(DELTA_RATIO_CUT)};")
lines.append(f"constexpr float MIN_DUR_MS     = {cf(MIN_DUR_MS)};")
lines.append(f"constexpr float ATK_PCT_T_MAX  = {cf(ATK_PCT_T_MAX)};")
lines.append(f"constexpr float ATK_PCT_DT     = {cf(ATK_PCT_DT)};")
lines.append(f"constexpr int   N_T            = {N_T};")
lines.append(f"constexpr int   N_P            = {N_P};")
lines.append(f"constexpr float NAN_SENT       = {cf(NAN_SENT)};")
lines.append(f"constexpr float EXPR_SLOPE     = {cf(EXPR_SLOPE)};")
lines.append("// 上端アンカー: L_max を 0dB と再定義し expr=127 を対応（本文 2.7.3, eq:upper-anchor-expr）")
lines.append(f"constexpr float EXPR_LMAX      = {cf(EXPR_LMAX)};")
lines.append(f"// EXPR_INTERCEPT = EXPR_LMAX - EXPR_SLOPE*log10(127) （下式を等価に保つための合成値）")
lines.append(f"constexpr float EXPR_INTERCEPT = {cf(EXPR_INTERCEPT)};")
lines.append(f"constexpr float EXPR_MIN       = {cf(EXPR_MIN)};")
lines.append(f"constexpr float EXPR_MAX       = {cf(EXPR_MAX)};")
lines.append("")

# seg params
sp = models["seg_params"]
lines.append("struct SegParams {")
for k in sp:
    lines.append(f"  float {k};")
lines.append("};")
seg_init = ", ".join(cf(sp[k]) for k in sp)
lines.append(f"constexpr SegParams SEG = {{ {seg_init} }};")
lines.append("// seg field order: " + ", ".join(sp.keys()))
lines.append("")

# KRR bank:
#   KRR_X      [n][T_cap]  生 pad 済み Δt 行列 (全 k 共通)
#   KRR_SCALE  三角バンク  per-k StandardScaler.scale_ (offsets[k] = k*(k+1)/2)
#   KRR_ALPHA  [k][n]      per-k dual coef
#   KRR_GAMMA  [k]         per-k RBF gamma
# mean は距離で相殺されるため不要 (score_at_k 参照)。
flat_scale, offsets = [], []
acc = 0
for k in range(T_cap):
    offsets.append(acc)
    flat_scale.extend(krr_scale[k])
    acc += len(krr_scale[k])
total_tri = acc
lines.append(f"constexpr int KRR_N_TRAIN = {KRR_N_TRAIN};")
lines.append(f"constexpr int KRR_TRI_TOTAL = {total_tri};")
lines.append(f"constexpr int KRR_OFFSET[{T_cap}] = {{")
lines.append("\n".join(
    "  " + ", ".join(str(v) for v in offsets[i:i+16]) + "," for i in range(0, len(offsets), 16)))
lines.append("};")
lines.append(f"constexpr float KRR_SCALE[{total_tri}] = {{")
lines.append(c_arr_1d(flat_scale))
lines.append("};")
lines.append(f"constexpr float KRR_GAMMA[{T_cap}] = {{")
lines.append(c_arr_1d(krr_gamma))
lines.append("};")
flat_alpha = [v for row in krr_alpha for v in row]
lines.append(f"// index = k*KRR_N_TRAIN + i")
lines.append(f"constexpr float KRR_ALPHA[{T_cap * KRR_N_TRAIN}] = {{")
lines.append(c_arr_1d(flat_alpha))
lines.append("};")
flat_X = [v for row in krr_X for v in row]
lines.append(f"// index = i*T_CAP + j")
lines.append(f"constexpr float KRR_X[{KRR_N_TRAIN * T_cap}] = {{")
lines.append(c_arr_1d(flat_X))
lines.append("};")
lines.append("")

# tend
lines.append(f"constexpr int TEND_N = {len(tend_i)};")
lines.append(f"constexpr float TEND_I[{len(tend_i)}] = {{ {', '.join(cf(v) for v in tend_i)} }};")
lines.append(f"constexpr float TEND_T[{len(tend_t)}] = {{ {', '.join(cf(v) for v in tend_t)} }};")
lines.append("")

# softplus
lines.append("constexpr int SP_N = 5;")
lines.append(f"constexpr float SP_I_NODES[5] = {{ {', '.join(cf(v) for v in ATK_I_NODES)} }};")
flat_p1 = [v for pr in softplus_p1 for v in pr]
flat_p2 = [v for pr in softplus_p2 for v in pr]
flat_p0 = [v for pr in softplus_p0 for v in pr]
lines.append(f"constexpr float SP_P0[10] = {{ {', '.join(cf(v) for v in flat_p0)} }}; // [class*2 + (t,y)]")
lines.append(f"constexpr float SP_P1[10] = {{ {', '.join(cf(v) for v in flat_p1)} }};")
lines.append(f"constexpr float SP_P2[10] = {{ {', '.join(cf(v) for v in flat_p2)} }};")
lines.append(f"constexpr float SP_SIGMA[5] = {{ {', '.join(cf(v) for v in softplus_sigma)} }};")
lines.append("")

# db_all: pooled サステイン出力用 CDF (public_release, y0<-55dB除外済み)
lines.append(f"constexpr int DB_ALL_N = {len(db_all_pooled)};")
lines.append(f"constexpr float DB_ALL[{len(db_all_pooled)}] = {{")
lines.append(c_arr_1d(db_all_pooled))
lines.append("};")
lines.append("")

# pct grid
lines.append("constexpr int PCT_N_NODES = 5;")
lines.append(f"constexpr float PCT_I_NODES[5] = {{ {', '.join(cf(v) for v in ATK_I_NODES5)} }};")
lines.append(f"constexpr int PCT_PRESENT[5] = {{ {', '.join(str(v) for v in pct_grid_present)} }};")
lines.append(f"constexpr float PCT_T_GRID[{N_T}] = {{")
lines.append(c_arr_1d([float(x) for x in ATK_PCT_T_GRID]))
lines.append("};")
lines.append(f"constexpr float PCT_P_AXIS[{N_P}] = {{")
lines.append(c_arr_1d([float(x) for x in _ATK_P_AXIS]))
lines.append("};")
# flatten grid node-major: index = ((node*N_P)+p)*N_T + t
flat_grid = []
for node in range(5):
    for p in range(N_P):
        flat_grid.extend(pct_grid_flat[node][p])
lines.append(f"constexpr int PCT_GRID_TOTAL = {len(flat_grid)};")
lines.append(f"// index = ((node*N_P)+p)*N_T + t")
lines.append(f"constexpr float PCT_GRID[{len(flat_grid)}] = {{")
lines.append(c_arr_1d(flat_grid))
lines.append("};")
lines.append("")

# vol grid: (i,t) 音量パーセンタイルグリッド (2.5.2B, アタック偏差 dev(p,i,t) 用)
lines.append("constexpr int VOL_N_NODES = 5;")
lines.append(f"constexpr float VOL_I_NODES[5] = {{ {', '.join(cf(v) for v in VOLGRID_I_NODES)} }};")
lines.append(f"constexpr int VOL_N_T = {VOL_N_T};")
lines.append(f"constexpr int VOL_N_P = {VOL_N_P};")
lines.append(f"constexpr float VOL_T_GRID[{VOL_N_T}] = {{")
lines.append(c_arr_1d([float(x) for x in VOLGRID_T_GRID]))
lines.append("};")
lines.append(f"constexpr float VOL_P_AXIS[{VOL_N_P}] = {{")
lines.append(c_arr_1d([float(x) for x in VOLGRID_P_AXIS]))
lines.append("};")
# flatten grid node-major: index = ((node*VOL_N_P)+p)*VOL_N_T + t
flat_volgrid = []
for node in range(5):
    for p in range(VOL_N_P):
        flat_volgrid.extend(volgrid_flat[node][p])
lines.append(f"constexpr int VOL_GRID_TOTAL = {len(flat_volgrid)};")
lines.append("// index = ((node*VOL_N_P)+p)*VOL_N_T + t")
lines.append(f"constexpr float VOL_GRID[{len(flat_volgrid)}] = {{")
lines.append(c_arr_1d(flat_volgrid))
lines.append("};")
lines.append("")
lines.append("} // namespace m62")
lines.append("")

OUT_INCLUDE.write_text("\n".join(lines), encoding="utf-8")
print(f"    wrote {OUT_INCLUDE} ({OUT_INCLUDE.stat().st_size/1024:.0f} KB)")


# =========================================================================
# Python 参照波形 (parity ground truth)
# =========================================================================
print("[6/6] generating Python reference waveforms ...")


def emit_reference(csv_path: Path, stem: str):
    # raw 入力 (preprocess 済み vec_df = アクティブ m_events) を dump → C++ 全段テスト用
    vec_df = pl.preprocess_csv_to_m_event_vectors_df(csv_path)
    raw_rows = ["device_ms,m,note_num"]
    for _, r in vec_df.iterrows():
        raw_rows.append(f"{int(r['device_ms'])},{int(r['m'])},{int(r['note_num'])}")
    (REF_DIR / f"{stem}.raw.csv").write_text("\n".join(raw_rows) + "\n", encoding="utf-8")

    labs = pl.detect_start_labels(vec_df, seg_params)
    notes, _ = pl.segment_notes_from_labels(
        vec_df, labs, csv_path.name, min_m_events=int(seg_params["min_m_events_per_note"]))
    ev_rows = ["note_idx,offset_ms,m,note_num"]
    expr_rows = ["note_idx,t_ms,expr"]
    meta_rows_out = ["note_idx,i_tend,t_end,dur_ms,n_pts"]
    nidx = 0
    for note in notes:
        if float(note["end_ms"] - note["start_ms"]) < MIN_DUR_MS:
            continue
        nd = build_note_data(note, "normal")
        if nd is None:
            continue
        evs = sorted([e for e in note.get("m_events", []) if int(e.get("note_num", 0)) != 0],
                     key=lambda e: int(e["offset_ms"]))
        for e in evs:
            ev_rows.append(f"{nidx},{int(e['offset_ms'])},{int(e['m'])},{int(e['note_num'])}")
        t_e, e_wave = note_expr_waveform(nd)
        for t, ev in zip(t_e, e_wave):
            expr_rows.append(f"{nidx},{float(t):.6f},{float(ev):.6f}")
        meta_rows_out.append(f"{nidx},{nd['i_tend']:.6f},{nd['t_end']:.6f},{nd['dur_ms']:.6f},{len(t_e)}")
        nidx += 1
    (REF_DIR / f"{stem}.events.csv").write_text("\n".join(ev_rows) + "\n", encoding="utf-8")
    (REF_DIR / f"{stem}.expr.csv").write_text("\n".join(expr_rows) + "\n", encoding="utf-8")
    (REF_DIR / f"{stem}.notes.csv").write_text("\n".join(meta_rows_out) + "\n", encoding="utf-8")
    print(f"    {stem}: {nidx} notes -> {stem}.events/expr/notes.csv")


# =========================================================================
# 因果(causal) Python リファレンス: 1 パルス 1 expr。C++ causal.h と同一アルゴリズム。
# notebook バッチとは意図的にズレる (i 走査化 + trailing LPF)。C++↔Python の parity 用。
# =========================================================================
def _trailing(hist, win):
    n = len(hist)
    if n == 0:
        return float("nan")
    w = min(max(int(win), 1), n)
    return float(sum(hist[n - w:]) / w)


def _krr_score_prefix_causal(deltas, k):
    # C++ score_at_k と同一の式 (float64)。krr_score_prefix の別名だが
    # 因果リファレンスであることを明示するため分けている。
    return krr_score_prefix(deltas, k)


def _smoothstep(w):
    w = float(np.clip(w, 0.0, 1.0))
    return w * w * (3.0 - 2.0 * w) if HANDOFF_SMOOTH else w


class _CausalPy:
    """C++ m62::CausalEngine の Python ミラー (検証用・float64)。"""
    def __init__(self, emit):
        self.emit = emit
        self.g = 0
        self.prev = None
        self.in_note = False
        self.note_idx = -1

    def _begin(self, e):
        self.in_note = True
        self.note_idx += 1
        self.start_ms = e[0]
        self.deltas = []
        self.last_uoff = None
        self.raw_i = []
        self.last_alloff = None
        self.raw_spd = []
        self.db_atk_hist = []
        self.emit(self.note_idx, 0.0, 0.0)   # オンセットで expr=0 (C++ beginNote と一致)
        self._feed(e)

    def _end(self):
        # noteOff のみ相当。expr=0 は次ノートのオンセット (_begin) で送る。
        self.in_note = False

    def push(self, device_ms, m, note_num):
        e = (device_ms, m, note_num)
        g = self.g; self.g += 1
        if g == 0:
            self.prev = e; return
        start = (g == 1) or (transition_dist(self.prev, e) > seg_params["threshold"])
        self.prev = e
        if start or not self.in_note:
            self._end()
            self._begin(e)
        elif self._feed(e):             # Δt 比カット → このパルスを新ノート頭に
            self._end()
            self._begin(e)

    def flush(self):
        self._end()

    def _feed(self, e):
        offset = float(e[0] - self.start_ms)
        if self.last_alloff is not None:
            dt = max(offset - self.last_alloff, 1e-3)
            self.raw_spd.append(1000.0 / dt)
        self.last_alloff = offset
        io = int(offset)
        in_win = 0.0 <= offset <= float(bundle.W_ms)
        if in_win and (self.last_uoff is None or io != self.last_uoff):
            if self.last_uoff is not None:
                d_new = float(io - self.last_uoff)
                # Δt 比カット: 新 delta を確定する前に判定 (このパルスは旧ノートに含めない)
                if DELTA_RATIO_CUT > 0.0 and self.deltas:
                    d_prev = self.deltas[-1]
                    if d_prev > 0.0 and d_new / d_prev > DELTA_RATIO_CUT:
                        return True
                self.deltas.append(d_new)
            self.last_uoff = io
            k = len(self.deltas) - 1
            if 0 <= k <= T_cap - 1:
                self.raw_i.append(_krr_score_prefix_causal(self.deltas, k))
        if not self.raw_i:
            return False
        i_now = _trailing(self.raw_i, LPF_I_WIN)
        t = offset
        t_end = t_end_for_i(i_now, tend_table)
        speed_now = _trailing(self.raw_spd, LPF_SPEED_WIN)
        speed_atk = _trailing(self.raw_spd, LPF_ATK_WIN)
        pct = atk_bow_percentile_i(i_now, t, speed_now) if np.isfinite(speed_now) else float("nan")
        base_db = percentile_to_db_i(pct, i_now) if np.isfinite(pct) else float("nan")
        db_raw = float(atk_softplus_db_i([t], i_now)[0])
        # 2026-07-10: 旧・乗算 fac を廃止し、加算 dev ((i,t) 音量グリッド由来) に統一。
        p_atk = atk_bow_percentile_i(i_now, t, speed_atk) if np.isfinite(speed_atk) else float("nan")
        dev = atk_dev(p_atk, i_now, t)
        self.db_atk_hist.append(db_raw + dev)
        db_atk = _trailing(self.db_atk_hist, ATK_SMOOTH_WIN)
        if t <= t_end:
            out = db_atk
            if t_end > 1.0:
                start = 1.0 - HANDOFF_REMAIN
                frac = t / t_end
                if frac > start and np.isfinite(base_db):
                    w = _smoothstep((frac - start) / max(HANDOFF_REMAIN, 1e-6))
                    out = (1.0 - w) * db_atk + w * base_db
        else:
            out = base_db if np.isfinite(base_db) else db_atk
        self.emit(self.note_idx, t, float(db_to_expr(out)))


def transition_dist(a, b):
    dt_ms = float(b[0] - a[0]); dnote = float(b[2] - a[2]); dm = float(b[1] - a[1])
    m_changed = (b[1] != a[1])
    sp = seg_params
    dt_scale = (dt_ms / sp["dt_norm_anchor_ms"]) ** 3
    tcs = sp["dt_time_contrib_at_anchor"] / sp["anchor_w_time"]
    d0 = sp["w_time"] * dt_scale * tcs
    d1 = dnote * sp["w_note"]
    d2 = dm * sp["w_updown"]
    if dt_ms <= sp["rapid_switch_ms"] and m_changed:
        d2 *= float(np.clip(sp["rapid_switch_discount"], 0.0, 1.0))
    base = (d0 * d0 + d1 * d1 + d2 * d2) ** 0.5
    excess = max(0.0, dt_ms - sp["gap_boost_after_ms"])
    return base + excess * sp["gap_boost_per_ms"]


def emit_causal_reference(csv_path: Path, stem: str):
    vec_df = pl.preprocess_csv_to_m_event_vectors_df(csv_path)
    rows = ["note_idx,t_ms,expr"]
    eng = _CausalPy(lambda ni, t, ev: rows.append(f"{ni},{t:.6f},{ev:.6f}"))
    for _, r in vec_df.iterrows():
        eng.push(int(r["device_ms"]), int(r["m"]), int(r["note_num"]))
    eng.flush()
    (REF_DIR / f"{stem}.causal.csv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"    {stem}: causal {len(rows) - 1} points -> {stem}.causal.csv")


# 代表 CSV を 2 本 (shuttle 校正 + dataset)
ref_csvs = [
    (CSV_DIR / "20260610155734-rec-shuttle_60_S_1.csv", "shuttle_60_S_1"),
]
for cp in sorted(DATASET_ROOT.rglob("*.csv")):
    if any(s in SKIP_DIR_NAMES for s in cp.parts):
        continue
    if cp.with_suffix(".anno.json").exists():
        ref_csvs.append((cp, "dataset_" + cp.stem))
        break

for cp, stem in ref_csvs:
    try:
        emit_reference(cp, stem)
        emit_causal_reference(cp, stem)
    except Exception as e:
        print(f"    SKIP {stem}: {e}")

print("done.")
