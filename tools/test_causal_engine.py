#!/usr/bin/env python3
"""causal_engine.py の drift ガード: reference/*.causal.csv (C++/旧Python因果リファレンス)
と feed_note() の出力を突合し、expr の最大絶対誤差が閾値以内かを確認する。

注意: reference/*.events.csv はバッチ版セグメンテーション (transition_distance のみ) の
note_idx を持ち、causal.csv は因果版セグメンテーション (Δt比カットも効く) の note_idx を
持つ ---  両者はノート境界が意図的にズレる (export_for_mcu.py 内コメント参照)。
feed_note() はセグメンテーションを再実装しない設計 (ノート区切りは呼び出し側=実機/テレメトリの
既定を信頼する) ため、この drift ガードでは causal.csv 自身の t_ms 列をノートごとの
device_ms 列として与え、「セグメンテーションが正しく確定した後の per-pulse 数式」だけを
検証する。

使い方: python tools/test_causal_engine.py
"""
from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path

from causal_engine import Models, feed_note

REF_DIR = Path(__file__).parent.parent / "reference"
MAX_ABS_ERR = 1.0  # 既存 parity ゲート (run_parity.sh / compare_parity.py) と同一閾値


def load_causal_ref(causal_csv: Path) -> dict[int, list[tuple[float, float]]]:
    by_note: dict[int, list[tuple[float, float]]] = defaultdict(list)
    with open(causal_csv, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            by_note[int(row["note_idx"])].append((float(row["t_ms"]), float(row["expr"])))
    return by_note


def check_stem(stem: str, models: Models) -> bool:
    ref = load_causal_ref(REF_DIR / f"{stem}.causal.csv")

    max_err = 0.0
    n_pts = 0
    n_notes = 0
    for note_idx, ref_rows in sorted(ref.items()):
        # causal.csv 自身の t_ms (=ノート先頭からの相対 offset, 整数 ms) を
        # device_ms 列として与える (start_ms=0 相当、先頭行は t=0 のオンセット点)。
        device_ms_list = [int(round(t)) for t, _ in ref_rows]
        got_rows = feed_note(device_ms_list, models)
        if len(got_rows) != len(ref_rows):
            print(f"  [{stem}] note {note_idx}: 点数不一致 got={len(got_rows)} ref={len(ref_rows)}")
            return False
        for g, (rt, rexpr) in zip(got_rows, ref_rows):
            if abs(g["t_ms"] - rt) > 1e-3:
                print(f"  [{stem}] note {note_idx}: t_ms 不一致 got={g['t_ms']} ref={rt}")
                return False
            err = abs(g["expr"] - rexpr)
            max_err = max(max_err, err)
            n_pts += 1
        n_notes += 1

    status = "PASS" if max_err <= MAX_ABS_ERR else "FAIL"
    print(f"[{stem}] notes={n_notes} pts={n_pts} max_abs_expr_err={max_err:.4f} -> {status}")
    return max_err <= MAX_ABS_ERR


def main() -> int:
    models = Models()
    stems = sorted(p.stem.replace(".causal", "") for p in REF_DIR.glob("*.causal.csv"))
    if not stems:
        sys.exit(f"reference/*.causal.csv が見つからない: {REF_DIR}")
    ok = True
    for stem in stems:
        ok &= check_stem(stem, models)
    print("ALL PASS" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
