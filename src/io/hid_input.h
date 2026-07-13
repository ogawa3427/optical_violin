// dataSender 由来の USB-HID パケットを Serial2 から受信・解析し、
// 1 ボウパルスごとに (m, note_num, device_ms) を吐くライブ入力。
//
// 配線は dataSender と同一:
//   Serial2 = HID 入力 (RX=5, TX=6, 115200)  ← 外部 USB-HID ホストモジュールから
//   (出力 Serial1 = Unit Synth, RX=1/TX=2 は io/output.cpp 側)
//
// 解析ロジックは projects/dataSender/src/main.cpp の loop() から移植:
//   - マウススクロール packet → ボウパルス (m: 1=down / 2=up)
//   - キーボード packet       → 押下中ノート (scancode→MIDI、無音=0)
// note_num の付与規約は dataAnalysis a04_m_to_noted_m と同じ
// (各 m イベントに「その時点で押下中のノート」を貼る)。
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <functional>

namespace m62io {

class HidInput {
 public:
  // onPulse(m, note_num, device_ms): 弓パルス 1 個ごとに呼ばれる。
  using PulseCb = std::function<void(int m, int note_num, uint32_t device_ms)>;

  void begin();                       // Serial2 を開く (RX=5, TX=6, 115200)
  void poll(const PulseCb& onPulse);  // loop() から毎回呼ぶ

 private:
  int  held_note_   = 0;     // 現在押下中の MIDI ノート (無音=0)
  bool stop_fe_     = false; // 0xFE 境界の持ち越し (dataSender と同じ)
  char past_b15_    = 0;     // 2 鍵同時押し時にどちらが新規かを判定 (dataSender 準拠)
  char past_b16_    = 0;
};

}  // namespace m62io
