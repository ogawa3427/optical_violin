#!/usr/bin/env python3
"""C++ native 出力と Python 参照 expr 波形を突合する。

使い方:
    python compare_parity.py <cpp_out.csv> <reference.expr.csv> [<reference.notes.csv>]

note_idx ごとに点数・時刻・expr を比較し、最大絶対誤差/RMS を出す。
"""
import sys
from collections import defaultdict


def load(path):
    d = defaultdict(list)
    with open(path) as f:
        next(f)  # header
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            a, b, c = ln.split(",")
            d[int(a)].append((float(b), float(c)))
    return d


def main():
    cpp = load(sys.argv[1])
    ref = load(sys.argv[2])

    n_cpp = len(cpp)
    n_ref = len(ref)
    print(f"notes: cpp={n_cpp}  ref={n_ref}")
    if n_cpp != n_ref:
        print("!! note count mismatch")

    keys = sorted(set(cpp) & set(ref))
    global_max = 0.0
    global_sq = 0.0
    global_n = 0
    worst = None
    pt_mismatch = 0
    for k in keys:
        ca, ra = cpp[k], ref[k]
        if len(ca) != len(ra):
            pt_mismatch += 1
            print(f"  note {k}: point count cpp={len(ca)} ref={len(ra)}")
            continue
        nmax = 0.0
        for (tc, ec), (tr, er) in zip(ca, ra):
            dt = abs(tc - tr)
            de = abs(ec - er)
            if dt > 0.5:
                print(f"  note {k}: t mismatch cpp={tc} ref={tr}")
            if de > nmax:
                nmax = de
            global_sq += de * de
            global_n += 1
        if nmax > global_max:
            global_max = nmax
            worst = k
        if global_n:
            pass

    rms = (global_sq / global_n) ** 0.5 if global_n else float("nan")
    print(f"compared notes: {len(keys)}  points: {global_n}")
    print(f"point-count mismatches: {pt_mismatch}")
    print(f"max abs expr error: {global_max:.6f} (note {worst})")
    print(f"RMS expr error:     {rms:.6f}")

    ok = (n_cpp == n_ref) and (pt_mismatch == 0) and (global_max <= 1.0)
    print("PARITY:", "PASS (max err <= 1.0 expr)" if ok else "CHECK")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
