// 1パルスあたりの CausalEngine::push() コストを実測するマイクロベンチ (native専用)。
//   g++ -O2 -std=c++17 -I include -I src/infer test/bench_causal.cpp src/infer/m62.cpp -o /tmp/bench
// sample_events.h を多数回流して push() の平均/最悪時間を出す。
#include "../src/infer/m62.h"
#include "../src/infer/causal.h"
#include "models_62.h"
#include "sample_events.h"

#include <cstdio>
#include <chrono>
#include <vector>
#include <algorithm>

int main() {
  using clk = std::chrono::steady_clock;

  // sample_events を 1 ノート分のパルス列として使う (m=2 連続, note 一定)。
  // 各 push() を個別計測し、外れ値を見るため全サンプルを保持。
  std::vector<double> us;            // per-push 時間 [µs]
  long emitted = 0;

  const int REPEAT = 200;            // ノート列を何回流すか (統計安定化)
  for (int r = 0; r < REPEAT; ++r) {
    m62::CausalEngine eng;
    eng.onExpr = [&](int, float, float){ emitted++; };  // 出力はカウントのみ
    for (int i = 0; i < m62_sample::N; ++i) {
      const auto& e = m62_sample::EVENTS[i];
      auto t0 = clk::now();
      eng.push(e.device_ms, e.m, e.note_num);
      auto t1 = clk::now();
      us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    eng.flush();
  }

  std::sort(us.begin(), us.end());
  double sum = 0; for (double v : us) sum += v;
  auto pct = [&](double p){ return us[(size_t)(p * (us.size() - 1))]; };

  std::printf("pushes      : %zu  (emitted %ld expr)\n", us.size(), emitted);
  std::printf("mean        : %.3f us\n", sum / us.size());
  std::printf("median (p50): %.3f us\n", pct(0.50));
  std::printf("p90         : %.3f us\n", pct(0.90));
  std::printf("p99         : %.3f us\n", pct(0.99));
  std::printf("max         : %.3f us\n", us.back());
  return 0;
}
