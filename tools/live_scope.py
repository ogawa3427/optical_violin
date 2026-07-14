#!/usr/bin/env python3
"""実機テレメトリのライブスコープ: ノート単位で実機が送らない中間信号を再現して見る。

watch_telemetry.py の P/N ストリームを受け (ライブ or ログ再生)、ノートごとに
causal_engine.feed_note() で弓速・KRR i・アタック/サステイン dB・handoff 重みを
再現し、実機の実 cc11 と重ねて matplotlib で表示する。1 ノート = 1 画面、
左右キーでノート間を移動する。

使い方:
  ログ再生:
      python tools/live_scope.py --replay tools/logs/telemetry_20260713_161810.csv
  ライブ監視 (ポート自動検出、tools/logs/ に記録もする):
      python tools/live_scope.py
      python tools/live_scope.py --port /dev/cu.usbmodem1101

キー操作:
  ← / →       : 前 / 次のノート
  Home / End  : 最初 / 最新のノート
  f           : ライブ追従 ON/OFF (最新ノート完成時に自動でそこへ移動)
  1-9         : 系列の表示/非表示トグル (チェックボックスと同じ、10 系列目は画面のチェックボックスから)

依存: numpy, matplotlib (+ pyserial はライブ監視時のみ)。
"""
from __future__ import annotations

import argparse
import glob
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from queue import Empty, Queue

import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.widgets import CheckButtons

from causal_engine import DEFAULT_MODELS_PATH, Models, feed_note

# 日本語ラベル (凡例・タイトル・軸名) が豆腐化しないよう、インストール済みの
# 日本語対応フォントを優先させる (macOS 標準の Hiragino Sans を最優先)。
_JP_FONT_CANDIDATES = [
    "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Yu Gothic", "YuGothic",
    "Noto Sans CJK JP", "Noto Sans JP", "IPAexGothic", "TakaoPGothic",
]
_installed = {f.name for f in font_manager.fontManager.ttflist}
_jp_font = next((f for f in _JP_FONT_CANDIDATES if f in _installed), None)
if _jp_font:
    plt.rcParams["font.family"] = _jp_font
plt.rcParams["axes.unicode_minus"] = False  # 日本語フォントだとマイナス記号が豆腐化することがある

LOG_DIR = Path(__file__).parent / "logs"


# --------------------------------------------------------------- ノート収集
@dataclass
class Note:
    note_idx: int
    note_num: int
    pulses: list[dict] = field(default_factory=list)  # 生 P 行 (device_ms, cc11, ...)
    off_cause: str = ""


class NoteCollector:
    """P/N 行を逐次受けてノート単位に組み立てる (watch_telemetry.NoteAggregator と同じ境界規則、
    ただし解析用に生パルスを保持したままにする)。"""

    def __init__(self, on_note_done=None):
        self.on_note_done = on_note_done
        self.cur: Note | None = None
        self.pending_cause = ""
        self.bad_lines = 0

    def feed(self, line: str) -> None:
        try:
            self._feed(line)
        except (ValueError, IndexError):
            self.bad_lines += 1

    def _feed(self, line: str) -> None:
        line = line.strip()
        if not line or line.startswith("#"):
            return
        f = line.split(",")
        if f[0] == "P" and len(f) >= 8:
            p = {
                "device_ms": int(f[1]), "note_idx": int(f[2]), "note_num": int(f[3]),
                "m": int(f[4]), "cc11": int(f[5]), "infer_ns": int(f[6]), "send_ns": int(f[7]),
            }
            if self.cur is not None and p["note_idx"] != self.cur.note_idx:
                self._close()
            if self.cur is None:
                self.cur = Note(note_idx=p["note_idx"], note_num=p["note_num"])
            self.cur.pulses.append(p)
            return
        if f[0] == "N" and len(f) >= 5:
            if int(f[1]) == 0:  # note off
                self.pending_cause = f[3]
                self._close()
            return

    def _close(self) -> None:
        if self.cur is None or not self.cur.pulses:
            self.cur = None
            return
        self.cur.off_cause = self.pending_cause
        self.pending_cause = ""
        done = self.cur
        self.cur = None
        if self.on_note_done:
            self.on_note_done(done)

    def flush(self) -> None:
        self._close()


def load_notes_from_file(path: str) -> list[Note]:
    notes: list[Note] = []
    coll = NoteCollector(on_note_done=notes.append)
    with open(path, encoding="utf-8") as f:
        for line in f:
            coll.feed(line)
    coll.flush()
    if coll.bad_lines:
        print(f"(パース不能 {coll.bad_lines} 行をスキップ)")
    return notes


def autodetect_port() -> str:
    cands = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not cands:
        sys.exit("シリアルポートが見つからない。--port で指定して (ls /dev/cu.*)")
    if len(cands) > 1:
        print(f"複数候補: {cands} -> {cands[0]} を使用 (違ったら --port)")
    return cands[0]


# --------------------------------------------------------------- ビューア
LINE_SPECS = [
    # (key, panel, label, color, style)
    ("expr_actual", 0, "expr 実機(cc11)", "tab:orange", {"marker": "o", "linestyle": "none", "markersize": 4}),
    ("expr_repro", 0, "expr 再現", "tab:blue", {"linestyle": "-"}),
    ("i_now", 1, "KRR i(t)", "tab:green", {"linestyle": "-", "marker": "."}),
    ("speed_now", 2, "弓速 speed_now", "tab:blue", {"linestyle": "-"}),
    ("speed_atk", 2, "弓速 speed_atk", "tab:red", {"linestyle": "--"}),
    ("pct", 3, "percentile p(speed_now由来, サステインdB用)", "tab:blue", {"linestyle": "-"}),
    ("p_atk", 3, "percentile p_atk(speed_atk由来, atk_dev用)", "tab:red", {"linestyle": "--"}),
    ("base_db", 4, "dB base(サステイン)", "tab:purple", {"linestyle": "--"}),
    ("db_atk", 4, "dB atk(アタック)", "tab:red", {"linestyle": "-."}),
    ("out_db", 4, "dB out(最終)", "tab:blue", {"linestyle": "-", "linewidth": 2}),
]
PANEL_TITLES = ["expr [0,127]", "KRR i [-1,1]", "bow speed [pulse/s]", "percentile p [0,1]", "dB"]


class NoteViewer:
    def __init__(self, models: Models):
        self.models = models
        self.notes: list[Note] = []
        self.cache: dict[int, list[dict]] = {}  # list index -> feed_note 結果
        self.idx = -1
        self.follow_live = True
        self.lock = threading.Lock()

        self.fig, self.axes = plt.subplots(len(PANEL_TITLES), 1, sharex=True, figsize=(10, 11))
        self.fig.subplots_adjust(right=0.8, hspace=0.35)
        for ax, title in zip(self.axes, PANEL_TITLES):
            ax.set_ylabel(title, fontsize=9)
            ax.grid(alpha=0.3)
        self.axes[-1].set_xlabel("t [ms] (ノート先頭からの相対時刻)")
        self.title = self.fig.suptitle("(ノートなし)")

        self.lines = {}
        for key, panel, label, color, style in LINE_SPECS:
            (ln,) = self.axes[panel].plot([], [], label=label, color=color, **style)
            self.lines[key] = ln
        for ax in self.axes:
            ax.legend(loc="upper right", fontsize=7)

        check_ax = self.fig.add_axes([0.82, 0.15, 0.16, 0.7])
        check_ax.set_axis_off()
        labels = [spec[2] for spec in LINE_SPECS]
        self.check = CheckButtons(check_ax, labels, [True] * len(labels))
        self.check.on_clicked(self._on_toggle)

        self.fig.canvas.mpl_connect("key_press_event", self._on_key)

    # -- データ追加 --------------------------------------------------
    def add_note(self, note: Note) -> None:
        with self.lock:
            self.notes.append(note)
            new_idx = len(self.notes) - 1
        if self.follow_live or self.idx < 0:
            self.idx = new_idx
            self.refresh()
        else:
            self._update_title_only()

    # -- 内部計算 (キャッシュ付き) -------------------------------------
    def _rows_for(self, i: int) -> list[dict]:
        if i not in self.cache:
            note = self.notes[i]
            device_ms = [p["device_ms"] for p in note.pulses]
            self.cache[i] = feed_note(device_ms, self.models)
        return self.cache[i]

    # -- 表示 ----------------------------------------------------------
    def refresh(self) -> None:
        if not self.notes or self.idx < 0:
            return
        note = self.notes[self.idx]
        rows = self._rows_for(self.idx)
        pulses = note.pulses

        t = [r["t_ms"] for r in rows]
        t_act = [p["device_ms"] - pulses[0]["device_ms"] for p in pulses]
        cc_act = [p["cc11"] for p in pulses]

        def col(key):
            return [r[key] if r.get(key) is not None else float("nan") for r in rows]

        self.lines["expr_actual"].set_data(t_act, cc_act)
        for key, _panel, _label, *_ in LINE_SPECS:
            if key == "expr_actual":
                continue  # 実機値は rows ではなく pulses から作る (上で処理済み)
            self.lines[key].set_data(t, col(key))

        flagged_t = [r["t_ms"] for r in rows if r.get("flagged")]
        for ax in self.axes:
            for artist in list(getattr(ax, "_flag_lines", [])):
                artist.remove()
            ax._flag_lines = [ax.axvline(ft, color="red", alpha=0.3, linewidth=1) for ft in flagged_t]

        for ax in self.axes:
            ax.relim()
            ax.autoscale_view()

        self._update_title_only()
        self.fig.canvas.draw_idle()

    def _update_title_only(self) -> None:
        note = self.notes[self.idx]
        n_flag = sum(1 for r in self._rows_for(self.idx) if r.get("flagged"))
        flag_note = f" [Δ比フラグ x{n_flag}]" if n_flag else ""
        follow = " [LIVE追従]" if self.follow_live else ""
        self.title.set_text(
            f"note_idx={note.note_idx} note_num={note.note_num} pulses={len(note.pulses)} "
            f"off_cause={note.off_cause or '(継続中)'}{flag_note}  "
            f"[{self.idx + 1}/{len(self.notes)}]{follow}"
        )

    # -- イベント --------------------------------------------------------
    def _on_toggle(self, label: str) -> None:
        for key, _panel, lbl, *_ in LINE_SPECS:
            if lbl == label:
                ln = self.lines[key]
                ln.set_visible(not ln.get_visible())
        self.fig.canvas.draw_idle()

    def _on_key(self, event) -> None:
        if not self.notes:
            return
        if event.key == "left":
            self.follow_live = False
            self.idx = max(0, self.idx - 1)
            self.refresh()
        elif event.key == "right":
            self.idx = min(len(self.notes) - 1, self.idx + 1)
            self.follow_live = self.idx == len(self.notes) - 1 and self.follow_live
            self.refresh()
        elif event.key == "home":
            self.follow_live = False
            self.idx = 0
            self.refresh()
        elif event.key == "end":
            self.idx = len(self.notes) - 1
            self.refresh()
        elif event.key == "f":
            self.follow_live = not self.follow_live
            if self.follow_live:
                self.idx = len(self.notes) - 1
                self.refresh()
            else:
                self._update_title_only()
                self.fig.canvas.draw_idle()
        elif event.key and event.key.isdigit():
            n = int(event.key)
            if 1 <= n <= len(LINE_SPECS):
                label = LINE_SPECS[n - 1][2]
                self.check.set_active(n - 1)
                # set_active はトグルを内部で行い on_clicked を呼ぶのでここでは何もしない


# --------------------------------------------------------------- ライブ監視
def run_live(port: str, baud: int, viewer: NoteViewer) -> None:
    import serial

    LOG_DIR.mkdir(exist_ok=True)
    log_path = LOG_DIR / f"telemetry_{datetime.now():%Y%m%d_%H%M%S}.csv"
    print(f"port={port} baud={baud}\nlog -> {log_path}")

    q: Queue[str] = Queue()
    stop = threading.Event()

    def reader():
        with serial.Serial(port, baud, timeout=1) as ser, open(log_path, "w") as log:
            log.write(f"# live_scope.py {port} @ {datetime.now().isoformat()}\n")
            while not stop.is_set():
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                log.write(line + "\n")
                q.put(line)

    threading.Thread(target=reader, daemon=True).start()

    coll = NoteCollector(on_note_done=viewer.add_note)

    def pump(_frame=None):
        drained = 0
        while drained < 500:
            try:
                line = q.get_nowait()
            except Empty:
                break
            coll.feed(line)
            drained += 1

    timer = viewer.fig.canvas.new_timer(interval=100)
    timer.add_callback(pump)
    timer.start()
    plt.show()
    stop.set()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--replay", metavar="LOG.csv", help="記録済みログを再生 (シリアル不要)")
    ap.add_argument("--port", help="シリアルポート (ライブ監視、省略時は自動検出)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--models", default=str(DEFAULT_MODELS_PATH), help="models_62.json のパス")
    args = ap.parse_args()

    models = Models(args.models)
    viewer = NoteViewer(models)

    if args.replay:
        notes = load_notes_from_file(args.replay)
        if not notes:
            sys.exit("ノートが 1 つも見つからない (P 行がないログ?)")
        viewer.notes = notes
        viewer.follow_live = False
        viewer.idx = 0
        viewer.refresh()
        plt.show()
    else:
        run_live(args.port or autodetect_port(), args.baud, viewer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
