// 因果(causal)・逐次推論エンジン: m_event が 1 個来るたびに expr を 1 個出す。
//
// plan §0/§2 の本来のゴール。バッチ版 build_note_data との違いは 2 点だけ:
//   1. ノート末尾の確定スコア i_tend ではなく、走査中の i(t) (trailing LPF) を毎ステップ使う
//   2. 中央 LPF ではなく trailing(後方) LPF を使う
// dynamic handoff は元々「各ステップの i と仮 t_end」で動く設計なのでそのまま因果化できる。
// 焼き込みテーブル (models_62.h) は全段そのまま流用。
//
// ※ notebook のバッチ出力とは意図的にズレる (LPF 因果化・i 走査化)。
//    検証は C++ ↔ Python 因果リファレンス間の parity で行う (tools/export_for_mcu.py)。
#pragma once
#include "m62.h"
#include "models_62.h"
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>

namespace m62 {

// ノート終了の理由。Output::noteOff に渡してログ分類に使う。
enum class NoteOffReason {
  Distance,    // transition_distance > SEG.threshold (音程・弓向き・時間的な跳躍)
  DeltaRatio,  // Δ[k]/Δ[k-1] 超過 (急ブレーキ) または tick 無音タイムアウト
  Flush,       // flush() 呼び出し (末尾無音タイムアウト等)
};

class CausalEngine {
 public:
  // 外部コールバック。main.cpp で lambda で束縛する。
  std::function<void(int note_num)> onNoteOn;
  std::function<void(int note_num, NoteOffReason reason)> onNoteOff;
  // expr は [0,127] の連続値。MIDI 送出時は呼び出し側で四捨五入する。
  // i_a は走査中の思念スコア (trailing LPF 済み)。オンセットの expr=0 送出時は NAN。
  std::function<void(int note_idx, float t_ms, float expr, float i_a)> onExpr;

  // m_event を 1 個投入 (preprocess 済み: device_ms, m, note_num)。
  void push(int device_ms, int m, int note_num) {
    (void)m;                                          // m は将来の弓向きフィルタ用に温存
    Event e{device_ms, m, note_num};
    long g = g_++;                                    // グローバル受信カウンタを取得して進める

    if (g == 0) { prev_ = e; return; }               // 最初の行は Δt の基準点として保存するだけ

    // セグメント境界判定: g>=2 のときだけ距離を計算 (バッチ版の k=1..n-2 に対応)
    bool dist_start = (g >= 2) &&
                      (transition_distance(prev_, e) > SEG.threshold);
    // g==1 は先頭ノートの 1 行目なので無条件に新ノート開始
    bool start = (g == 1) || dist_start;

    prev_ = e;                                        // 次の境界判定のために直前イベントを更新

    // start か、カット直後でノートが開いていない場合は新ノードを立ち上げる
    if (start || !in_note_) {
      // dist_start ならすでにノートが開いているので先に閉じる
      if (in_note_ && dist_start) endNote(NoteOffReason::Distance);
      else endNote();                                 // flush 扱い (in_note_=false なら何もしない)
      beginNote(e);                                   // 新ノート開始・NoteOn 送出
    } else if (feed(e)) {                             // Δt 比カット検出 (feed が true を返す)
      // feed 内で「前パルスとの Δt が急拡大」と判定された = ノート境界
      endNote(NoteOffReason::DeltaRatio);
      beginNote(e);                                   // e は次ノートの先頭パルスとして再利用
    }
    // 上記どれにも該当しなければ feed 内で expr を吐いて終わり
  }

  // ライブ時に毎ループ呼ぶ。最後のパルスから Δ[k]*DELTA_RATIO_CUT を超えて
  // 無音が続いたら、次パルスを待たずに無音中で音を切る (本来の Δ比カット)。
  //
  // d_ref は「末尾 1 個」ではなく「末尾 3 個の最大」を使う (実機のみの堅牢化)。
  // エンコーダの二重パルス (Δt=2〜3ms) が末尾に入ると、末尾 1 個基準では
  // タイムアウトが 2×2ms=4ms に崩壊し、次の正常パルス (10〜20ms 後) を待てずに
  // 音が切れる (2026-07-13 実測)。末尾 3 個の max なら単発の極小 Δt を無視して
  // 直近の正常間隔を基準にでき、正常時は 3 個ともほぼ同値なので挙動は変わらない。
  // 注: tick() はライブ専用で Python 因果リファレンスに対応物がなく、parity 非拘束。
  //
  // ギャップの基準は last_uoff_ (ユニーク offset) ではなく last_alloff_ (全パルス) を使う。
  // last_uoff_ は KRR 窓内 (offset <= WINDOW_MS) でしか更新されないため、ノートが
  // 1 秒を超えると凍結し、弾いている最中でも「無音ギャップ」が増え続けて
  // WINDOW_MS + 2Δt ≈ 1.02〜1.04 秒で必ず切られていた (2026-07-13 実測 14 件)。
  // last_alloff_ は feed() で毎パルス無条件更新されるので窓の外でも正しく追従する。
  void tick(uint32_t now_ms) {
    if (!in_note_ || !has_alloff_ || deltas_.empty() || DELTA_RATIO_CUT <= 0.0f) return;
    size_t n = deltas_.size();
    float d_ref = 0.0f;                               // 末尾 min(3,n) 個の最大 Δt
    for (size_t i = n - std::min<size_t>(3, n); i < n; ++i)
      d_ref = std::max(d_ref, deltas_[i]);
    if (d_ref <= 0.0f) return;
    // 「最終パルスの絶対時刻」= ノート開始時刻 + 最後の相対 offset (窓制限なし)
    long last_abs = (long)start_ms_ + (long)last_alloff_;
    long gap = (long)now_ms - last_abs;               // 現在時刻との無音ギャップ
    // ギャップが Δ[k]*RATIO を超えたらノートを終了
    if ((float)gap > DELTA_RATIO_CUT * d_ref)
      endNote(NoteOffReason::DeltaRatio);
  }

  // 入力終端 / 一定時間無音で呼ぶ。末尾ノートを閉じる。
  void flush() { endNote(NoteOffReason::Flush); }

 private:
  // ---- ノート状態 ----
  bool in_note_ = false;           // 現在ノートが開いているか
  int note_idx_ = -1;              // ノート通し番号 (onExpr のログ用)
  int start_ms_ = 0;               // 現ノートの開始絶対時刻 [ms]
  int note_num_ = 0;               // 現ノートの MIDI ノート番号
  std::vector<float> deltas_;      // ユニーク offset の Δt 列 ([ms], K要素)
  int last_uoff_ = -2147483647;    // 最後に登録したユニーク offset (int 最小値で初期化)
  bool has_uoff_ = false;          // ユニーク offset を 1 個でも受けたか
  std::vector<float> raw_i_;       // prefix KRR スコア の履歴 (trailing LPF の入力)
  float last_alloff_ = 0.0f;       // 全 m_event の直前 offset (弓速計算用)
  bool has_alloff_ = false;        // 弓速の基準点が確定しているか
  std::vector<float> raw_spd_;     // 全 m_event 間の生弓速 [パルス/秒] 履歴
  std::vector<float> db_atk_hist_; // アタック瞬時 dB の履歴 (ATK_SMOOTH 平滑用)

  // ---- グローバル ----
  Event prev_{0, 0, 0};           // 境界判定用の直前イベント
  long g_ = 0;                     // 全体の受信カウンタ

  void beginNote(const Event& e) {
    in_note_ = true;
    note_idx_++;                   // ノート番号をインクリメント (0 始まり)
    start_ms_ = e.device_ms;      // このパルスをノートの絶対時刻原点にする
    note_num_ = e.note_num;

    // 全バッファをクリア (前ノートの値が trailing LPF に漏れないようにする)
    deltas_.clear();
    last_uoff_ = -2147483647; has_uoff_ = false;
    raw_i_.clear();
    has_alloff_ = false;
    raw_spd_.clear();
    db_atk_hist_.clear();

    if (onNoteOn) onNoteOn(note_num_);

    // オンセットで expr CC=0 を送出 (noteOn 後 = 新ノートの ch)。
    // 前ノートのリリースは自然減衰させ、新ノートはこの ch を 0 からクリーンスタートさせる。
    // i_a は未確定なので NAN (LCD の iA 累積には載せない)。
    if (onExpr) onExpr(note_idx_, 0.0f, 0.0f, NAN);

    (void)feed(e);  // 先頭パルスも feed に通す (Δt 比カット判定は先頭なので必ず false)
  }

  void endNote(NoteOffReason reason = NoteOffReason::Flush) {
    // noteOff のみ送出。expr=0 は次ノートのオンセット (beginNote) で送る。
    // こうすることで前ノートのリリースカーブを音源に任せられる。
    if (in_note_ && onNoteOff) onNoteOff(note_num_, reason);
    in_note_ = false;
  }

  // 直近 win 個の平均 (trailing/後方 LPF)。win<=0 か空なら末尾値。
  static float trailing(const std::vector<float>& h, int win) {
    int n = (int)h.size();
    if (n == 0) return NAN;
    int w = std::min(win <= 0 ? 1 : win, n);  // win が n を超える場合は全要素平均
    float s = 0.0f;
    for (int i = n - w; i < n; ++i) s += h[i]; // 末尾 w 個だけ足す (未来を参照しない)
    return s / (float)w;
  }

  static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }

  // smoothstep: w∈[0,1] を 3w²-2w³ に変換して S 字カーブを作る。
  // HANDOFF_SMOOTH=false なら線形クロスフェード。
  static inline float smoothstep(float w) {
    w = clampf(w, 0.0f, 1.0f);
    return HANDOFF_SMOOTH ? w * w * (3.0f - 2.0f * w) : w;
  }

  // パルス 1 個分の処理。戻り値: true = Δt 比カット要求 (このパルスを旧ノートに含めない)。
  bool feed(const Event& e) {
    float offset = (float)(e.device_ms - start_ms_); // ノート先頭からの相対時刻 [ms]

    // --- 全 m_event 弓速 (重複 offset も含む) ---
    if (has_alloff_) {
      // dt = 直前パルスからの経過時間。0 除算防止で最小 0.001 ms にクランプ
      float dt = std::max(offset - last_alloff_, 1e-3f);
      raw_spd_.push_back(1000.0f / dt);  // [パルス/秒] = 1000 ms/s ÷ dt[ms]
    }
    last_alloff_ = offset; has_alloff_ = true;

    // --- ユニーク offset のみ → KRR prefix スコア ---
    int io = (int)offset;                             // ms を整数化 (KRR の特徴空間単位)
    bool in_win = (offset >= 0.0f && offset <= (float)WINDOW_MS); // 学習時の窓幅内か
    if (in_win && (!has_uoff_ || io != last_uoff_)) {
      if (has_uoff_) {
        float d_new = (float)(io - last_uoff_);       // 今回の Δt (整数 ms)

        // Δt 比カット: d_new/d_prev > DELTA_RATIO_CUT なら急ブレーキ = ノート境界
        // このパルスを旧ノートに含める前に判定する (含めると急激な i 変動が起きる)
        if (DELTA_RATIO_CUT > 0.0f && !deltas_.empty()) {
          float d_prev = deltas_.back();
          if (d_prev > 0.0f && d_new / d_prev > DELTA_RATIO_CUT) return true; // カット要求
        }
        deltas_.push_back(d_new); // カット判定を通過したら Δt を正式に記録
      }
      last_uoff_ = io; has_uoff_ = true;
      int k = (int)deltas_.size() - 1;               // 現在の prefix インデックス (0 始まり)
      // k が有効範囲 [0, T_CAP-1] のとき KRR スコアを計算して履歴に追加
      if (k >= 0 && k <= T_CAP - 1) raw_i_.push_back(krr_score_prefix(deltas_, k));
    }

    // i スコアがまだ 1 個も確定していなければ expr を吐けない (オンセット直後)
    if (raw_i_.empty()) return false;

    // ---- 各テーブルを「走査中の i(t)」で引く ----
    // trailing LPF で平滑した現時点の思念スコア (バッチ版の i_tend とは別物)
    float i_now = trailing(raw_i_, LPF_I_WIN);
    float t = offset;                                 // 現在のノート内時刻 [ms]
    float t_end = t_end_for_i(i_now);                // 現時点の i から推定する仮アタック終端

    // 弓速の trailing LPF: サステイン用(LPF_SPEED_WIN) とアタック用(LPF_ATK_WIN) を分ける
    float speed_now = trailing(raw_spd_, LPF_SPEED_WIN);
    float speed_atk = trailing(raw_spd_, LPF_ATK_WIN);

    // --- 全域ベース dB (サステイン領域で使う) ---
    // 弓速 → パーセンタイル (隣接2ノードブレンド) → dB (pooled CDF)
    float pct = std::isfinite(speed_now) ? atk_bow_percentile_i(i_now, t, speed_now) : NAN;
    float base_db = std::isfinite(pct) ? percentile_to_db_i(pct, i_now) : NAN;

    // --- アタック瞬時 dB = softplus 形状 + 弓速偏差 (2026-07-10: 乗算fac→加算devに統一) ---
    float db_raw = atk_softplus_db_i(t, i_now);      // softplus 形状 (i 軸補間)
    float p_atk = std::isfinite(speed_atk) ? atk_bow_percentile_i(i_now, t, speed_atk) : NAN;
    float dev = atk_dev(p_atk, i_now, t);            // (i,t) 音量グリッド由来の加算オフセット
    db_atk_hist_.push_back(db_raw + dev);            // 瞬時アタック dB を履歴に追加
    float db_atk = trailing(db_atk_hist_, ATK_SMOOTH_WIN); // trailing LPF で平滑化

    // --- dynamic handoff: アタック末尾 REMAIN 区間でアタック→ベース dB へクロスフェード ---
    float out;
    if (t <= t_end) {
      out = db_atk;                                   // アタック区間はそのまま
      if (t_end > 1.0f) {
        float start = 1.0f - HANDOFF_REMAIN;         // クロスフェード開始フラクション
        float frac = t / t_end;                       // ノート内時刻の正規化位置 [0,1]
        if (frac > start && std::isfinite(base_db)) {
          // smoothstep で S 字混合 (w=0 → アタック、w=1 → ベース)
          float w = smoothstep((frac - start) / std::max(HANDOFF_REMAIN, 1e-6f));
          out = (1.0f - w) * db_atk + w * base_db;
        }
      }
    } else {
      // t_end を超えたらサステイン = ベース dB のみ (アタック形状は役目を終えた)
      out = std::isfinite(base_db) ? base_db : db_atk;
    }

    // dB → CC#11 [0,127] に変換してコールバック (i_now も LCD/テレメトリ用に同梱)
    if (onExpr) onExpr(note_idx_, t, db_to_expr_one(out), i_now);
    return false; // カット要求なし
  }
};

}  // namespace m62
