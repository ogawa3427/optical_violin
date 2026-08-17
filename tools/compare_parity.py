#!/usr/bin/env python3
"""C++ native 出力と Python 参照 expr 波形を突合する。

使い方:
    python compare_parity.py <cpp_out.csv> <reference.csv>

    <cpp_out.csv> は `-` で stdin。ヘッダ行 (note_idx,t_ms,expr) は有無どちらでもよい。

比較対象のペアリングに注意 (取り違えると全ノートが点数不一致になる):

    program <raw.csv>            → reference/<stem>.expr.csv    (バッチ版)
    program <raw.csv> --causal   → reference/<stem>.causal.csv  (因果版)

両方まとめて回すなら `tools/run_parity.sh` を使う。

note_idx ごとに点数・時刻・expr を比較し、最大絶対誤差/RMS を出す。
終了コード: 0=PASS / 1=数値不一致 / 2=突合不能 (比較点数ゼロ・入力不正)。
"""
import sys
from collections import defaultdict

MAX_ABS_ERR = 1.0  # expr [0,127] 上の許容誤差 (test_causal_engine.py と同一)


def load(path):
    """note_idx -> [(t_ms, expr), ...]。ヘッダ行は自動判別する。"""
    f = sys.stdin if path == "-" else open(path)
    try:
        d = defaultdict(list)
        for lineno, ln in enumerate(f, 1):
            ln = ln.strip()
            if not ln:
                continue
            parts = ln.split(",")
            if len(parts) != 3:
                sys.exit(f"error: {path}:{lineno}: 3 列を期待したが {len(parts)} 列: {ln!r}")
            try:
                a, b, c = int(parts[0]), float(parts[1]), float(parts[2])
            except ValueError:
                # 先頭行だけはヘッダとして黙って読み飛ばす
                if lineno == 1:
                    continue
                sys.exit(f"error: {path}:{lineno}: 数値として解釈できない: {ln!r}")
            d[a].append((b, c))
        return d
    finally:
        if f is not sys.stdin:
            f.close()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cpp = load(sys.argv[1])
    ref = load(sys.argv[2])

    if not cpp:
        sys.exit(f"error: {sys.argv[1]} にデータ行が 1 行もない")
    if not ref:
        sys.exit(f"error: {sys.argv[2]} にデータ行が 1 行もない")

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

    print(f"compared notes: {len(keys)}  points: {global_n}")
    print(f"point-count mismatches: {pt_mismatch}")

    # 1 点も突合できていないなら「誤差 0 で PASS」に見える出力を出してはいけない。
    if global_n == 0:
        print("!! 比較できた点が 0 — この実行は何も検証していない (vacuous)")
        if keys and pt_mismatch == len(keys):
            print("!! 全ノートが点数不一致。バッチ出力を .causal.csv と (あるいはその逆で)")
            print("!! 突合していないか? 正しいペアは:")
            print("!!     program <raw.csv>          ↔ reference/<stem>.expr.csv")
            print("!!     program <raw.csv> --causal ↔ reference/<stem>.causal.csv")
            print("!! まとめて回すなら tools/run_parity.sh")
        elif not keys:
            print("!! cpp 側と ref 側で共通する note_idx が 1 つもない")
        print("PARITY: FAIL (no points compared)")
        sys.exit(2)

    rms = (global_sq / global_n) ** 0.5
    print(f"max abs expr error: {global_max:.6f} (note {worst})")
    print(f"RMS expr error:     {rms:.6f}")

    ok = (n_cpp == n_ref) and (pt_mismatch == 0) and (global_max <= MAX_ABS_ERR)
    print("PARITY:", f"PASS (max err <= {MAX_ABS_ERR} expr)" if ok else "CHECK")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
