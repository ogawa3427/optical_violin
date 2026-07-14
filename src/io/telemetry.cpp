// telemetry.h の実装。Core 0 タスク本体はここ。
#include "telemetry.h"

#include <M5Unified.h>
#include <cmath>
#include <cstring>

namespace m62io {

// ---- Core1/Core0 間の共有物 (単一キューと drop カウンタのみ) ----
static QueueHandle_t s_q = nullptr;          // TelemetryRecord (P/N 混載・発生順)
static volatile uint32_t s_pulse_drops = 0;  // キュー満杯で捨てたレコード数

// ---- Core 0 専用の統計 (タスク以外から触らない) ----
struct State {
  uint32_t pulse_count = 0;
  uint32_t note_count  = 0;
  uint32_t end_d       = 0;   // noteOff: distance
  uint32_t end_t       = 0;   // noteOff: delta (+ flush も時間系としてここ)
  int16_t  cur_note    = -1;  // 発音中ノート (-1=無音)
  uint8_t  last_cc11   = 0;
  uint8_t  last_m      = 0;   // 1=DOWN 2=UP
  char     last_cause[10] = "";
  // iA 累積ヒストグラム: x=0 → i=-1, x=127 → i=+1。ヒット数で明るくする。
  uint8_t  ia_hits[128] = {};
};
static State s_st;

// MIDI ノート番号 → 音名。シャープはスケッチ準拠で "G4#" 形式。
static void midiToName(int note, char* buf, size_t nbuf) {
  static const char* letters[] = {
      "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
  static const bool sharp[] = {
      false, true, false, true, false, false, true, false, true, false, true, false};
  if (note < 0 || note > 127) {
    snprintf(buf, nbuf, "--");
    return;
  }
  int pc = note % 12;
  int oct = note / 12 - 1;
  if (sharp[pc])
    snprintf(buf, nbuf, "%s%d#", letters[pc], oct);
  else
    snprintf(buf, nbuf, "%s%d", letters[pc], oct);
}

// iA ヒット数 → 色 (見えやすいシアン → 白寄り)。n=0 は呼ばない想定。
static uint16_t iaColor(uint8_t n) {
  // 1発目から十分明るく、重なりで白に近づける
  uint8_t t = n > 8 ? 8 : n;
  uint8_t g = (uint8_t)(140 + t * 14);  // 140..252
  uint8_t b = 255;
  uint8_t r = (uint8_t)(40 + t * 26);   // 40..248
  return M5.Display.color565(r, g, b);
}

uint32_t cyclesToNs(uint32_t cycles) {
  static uint32_t mhz = getCpuFrequencyMhz();  // AtomS3R 既定 240
  return (uint32_t)((uint64_t)cycles * 1000u / mhz);
}

void Telemetry::begin() {
  // LCD 初期化 (M5Unified が AtomS3R / GC9107 を自動検出)。
  // Serial は main 側で begin 済みなので M5 には触らせない。
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5.begin(cfg);
  M5.Display.setBrightness(64);

  s_q = xQueueCreate(256, sizeof(TelemetryRecord));
  // 優先度 1 (低)。演奏パスは Core 1 なのでそもそも競合しない。
  xTaskCreatePinnedToCore(taskEntry, "m62_telemetry", 8192, this, 1,
                          nullptr, 0 /* Core 0 */);
}

void Telemetry::pushPulse(const PulseRecord& r) {
  TelemetryRecord tr;
  tr.kind = 'P';
  tr.pulse = r;
  if (!s_q || xQueueSend(s_q, &tr, 0) != pdTRUE) ++s_pulse_drops;
}

void Telemetry::pushNote(const NoteRecord& r) {
  TelemetryRecord tr;
  tr.kind = 'N';
  tr.note = r;
  if (!s_q || xQueueSend(s_q, &tr, 0) != pdTRUE) ++s_pulse_drops;
}

void Telemetry::taskEntry(void* self) {
  static_cast<Telemetry*>(self)->taskLoop();
}

void Telemetry::taskLoop() {
#ifdef M62_LIVE_HID
  Serial.println("# telemetry: P,device_ms,note_idx,note_num,m,cc11,infer_ns,send_ns,i_a"
                 " | N,ev,note_num,cause,device_ms");
#endif
  uint32_t next_draw = 0;
  char line[112];
  for (;;) {
    // 1) 単一キューを空になるまで発生順に吸い上げ (統計更新 + CSV)
    TelemetryRecord tr;
    while (xQueueReceive(s_q, &tr, 0) == pdTRUE) {
      int n = 0;
      if (tr.kind == 'P') {
        const PulseRecord& p = tr.pulse;
        ++s_st.pulse_count;
        s_st.last_cc11 = p.cc11;
        s_st.last_m    = p.m;
        // iA 累積: 有限値だけ 1px カラムに加算 (オンセット NAN は無視)
        if (std::isfinite(p.i_a)) {
          float clamped = p.i_a;
          if (clamped < -1.0f) clamped = -1.0f;
          if (clamped >  1.0f) clamped =  1.0f;
          int x = (int)((clamped + 1.0f) * 0.5f * 127.0f + 0.5f);
          if (x < 0) x = 0;
          if (x > 127) x = 127;
          if (s_st.ia_hits[x] < 255) ++s_st.ia_hits[x];
        }
#ifdef M62_LIVE_HID
        n = snprintf(line, sizeof(line), "P,%lu,%d,%d,%u,%u,%lu,%lu,%.4f\r\n",
                     (unsigned long)p.device_ms, p.note_idx, p.note_num,
                     p.m, p.cc11, (unsigned long)p.infer_ns,
                     (unsigned long)p.send_ns, (double)p.i_a);
#endif
      } else {  // 'N'
        const NoteRecord& nr = tr.note;
        if (nr.on) {
          s_st.cur_note = nr.note_num;
          ++s_st.note_count;
        } else {
          s_st.cur_note = -1;
          s_st.last_m   = 0;
          strncpy(s_st.last_cause, nr.cause, sizeof(s_st.last_cause) - 1);
          s_st.last_cause[sizeof(s_st.last_cause) - 1] = '\0';
          // d = distance / t = delta(+flush)。noteOff で iA ヒストをリセット。
          if (strcmp(nr.cause, "distance") == 0) {
            ++s_st.end_d;
          } else {
            // delta / flush / その他 → 時間系カウンタ t
            ++s_st.end_t;
          }
          memset(s_st.ia_hits, 0, sizeof(s_st.ia_hits));
          s_st.last_cc11 = 0;
        }
#ifdef M62_LIVE_HID
        n = snprintf(line, sizeof(line), "N,%u,%d,%s,%lu\r\n", nr.on,
                     nr.note_num, nr.cause, (unsigned long)nr.device_ms);
#endif
      }
      // CDC バッファに空きがある時だけ書く (PC が読んでなくてもブロックしない)
      if (n > 0 && Serial.availableForWrite() >= n)
        Serial.write((uint8_t*)line, n);
    }
    // 2) LCD は 100ms 周期 (10fps) で再描画
    uint32_t now = millis();
    if (now >= next_draw) {
      draw();
      next_draw = now + 100;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void Telemetry::draw() {
  static M5Canvas canvas(&M5.Display);
  static bool inited = false;
  if (!inited) {
    canvas.createSprite(128, 128);
    inited = true;
  }
  canvas.fillSprite(TFT_BLACK);

  const bool active = (s_st.cur_note >= 0);

  // ========== 1段目: 音名 | 弓方向 ==========
  // 音名 (左)
  char name[8];
  if (active) midiToName(s_st.cur_note, name, sizeof(name));
  else        snprintf(name, sizeof(name), "--");
  canvas.setTextSize(2);
  canvas.setTextColor(active ? TFT_WHITE : TFT_DARKGREY);
  canvas.setCursor(2, 2);
  canvas.print(name);

  // 中央セパレータ
  canvas.drawFastVLine(72, 1, 18, TFT_DARKGREY);

  // 弓方向 (右): m=2 UP=ピンク / m=1 DOWN=水色 / 無音=グレー
  canvas.setCursor(78, 2);
  if (!active || s_st.last_m == 0) {
    canvas.setTextColor(TFT_DARKGREY);
    canvas.print("--");
  } else if (s_st.last_m == 2) {
    canvas.setTextColor(canvas.color565(255, 105, 180));  // ピンク
    canvas.print("UP");
  } else {
    canvas.setTextColor(canvas.color565(80, 200, 255));   // 水色
    canvas.print("DN");
  }

  // ========== 2段目: expr メーター [0,127] ==========
  canvas.drawRect(2, 24, 124, 12, TFT_DARKGREY);
  int w = (int)s_st.last_cc11 * 122 / 127;
  if (w > 0) canvas.fillRect(3, 25, w, 10, TFT_CYAN);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(4, 26);
  canvas.printf("%3u", s_st.last_cc11);

  // ========== 3段目: iA 累積 (左=-1 … 右=+1) ==========
  constexpr int kIaY = 40;
  constexpr int kIaH = 42;
  canvas.drawRect(0, kIaY, 128, kIaH, TFT_DARKGREY);
  // 中央 (i=0) ガイド
  canvas.drawFastVLine(64, kIaY + 1, kIaH - 2, canvas.color565(40, 40, 40));
  for (int x = 0; x < 128; ++x) {
    uint8_t h = s_st.ia_hits[x];
    if (h == 0) continue;
    canvas.drawFastVLine(x, kIaY + 1, kIaH - 2, iaColor(h));
  }
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.setCursor(2, kIaY + 2);
  canvas.print("iA");
  canvas.setCursor(2, kIaY + kIaH - 10);
  canvas.print("-1");
  canvas.setCursor(110, kIaY + kIaH - 10);
  canvas.print("+1");

  // ========== 4段目: pulse / note を1行 ==========
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(2, 86);
  canvas.printf("p %lu  n %lu", (unsigned long)s_st.pulse_count,
                (unsigned long)s_st.note_count);

  // ========== 5段目: end reason (size2) ==========
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_YELLOW);
  canvas.setCursor(2, 98);
  canvas.printf("d%lu", (unsigned long)s_st.end_d);
  canvas.setCursor(50, 98);
  canvas.printf("t%lu", (unsigned long)s_st.end_t);

  // 直前 end reason: distance=橙 / delta・flush=紫 (2色)
  const bool is_dist = (strcmp(s_st.last_cause, "distance") == 0);
  if (s_st.last_cause[0] == '\0') {
    canvas.setTextColor(TFT_DARKGREY);
    canvas.setCursor(2, 114);
    canvas.print("---");
  } else {
    canvas.setTextColor(is_dist ? TFT_ORANGE : canvas.color565(200, 80, 255));
    canvas.setCursor(2, 114);
    if (is_dist) canvas.print("dist");
    else if (strcmp(s_st.last_cause, "delta") == 0) canvas.print("delta");
    else if (strcmp(s_st.last_cause, "flush") == 0) canvas.print("flush");
    else canvas.print(s_st.last_cause);
  }

  if (s_pulse_drops) {
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_RED);
    canvas.setCursor(90, 118);
    canvas.printf("dr%lu", (unsigned long)s_pulse_drops);
  }

  canvas.pushSprite(0, 0);
}

}  // namespace m62io
