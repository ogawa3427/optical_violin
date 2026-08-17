#!/usr/bin/env python3
"""テレメトリ CSV の移調ツール。

MIDI ノート番号を一律 N 半音シフトする。パルス時刻・弓方向 (m)・cc11 はそのまま
なので、演奏の間合いとアーティキュレーションは完全に保たれる。音高差が変わらない
ため、セグメンテーション (w_note × 音高距離) の挙動も不変。

テレメトリ CSV 形式 (src/io/telemetry.cpp):
    P,device_ms,note_idx,note_num,m,cc11,infer_ns,send_ns[,i_a]   ← note_num は 4 列目
    N,ev,note_num,cause,device_ms                                 ← note_num は 3 列目

使い方:
    python3 transpose_telemetry.py in.csv +2 -o out.csv
    python3 transpose_telemetry.py in.csv +2            # 出力名を自動生成
    python3 transpose_telemetry.py in.csv +2 --dry-run  # 音名だけ確認
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

LETTERS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(note: int) -> str:
    return f"{LETTERS[note % 12]}{note // 12 - 1}"


def transpose_line(line: str, semitones: int) -> tuple[str, int | None]:
    """1 行を移調する。戻り値は (移調後の行, 元の note_num or None)。

    壊れ行・コメント行・想定外の行はそのまま返す (watch_telemetry.py と同じ寛容さ)。
    """
    s = line.rstrip("\n").rstrip("\r")
    if not s or s.startswith("#"):
        return line, None
    f = s.split(",")
    col = {"P": 3, "N": 2}.get(f[0])
    if col is None or len(f) <= col:
        return line, None
    try:
        note = int(f[col])
    except ValueError:
        return line, None
    shifted = note + semitones
    if not 0 <= shifted <= 127:
        raise ValueError(
            f"移調後のノート番号が MIDI 範囲外: {note} ({note_name(note)}) "
            f"{semitones:+d} → {shifted}"
        )
    f[col] = str(shifted)
    return ",".join(f) + "\n", note


def main() -> int:
    ap = argparse.ArgumentParser(description="テレメトリ CSV を移調する")
    ap.add_argument("src", type=Path, help="入力テレメトリ CSV")
    ap.add_argument("semitones", type=int, help="シフト量 (半音、+ で上、- で下)")
    ap.add_argument("-o", "--out", type=Path, help="出力先 (既定: <src>_up<N>/_dn<N>.csv)")
    ap.add_argument("--dry-run", action="store_true", help="書き出さず音名の対応だけ表示")
    args = ap.parse_args()

    text = args.src.read_text(encoding="utf-8", errors="replace")
    out_lines: list[str] = []
    used: set[int] = set()
    for line in text.splitlines(keepends=True):
        new_line, note = transpose_line(line, args.semitones)
        out_lines.append(new_line)
        if note is not None:
            used.add(note)

    print(f"使用ノート ({len(used)} 種):", file=sys.stderr)
    for n in sorted(used):
        print(
            f"  {n:3d} {note_name(n):4s} → {n + args.semitones:3d} "
            f"{note_name(n + args.semitones)}",
            file=sys.stderr,
        )

    if args.dry_run:
        return 0

    # DAW の replay サーバはログ名を ^telemetry_[\w-]+\.csv$ で検査するので、
    # サフィックスに "+" は使えない (up/dn 表記にする)。
    n = args.semitones
    tag = "up" if n >= 0 else "dn"
    out = args.out or args.src.with_name(f"{args.src.stem}_{tag}{abs(n)}{args.src.suffix}")
    out.write_text("".join(out_lines), encoding="utf-8")
    print(f"書き出し: {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
