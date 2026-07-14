#!/usr/bin/env python3
"""因果(causal) Python 再現エンジン。

C++ `src/infer/{m62.cpp, causal.h}` のライブ推論 (feed() 1パルス分) を
`models_62.json` (実機と同一の焼き込みテーブル。sklearn/dataAnalysis 非依存) から
純 Python (numpy のみ) で再現する。

セグメンテーション (transition_distance によるノート境界判定) は再実装しない ---
ノート区切りは実機 CausalEngine が既に確定させたもの (テレメトリの note_idx) を
そのまま使う。1 ノート分の物理パルス device_ms 列を feed_note() に渡すと、
C++ feed() と同じ計算をパルスごとに再現し、実機が送らない内部量
(弓速・KRR i・アタック/サステイン dB・handoff 重み) を含む dict のリストを返す。

先頭要素 (index 0) は beginNote() が送る合成オンセット点 (t=0, expr=0) に対応する。
以降 (index >= 1) は物理パルスごとの feed() 出力に対応する。

依存: 標準ライブラリ + numpy のみ。
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

DEFAULT_MODELS_PATH = Path(__file__).parent.parent / "reference" / "models_62.json"


# --------------------------------------------------------------------- モデル
class Models:
    """models_62.json を読み込んで numpy 配列に整形する。"""

    def __init__(self, path: Path | str = DEFAULT_MODELS_PATH):
        d = json.loads(Path(path).read_text(encoding="utf-8"))

        c = d["consts"]
        self.window_ms = float(d["meta"]["window_ms"])
        self.T_cap = int(d["krr"]["T_cap"])
        self.LPF_I_WIN = int(c["LPF_I_WIN"])
        self.LPF_SPEED_WIN = int(c["LPF_SPEED_WIN"])
        self.LPF_ATK_WIN = int(c["LPF_ATK_WIN"])
        self.ATK_SMOOTH_WIN = int(c["ATK_SMOOTH_WIN"])
        self.HANDOFF_REMAIN = float(c["HANDOFF_REMAIN"])
        self.HANDOFF_SMOOTH = bool(c["HANDOFF_SMOOTH"])
        self.DELTA_RATIO_CUT = float(c["DELTA_RATIO_CUT"])
        self.NAN_SENT = float(c["NAN_SENT"])
        self.EXPR_SLOPE = float(c["EXPR_SLOPE"])
        self.EXPR_INTERCEPT = float(c["EXPR_INTERCEPT"])
        self.EXPR_MIN = float(c["EXPR_MIN"])
        self.EXPR_MAX = float(c["EXPR_MAX"])

        self.tend_i = np.asarray(d["tend"]["i"], dtype=np.float64)
        self.tend_t = np.asarray(d["tend"]["t"], dtype=np.float64)

        sp = d["softplus"]
        self.sp_i_nodes = np.asarray(sp["i_nodes"], dtype=np.float64)
        self.sp_p0 = np.asarray(sp["p0"], dtype=np.float64)  # [5,2]
        self.sp_p1 = np.asarray(sp["p1"], dtype=np.float64)
        self.sp_p2 = np.asarray(sp["p2"], dtype=np.float64)
        self.sp_sigma = np.asarray(sp["sigma"], dtype=np.float64)

        self.db_all = np.asarray(d["db_all"], dtype=np.float64)

        pg = d["pct_grid"]
        self.pct_i_nodes = np.asarray(pg["i_nodes"], dtype=np.float64)
        self.pct_present = pg["present"]
        self.pct_t_grid = np.asarray(pg["t_grid"], dtype=np.float64)
        self.pct_p_axis = np.asarray(pg["p_axis"], dtype=np.float64)
        self.pct_data = self._desentinel(np.asarray(pg["data"], dtype=np.float64))  # [node,p,t]

        vg = d["vol_grid"]
        self.vol_i_nodes = np.asarray(vg["i_nodes"], dtype=np.float64)
        self.vol_t_grid = np.asarray(vg["t_grid"], dtype=np.float64)
        self.vol_p_axis = np.asarray(vg["p_axis"], dtype=np.float64)
        self.vol_data = self._desentinel(np.asarray(vg["data"], dtype=np.float64))  # [node,p,t]

        krr = d["krr"]
        self.krr_gamma = np.asarray(krr["gamma"], dtype=np.float64)          # [T_cap]
        self.krr_scale = [np.asarray(s, dtype=np.float64) for s in krr["scale"]]  # ragged [T_cap][k+1]
        self.krr_alpha = np.asarray(krr["alpha"], dtype=np.float64)          # [T_cap, n_train]
        self.krr_X = np.asarray(krr["X"], dtype=np.float64)                  # [n_train, T_cap]

    def _desentinel(self, arr: np.ndarray) -> np.ndarray:
        arr = arr.copy()
        arr[arr >= self.NAN_SENT * 0.5] = np.nan
        return arr


# ------------------------------------------------------------------- ヘルパ
def _clampf(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else (hi if v > hi else v)


def _interp1(x: float, xp: np.ndarray, fp: np.ndarray,
             left: float | None = None, right: float | None = None) -> float:
    """C++ interp1 と同じ numpy.interp 互換の 1D 線形補間 (xp は昇順)。"""
    n = len(xp)
    if n == 0:
        return float("nan")
    if n == 1:
        return float(fp[0])
    lo = float(fp[0]) if left is None else left
    hi = float(fp[-1]) if right is None else right
    if x <= xp[0]:
        return lo if x < xp[0] else float(fp[0])
    if x >= xp[-1]:
        return hi if x > xp[-1] else float(fp[-1])
    i = int(np.searchsorted(xp, x, side="right")) - 1
    i = min(max(i, 0), n - 2)
    x0, x1 = xp[i], xp[i + 1]
    y0, y1 = fp[i], fp[i + 1]
    if x1 == x0:
        return float(y0)
    return float(y0 + (y1 - y0) * (x - x0) / (x1 - x0))


def _trailing(hist: list[float], win: int) -> float:
    n = len(hist)
    if n == 0:
        return float("nan")
    w = min(win if win > 0 else 1, n)
    return float(sum(hist[n - w:]) / w)


def _smoothstep(w: float, smooth: bool) -> float:
    w = _clampf(w, 0.0, 1.0)
    return w * w * (3.0 - 2.0 * w) if smooth else w


def _sp_func(x: float) -> float:
    if x > 30.0:
        return x
    return math.log1p(math.exp(min(x, 30.0)))


def _db_one(arr: np.ndarray, p: float) -> float:
    n = len(arr)
    idx = _clampf(p, 0.0, 1.0) * (n - 1)
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    return float(arr[lo] + (idx - lo) * (arr[hi] - arr[lo]))


# --------------------------------------------------------------- テーブル参照
def t_end_for_i(i_val: float, m: Models) -> float:
    return _interp1(_clampf(i_val, -1.0, 1.0), m.tend_i, m.tend_t,
                     m.tend_t[0], m.tend_t[-1])


def atk_softplus_db_i(t_ms: float, i_val: float, m: Models) -> float:
    ys = np.empty(len(m.sp_i_nodes))
    for c in range(len(m.sp_i_nodes)):
        t0, y0 = m.sp_p0[c]
        t1, y1 = m.sp_p1[c]
        t2, y2 = m.sp_p2[c]
        s1 = (y1 - y0) / (t1 - t0) if abs(t1 - t0) > 1e-6 else 0.0
        s2 = (y2 - y1) / (t2 - t1) if abs(t2 - t1) > 1e-6 else 0.0
        sig = max(m.sp_sigma[c], 0.01)
        spv = _sp_func((t_ms - t1) / sig)
        ys[c] = y0 + s1 * (t_ms - t0) + (s2 - s1) * sig * spv
    return _interp1(i_val, m.sp_i_nodes, ys, ys[0], ys[-1])


def percentile_to_db_i(p: float, m: Models) -> float:
    return _db_one(m.db_all, p)


def _node_col_pct(node: int, tt: float, m: Models) -> np.ndarray:
    grid = m.pct_t_grid
    tt = _clampf(tt, grid[0], grid[-1])
    j0 = int(np.searchsorted(grid, tt, side="right")) - 1
    j0 = min(max(j0, 0), len(grid) - 2)
    j1 = j0 + 1
    x0, x1 = grid[j0], grid[j1]
    w = (tt - x0) / (x1 - x0) if x1 > x0 else 0.0
    return (1.0 - w) * m.pct_data[node, :, j0] + w * m.pct_data[node, :, j1]


def atk_bow_percentile(i_node: float, t_ms: float, speed: float, m: Models) -> float:
    node = -1
    for k in range(len(m.pct_i_nodes)):
        if abs(m.pct_i_nodes[k] - i_node) < 1e-6:
            node = k
            break
    if node < 0 or not m.pct_present[node]:
        return float("nan")
    col = _node_col_pct(node, t_ms, m)
    if np.any(np.isnan(col)):
        return float("nan")
    return _interp1(speed, col, m.pct_p_axis, 0.0, 1.0)


def atk_bow_percentile_i(i_val: float, t_ms: float, speed: float, m: Models) -> float:
    iv = _clampf(i_val, -1.0, 1.0)
    nodes = m.pct_i_nodes
    k = 0
    while k < len(nodes) - 2 and nodes[k + 1] <= iv:
        k += 1
    n0, n1 = nodes[k], nodes[k + 1]
    w = (iv - n0) / (n1 - n0) if n1 > n0 else 0.0
    p0 = atk_bow_percentile(n0, t_ms, speed, m)
    p1 = atk_bow_percentile(n1, t_ms, speed, m)
    if not math.isfinite(p0):
        return p1
    if not math.isfinite(p1):
        return p0
    return (1.0 - w) * p0 + w * p1


def _node_col_vol(node: int, tt: float, m: Models) -> np.ndarray:
    grid = m.vol_t_grid
    tt = _clampf(tt, grid[0], grid[-1])
    j0 = int(np.searchsorted(grid, tt, side="right")) - 1
    j0 = min(max(j0, 0), len(grid) - 2)
    j1 = j0 + 1
    x0, x1 = grid[j0], grid[j1]
    w = (tt - x0) / (x1 - x0) if x1 > x0 else 0.0
    return (1.0 - w) * m.vol_data[node, :, j0] + w * m.vol_data[node, :, j1]


def _vol_db_node(node: int, p: float, t_ms: float, m: Models) -> float:
    col = _node_col_vol(node, t_ms, m)
    if np.any(np.isnan(col)):
        return float("nan")
    return _interp1(_clampf(p, 0.0, 1.0), m.vol_p_axis, col, col[0], col[-1])


def vol_db_grid(p: float, i_val: float, t_ms: float, m: Models) -> float:
    iv = _clampf(i_val, -1.0, 1.0)
    nodes = m.vol_i_nodes
    k = 0
    while k < len(nodes) - 2 and nodes[k + 1] <= iv:
        k += 1
    n0, n1 = nodes[k], nodes[k + 1]
    w = (iv - n0) / (n1 - n0) if n1 > n0 else 0.0
    d0 = _vol_db_node(k, p, t_ms, m)
    d1 = _vol_db_node(k + 1, p, t_ms, m)
    if not math.isfinite(d0):
        return d1
    if not math.isfinite(d1):
        return d0
    return (1.0 - w) * d0 + w * d1


def atk_dev(p: float, i_val: float, t_ms: float, m: Models) -> float:
    if not math.isfinite(p):
        return 0.0
    d = vol_db_grid(p, i_val, t_ms, m)
    d50 = vol_db_grid(0.5, i_val, t_ms, m)
    if not math.isfinite(d) or not math.isfinite(d50):
        return 0.0
    return d - d50


def db_to_expr_one(db: float, m: Models) -> float:
    e = 10.0 ** ((db - m.EXPR_INTERCEPT) / m.EXPR_SLOPE)
    return _clampf(e, m.EXPR_MIN, m.EXPR_MAX)


def score_at_k(deltas: list[float], k: int, m: Models) -> float:
    """C++ score_at_k と同一の式 (float64)。obs=deltas の先頭 k+1 個だけ見る。"""
    gamma = m.krr_gamma[k]
    alpha = m.krr_alpha[k]          # [n_train]
    scale = m.krr_scale[k]          # [k+1]
    obs = np.asarray(deltas[:k + 1], dtype=np.float64)
    X = m.krr_X[:, :k + 1]          # [n_train, k+1]
    diff = (obs - X) / scale
    d2 = np.sum(diff * diff, axis=1)
    acc = float(np.sum(alpha * np.exp(-gamma * d2)))
    return _clampf(acc, -1.0, 1.0)


# --------------------------------------------------------------- feed() 再現
def feed_note(device_ms_list: list[int], models: Models) -> list[dict]:
    """1 ノート分の物理パルス device_ms 列 (先頭 = ノート開始 = オンセット行) を feed() で再生する。

    返り値の各要素は 1 テレメトリ P 行に対応する dict:
      t_ms, expr, i_now, speed_now, speed_atk, pct, base_db, db_atk, t_end,
      handoff_w, out_db, ratio(前Δtとの比、無ければ None), flagged(比カット閾値超過)
    セグメンテーション自体は実機がすでに確定済み (note_idx で分割済み) という前提のため、
    Δt比カットは検出のみ行い (flagged)、ノートは割らない (実機ログ上ここで割れていた
    はずのケースは既に別の note_idx として渡ってくる)。
    """
    if not device_ms_list:
        return []
    start_ms = device_ms_list[0]
    deltas: list[float] = []
    last_uoff = None
    raw_i: list[float] = []
    last_alloff = None
    raw_spd: list[float] = []
    db_atk_hist: list[float] = []

    def state_update(offset: float) -> tuple[float | None, bool]:
        """1 物理パルス分の弓速/KRR状態更新。C++ feed() 前半 (テーブル参照より前) と同一。

        beginNote() は先頭パルスも feed() に通す (KRR の last_uoff_ 初期化のためだけで、
        raw_i_ はまだ空なので emit はしない) ので、index 0 (オンセット行) も含めて
        device_ms_list の全要素をこの状態更新に通す必要がある。
        """
        nonlocal last_alloff, last_uoff
        ratio: float | None = None
        flagged = False
        if last_alloff is not None:
            dt = max(offset - last_alloff, 1e-3)
            raw_spd.append(1000.0 / dt)
        last_alloff = offset

        io = int(offset)
        in_win = 0.0 <= offset <= models.window_ms
        if in_win and (last_uoff is None or io != last_uoff):
            if last_uoff is not None:
                d_new = float(io - last_uoff)
                if models.DELTA_RATIO_CUT > 0.0 and deltas:
                    d_prev = deltas[-1]
                    if d_prev > 0.0:
                        ratio = d_new / d_prev
                        flagged = ratio > models.DELTA_RATIO_CUT
                deltas.append(d_new)
            last_uoff = io
            k = len(deltas) - 1
            if 0 <= k <= models.T_cap - 1:
                raw_i.append(score_at_k(deltas, k, models))
        return ratio, flagged

    state_update(0.0)  # index 0 (オンセット) 分の feed() 状態更新のみ (emit はしない)
    rows = [{
        "t_ms": 0.0, "expr": 0.0, "i_now": float("nan"), "speed_now": float("nan"),
        "speed_atk": float("nan"), "pct": float("nan"), "p_atk": float("nan"), "base_db": float("nan"),
        "db_atk": float("nan"), "t_end": float("nan"), "handoff_w": float("nan"),
        "out_db": float("nan"), "ratio": None, "flagged": False,
    }]

    for dm in device_ms_list[1:]:
        offset = float(dm - start_ms)
        ratio, flagged = state_update(offset)

        if not raw_i:
            rows.append({
                "t_ms": offset, "expr": None, "i_now": float("nan"),
                "speed_now": float("nan"), "speed_atk": float("nan"), "pct": float("nan"),
                "p_atk": float("nan"), "base_db": float("nan"), "db_atk": float("nan"), "t_end": float("nan"),
                "handoff_w": float("nan"), "out_db": float("nan"),
                "ratio": ratio, "flagged": flagged,
            })
            continue

        i_now = _trailing(raw_i, models.LPF_I_WIN)
        t = offset
        t_end = t_end_for_i(i_now, models)
        speed_now = _trailing(raw_spd, models.LPF_SPEED_WIN)
        speed_atk = _trailing(raw_spd, models.LPF_ATK_WIN)

        pct = atk_bow_percentile_i(i_now, t, speed_now, models) if math.isfinite(speed_now) else float("nan")
        base_db = percentile_to_db_i(pct, models) if math.isfinite(pct) else float("nan")

        db_raw = atk_softplus_db_i(t, i_now, models)
        p_atk = atk_bow_percentile_i(i_now, t, speed_atk, models) if math.isfinite(speed_atk) else float("nan")
        dev = atk_dev(p_atk, i_now, t, models)
        db_atk_hist.append(db_raw + dev)
        db_atk = _trailing(db_atk_hist, models.ATK_SMOOTH_WIN)

        handoff_w = 0.0
        if t <= t_end:
            out = db_atk
            if t_end > 1.0:
                start = 1.0 - models.HANDOFF_REMAIN
                frac = t / t_end
                if frac > start and math.isfinite(base_db):
                    handoff_w = _smoothstep((frac - start) / max(models.HANDOFF_REMAIN, 1e-6),
                                             models.HANDOFF_SMOOTH)
                    out = (1.0 - handoff_w) * db_atk + handoff_w * base_db
        else:
            out = base_db if math.isfinite(base_db) else db_atk
            handoff_w = 1.0

        expr = db_to_expr_one(out, models)
        rows.append({
            "t_ms": t, "expr": expr, "i_now": i_now, "speed_now": speed_now,
            "speed_atk": speed_atk, "pct": pct, "p_atk": p_atk, "base_db": base_db, "db_atk": db_atk,
            "t_end": t_end, "handoff_w": handoff_w, "out_db": out,
            "ratio": ratio, "flagged": flagged,
        })

    return rows
