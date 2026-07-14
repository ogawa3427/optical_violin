#!/usr/bin/env python3
"""AtomS3R テレメトリ (USB CDC) のウォッチ・記録・ノート単位分析。

ファーム側 (src/io/telemetry.cpp, M62_LIVE_HID 時) が吐く CSV を受ける:
  P,device_ms,note_idx,note_num,m,cc11,infer_ns,send_ns   … 1 パルス 1 行
  N,ev,note_num,cause,device_ms                            … ノート境界 (ev: 1=on 0=off)

使い方:
  ライブ監視 + 記録 (ポート自動検出):
      python tools/watch_telemetry.py
  ポート指定:
      python tools/watch_telemetry.py --port /dev/cu.usbmodem1101
  記録済みログのノート単位分析 (シリアル不要):
      python tools/watch_telemetry.py --analyze logs/telemetry_20260713_153000.csv

依存: pyserial (ライブ監視時のみ)。--analyze は標準ライブラリだけで動く。
記録先: tools/logs/telemetry_YYYYMMDD_HHMMSS.csv (生 P/N 行そのまま)
"""
import argparse
import glob
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

LOG_DIR = Path(__file__).parent / "logs"


# ---------------------------------------------------------------- ノート集計
class NoteAggregator:
    """P/N 行を逐次受けてノート単位の統計を組み立てる。

    ライブ監視と --analyze の両方から同じロジックを使う。
    ノートの区切りは note_idx の変化で判定する (N 行は cause の付与に使う)。
    """

    def __init__(self, on_note_done=None):
        self.on_note_done = on_note_done  # ノート完成時コールバック (summary dict)
        self.cur_idx = None
        self.pulses = []          # 現ノートの P 行 (dict のリスト)
        self.pending_cause = ""  # 直近の N,0 行の cause
        self.summaries = []
        self.bad_lines = 0        # パースできなかった行数 (混線・ノイズ)

    def feed(self, line: str):
        try:
            return self._feed(line)
        except (ValueError, IndexError):
            # 行の混線・起動メッセージ・USB ノイズ等。1 行捨てるだけで続行。
            self.bad_lines += 1
            return None

    def _feed(self, line: str):
        line = line.strip()
        if not line or line.startswith("#"):
            return None
        f = line.split(",")
        if f[0] == "P" and len(f) >= 8:
            p = {
                "device_ms": int(f[1]), "note_idx": int(f[2]),
                "note_num": int(f[3]), "m": int(f[4]), "cc11": int(f[5]),
                "infer_ns": int(f[6]), "send_ns": int(f[7]),
            }
            if self.cur_idx is not None and p["note_idx"] != self.cur_idx:
                self._close_note()
            self.cur_idx = p["note_idx"]
            self.pulses.append(p)
            return ("P", p)
        if f[0] == "N" and len(f) >= 5:
            n = {"ev": int(f[1]), "note_num": int(f[2]),
                 "cause": f[3], "device_ms": int(f[4])}
            if n["ev"] == 0:
                self.pending_cause = n["cause"]
                self._close_note()
            return ("N", n)
        return None

    def _close_note(self):
        if not self.pulses:
            return
        ps = self.pulses
        infer = [p["infer_ns"] for p in ps]
        send = [p["send_ns"] for p in ps]
        cc = [p["cc11"] for p in ps]
        s = {
            "note_idx": ps[0]["note_idx"],
            "note_num": ps[0]["note_num"],
            "pulses": len(ps),
            "dur_ms": ps[-1]["device_ms"] - ps[0]["device_ms"],
            "cc11_mean": statistics.fmean(cc),
            "cc11_peak": max(cc),
            "infer_us_mean": statistics.fmean(infer) / 1000,
            "infer_us_max": max(infer) / 1000,
            "send_us_mean": statistics.fmean(send) / 1000,
            "send_us_max": max(send) / 1000,
            "off_cause": self.pending_cause,
        }
        self.summaries.append(s)
        self.pulses = []
        self.pending_cause = ""
        if self.on_note_done:
            self.on_note_done(s)

    def flush(self):
        self._close_note()


SUMMARY_HDR = (f"{'idx':>4} {'note':>4} {'pulse':>5} {'dur_ms':>7} "
               f"{'cc_mean':>7} {'cc_pk':>5} {'inf_us':>7} {'inf_mx':>7} "
               f"{'snd_us':>7} {'snd_mx':>7}  cause")


def fmt_summary(s):
    return (f"{s['note_idx']:>4} {s['note_num']:>4} {s['pulses']:>5} "
            f"{s['dur_ms']:>7} {s['cc11_mean']:>7.1f} {s['cc11_peak']:>5} "
            f"{s['infer_us_mean']:>7.1f} {s['infer_us_max']:>7.1f} "
            f"{s['send_us_mean']:>7.1f} {s['send_us_max']:>7.1f}  {s['off_cause']}")


# ---------------------------------------------------------------- ライブ監視
def autodetect_port():
    """macOS の USB CDC 候補を探す。複数あれば先頭 (エラーで列挙表示)。"""
    cands = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not cands:
        sys.exit("シリアルポートが見つからない。--port で指定して (ls /dev/cu.*)")
    if len(cands) > 1:
        print(f"複数候補: {cands} → {cands[0]} を使用 (違ったら --port)")
    return cands[0]


def watch(port: str, baud: int):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial が要る: python -m pip install pyserial")

    LOG_DIR.mkdir(exist_ok=True)
    log_path = LOG_DIR / f"telemetry_{datetime.now():%Y%m%d_%H%M%S}.csv"
    print(f"port={port} baud={baud}\nlog -> {log_path}")
    print("ノート完成ごとにサマリ 1 行。Ctrl-C で終了+全体統計。\n")
    print(SUMMARY_HDR)

    agg = NoteAggregator(on_note_done=lambda s: print(fmt_summary(s)))
    with serial.Serial(port, baud, timeout=1) as ser, open(log_path, "w") as log:
        log.write("# " + " ".join(sys.argv) + f" @ {datetime.now().isoformat()}\n")
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                log.write(line + "\n")
                if line.startswith("#") or line.startswith("NOTE"):
                    # ファームのログ行はそのまま見せる (parity 行等は記録のみ)
                    if line.startswith("#"):
                        print(f"  {line}")
                    continue
                agg.feed(line)
        except KeyboardInterrupt:
            pass
    agg.flush()
    print_overall(agg.summaries)
    if agg.bad_lines:
        print(f"(パース不能 {agg.bad_lines} 行をスキップ)")
    print(f"\nlog: {log_path}")
    print(f"再分析: python tools/watch_telemetry.py --analyze {log_path}")


# ---------------------------------------------------------------- 事後分析
def analyze(path: str):
    agg = NoteAggregator()
    with open(path) as f:
        for line in f:
            agg.feed(line)
    agg.flush()
    if not agg.summaries:
        sys.exit("P 行が 1 つもない (ライブモードのログ?)")
    print(SUMMARY_HDR)
    for s in agg.summaries:
        print(fmt_summary(s))
    print_overall(agg.summaries)


def print_overall(summaries):
    if not summaries:
        print("\n(ノート 0 個)")
        return
    total_pulses = sum(s["pulses"] for s in summaries)
    inf_max = max(s["infer_us_max"] for s in summaries)
    snd_max = max(s["send_us_max"] for s in summaries)
    causes = {}
    for s in summaries:
        causes[s["off_cause"]] = causes.get(s["off_cause"], 0) + 1
    print(f"\n==== 全体: note={len(summaries)} pulse={total_pulses} "
          f"infer_max={inf_max:.1f}us send_max={snd_max:.1f}us "
          f"off_cause={causes} ====")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="シリアルポート (省略時 /dev/cu.usbmodem* 自動検出)")
    ap.add_argument("--baud", type=int, default=115200,
                    help="ボーレート (USB CDC なので実質無関係、既定 115200)")
    ap.add_argument("--analyze", metavar="LOG.csv",
                    help="記録済みログをノート単位で再集計 (シリアル不要)")
    args = ap.parse_args()
    if args.analyze:
        analyze(args.analyze)
    else:
        watch(args.port or autodetect_port(), args.baud)


if __name__ == "__main__":
    main()
