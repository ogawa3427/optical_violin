// テレメトリ (LCD 状態表示 + PC 向け CSV 送出) — Core 0 専用タスク。
//
// 設計方針: 演奏パス (Core 1: HID → CausalEngine → MIDI) を絶対に待たせない。
//   - Core 1 からは pushPulse()/pushNote() でレコードを FreeRTOS キューに投入するだけ
//     (キュー満杯なら捨てる = ノンブロッキング。捨てた数は drops として LCD に出す)
//   - パルスとノート境界は **単一キュー** に積む。別キューにすると Core 0 の吸い上げ順で
//     NoteOff が次ノートのパルスより後に出力され、PC 側のノート集計が壊れる (実測済み)
//   - Core 0 タスクがキューを吸い上げて統計更新・LCD 描画 (~10fps)・CSV 送出を行う
//   - レイテンシは CPU サイクルカウンタ (240MHz → 1cycle ≈ 4.17ns) で計測した値を
//     Core 1 側で ns に換算して渡す
//
// CSV 形式 (USB CDC / M62_LIVE_HID 時のみ送出。リプレイ parity 出力を汚さないため):
//   P,device_ms,note_idx,note_num,m,cc11,infer_ns,send_ns,i_a  … 1 パルス 1 行
//   N,ev,note_num,cause,device_ms                               … ノート境界 (ev: 1=on 0=off)
//   ※ i_a は末尾追加。旧パーサ (列 0..7) はそのまま動く。
//
// emit_csv: レコードごとの CSV 送出可否。GPIO(Serial2) HID 由来のパルスは true
//   (従来通り PC へ CSV)、USB CDC からのパルスリプレイ由来は false にして CSV を
//   止める (DAW がパルスを送り込んでいる入力チャネルに出力を被せない = 混線回避)。
//   LCD 用統計 (s_st) は emit_csv に関わらず常に更新するので表示は生きる。
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace m62io {

// 1 パルス分の計測レコード (Core1 → Core0)
struct PulseRecord {
  uint32_t device_ms;  // イベント時刻 (HID 由来)
  uint32_t infer_ns;   // パルス受信 → expr 確定まで
  uint32_t send_ns;    // パルス受信 → CC#11 送出完了まで
  int16_t  note_idx;   // CausalEngine のノート通番
  int16_t  note_num;   // MIDI ノート番号
  uint8_t  m;          // 1=下スクロール 2=上スクロール
  uint8_t  cc11;       // 送出した CC#11 値 [0,127]
  float    i_a;        // 走査中の思念スコア (NAN = オンセットの expr=0 等)
  bool     emit_csv;   // CSV 送出可否 (USB リプレイ由来なら false)
};

// ノート境界レコード (Core1 → Core0)
struct NoteRecord {
  uint32_t device_ms;
  int16_t  note_num;
  uint8_t  on;         // 1=noteOn / 0=noteOff
  char     cause[10];  // noteOff 理由 ("distance"|"delta"|"flush")、noteOn は ""
  bool     emit_csv;   // CSV 送出可否 (USB リプレイ由来なら false)
};

// 単一キューに積む共用レコード (発生順を保存するため P/N を 1 本にまとめる)
struct TelemetryRecord {
  uint8_t kind;        // 'P' or 'N'
  union {
    PulseRecord pulse;
    NoteRecord  note;
  };
};

class Telemetry {
 public:
  // M5 (LCD) 初期化 + キュー作成 + Core 0 タスク起動。setup() から呼ぶ。
  void begin();
  // 以下 2 つは演奏パス (Core 1) から呼ぶ。ノンブロッキング保証。
  void pushPulse(const PulseRecord& r);
  void pushNote(const NoteRecord& r);
  // 乗っ取り状態 (mode 0=なし 1=iA 2=p 3=iA+p, 現在の iA/p 値) を LCD 表示用に渡す。Core 1 から。
  void setStatus(uint8_t mode, float i_ext, float pct_ext);

 private:
  static void taskEntry(void* self);
  void taskLoop();   // Core 0: キュー吸い上げ → 統計 → CSV → 描画
  void draw();       // LCD 1 フレーム描画 (M5Canvas → pushSprite)
};

// CPU サイクル数 → ns 換算 (CPU クロックは起動時に取得してキャッシュ)
uint32_t cyclesToNs(uint32_t cycles);

}  // namespace m62io
