// 出力経路の抽象 (plan §4.4)。
//   Serial  (USB CDC)     = PC デバッグ (常時・NOTE_ON/OFF ログ等)
//   Serial1 (G1/G2 31250) = Grove Unit Synth MIDI (M62_OUTPUT_MIDI 時)
//   Serial2 (RX5/TX6)     = HID 入力 (M62_LIVE_HID 時、hid_input.cpp)
//
// M62_OUTPUT_MIDI 有効時も Serial へのデバッグ出力は止めない (MIDI と PC ログは別 UART)。
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace m62io {

class Output {
 public:
  void begin();
  void noteOn(int note_num, int velocity = 100);
  // cause: "distance" | "delta" | "flush"
  void noteOff(int note_num, const char* cause);
  void expr(int cc11_value);            // CC#11 Expression [0,127]
  // デバッグ行: ノート完成時に 1 点ずつ吐く (note_idx,t_ms,expr)
  void debugExprPoint(int note_idx, float t_ms, float expr_val);

 private:
  int noteoff_dist_  = 0;
  int noteoff_delta_ = 0;
#ifdef M62_OUTPUT_MIDI
  void sendMidi3(uint8_t status, uint8_t d1, uint8_t d2);
  int last_note_[2] = {-1, -1};  // ch ごとの最終 noteOn (デカップリング用)
  int cur_ch_ = 1;               // 現ノートの ch。noteOn で 0/1 トグル
#endif
};

}  // namespace m62io
