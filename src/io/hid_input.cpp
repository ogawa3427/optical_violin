#include "hid_input.h"

namespace m62io {

// HID 入力 UART (dataSender の Serial2 と同一配線)。
static constexpr int      kHidRxPin = 5;         // ESP32-S3 の GPIO5 を RX に使用
static constexpr int      kHidTxPin = 6;         // GPIO6 は TX (受信専用なので実質未使用)
static constexpr uint32_t kHidBaud  = 115200;    // CH559 Extender (ATOMIC USB Extender v1.0) の出力ボーレートと一致

// dataSender packetConfig 相当 (src/main.cpp setup())。
// CH559 Extender (ATOMIC USB Extender v1.0) が出力するパケット形式の定数定義。
static constexpr int  kTypeByteIndex    = 1;     // byte[1] でマウス/キーボードを区別
static constexpr char kMouseTypeValue   = 0x04;  // byte[1]=0x04 → マウスパケット
static constexpr char kKeyboardTypeValue= 0x08;  // byte[1]=0x08 → キーボードパケット
static constexpr int  kScrollByteIndex  = 14;    // byte[14] にスクロール値が入る
static constexpr char kScrollUpValue    = (char)0xFF; // 上スクロール (アップボウ) の値
static constexpr char kScrollDownValue  = 0x01;  // 下スクロール (ダウンボウ) の値

// HID スキャンコード → MIDI ノート番号。
// projects/dataSender/src/{tone.h,note.h} の tonedict を移植 (NOTE_* は MIDI 番号)。
// 注: dataSender は char(符号付き) で索いていたため 0x87 が引けない潜在バグがあった。
//     ここでは uint8_t に正規化してから索くので 0x87(=135)→D5 も正しく当たる。
static int noteFromScan(uint8_t code) {
  switch (code) {
    // 第 1 ブロック: キーボード左側の配列 (dataSender tone.h 前半)
    case 0x1D: return 64;  // Z → E4
    case 0x1B: return 65;  // X → F4
    case 0x06: return 66;  // C → F#4
    case 0x19: return 67;  // V → G4
    case 0x05: return 68;  // B → G#4
    case 0x11: return 69;  // N → A4
    case 0x10: return 70;  // M → A#4
    case 0x36: return 71;  // , → B4
    case 0x37: return 72;  // . → C5
    case 0x38: return 73;  // / → C#5
    case 0x87: return 74;  // International1 → D5 (dataSender の char バグ修正)

    // 第 2 ブロック: A 行 (home row)
    case 0x04: return 69;  // A → A4
    case 0x16: return 70;  // S → A#4
    case 0x07: return 71;  // D → B4
    case 0x09: return 72;  // F → C5
    case 0x0A: return 73;  // G → C#5
    case 0x0B: return 74;  // H → D5
    case 0x0D: return 75;  // J → D#5
    case 0x0E: return 76;  // K → E5
    case 0x0F: return 77;  // L → F5
    case 0x33: return 78;  // ; → F#5
    case 0x34: return 79;  // ' → G5

    // 第 3 ブロック: Q 行 (top row)
    case 0x14: return 74;  // Q → D5
    case 0x1A: return 75;  // W → D#5
    case 0x08: return 76;  // E → E5
    case 0x15: return 77;  // R → F5
    case 0x17: return 78;  // T → F#5
    case 0x1C: return 79;  // Y → G5
    case 0x18: return 80;  // U → G#5
    case 0x0C: return 81;  // I → A5
    case 0x12: return 82;  // O → A#5
    case 0x13: return 83;  // P → B5
    case 0x2F: return 84;  // [ → C6

    // 第 4 ブロック: 数字行
    case 0x1E: return 79;  // 1 → G5
    case 0x1F: return 80;  // 2 → G#5
    case 0x20: return 81;  // 3 → A5
    case 0x21: return 82;  // 4 → A#5
    case 0x22: return 83;  // 5 → B5
    case 0x23: return 84;  // 6 → C6
    case 0x24: return 85;  // 7 → C#6
    case 0x25: return 86;  // 8 → D6
    case 0x26: return 87;  // 9 → D#6
    case 0x27: return 88;  // 0 → E6
    case 0x2D: return 89;  // - → F6

    default:   return 0;   // 未登録スキャンコード = 無音扱い (hold_note_ を変えない)
  }
}

// --- dataSender の packet 判定 (isMousePacket/isScrollUp/Down) を移植 ---

// byte[1] がマウスタイプか判定。一部デバイスはキーボード型式で送ってくる例外あり。
static bool isMousePacket(const String& d) {
  if ((int)d.length() <= kTypeByteIndex) return false; // パケットが短すぎる
  if (d[kTypeByteIndex] == kMouseTypeValue) return true; // 通常パターン

  // 例外: byte[1]==0x08 でも byte[3]==0x04 && byte[4]==0x02 の場合はマウスとして扱う
  // (特定 HID デバイスの挙動)
  if ((int)d.length() > 4 && d[kTypeByteIndex] == kKeyboardTypeValue &&
      d[3] == 0x04 && d[4] == 0x02)
    return true;
  return false;
}

// byte[14] の値でアップボウ (上スクロール) か判定
static bool isScrollUp(const String& d) {
  if (!isMousePacket(d) || (int)d.length() <= kScrollByteIndex) return false;
  return d[kScrollByteIndex] == kScrollUpValue;
}

// byte[14] の値でダウンボウ (下スクロール) か判定
static bool isScrollDown(const String& d) {
  if (!isMousePacket(d) || (int)d.length() <= kScrollByteIndex) return false;
  return d[kScrollByteIndex] == kScrollDownValue;
}

void HidInput::begin() {
  // Serial2 を HID 入力用にオープン (dataSender と同じピン・ボーレート)
  Serial2.begin(kHidBaud, SERIAL_8N1, kHidRxPin, kHidTxPin);
}

void HidInput::poll(const PulseCb& onPulse) {
  // --- 1 行受信 (dataSender loop() の readStringUntil + 0xFE 境界処理) ---
  String rx = "";
  // 前回ループで 0xFE を検出していた場合: 次パケットの先頭バイトとして持ち越す
  if (stop_fe_) { stop_fe_ = false; rx += (char)0xFE; }

  if (Serial2.available()) {
    rx = Serial2.readStringUntil('\n');     // '\n' を区切りとして 1 パケット分を読む
    bool first = true;
    while (Serial2.available()) {
      // 次のパケット先頭の可能性があるバイトをバッファに追加
      if (first) { rx += '\n'; first = false; } // '\n' を明示的に追記 (readStringUntil が食ってしまうため)
      char c = Serial2.read();
      if (c == '\n' || c == (char)0xFE) {
        // '\n' は通常区切り。0xFE は次パケット先頭なので次ループへ持ち越す。
        if (c == (char)0xFE) stop_fe_ = true;
        break;                               // このパケットの終端
      }
      rx += c;
    }
  }
  if (rx.length() == 0) return;             // 受信データなし → 今回は何もしない

  const int len = rx.length();
  // 各バイト位置を変数に取り出す (dataSender の byte5〜byte18 に対応)
  char byte5  = len > 4  ? rx[4]  : 0;     // パケット内サブタイプ
  char byte15 = len > 13 ? rx[13] : 0;     // キー1 スキャンコード
  char byte16 = len > 14 ? rx[14] : 0;     // キー2 スキャンコード
  char byte17 = len > 15 ? rx[15] : 0;     // キー3 (修飾キー等の補助)
  char byte18 = len > 16 ? rx[16] : 0;     // キー4

  // --- キーボード packet → 押下中ノート更新 (dataSender byte5==0x06 ブロック) ---
  if (byte5 == 0x06) {
    if (byte15 == 0 && byte16 == 0 && byte17 == 0 && byte18 == 0) {
      held_note_ = 0;                                   // 全キー離鍵 → 無音
    } else if (byte15 != 0 && byte16 == 0 && byte17 == 0 && byte18 == 0) {
      held_note_ = noteFromScan((uint8_t)byte15);       // 1 鍵のみ押下
    } else if (byte15 == 0 && byte16 != 0 && byte17 == 0 && byte18 == 0) {
      held_note_ = noteFromScan((uint8_t)byte16);       // 2 番目スロットのみ有効
    } else if (byte15 != 0 && byte16 != 0 && byte17 == 0 && byte18 == 0) {
      // 2 鍵同時: 直前に押されていなかった側を新規ノートとみなす (dataSender 準拠)
      // past_b15_ が 0 だった (= byte15 が新規追加) ならば byte15 を採用
      if (past_b15_ == 0 && past_b16_ != 0) held_note_ = noteFromScan((uint8_t)byte15);
      else                                  held_note_ = noteFromScan((uint8_t)byte16);
    }
    // 次回の 2 鍵判定のために過去スキャンコードを保存
    past_b15_ = byte15;
    past_b16_ = byte16;
  }

  // --- マウススクロール packet → 弓パルス (m: 2=up / 1=down) ---
  // 押下中ノートを貼って 1 パルス = 1 イベントとして CausalEngine へ渡す。
  if (isScrollUp(rx))        onPulse(2, held_note_, millis()); // アップボウ
  else if (isScrollDown(rx)) onPulse(1, held_note_, millis()); // ダウンボウ
}

}  // namespace m62io
