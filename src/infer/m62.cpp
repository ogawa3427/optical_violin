// 62 パイプライン 推論コア 実装。notebook 62 とのバッチ数値 parity を狙う。
#include "m62.h"
#include "models_62.h"

#include <cmath>
#include <algorithm>
#include <vector>

namespace m62 {

// ---------------------------------------------------------------------------
// 数値ヘルパ
// ---------------------------------------------------------------------------
static const float NAN_F = NAN;

// models_62.h の NaN センチネル値（テーブル欠損セル）か判定
static inline bool is_sentinel(float v) { return v >= NAN_SENT * 0.5f; }

// numpy.interp 互換の 1D 線形補間。xp は昇順配列。
// x < xp[0] → left、x > xp[-1] → right、内側は区間線形補間。
static float interp1(float x, const float* xp, const float* fp, int n,
                     float left, float right) {
  if (n <= 0) return NAN_F;                     // 空配列ならそもそも補間不能
  if (n == 1) return fp[0];                     // 要素 1 個は定数扱い
  if (x <= xp[0]) return (x < xp[0]) ? left : fp[0];    // 左端外挿
  if (x >= xp[n - 1]) return (x > xp[n - 1]) ? right : fp[n - 1]; // 右端外挿
  // x を挟む区間 [lo, hi] を二分探索で特定 (O(log n))
  int lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (xp[mid] <= x) lo = mid; else hi = mid;
  }
  float x0 = xp[lo], x1 = xp[hi];
  float y0 = fp[lo], y1 = fp[hi];
  if (x1 == x0) return y0;                      // 重複 x → 左側の値を返す (ゼロ除算防止)
  return y0 + (y1 - y0) * (x - x0) / (x1 - x0); // 区間内線形補間
}

// vector オーバーロード (クランプは fp の端値)
static float interp1(float x, const std::vector<float>& xp,
                     const std::vector<float>& fp) {
  int n = (int)xp.size();
  if (n == 0) return NAN_F;
  return interp1(x, xp.data(), fp.data(), n, fp.front(), fp.back());
}

// vector オーバーロード (クランプ値を明示指定)
static float interp1(float x, const std::vector<float>& xp,
                     const std::vector<float>& fp, float left, float right) {
  int n = (int)xp.size();
  if (n == 0) return NAN_F;
  return interp1(x, xp.data(), fp.data(), n, left, right);
}

// notebook lpf (中央移動平均): win が奇数でも偶数でも対称になるよう端は edge パディング。
// win<=0 か len<2 の場合はそのまま素通し。
static std::vector<float> lpf(const std::vector<float>& a, int win) {
  int len = (int)a.size();
  if (win <= 0 || len < 2) return a;
  int w = std::min(win, len);                   // win が len を超えたら全体平均相当
  int pad_l = (w - 1) / 2;                      // 左 pad 幅: 窓の中央が各要素に来るように
  // out[j] = mean of padded[j .. j+w-1]
  // padded[i] = a[clamp(i - pad_l, 0, len-1)] (端は edge クランプ)
  std::vector<float> out(len);
  for (int j = 0; j < len; ++j) {
    float s = 0.0f;
    for (int i = j; i < j + w; ++i) {
      int idx = i - pad_l;
      if (idx < 0) idx = 0;                     // 左端クランプ
      if (idx > len - 1) idx = len - 1;         // 右端クランプ
      s += a[idx];
    }
    out[j] = s / (float)w;
  }
  return out;
}

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------
// ④ t_end_for_i
// ---------------------------------------------------------------------------
// i ∈ [-1,1] から仮アタック終端時刻 [ms] を補間。
// テーブルは a_liner_03_01_seed.json の i→t_clip_ms アンカー点列 (TEND_I, TEND_T)。
float t_end_for_i(float i) {
  return interp1(clampf(i, -1.0f, 1.0f), TEND_I, TEND_T, TEND_N,
                 TEND_T[0], TEND_T[TEND_N - 1]);
}

// i を 5 ノード [-1,-0.5,0,0.5,1] の最近傍へ (距離タイは小さい側を選ぶ)
// パーセンタイルテーブルの行インデックスとして使うので i_to_node5 の出力は必ずテーブルの軸値。
float i_to_node5(float i) {
  float iv = clampf(i, -1.0f, 1.0f);
  int best = 0;
  float bestd = std::fabs(PCT_I_NODES[0] - iv);
  for (int k = 1; k < PCT_N_NODES; ++k) {
    float d = std::fabs(PCT_I_NODES[k] - iv);
    if (d < bestd) { bestd = d; best = k; }     // より近いノードに更新
  }
  return PCT_I_NODES[best];
}

// ---------------------------------------------------------------------------
// ⑤ softplus アタック dB
// ---------------------------------------------------------------------------
// softplus(x) = log(1+exp(x))。x>30 では exp がオーバーフローするので線形近似に切り替え。
static inline float sp_func(float x) {
  if (x > 30.0f) return x;                      // exp(30)≈1e13 → オーバーフロー回避
  return std::log1p(std::exp(std::min(x, 30.0f)));
}

// 3 点 (p0, p1, p2) + sigma で定義される softplus 折れ線を時刻 t で評価。
// アタック形状は「onset→ピーク→サステイン」の 2 直線を softplus で滑らかにつなぐ。
static float softplus_blend_one(float t, const float* p0, const float* p1,
                                const float* p2, float sigma) {
  float t0 = p0[0], y0 = p0[1]; // onset 点 (t, dB)
  float t1 = p1[0], y1 = p1[1]; // ピーク点
  float t2 = p2[0], y2 = p2[1]; // サステイン点
  // s1: onset→ピークの傾き; s2: ピーク→サステインの傾き
  float s1 = (std::fabs(t1 - t0) > 1e-6f) ? (y1 - y0) / (t1 - t0) : 0.0f;
  float s2 = (std::fabs(t2 - t1) > 1e-6f) ? (y2 - y1) / (t2 - t1) : 0.0f;
  float sig = std::max(sigma, 0.01f);            // sigma が 0 に近い場合の数値安定化
  // softplus((t-t1)/sigma) で「s1→s2 への傾き変化」を滑らかに補間
  float spv = sp_func((t - t1) / sig);
  return y0 + s1 * (t - t0) + (s2 - s1) * sig * spv;
}

// 全クラス (SP_N=5) の softplus を計算し、i 軸で線形補間して返す。
// SP_I_NODES は 5 クラスに対応する i 値 (models_62.h で定義)。
float atk_softplus_db_i(float t_ms, float i_val) {
  float ys[SP_N];                                // 各クラスでの dB 値を格納
  for (int c = 0; c < SP_N; ++c) {
    ys[c] = softplus_blend_one(t_ms, &SP_P0[c * 2], &SP_P1[c * 2],
                               &SP_P2[c * 2], SP_SIGMA[c]);
  }
  // i_val に応じてクラス間を線形補間 (端外挿は端値で固定)
  return interp1(i_val, SP_I_NODES, ys, SP_N, ys[0], ys[SP_N - 1]);
}

// ---------------------------------------------------------------------------
// ⑥ percentile_to_db_i / atk_bow_percentile
// ---------------------------------------------------------------------------
// パーセンタイル p [0,1] → dB (1D 配列の線形補間)
static float _db_one(const float* arr, int n, float p) {
  // p*(n-1) でインデックスに変換 (numpy.percentile 相当)
  float idx = clampf(p, 0.0f, 1.0f) * (float)(n - 1);
  int lo = (int)idx;
  int hi = std::min(lo + 1, n - 1);
  return arr[lo] + (idx - (float)lo) * (arr[hi] - arr[lo]); // 小数インデックス補間
}

// サステイン出力用 pooled CDF (y0<-55dB 除外済み, public_release 正典)。
// i_val は引数互換のため残すが未使用 (2026-07-07 決定でクラス依存を撤廃済み)。
float percentile_to_db_i(float p, float i_val) {
  (void)i_val;
  return _db_one(DB_ALL, DB_ALL_N, p);
}

// 弓速 [パルス/秒] → パーセンタイル [0,1]。
// PCT_GRID は [node][p][t] の 3D テーブルで「時刻 t・ノード node のとき弓速 speed はどのパーセンタイルか」
// を逆引きする (searchsorted の逆)。
float atk_bow_percentile(float i_node, float t_ms, float speed) {
  // i_node は i_to_node5 で確定済み → PCT_I_NODES の完全一致を探す
  int node = -1;
  for (int k = 0; k < PCT_N_NODES; ++k) {
    if (std::fabs(PCT_I_NODES[k] - i_node) < 1e-6f) { node = k; break; }
  }
  if (node < 0 || PCT_PRESENT[node] == 0) return NAN_F; // テーブルにないノードは欠損扱い

  float tt = clampf(t_ms, PCT_T_GRID[0], PCT_T_GRID[N_T - 1]);
  // 一様グリッド (0..T_MAX step DT) なので jf = tt/DT で直接インデックスへ変換
  float jf = tt / ATK_PCT_DT;
  int j0 = (int)std::floor(jf);
  if (j0 < 0) j0 = 0;
  if (j0 > N_T - 1) j0 = N_T - 1;
  int j1 = std::min(j0 + 1, N_T - 1);
  float w = jf - (float)j0;                     // 時間方向の補間重み [0,1]

  // t 方向に補間した弓速→パーセンタイルの 1D 配列 col を作る
  float col[N_P];                                // N_P 個のパーセンタイル軸値
  const int base = node * N_P;                   // このノードの p 行先頭オフセット (×N_T)
  for (int p = 0; p < N_P; ++p) {
    int row = (base + p) * N_T;
    float a = PCT_GRID[row + j0];               // t=j0 でのテーブル値
    float b = PCT_GRID[row + j1];               // t=j1 でのテーブル値
    if (is_sentinel(a) || is_sentinel(b)) return NAN_F; // 欠損セルなら nan
    col[p] = a * (1.0f - w) + b * w;            // t 方向線形補間
  }
  // col は「各パーセンタイル p での弓速」= p→speed の写像。speed を逆引きして p を返す。
  return interp1(speed, col, PCT_P_AXIS, N_P, 0.0f, 1.0f);
}

// 連続 i 版: PCT_I_NODES の隣接2ノードでそれぞれ p を求め、i で線形ブレンド。
// 最近傍割当だとクラス境界 ±0.25/±0.75 で参照分布が不連続に切り替わる問題を解消する
// (private 62 / export_for_mcu.py と同一, 2026-07-07 決定)。
float atk_bow_percentile_i(float i_val, float t_ms, float speed) {
  float iv = clampf(i_val, -1.0f, 1.0f);
  int k = 0;
  while (k < PCT_N_NODES - 2 && PCT_I_NODES[k + 1] <= iv) ++k;
  float n0 = PCT_I_NODES[k], n1 = PCT_I_NODES[k + 1];
  float w = (n1 > n0) ? (iv - n0) / (n1 - n0) : 0.0f;
  float p0 = atk_bow_percentile(n0, t_ms, speed);
  float p1 = atk_bow_percentile(n1, t_ms, speed);
  if (!std::isfinite(p0)) return p1;
  if (!std::isfinite(p1)) return p0;
  return (1.0f - w) * p0 + w * p1;
}

// ---------------------------------------------------------------------------
// ⑥ (i,t) 音量パーセンタイルグリッド (2.5.2B) → dev(p,i,t) 加算オフセット
// ---------------------------------------------------------------------------
// tt を grid (昇順、任意間隔) の実数インデックスへ変換 (j0/j1 とその間の補間重み w)。
static void grid_index(float tt, const float* grid, int n, int& j0, int& j1, float& w) {
  if (n <= 1) { j0 = j1 = 0; w = 0.0f; return; }
  float c = clampf(tt, grid[0], grid[n - 1]);
  int lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (grid[mid] <= c) lo = mid; else hi = mid;
  }
  j0 = lo; j1 = hi;
  float x0 = grid[lo], x1 = grid[hi];
  w = (x1 > x0) ? (c - x0) / (x1 - x0) : 0.0f;
}

// VOL_GRID の 1 ノード (node ∈ [0, VOL_N_NODES)) から (p, t) の dB を補間する。
static float _vol_db_node(int node, float p, float t_ms) {
  int j0, j1; float wt;
  grid_index(t_ms, VOL_T_GRID, VOL_N_T, j0, j1, wt);
  float col[VOL_N_P];
  const int base = node * VOL_N_P;
  for (int pi = 0; pi < VOL_N_P; ++pi) {
    int row = (base + pi) * VOL_N_T;
    float a = VOL_GRID[row + j0];
    float b = VOL_GRID[row + j1];
    if (is_sentinel(a) || is_sentinel(b)) return NAN_F;
    col[pi] = a * (1.0f - wt) + b * wt;
  }
  return interp1(clampf(p, 0.0f, 1.0f), VOL_P_AXIS, col, VOL_N_P, col[0], col[VOL_N_P - 1]);
}

// (p, i, t) → 実演奏音量 dB。(i,t) グリッド、i 隣接2ノードブレンド (2.5.1 弓速側と対称)。
float vol_db_grid(float p, float i_val, float t_ms) {
  float iv = clampf(i_val, -1.0f, 1.0f);
  int k = 0;
  while (k < VOL_N_NODES - 2 && VOL_I_NODES[k + 1] <= iv) ++k;
  float n0 = VOL_I_NODES[k], n1 = VOL_I_NODES[k + 1];
  float w = (n1 > n0) ? (iv - n0) / (n1 - n0) : 0.0f;
  float d0 = _vol_db_node(k, p, t_ms);
  float d1 = _vol_db_node(k + 1, p, t_ms);
  if (!std::isfinite(d0)) return d1;
  if (!std::isfinite(d1)) return d0;
  return (1.0f - w) * d0 + w * d1;
}

// アタック偏差 dev(t) = vol_db_grid(p,i,t) - vol_db_grid(0.5,i,t)。p が NaN なら偏差なし(0.0)。
float atk_dev(float p, float i_val, float t_ms) {
  if (!std::isfinite(p)) return 0.0f;
  float d = vol_db_grid(p, i_val, t_ms);
  float d50 = vol_db_grid(0.5f, i_val, t_ms);
  if (!std::isfinite(d) || !std::isfinite(d50)) return 0.0f;
  return d - d50;
}

// ---------------------------------------------------------------------------
// ⑧ db -> expr
// ---------------------------------------------------------------------------
// 50F 較正の逆写像: expr = 10^((db - intercept) / slope)
// 結果を [EXPR_MIN, EXPR_MAX] = [0, 127] にクランプ。
float db_to_expr_one(float db) {
  float e = std::pow(10.0f, (db - EXPR_INTERCEPT) / EXPR_SLOPE);
  return clampf(e, EXPR_MIN, EXPR_MAX);
}

// ---------------------------------------------------------------------------
// ② セグメンテーション
// ---------------------------------------------------------------------------
// 2 イベント間の重み付き距離。3 軸のユークリッド距離 + gap_boost。
// dt: 時間差 → 3 乗スケールで非線形強調 (長い間隔を過剰評価する)
// dnote: 音程変化 → w_note で重み付け
// dm: 弓向き変化 → w_updown で重み付け (rapid_switch の割引あり)
float transition_distance(const Event& a, const Event& b) {
  float dt_ms = (float)(b.device_ms - a.device_ms); // 時間差 [ms]
  float dnote = (float)(b.note_num - a.note_num);   // 音程変化
  float dm = (float)(b.m - a.m);                    // 弓向き変化
  bool m_changed = (b.m != a.m);                    // 弓向きが変わったか

  // 時間差を anchor (中央値的な参照間隔) で正規化して 3 乗 (非線形強調)
  float dt_scale = std::pow(dt_ms / SEG.dt_norm_anchor_ms, 3.0f);
  // 時間軸の寄与率を anchor 時のターゲット値に揃えるスケール
  float time_contrib_scale = SEG.dt_time_contrib_at_anchor / SEG.anchor_w_time;

  float d0 = SEG.w_time * dt_scale * time_contrib_scale; // 時間成分
  float d1 = dnote * SEG.w_note;                         // 音程成分
  float d2 = dm * SEG.w_updown;                          // 弓向き成分

  // rapid_switch: 短時間 (< rapid_switch_ms) での弓向き変化はアップ→ダウンの往復弓と解釈し
  //               d2 を割り引く (= 切れ目とみなしにくくする)
  if (dt_ms <= SEG.rapid_switch_ms && m_changed) {
    float disc = clampf(SEG.rapid_switch_discount, 0.0f, 1.0f);
    d2 *= disc;
  }
  float base = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2); // 3 軸ユークリッド距離

  // gap_boost: dt が gap_boost_after_ms を超えた分を線形に加算 (長い無音を確実に切る)
  float excess = std::max(0.0f, dt_ms - SEG.gap_boost_after_ms);
  return base + excess * SEG.gap_boost_per_ms;
}

// rows から 1 ノートを生成。note_num はモード (最頻値)、タイは最小値優先。
bool make_note(const std::vector<Event>& rows, Note& note) {
  int cnt = (int)rows.size();
  if (cnt <= 0) return false;
  int start_ms = rows[0].device_ms;

  // note_num のモードを O(n²) で探す (nは小さいので許容)
  int best_note = rows[0].note_num, best_cnt = 0;
  for (int r = 0; r < cnt; ++r) {
    int v = rows[r].note_num;
    int c = 0;
    for (int s = 0; s < cnt; ++s) if (rows[s].note_num == v) ++c;
    // 頻度が高い、もしくは同頻度なら小さい方のノートを採用
    if (c > best_cnt || (c == best_cnt && v < best_note)) {
      best_cnt = c; best_note = v;
    }
  }

  note.m_events.clear();
  note.note_num = best_note;
  note.start_ms = start_ms;
  for (int r = 0; r < cnt; ++r) {
    if (rows[r].note_num == 0) continue; // 無音イベントは m_events に含めない
    MEvent me;
    me.offset_ms = rows[r].device_ms - start_ms; // 相対時刻に変換
    me.m = rows[r].m;
    me.note_num = rows[r].note_num;
    note.m_events.push_back(me);
  }
  if (note.m_events.empty()) return false;                   // 有効パルスがゼロ
  if ((int)note.m_events.size() < (int)SEG.min_m_events_per_note) return false; // 最小パルス数チェック
  note.end_ms = start_ms + note.m_events.back().offset_ms;  // 最後のパルス時刻をノート終端に
  return true;
}

// バッチ版セグメンテーション: 全 Event → ノート列。
// notebook の detect_start_labels + segment_notes に相当。
std::vector<Note> segment(const std::vector<Event>& ev) {
  std::vector<Note> notes;
  int n = (int)ev.size();
  if (n < 2) return notes;

  // start_indices[0]=0 は先頭マーカー。k>=1 でイベント間距離を評価してセグメント境界を探す。
  // notebook の labels: distances[k]=dist(ev[k],ev[k+1]); label k+1 を start にするのと等価。
  std::vector<int> start_indices;
  start_indices.push_back(0);
  for (int k = 1; k < n - 1; ++k) {
    if (transition_distance(ev[k], ev[k + 1]) > SEG.threshold)
      start_indices.push_back(k);
  }

  for (size_t i = 0; i < start_indices.size(); ++i) {
    int si = start_indices[i];
    int ei = (i + 1 < start_indices.size()) ? start_indices[i + 1] : n;
    // Python: iloc[si+1:ei+1] → C++: rows = ev[si+1 .. ei] inclusive
    int lo = si + 1, hi = std::min(ei, n - 1);
    if (lo > hi) continue;
    std::vector<Event> rows(ev.begin() + lo, ev.begin() + hi + 1);
    Note note;
    if (make_note(rows, note)) notes.push_back(std::move(note));
  }
  return notes;
}

// ---------------------------------------------------------------------------
// ③ obs (ユニーク offset の Δt, pad_hold_last) + KRR バンク
// ---------------------------------------------------------------------------
// ノートの m_events から KRR の入力特徴 obs (Δt 列, T_CAP 要素) を生成する。
// offs_win: ユニーク offset (ms, [0,W] 範囲) の配列
// obs: Δt 列 (サイズ T_CAP, 末尾は pad_hold_last で最終 Δt を繰り返し埋める)
static void note_to_obs(const Note& note, std::vector<int>& offs_win,
                        std::vector<float>& obs) {
  offs_win.clear();
  obs.clear();
  // W = ノート持続時間と WINDOW_MS の小さい方 (KRR の学習時と同じ窓幅制限)
  int W = (int)std::min((float)WINDOW_MS, (float)std::max(0, note.end_ms - note.start_ms));
  int last = -2147483647;
  for (const auto& e : note.m_events) {
    int o = e.offset_ms;
    if (o < 0 || o > W) continue;              // 窓外は無視
    if (o == last) continue;                    // 重複 offset はユニーク化 (昇順なので直前比較で十分)
    offs_win.push_back(o);
    last = o;
  }
  if ((int)offs_win.size() < 2) { offs_win.clear(); return; } // Δt を 1 個も作れないなら失敗

  // Δt = diff(offs_win)。feature_mode=delta, use_log1p=false (notebook 準拠)
  std::vector<float> d;
  d.reserve(offs_win.size() - 1);
  for (size_t k = 1; k < offs_win.size(); ++k)
    d.push_back((float)(offs_win[k] - offs_win[k - 1]));

  // T_CAP 要素に拡張: 実データを先頭に、残りは最後の Δt で埋める (pad_hold_last)
  obs.resize(T_CAP);
  int dn = (int)d.size();
  for (int k = 0; k < T_CAP; ++k)
    obs[k] = (k < dn) ? d[k] : d[dn - 1];     // k < 実 Δt 数 → 実値、超えたら末尾繰り返し
}

// prefix k の KRR(RBF) スコア。
// pred = Σ_i α[k][i] · exp(-γ[k] · Σ_{j<=k} ((obs[j]-X[i][j])/scale[k][j])²)
// StandardScaler の mean はカーネル距離で相殺されるためテーブルに持たない。
static float score_at_k(const std::vector<float>& obs, int k) {
  int off = KRR_OFFSET[k];              // k 番目プレフィックスのスケールテーブル先頭オフセット
  int len = k + 1;                      // prefix k の特徴次元数 (三角バンク)
  float gamma = KRR_GAMMA[k];          // RBF カーネルの γ
  const float* alpha = &KRR_ALPHA[(size_t)k * KRR_N_TRAIN]; // k 番目の双対係数ベクトル
  float acc = 0.0f;                     // 予測値の累積
  for (int i = 0; i < KRR_N_TRAIN; ++i) {
    const float* xi = &KRR_X[(size_t)i * T_CAP]; // i 番目訓練サンプル (T_CAP 次元)
    float d2 = 0.0f;                    // スケール正規化後の二乗距離
    for (int j = 0; j < len; ++j) {
      float z = (obs[j] - xi[j]) / KRR_SCALE[off + j]; // StandardScaler 正規化
      d2 += z * z;                      // 次元 j の寄与を加算
    }
    acc += alpha[i] * std::exp(-gamma * d2); // RBF カーネル × 双対係数
  }
  return clampf(acc, -1.0f, 1.0f);     // 思念スコア i を [-1,1] にクランプ
}

// 公開 API: deltas の先頭 k+1 個だけ見て KRR スコアを返す。
float krr_score_prefix(const std::vector<float>& deltas, int k) {
  if (k < 0) k = 0;
  if (k > T_CAP - 1) k = T_CAP - 1;
  return score_at_k(deltas, k);        // score_at_k は obs[0..k] のみ参照 (len=k+1)
}

// obs (T_CAP 要素) の全 prefix について一括スコア計算 (バッチ版専用)
static std::vector<float> prefix_scores(const std::vector<float>& obs) {
  std::vector<float> out(T_CAP);
  for (int k = 0; k < T_CAP; ++k) out[k] = score_at_k(obs, k);
  return out;
}

// ---------------------------------------------------------------------------
// ③〜⑦ build_note_data
// ---------------------------------------------------------------------------
// Note → NoteData。ノート全体を既知として処理するバッチ版。
// causal.h の CausalEngine とは別物で、parity 検証に使う。
NoteData build_note_data(const Note& note) {
  NoteData nd;
  std::vector<int> offs_win;
  std::vector<float> obs;
  note_to_obs(note, offs_win, obs);
  if (offs_win.size() < 2) return nd;  // パルス不足 → valid=false のまま返す

  // 全 prefix のスコアを計算し、中央 LPF で平滑 (バッチ版は未来も使える)
  std::vector<float> reg_sk_raw = prefix_scores(obs);
  std::vector<float> reg_sk = lpf(reg_sk_raw, LPF_I_WIN); // 中央 LPF

  int now = (int)offs_win.size();
  // k_last: 最後に有効な prefix インデックス (T_CAP と offs_win のどちらかで上限)
  int k_last = std::min((int)obs.size() - 1, now - 2);
  float i_tend = reg_sk[k_last];       // ノート末尾の確定思念スコア
  float t_end = t_end_for_i(i_tend);  // i_tend に対応する仮アタック終端 [ms]
  float dur_ms = (float)(note.end_ms - note.start_ms); // ノート総持続時間

  // i(t) 系列: t_end 以前の offset に対応する LPF 済みスコアを収集
  int kmax = std::min(now - 1, (int)reg_sk.size());
  for (int k = 0; k < kmax; ++k) {
    if ((float)offs_win[k + 1] <= t_end) {      // t_end 以前の offset のみ
      nd.t_i.push_back((float)offs_win[k + 1]);
    }
  }
  for (size_t j = 0; j < nd.t_i.size(); ++j) nd.i_ts.push_back(reg_sk[j]);
  // t_end を末端アンカーとして追加 (補間範囲を t_end まで延ばす)
  if (!nd.t_i.empty() && nd.t_i.back() < t_end) {
    nd.t_i.push_back(t_end);
    nd.i_ts.push_back(i_tend);
  }

  const auto& evs = note.m_events;
  int nev = (int)evs.size();

  // --- 全域ベース dB: 全 m_event の弓速 → パーセンタイル → dB ---
  std::vector<float> all_offs(nev);
  for (int j = 0; j < nev; ++j) all_offs[j] = (float)evs[j].offset_ms;
  std::vector<float> all_spd, t_base_v, vol_base_v;
  std::vector<float> spd_atk, t_spd;
  if (nev >= 2) {
    // 隣接パルス間 Δt から弓速 [パルス/秒] を算出 (0 除算防止で最小 0.001 ms)
    std::vector<float> spd_raw(nev - 1);
    for (int j = 0; j < nev - 1; ++j) {
      float dt = std::max(all_offs[j + 1] - all_offs[j], 1e-3f);
      spd_raw[j] = 1000.0f / dt;
    }
    all_spd = lpf(spd_raw, LPF_SPEED_WIN);  // 中央 LPF で平滑した弓速

    // 各弓速点 (offs の [1:]) に対してベース dB を計算
    t_base_v.assign(all_offs.begin() + 1, all_offs.end());
    vol_base_v.resize(nev - 1);
    for (int j = 0; j < nev - 1; ++j) {
      float p = atk_bow_percentile_i(i_tend, t_base_v[j], all_spd[j]);
      vol_base_v[j] = std::isfinite(p) ? percentile_to_db_i(p, i_tend) : NAN_F;
    }
    // アタック用大窓弓速 (LPF_ATK_WIN でより強く平滑)
    spd_atk = lpf(spd_raw, LPF_ATK_WIN);
    t_spd.resize(nev - 1);
    for (int j = 0; j < nev - 1; ++j) t_spd[j] = (all_offs[j] + all_offs[j + 1]) * 0.5f;
  }
  nd.t_base = t_base_v;
  nd.vol_base = vol_base_v;

  // アタック用弓速補間ラムダ: 任意の t ms での大窓弓速を返す
  auto spd_at = [&](float t) -> float {
    if (spd_atk.empty()) return NAN_F;
    return interp1(t, t_spd, spd_atk, spd_atk.front(), spd_atk.back());
  };

  // --- アタック dB: [0, t_end] 範囲の softplus + 偏差倍率 ---
  std::vector<float> t_atk, i_atk;
  int akmax = std::min(now, (int)reg_sk.size());
  for (int k = 0; k < akmax; ++k) {
    if ((float)offs_win[k] <= t_end) {
      t_atk.push_back((float)offs_win[k]);
      i_atk.push_back(reg_sk[k]);       // この時刻の思念スコア
    }
  }
  // t_end をアタック系列の末端アンカーに追加
  if (!t_atk.empty() && t_atk.back() < t_end) {
    t_atk.push_back(t_end);
    i_atk.push_back(i_tend);
  }

  int na = (int)t_atk.size();
  std::vector<float> db_atk_raw(na), dev_atk(na), db_scaled(na);
  for (int j = 0; j < na; ++j) {
    db_atk_raw[j] = atk_softplus_db_i(t_atk[j], i_atk[j]); // softplus 基本形状

    // --- アタック偏差 dev の計算 (2026-07-10: 旧・乗算 fac を廃止し加算に統一) ---
    float ik = i_atk[j];                         // この時刻の思念スコア
    float p = atk_bow_percentile_i(ik, t_atk[j], spd_at(t_atk[j]));
    dev_atk[j] = atk_dev(p, ik, t_atk[j]);       // (i,t) 音量グリッド由来の加算オフセット
    db_scaled[j] = db_atk_raw[j] + dev_atk[j];  // softplus + 弓速偏差
  }
  // ATK_SMOOTH_WIN で中央 LPF (アタック形状を時間方向に滑らかに)
  std::vector<float> db_atk = lpf(db_scaled, ATK_SMOOTH_WIN);

  // onset (t=0, db=0) アンカー: アタック系列の先頭を 0 に固定してから出力
  if (na > 0) {
    if (t_atk[0] > 0.0f) {
      t_atk.insert(t_atk.begin(), 0.0f);   // t=0 を先頭に挿入
      db_atk.insert(db_atk.begin(), 0.0f); // db=0 を先頭に挿入
    } else {
      db_atk[0] = 0.0f;                    // t[0]==0 なら上書き
    }
  }
  nd.t_atk = t_atk;
  nd.db_atk = db_atk;

  // --- サステイン dB: [t_end] + base(t>t_end) + [dur_ms, 0] ---
  std::vector<float> t_sust, db_sust;
  if (nev >= 2) {
    // t_end でのベース dB を補間 (サステイン区間の始点)
    float base_tend = (t_base_v.size() >= 2)
        ? interp1(t_end, t_base_v, vol_base_v)
        : NAN_F;
    t_sust.push_back(t_end);
    db_sust.push_back(std::isfinite(base_tend) ? base_tend : 0.0f);

    // t_end より後のベース dB 点を追加
    for (size_t j = 0; j < t_base_v.size(); ++j) {
      if (t_base_v[j] > t_end) {
        t_sust.push_back(t_base_v[j]);
        db_sust.push_back(vol_base_v[j]);
      }
    }
    // ノート末端 (dur_ms, 0): リリース後は無音
    t_sust.push_back(dur_ms);
    db_sust.push_back(0.0f);
  } else {
    // パルス 1 個以下 → サステインは定数 0
    t_sust = {t_end, dur_ms};
    db_sust = {0.0f, 0.0f};
  }
  nd.t_sust = t_sust;
  nd.db_sust = db_sust;

  // 中間生成物まとめ
  nd.i_tend = i_tend;
  nd.t_end = t_end;
  nd.dur_ms = dur_ms;
  nd.valid = true;
  return nd;
}

// ---------------------------------------------------------------------------
// ⑦ 動的ハンドオフ
// ---------------------------------------------------------------------------
// smoothstep: [0,1] の線形値 w を 3w²-2w³ に変換して S 字クロスフェード。
static inline float handoff_step(float w) {
  w = clampf(w, 0.0f, 1.0f);
  if (HANDOFF_SMOOTH) return w * w * (3.0f - 2.0f * w); // S 字
  return w;                                               // 線形
}

// アタック dB 系列の末尾 HANDOFF_REMAIN 区間でベース dB へ線形(or S字)クロスフェード。
// 「各時刻での i(t) から仮 t_end を再計算」するのが dynamic の肝。
static std::vector<float> apply_dynamic_handoff(const NoteData& nd) {
  std::vector<float> out = nd.db_atk;            // コピーして上書き
  if (nd.t_atk.size() < 2 || nd.t_base.size() < 2) return out;
  float start = 1.0f - HANDOFF_REMAIN;           // クロスフェード開始フラクション (例: 0.8)
  for (size_t k = 0; k < nd.t_atk.size(); ++k) {
    float t = nd.t_atk[k];
    // この時刻での i(t) を t_i/i_ts 系列から補間
    float i_at = interp1(t, nd.t_i, nd.i_ts, nd.i_ts.front(), nd.i_ts.back());
    float base_at = interp1(t, nd.t_base, nd.vol_base); // この時刻でのベース dB
    float te = t_end_for_i(i_at);                // i_at から仮 t_end を再計算 (dynamic)
    if (te <= 1.0f) continue;                    // t_end が 1ms 以下なら意味なし
    float frac = t / te;                          // ノート内の正規化位置 [0,1]
    if (frac <= start) continue;                 // まだハンドオフ区間外
    // w: start→1.0 の範囲を [0,1] に正規化して smoothstep
    float w = handoff_step((frac - start) / std::max(HANDOFF_REMAIN, 1e-6f));
    if (std::isfinite(base_at))
      out[k] = (1.0f - w) * nd.db_atk[k] + w * base_at; // アタック↔ベースのクロスフェード
  }
  return out;
}

// ---------------------------------------------------------------------------
// ⑧ expr 波形
// ---------------------------------------------------------------------------
// アタック + サステインを時刻順にマージして dB→expr[0,127] に変換。
std::vector<ExprPoint> note_expr_waveform(const NoteData& nd) {
  std::vector<ExprPoint> result;
  if (!nd.valid) return result;
  std::vector<float> db_atk_h = apply_dynamic_handoff(nd); // ハンドオフ済みアタック dB

  // アタックとサステインの (t, db) 点を 1 つの配列にまとめる
  struct TD { float t; float db; };
  std::vector<TD> all;
  all.reserve(nd.t_atk.size() + nd.t_sust.size());
  for (size_t j = 0; j < nd.t_atk.size(); ++j) all.push_back({nd.t_atk[j], db_atk_h[j]});
  for (size_t j = 0; j < nd.t_sust.size(); ++j) all.push_back({nd.t_sust[j], nd.db_sust[j]});

  // 安定ソート: 時刻昇順 (同時刻はアタック→サステインの元順を保つ)
  std::stable_sort(all.begin(), all.end(),
                   [](const TD& a, const TD& b) { return a.t < b.t; });

  result.reserve(all.size());
  for (const auto& e : all) result.push_back({e.t, db_to_expr_one(e.db)}); // dB→expr 変換
  return result;
}

}  // namespace m62
