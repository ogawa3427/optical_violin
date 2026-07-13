// 62 パイプライン AtomS3R 実機ファーム (PlatformIO / Arduino)
//
// データ経路:  (m, note_num, device_ms) → CausalEngine → 1 パルス毎に expr →
//              出力 (CDC デバッグ / Grove Unit Synth CC#11)
//
// **因果(causal)・逐次**: m_event が 1 個来るたびに expr を 1 個確定して即出力する
// (plan §0 の本来のゴール)。ノート完成を待たない。i(t) は走査中の trailing LPF、
// dynamic handoff は各ステップの仮 t_end で動く。詳細は src/infer/causal.h。
//
// 2 モード (build_flag で切替):
//   既定 (リプレイ自己検証): 起動時に include/sample_events.h を CausalEngine へ流し、
//       note_idx,t_ms,expr を CDC へ出力 → PC で reference/*.causal.csv と突合できる。
//   -D M62_LIVE_HID: onHidEvent() に dataSender 由来の HID イベントを流す実演奏モード。

#include <Arduino.h>

#include "infer/m62.h"
#include "infer/causal.h"
#include "models_62.h"
#include "io/output.h"
#include "sample_events.h"
#ifdef M62_LIVE_HID
#include "io/hid_input.h"
#endif

// グローバルオブジェクト。setup() で初期化し loop() で使い続ける。
static m62io::Output g_out;       // MIDI/CDC 出力経路
static m62::CausalEngine g_eng;   // 因果推論エンジン本体

#ifdef M62_LIVE_HID
static m62io::HidInput g_hid;     // HID パーサ (M62_LIVE_HID 時のみ使用)

// 末尾ノートを閉じる無音タイムアウト。
// CausalEngine は「次のイベントが来た時」しか境界を判定しないため、弓が完全に
// 止まって以降パルスが来ない末尾ノートだけは自前タイマで flush する。
// transition_distance の gap_boost(dt-100ms を×0.75 加算, threshold=100) より、
// 約234ms 以上空ければ再開後の最初のパルスは必ず新ノートとして立ち上がるので、
// この値はそれを上回る 300ms にしてエンジンの区切りと齟齬させない。
static constexpr uint32_t kSilenceFlushMs = 300;
static uint32_t g_last_pulse_ms = 0;  // 最後のパルスを受けた時刻 (millis)
static bool     g_note_open     = false; // flush 待ちの末尾ノートがあるか
#endif

// CausalEngine のコールバックを Output に接続して初期化する。
// lambda でキャプチャなし = 軽量関数ポインタ相当。
static void setupEngine() {
  // NoteOn: ノート開始時に呼ばれる
  g_eng.onNoteOn  = [](int note_num) { g_out.noteOn(note_num); };

  // NoteOff: ノート終了時に呼ばれる。reason を文字列に変換してログに残す。
  g_eng.onNoteOff = [](int note_num, m62::NoteOffReason reason) {
    const char* cause = "?";
    switch (reason) {
      case m62::NoteOffReason::Distance:   cause = "distance"; break; // 遷移距離超過
      case m62::NoteOffReason::DeltaRatio: cause = "delta";    break; // Δt 比カット
      case m62::NoteOffReason::Flush:      cause = "flush";    break; // タイムアウト/終端
    }
    g_out.noteOff(note_num, cause);  // Serial=PCログ, Serial1=MIDI NoteOff
  };

  // onExpr: パルス 1 個ごとに expr 値が確定したら呼ばれる (1 パルス 1 expr の核心)
  g_eng.onExpr    = [](int note_idx, float t_ms, float expr) {
#ifdef M62_OUTPUT_MIDI
    (void)note_idx; (void)t_ms;                   // MIDI モードでは t_ms は不要
    g_out.expr((int)(expr + 0.5f));               // 連続値を四捨五入して CC#11 即送出
#else
    g_out.debugExprPoint(note_idx, t_ms, expr);   // CDC: parity 検証用に t_ms も出す
#endif
  };
}

// dataSender の HID パース結果をここへ流す (live モード用フック)。
// onHidEvent は hid_input.cpp の poll コールバックから呼ばれる。
//   m       : 1=下スクロール / 2=上スクロール (= dataSender の m)
//   note_num: 押下中の MIDI ノート番号 (無音=0)
//   device_ms: イベント時刻 (millis 等)
void onHidEvent(int m, int note_num, uint32_t device_ms) {
  g_eng.push((int)device_ms, m, note_num); // CausalEngine に 1 イベントを投入 → 即 expr
}

void setup() {
  Serial.begin(115200);  // USB CDC (PC 側デバッグ・parity 出力用)
  delay(300);            // USB 列挙が安定するまで少し待つ
  g_out.begin();         // Unit Synth 初期化 (Serial1 + 音色設定)
  setupEngine();         // CausalEngine にコールバックを接続

#ifndef M62_LIVE_HID
  // --- リプレイ自己検証モード ---
  // 固定サンプルイベント (sample_events.h) を全部流して CDC に出力し、
  // PC 側で reference/*.causal.csv と diff する。
  Serial.println("# m62 causal replay self-check (compare with reference/*.causal.csv)");
  Serial.println("note_idx,t_ms,expr");    // CSV ヘッダ
  for (int i = 0; i < m62_sample::N; ++i) {
    const auto& e = m62_sample::EVENTS[i];
    g_eng.push(e.device_ms, e.m, e.note_num); // 各イベントを逐次投入
  }
  g_eng.flush();         // 末尾ノートを明示的に閉じる
  Serial.println("# done");
#else
  // --- ライブ演奏モード ---
  g_hid.begin();         // HID UART (Serial2) を開く
  Serial.println("# m62 live HID mode — Serial2(RX5/TX6) HID -> CausalEngine -> Unit Synth");
#endif
}

void loop() {
#ifdef M62_LIVE_HID
  // 1) Serial2 の HID パケットを解析し、弓パルス毎に CausalEngine へ投入。
  //    各イベントで CausalEngine が即 expr を吐く (1 パルス 1 expr)。
  g_hid.poll([](int m, int note_num, uint32_t device_ms) {
    g_last_pulse_ms = device_ms;      // 無音タイムアウトのために最終パルス時刻を記録
    g_note_open = true;               // flush 待ちノートがあるとマーク
    onHidEvent(m, note_num, device_ms);
  });

  // 2) Δ比タイムアウト: 最後のパルスから Δ[k]*DELTA_RATIO_CUT 超で無音中に音を切る。
  //    次パルスを待たずにリアルタイムで切ることで、弓を止めた瞬間に確実に off できる。
  g_eng.tick(millis());

  // 3) 一定時間 (kSilenceFlushMs=300ms) 無音が続いたら末尾ノートを強制終了 (保険)。
  //    tick では切れなかった場合 (パルスが止まり tick も動かない等) のフォールバック。
  if (g_note_open && (millis() - g_last_pulse_ms) > kSilenceFlushMs) {
    g_eng.flush();        // 末尾ノートを Flush 理由で閉じる
    g_note_open = false;  // 次の flush 待ちフラグをリセット
  }
#else
  delay(1000);  // リプレイ自己検証は setup() で完了済み。loop() は何もしない。
#endif
}
