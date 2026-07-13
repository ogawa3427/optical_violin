// native parity ハーネス: raw.csv (device_ms,m,note_num) を読み、
// C++ 推論コアを回して note_idx,t_ms,expr を stdout に CSV 出力する。
//
//   ./m62_native <raw.csv>            バッチ版 (notebook 62 と突合 → *.expr.csv)
//   ./m62_native <raw.csv> --causal   因果版 1パルス1expr (Python 因果と突合 → *.causal.csv)
#include "../src/infer/m62.h"
#include "../src/infer/stream.h"
#include "../src/infer/causal.h"
#include "models_62.h"  // m62::MIN_DUR_MS

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <raw.csv>\n", argv[0]);
    return 2;
  }
  FILE* f = std::fopen(argv[1], "r");
  if (!f) { std::perror("fopen"); return 1; }

  std::vector<m62::Event> events;
  char line[256];
  bool header = true;
  while (std::fgets(line, sizeof(line), f)) {
    if (header) { header = false; continue; }  // skip "device_ms,m,note_num"
    int dms, m, nn;
    if (std::sscanf(line, "%d,%d,%d", &dms, &m, &nn) == 3) {
      events.push_back({dms, m, nn});
    }
  }
  std::fclose(f);

  bool causal = (argc >= 3 && std::strcmp(argv[2], "--causal") == 0);
  std::printf("note_idx,t_ms,expr\n");

  if (causal) {
    // 因果版: 1 パルス 1 expr。CausalEngine をそのまま使う (実機と同一コード)。
    int npts = 0;
    m62::CausalEngine eng;
    eng.onExpr = [&](int ni, float t, float expr) {
      std::printf("%d,%.6f,%.6f\n", ni, t, expr);
      npts++;
    };
    for (const auto& e : events) eng.push(e.device_ms, e.m, e.note_num);
    eng.flush();
    std::fprintf(stderr, "emitted %d causal points\n", npts);
    return 0;
  }

  // バッチ版: 逐次セグメンタ → build_note_data → note_expr_waveform
  int nidx = 0;
  auto on_note = [&](const m62::Note& note) {
    float dur = (float)(note.end_ms - note.start_ms);
    if (dur < m62::MIN_DUR_MS) return;
    m62::NoteData nd = m62::build_note_data(note);
    if (!nd.valid) return;
    std::vector<m62::ExprPoint> wave = m62::note_expr_waveform(nd);
    for (const auto& p : wave) {
      std::printf("%d,%.6f,%.6f\n", nidx, p.t_ms, p.expr);
    }
    nidx++;
  };
  m62::StreamSegmenter seg(on_note);
  for (const auto& e : events) seg.push(e.device_ms, e.m, e.note_num);
  seg.flush();
  std::fprintf(stderr, "emitted %d notes\n", nidx);
  return 0;
}
