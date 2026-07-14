#include "output.h"

#include <cstring>

namespace m62io {

// Grove 端子 (AtomS3 G1/G2) → Unit Synth UART の配線定数。
// soundCard/dataSender と同じ配線なので他プロジェクトと共通。
static constexpr int kSynthRxPin = 1;              // G2 → Unit Synth の TX (受信不要、未使用)
static constexpr int kSynthTxPin = 2;              // G1 → Unit Synth の RX
static constexpr uint32_t kSynthBaud = 31250;      // MIDI 標準ボーレート

void Output::begin() {
#ifdef M62_OUTPUT_MIDI
  Serial1.begin(kSynthBaud, SERIAL_8N1, kSynthRxPin, kSynthTxPin);

  // 音色初期化 (bank0 / program42 = チェロ)。ch0/ch1 両方に同じ設定を送る。
  // 2ch トグルで隣接ノートを別 ch に割り当てるため、事前に両 ch を揃えておく。
  for (uint8_t ch = 0; ch < 2; ++ch) {
    sendMidi3(0xB0 | ch, 0x00, 0x00);  // CC#0  Bank Select MSB = 0 (GM bank)
    sendMidi3(0xC0 | ch, 42, 0x00);    // Program Change = 42 (Cello; 2 バイト命令だが 3 バイト送っても Unit Synth は許容)
    sendMidi3(0xB0 | ch, 0x49, 0x01);  // CC#73 Attack Time = 1 (最速アタック; CC#11 の波形で制御するため)
    sendMidi3(0xB0 | ch, 0x4A, 50);    // CC#74 Release Time = 50 (リリースを適度に付ける)
    sendMidi3(0xB0 | ch, 0x5B, 0x00);  // CC#91 Reverb Send Level = 0 (リバーブなし)
    sendMidi3(0xB0 | ch, 0x5C, 0x00);  // CC#92 Hall Send Level = 0
    sendMidi3(0xB0 | ch, 0x5D, 0x00);  // CC#93 Delay Send Level = 0
  }
#endif
}

#ifdef M62_OUTPUT_MIDI
// MIDI 3 バイトメッセージを Serial1 へ送出。
// Program Change (0xC0) は仕様上 2 バイトだが Unit Synth は 3 バイト受けても問題ない。
void Output::sendMidi3(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t buf[3] = {status, d1, d2};
  Serial1.write(buf, (status & 0xF0) == 0xC0 ? 2 : 3); // PC は 2 バイト、他は 3 バイト
}
#endif

void Output::noteOn(int note_num, int velocity) {
#ifndef M62_LIVE_HID
  // PC 側デバッグログ (CDC)。ライブモードでは telemetry (Core 0) が N 行を出すため
  // ここで print しない — Core 1 から書くと Core 0 の CSV 行と混線する。
  Serial.printf("NOTE_ON %d v%d\n", note_num, velocity);
#endif
#ifdef M62_OUTPUT_MIDI
  cur_ch_ ^= 1;                         // ch0/ch1 をノートごとにトグル (デカップリング)
  uint8_t ch = (uint8_t)cur_ch_;
  // このチャンネルに 2 ノート前の音が残っている場合は先に off してから発音する
  if (last_note_[cur_ch_] >= 0) sendMidi3(0x80 | ch, last_note_[cur_ch_], 0);
  sendMidi3(0x90 | ch, note_num & 0x7F, velocity & 0x7F); // Note On
  last_note_[cur_ch_] = note_num & 0x7F; // 後で off するために記憶
#endif
}

void Output::noteOff(int note_num, const char* cause) {
  // 原因別カウンタを更新 (デバッグ出力で「何回距離切れ/Δ比切れか」を見るため)
  if (std::strcmp(cause, "distance") == 0) noteoff_dist_++;
  else if (std::strcmp(cause, "delta") == 0) noteoff_delta_++;
#ifndef M62_LIVE_HID
  // noteOn と同じ理由でライブモードでは print しない (N 行が代替)
  Serial.printf("NOTE_OFF %d cause=%s dist=%d delta=%d\n",
                note_num, cause, noteoff_dist_, noteoff_delta_);
#endif
#ifdef M62_OUTPUT_MIDI
  int n = note_num & 0x7F;
  // 2 ch 両方を探して off にする (どちらに残っているか分からないため)
  for (int ch = 0; ch < 2; ++ch) {
    if (last_note_[ch] == n) {
      sendMidi3(0x80 | (uint8_t)ch, n, 0); // Note Off (velocity=0)
      last_note_[ch] = -1;                 // off 済みとしてクリア
    }
  }
#endif
}

void Output::expr(int cc11_value) {
  // [0,127] にクランプ
  int v = cc11_value < 0 ? 0 : (cc11_value > 127 ? 127 : cc11_value);
#ifdef M62_OUTPUT_MIDI
  // デバッグ用: 音量を最大 127 の範囲で 2 倍にして送出 (Unit Synth の感度が低い場合の暫定対処)
  // v = std::min(v * 2, 127);
  sendMidi3(0xB0 | (uint8_t)cur_ch_, 0x0B, (uint8_t)v); // CC#11 Expression を現ノートの ch へ
#else
  Serial.printf("EXPR %d\n", v); // CDC モード: デバッグ用に数値を吐く
#endif
}

// parity 検証モード用。note_idx,t_ms,expr の CSV 行を CDC へ出力。
// reference/*.causal.csv と突合して C++↔Python の一致を確認する。
void Output::debugExprPoint(int note_idx, float t_ms, float expr_val) {
  Serial.printf("%d,%.3f,%.3f\n", note_idx, t_ms, expr_val);
}

}  // namespace m62io
