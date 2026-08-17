#!/usr/bin/env python3
"""ノートスコープを 1 枚の PNG に書き出す (DAW の「グラフ」ワンクリック用・ヘッドレス)。

live_scope.py の NoteViewer をそのまま Agg バックエンドで使い、指定ログの 1 ノートを
描画して savefig する。実機 cc11 と causal_engine 再現 (expr/KRR i/弓速/percentile/dB)
の重ね描きは live_scope と完全に同じ見た目になる。

使い方:
  python tools/scope_png.py --log tools/logs/telemetry_XXXX.csv --index 5 --out out.png
"""
import argparse
import sys

import matplotlib
matplotlib.use("Agg")  # live_scope (pyplot) を import する前にヘッドレス確定

from causal_engine import DEFAULT_MODELS_PATH, Models  # noqa: E402
from live_scope import load_notes_from_file, NoteViewer  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--log", required=True, help="telemetry CSV パス")
    ap.add_argument("--index", type=int, required=True, help="ノート番号 (0 始まり, 先頭からの順)")
    ap.add_argument("--out", required=True, help="出力 PNG パス")
    ap.add_argument("--models", default=str(DEFAULT_MODELS_PATH), help="models_62.json")
    ap.add_argument("--dpi", type=int, default=110)
    args = ap.parse_args()

    notes = load_notes_from_file(args.log)
    if not notes:
        print("ノートが無い", file=sys.stderr)
        return 2
    if args.index < 0 or args.index >= len(notes):
        print(f"index {args.index} は範囲外 (0..{len(notes) - 1})", file=sys.stderr)
        return 2

    models = Models(args.models)
    viewer = NoteViewer(models)
    viewer.add_note(notes[args.index])  # idx 確定 + refresh() で全パネル描画
    viewer.fig.savefig(args.out, dpi=args.dpi, bbox_inches="tight")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
