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
// サイクルカウンタは Arduino API (ESP.getCycleCount) を使用 — IDF4/5 両対応
#ifdef M62_LIVE_HID
#include <Wire.h>        // Joystick Unit (I2C) 用
#include <M5Unified.h>   // BtnA 読取用 (M5.begin は telemetry 側で実施済み)
#endif

#include "infer/m62.h"
#include "infer/causal.h"
#include "models_62.h"
#include "io/output.h"
#include "io/telemetry.h"
#include "sample_events.h"
#ifdef M62_LIVE_HID
#include "io/hid_input.h"
#endif

// グローバルオブジェクト。setup() で初期化し loop() で使い続ける。
static m62io::Output g_out;       // MIDI/CDC 出力経路
static m62::CausalEngine g_eng;   // 因果推論エンジン本体
static m62io::Telemetry g_tel;    // Core 0: LCD 表示 + PC 向け CSV

// レイテンシ計測用: パルス受信時のサイクルカウンタと HID イベントの生値。
// push() は同期実行 (onExpr は push 内から呼ばれる) なので単純なグローバルで足りる。
static uint32_t g_c0        = 0;  // パルス受信直後のサイクル値
static uint8_t  g_cur_m     = 0;  // 直近イベントの m
static int16_t  g_cur_note  = 0;  // 直近イベントの note_num
static uint32_t g_cur_ms    = 0;  // 直近イベントの device_ms

// 直近パルスの入力源。true=USB CDC からの記録リプレイ / false=GPIO(Serial2) HID。
// USB リプレイ中は CSV 送出を止める (テレメトリ emit_csv に反映)。sticky (次の入力で更新)。
static bool     g_src_usb   = false;

// 出力 expr にかける定数倍 (既定 1.0 = 素通し)。PC から "EXPRDB,<dB>" で設定する。
// 正典フィット expr = 10^((db - EXPR_INTERCEPT)/EXPR_SLOPE) (m62.cpp db_to_expr_one) より、
// dB の定数オフセット Δ は expr の定数倍 10^(Δ/EXPR_SLOPE) と厳密に等価。つまり包絡の
// 「形」を一切変えずレベルだけを動かせる。聴取実験で音量交絡を潰した刺激を作るための機構。
// dB 空間でスケール演算を行うという正典ルールに沿った実装 (CLAUDE.md)。
static float    g_expr_gain = 1.0f;

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

// ---- 演奏中パラメータ乗っ取り (BtnA で 4 モード巡回) ----
//   mode 0: なし (iA も音量も弓から)
//   mode 1: iA 乗っ取り (joystick X → i)
//   mode 2: 音量乗っ取り (fader → サステインパーセンタイル)
//   mode 3: 両方
static constexpr uint8_t  kJoyAddr = 0x52;   // Joystick Unit 初期型 I2C アドレス
static constexpr int      kJoySda  = 39;     // G39 (実機スキャンで確定: 0x52 応答はこの向き)
static constexpr int      kJoyScl  = 38;     // G38
static constexpr int      kFaderPin = 7;     // G7 = ADC1_CH6 (Unit Fader、実機で adc7 が追従を確認)
static constexpr uint32_t kInputPollMs = 15; // I2C/ADC 読取スロットル (HID レイテンシ保護)
static uint8_t  g_mode      = 0;
static uint32_t g_next_poll = 0;
// PC (DAW) から IEXT,<float> / PCT,<float> で値を固定したとき true。true の間は
// readJoystickY() / readFader() を呼ばず、物理入力の揺れで PC 指定値が上書きされない
// ようにする。実験刺激を再現可能に録るための機構。BtnA 物理操作か MODE コマンドで
// 手動に戻すと解除する。
static bool     g_i_ext_pc_lock = false;
static bool     g_pct_pc_lock   = false;

// mode に応じて CausalEngine の乗っ取りフラグを立て直す。
static void applyMode() {
  g_eng.override_i   = (g_mode == 1 || g_mode == 3);
  g_eng.override_pct = (g_mode == 2 || g_mode == 3);
  g_tel.setStatus(g_mode, g_eng.i_ext, g_eng.pct_ext);
}

// ---- デバッグ: 乗っ取り入力の生値を 500ms ごとに CDC へ吐く (切り分け用) ----
#define M62_OVERRIDE_DEBUG 0
#if M62_OVERRIDE_DEBUG
static int      g_dbg_joy_x    = -1;   // 直近の joystick X 生値 (-1=未受信)
static int      g_dbg_joy_n    = 0;    // requestFrom で実際に返ってきたバイト数
static int      g_dbg_fader    = -1;   // 直近の fader ADC 生値
static uint32_t g_next_dbg     = 0;
#endif

// Joystick Y (0..255, 中央128) を読んで i_ext [-1,1] に反映。失敗時は据え置き。
static void readJoystickY() {
  int n = Wire.requestFrom(kJoyAddr, (uint8_t)3); // 初期型は X,Y,Btn の 3 バイト連続読み
#if M62_OVERRIDE_DEBUG
  g_dbg_joy_n = n;
#endif
  if (n < 3 || Wire.available() < 3) return;
  Wire.read();                              // X (未使用)
  int y = Wire.read();
  Wire.read();                              // Btn (未使用)
#if M62_OVERRIDE_DEBUG
  g_dbg_joy_x = y;                          // デバッグ表示は joy_x 欄に Y 値を流用
#endif
  g_eng.i_ext = (y - 128) / 127.0f;          // 中央=0, 端=±1弱
}

// Unit Fader (0..4095) を読んで pct_ext [0,1] に反映。
static void readFader() {
  int raw = analogRead(kFaderPin);           // 12bit
#if M62_OVERRIDE_DEBUG
  g_dbg_fader = raw;
#endif
  g_eng.pct_ext = raw / 4095.0f;
}
#endif

// CausalEngine のコールバックを Output に接続して初期化する。
// lambda でキャプチャなし = 軽量関数ポインタ相当。
static void setupEngine() {
  // NoteOn: ノート開始時に呼ばれる
  g_eng.onNoteOn  = [](int note_num) {
    g_out.noteOn(note_num);
    m62io::NoteRecord nr{g_cur_ms, (int16_t)note_num, 1, ""};
    nr.emit_csv = !g_src_usb;        // USB リプレイ由来は CSV 抑止 (LCD は出る)
    g_tel.pushNote(nr);              // Core 0 へ (LCD 表示 + CSV)
  };

  // NoteOff: ノート終了時に呼ばれる。reason を文字列に変換してログに残す。
  g_eng.onNoteOff = [](int note_num, m62::NoteOffReason reason) {
    const char* cause = "?";
    switch (reason) {
      case m62::NoteOffReason::Distance:   cause = "distance"; break; // 遷移距離超過
      case m62::NoteOffReason::DeltaRatio: cause = "delta";    break; // Δt 比カット
      case m62::NoteOffReason::Flush:      cause = "flush";    break; // タイムアウト/終端
    }
    g_out.noteOff(note_num, cause);  // Serial=PCログ, Serial1=MIDI NoteOff
    m62io::NoteRecord nr{g_cur_ms, (int16_t)note_num, 0, ""};
    strncpy(nr.cause, cause, sizeof(nr.cause) - 1);
    nr.emit_csv = !g_src_usb;        // USB リプレイ由来は CSV 抑止
    g_tel.pushNote(nr);
  };

  // onExpr: パルス 1 個ごとに expr 値が確定したら呼ばれる (1 パルス 1 expr の核心)
  g_eng.onExpr    = [](int note_idx, float t_ms, float expr, float i_a) {
#ifdef M62_OUTPUT_MIDI
    (void)t_ms;                                   // MIDI モードでは t_ms は不要
    // EXPRDB で指定された dB オフセットを定数倍として適用 (既定 1.0 = 無変換)。
    // クランプ範囲は db_to_expr_one と同じ [EXPR_MIN, EXPR_MAX]。
    float e = expr * g_expr_gain;
    if (e < m62::EXPR_MIN) e = m62::EXPR_MIN;
    if (e > m62::EXPR_MAX) e = m62::EXPR_MAX;
    const int cc11 = (int)(e + 0.5f);             // 連続値を四捨五入
    const uint32_t c_inf = ESP.getCycleCount();  // ← 推論完了時点
    g_out.expr(cc11);                             // CC#11 即送出
    const uint32_t c_snd = ESP.getCycleCount();  // ← 送出完了時点
    // パルス受信 (g_c0) からの経過を ns 換算して Core 0 へ渡す
    m62io::PulseRecord r;
    r.device_ms = g_cur_ms;
    r.infer_ns  = m62io::cyclesToNs(c_inf - g_c0);
    r.send_ns   = m62io::cyclesToNs(c_snd - g_c0);
    r.note_idx  = (int16_t)note_idx;
    r.note_num  = g_cur_note;
    r.m         = g_cur_m;
    r.cc11      = (uint8_t)cc11;
    r.i_a       = i_a;                            // LCD iA 累積用 (オンセットは NAN)
    r.emit_csv  = !g_src_usb;                     // USB リプレイ由来は CSV 抑止
    g_tel.pushPulse(r);
#else
    (void)i_a;
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
  g_c0       = ESP.getCycleCount();  // レイテンシ計測の起点 (パルス受信)
  g_cur_m    = (uint8_t)m;
  g_cur_note = (int16_t)note_num;
  g_cur_ms   = device_ms;
  g_eng.push((int)device_ms, m, note_num); // CausalEngine に 1 イベントを投入 → 即 expr
}

#ifdef M62_LIVE_HID
// ---- USB CDC からの記録パルス列リプレイ ----
// DAW が USB CDC(Serial) へ "device_ms,m,note_num\n" を 1 行 1 パルスで流し込む。
// 出力は通常どおり Unit Synth(MIDI) だが、CSV テレメトリは g_src_usb で抑止される。
//
// タイムベース: engine の tick()/Δt は device_ms を基準にするが、tick() は millis()
// (リアル時計) と比較する。記録 device_ms は別セッションの epoch なのでそのまま渡すと
// tick が壊れる。そこでバースト(=1 ノート分の連続送信)の先頭で原点を取り、
//   local_ms = loc0 + (device_ms - dev0)
// とローカル millis epoch へ再基準化して渡す。記録 Δt はそのまま保存され、かつ
// millis() と同じ時間軸に乗るので tick()/無音 flush が正しく働く。
// バーストの切れ目は受信ギャップ (kReplayGapMs) で検出し原点を取り直す。
static constexpr uint32_t kReplayGapMs = 500;   // これ以上受信が空けば新バースト扱い
static char     g_rp_buf[48];
static uint8_t  g_rp_len       = 0;
static uint32_t g_rp_last_recv = 0;   // 最後に 1 バイト受けた millis
static bool     g_rp_origin    = false;
static uint32_t g_rp_dev0      = 0;    // バースト先頭の記録 device_ms
static uint32_t g_rp_loc0      = 0;    // バースト先頭のローカル millis

static void handleReplayLine(char* s) {
  // PC (DAW) から乗っ取りモードを遠隔切替: "MODE,<0-3>\n"。BtnA 押下と同じ
  // applyMode()/ログ行を通すので、UI 側は既存の "# mode=" パースがそのまま使える。
  if (strncmp(s, "MODE,", 5) == 0) {
    const long v = strtol(s + 5, nullptr, 10);
    g_mode = (uint8_t)(v & 3);
    g_i_ext_pc_lock = false;   // 手動モード切替で PC 固定は解除
    g_pct_pc_lock   = false;
    applyMode();
    Serial.printf("# mode=%u\n", g_mode);
    return;
  }
  // 実験刺激生成用: PC から i_ext を直接固定する "IEXT,<-1..1>\n"。joystick の
  // 揺れに左右されず ±1 ぴったりを再現可能に録るための経路 (readJoystickY は
  // g_i_ext_pc_lock 中スキップされる)。iA 乗っ取りビットだけを立てる (|=1) ので、
  // 先に PCT で音量乗っ取りを掛けてあってもそれを落とさない。
  if (strncmp(s, "IEXT,", 5) == 0) {
    const float v = strtof(s + 5, nullptr);
    g_eng.i_ext = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    g_i_ext_pc_lock = true;
    if (!g_eng.override_i) { g_mode |= 1; applyMode(); }
    Serial.printf("# iext=%.3f mode=%u\n", (double)g_eng.i_ext, g_mode);
    return;
  }
  // 同じく PC からサステインパーセンタイルを固定する "PCT,<0..1>\n"。fader の
  // 揺れを排除して全刺激のサステイン音量を揃えるために使う (readFader は
  // g_pct_pc_lock 中スキップ)。音量乗っ取りビットだけを立てる (|=2)。
  if (strncmp(s, "PCT,", 4) == 0) {
    const float v = strtof(s + 4, nullptr);
    g_eng.pct_ext = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    g_pct_pc_lock = true;
    if (!g_eng.override_pct) { g_mode |= 2; applyMode(); }
    Serial.printf("# pct=%.3f mode=%u\n", (double)g_eng.pct_ext, g_mode);
    return;
  }
  // 出力 expr のレベルだけを dB オフセットで動かす "EXPRDB,<dB>\n"。
  // 正典フィットより dB の定数オフセットは expr の定数倍と等価なので、包絡の形は
  // 完全に保たれる。音量交絡を潰した聴取実験刺激を作るための機構。
  // 0 を送れば無変換に戻る。上げ方向は EXPR_MAX でクランプされ形が潰れるので、
  // 揃えるときは必ず「一番小さい刺激に合わせて他を下げる」向きで使うこと。
  if (strncmp(s, "EXPRDB,", 7) == 0) {
    const float db = strtof(s + 7, nullptr);
    g_expr_gain = powf(10.0f, db / m62::EXPR_SLOPE);
    Serial.printf("# exprdb=%.3f gain=%.4f\n", (double)db, (double)g_expr_gain);
    return;
  }
  char* p = s;
  char* endp = nullptr;
  long dev = strtol(p, &endp, 10);  if (endp == p || *endp != ',') return; p = endp + 1;
  long m   = strtol(p, &endp, 10);  if (endp == p || *endp != ',') return; p = endp + 1;
  long note= strtol(p, &endp, 10);  if (endp == p) return;
  if (note == 0) return;                          // 無音パルスは無視 (live と同じ)
  const uint32_t now = millis();
  if (!g_rp_origin) { g_rp_dev0 = (uint32_t)dev; g_rp_loc0 = now; g_rp_origin = true; }
  const uint32_t local_ms = g_rp_loc0 + ((uint32_t)dev - g_rp_dev0);  // Δt 保存 + epoch 変換
  g_src_usb       = true;                         // 以降 CSV 抑止 (sticky)
  g_last_pulse_ms = now;                          // 無音 flush 用 (リアル millis)
  g_note_open     = true;
  onHidEvent((int)m, (int)note, local_ms);
}

static void pollReplaySerial() {
  const uint32_t now = millis();
  if (g_rp_origin && (now - g_rp_last_recv) > kReplayGapMs) {
    g_rp_origin = false;   // バースト境界 → 次の行で原点を取り直す
    g_rp_len = 0;
  }
  while (Serial.available() > 0) {
    const int c = Serial.read();
    g_rp_last_recv = millis();
    if (c == '\n' || c == '\r') {
      if (g_rp_len > 0) { g_rp_buf[g_rp_len] = '\0'; handleReplayLine(g_rp_buf); g_rp_len = 0; }
    } else if (g_rp_len < sizeof(g_rp_buf) - 1) {
      g_rp_buf[g_rp_len++] = (char)c;
    } else {
      g_rp_len = 0;         // 長すぎ = ノイズ、リセット
    }
  }
}
#endif

void setup() {
  Serial.begin(115200);  // USB CDC (PC 側デバッグ・parity 出力用)
  delay(300);            // USB 列挙が安定するまで少し待つ
  g_out.begin();         // Unit Synth 初期化 (Serial1 + 音色設定)
  g_tel.begin();         // LCD + Core 0 テレメトリタスク起動
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
  // 乗っ取り入力: Joystick Unit (Wire0=G38/G39) と Unit Fader (G8)
#if M62_OVERRIDE_DEBUG
  // I2C バススキャン: SDA/SCL 両方の組み合わせで試し、joystick (0x52) がどちらで
  // 見えるか確認する (Grove 配線の白/黄取り違え切り分け)。
  {
    const int pins[2][2] = {{kJoySda, kJoyScl}, {kJoyScl, kJoySda}};
    for (int k = 0; k < 2; ++k) {
      Wire.begin(pins[k][0], pins[k][1], 100000);
      Serial.printf("# i2c scan(SDA=G%d SCL=G%d):", pins[k][0], pins[k][1]);
      int found = 0;
      for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); ++found; }
      }
      if (!found) Serial.print(" (none)");
      Serial.println();
      Wire.end();
    }
  }
#endif
  Wire.begin(kJoySda, kJoyScl, 100000);   // 初期型は 100kHz 推奨
  analogReadResolution(12);                // fader を 0..4095 で読む
  pinMode(kFaderPin, INPUT);
  applyMode();                             // mode 0 (乗っ取りなし) で初期化
  Serial.println("# m62 live HID mode — Serial2(RX5/TX6) HID -> CausalEngine -> Unit Synth");
  Serial.println("# BtnA: 0(none)->1(iA=joyY)->2(vol=fader)->3(both)");
#endif
}

void loop() {
#ifdef M62_LIVE_HID
  // 1) Serial2 の HID パケットを解析し、弓パルス毎に CausalEngine へ投入。
  //    各イベントで CausalEngine が即 expr を吐く (1 パルス 1 expr)。
  g_hid.poll([](int m, int note_num, uint32_t device_ms) {
    // 鍵盤なし (note_num=0 = dataSender の無音) の弓パルスは完全無視。
    // 入れると CausalEngine が「ノート 0 (C-1)」として NoteOn まで送ってしまう (実測)。
    // タイマも触らない: 押鍵中ノートの残骸は下の tick/flush が通常経路で閉じる。
    if (note_num == 0) return;
    g_src_usb = false;                // GPIO(Serial2) 由来 → CSV 送出は従来通り
    g_last_pulse_ms = device_ms;      // 無音タイムアウトのために最終パルス時刻を記録
    g_note_open = true;               // flush 待ちノートがあるとマーク
    onHidEvent(m, note_num, device_ms);
  });

  // 1b) USB CDC から記録パルス列が来ていれば流し込む (DAW リプレイ)。
  //     HID(Serial2) とは排他運用 (弓を弾いてない時にだけ DAW が送る)。
  pollReplaySerial();

  // 2) Δ比タイムアウト: 最後のパルスから Δ[k]*DELTA_RATIO_CUT 超で無音中に音を切る。
  //    次パルスを待たずにリアルタイムで切ることで、弓を止めた瞬間に確実に off できる。
  //
  //    ただし USB リプレイ中 (g_src_usb) は呼ばない。tick() は実時計 millis() 基準だが、
  //    DAW のパルス送信には setTimeout/GC/USB バッファリングのジッタが乗る。実到着ギャップが
  //    たまたま Δ[k]*RATIO を超えるとノートを途中で誤カットし、残りが別ノート扱いになって
  //    expr が変わる (=リプレイの度に音が違う原因)。expr の中身を作る feed() は記録 Δt 列
  //    だけの決定的関数なので、実時計依存の tick() を止めれば再生は決定的になる。ノート末尾は
  //    下の無音フラッシュ (最終パルス到着基準、毎回一定) で閉じるので途中カットだけが消える。
  if (!g_src_usb) g_eng.tick(millis());

  // 3) 一定時間 (kSilenceFlushMs=300ms) 無音が続いたら末尾ノートを強制終了 (保険)。
  //    tick では切れなかった場合 (パルスが止まり tick も動かない等) のフォールバック。
  if (g_note_open && (millis() - g_last_pulse_ms) > kSilenceFlushMs) {
    g_eng.flush();        // 末尾ノートを Flush 理由で閉じる
    g_note_open = false;  // 次の flush 待ちフラグをリセット
  }

  // 4) 乗っ取り入力を 15ms 周期で読む (毎ループ I2C/ADC を叩くと HID→expr に遅延が乗る)。
  if (millis() >= g_next_poll) {
    g_next_poll = millis() + kInputPollMs;
    M5.update();                                  // BtnA 状態更新
    if (M5.BtnA.wasPressed()) {                   // 押すたびにモード巡回
      g_mode = (g_mode + 1) & 3;
      g_i_ext_pc_lock = false;                     // 物理操作で PC 固定を解除
      g_pct_pc_lock   = false;
      applyMode();
      Serial.printf("# mode=%u\n", g_mode);
    }
    // IEXT/PCT で PC 固定中は物理入力の揺れで上書きしない (刺激再現性のため)。
    if (g_eng.override_i   && !g_i_ext_pc_lock) readJoystickY();
    if (g_eng.override_pct && !g_pct_pc_lock)   readFader();
    // 読んだ最新値を LCD 表示へ (iA マーカー / p バー)
    g_tel.setStatus(g_mode, g_eng.i_ext, g_eng.pct_ext);
#if M62_OVERRIDE_DEBUG
    // 500ms ごとに生値を CDC へ (mode / I2C 受信バイト数 / joyX / fader ADC)
    if (millis() >= g_next_dbg) {
      g_next_dbg = millis() + 500;
      // G7/G8 両方の ADC を毎回読む (fader のアナログ線がどちらに来ているか切り分け)
      Serial.printf("# ov mode=%u joy_n=%d joy_x=%d adc7=%d adc8=%d i_ext=%.3f pct=%.3f\n",
                    g_mode, g_dbg_joy_n, g_dbg_joy_x,
                    analogRead(7), analogRead(8),
                    (double)g_eng.i_ext, (double)g_eng.pct_ext);
    }
#endif
  }
#else
  delay(1000);  // リプレイ自己検証は setup() で完了済み。loop() は何もしない。
#endif
}
