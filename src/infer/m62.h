// 62 パイプライン 推論コア (Arduino 非依存・純 C++)
//
// 入力: アクティブ m_event ストリーム (device_ms, m, note_num)
//       = 既存 dataSender の preprocess 後に相当
// 出力: ノートごとの CC expr[0,127] 波形 (時刻 t_ms とともに)
//
// notebook 62 の Cell14(build_note_data)/Cell18(handoff)/Cell20(expr) を逐次/バッチ両対応で再現。
// 焼き込みテーブルは include/models_62.h (export_for_mcu.py が生成)。
#pragma once
#include <vector>
#include <cstdint>

namespace m62 {

// 生の (preprocess 済み) イベント 1 個。dataSender が CH559 Extender (ATOMIC USB Extender v1.0) から読んで整形した値。
struct Event {
  int device_ms;   // マイコン millis() ベースのタイムスタンプ [ms]
  int m;           // 弓方向: 1=ダウンボウ(下スクロール) / 2=アップボウ(上スクロール)
  int note_num;    // 押下中の MIDI ノート番号 (離鍵中=0)
};

// ノート内 m_event 1 個 (ノート開始時刻からの相対時刻)
struct MEvent {
  int offset_ms;   // ノート先頭 start_ms からの相対時刻 [ms]
  int m;           // 弓方向 (Event と同じ)
  int note_num;    // MIDI ノート番号
};

// セグメンテーション結果ノート (1 音符 = 連続する m_event のまとまり)
struct Note {
  int note_num;                    // ノートの代表音高 (最頻値、タイは最小値)
  int start_ms;                    // ノート開始の絶対時刻 [ms]
  int end_ms;                      // ノート終了の絶対時刻 [ms]
  std::vector<MEvent> m_events;    // note_num!=0 のイベントのみ、offset 昇順
};

// build_note_data の中間生成物 (notebook の nd 相当)
// 全段の出力を束ねる DTO。valid=false なら後段へ渡さない。
struct NoteData {
  bool valid = false;              // ここが false なら expr 生成不可 (パルス不足など)
  float i_tend = 0.0f;            // ノート末尾の確定思念スコア (バッチ用)
  float t_end = 0.0f;             // i_tend から推定したアタック終端時刻 [ms]
  float dur_ms = 0.0f;            // ノート総持続時間 [ms]
  std::vector<float> t_i, i_ts;   // i(t) 系列: t_i[k] での思念スコア i_ts[k]
  std::vector<float> t_base, vol_base; // 全域ベース dB 系列 (t_base[k] での vol_base[k])
  std::vector<float> t_atk, db_atk;   // アタック dB 系列 (onset アンカー済み)
  std::vector<float> t_sust, db_sust; // サステイン dB 系列
};

// expr 波形の 1 点 (note_expr_waveform の出力要素)
struct ExprPoint {
  float t_ms;  // ノート先頭からの時刻 [ms]
  float expr;  // CC#11 Expression 値 [0,127] の連続値 (整数化は呼び出し側)
};

// ② 生イベント列 → ノート列 (重み付き距離+閾値, min_m_events フィルタ込み・バッチ)
std::vector<Note> segment(const std::vector<Event>& events);

// 2 イベント間の重み付き遷移距離 (セグメンテーション閾値判定用)
// Δt / Δnote / Δm_direction の 3 軸ユークリッド距離 + gap_boost
float transition_distance(const Event& a, const Event& b);

// 1 セグメント (start マーカー除外済みの行列) → Note。
// note_num!=0 / min_m_events フィルタを満たさなければ false。
bool make_note(const std::vector<Event>& rows, Note& out);

// ③〜⑦ ノート → 中間波形 (dur < MIN_DUR_MS や m_event 不足なら valid=false)
NoteData build_note_data(const Note& note);

// ⑦⑧ 中間波形 → expr 波形 (動的ハンドオフ + 50F 較正, 時刻昇順)
std::vector<ExprPoint> note_expr_waveform(const NoteData& nd);

// 個別ステージ (テスト/デバッグ・実機ストリーム用に公開)

// ③ prefix k の KRR スコア。deltas は先頭 k+1 個以上の Δt[ms]。k は [0, T_CAP-1]。
// 結果は [-1, 1] にクランプ (鋭い=-1, 鈍い=+1 の思念スコア)。
float krr_score_prefix(const std::vector<float>& deltas, int k);

// ④ i → 仮アタック終端時刻 [ms]。a_liner_03 シードの線形補間。
float t_end_for_i(float i);

// i を 5 ノード {-1,-0.5,0,0.5,1} の最近傍に丸める (パーセンタイルテーブルの索引用)
float i_to_node5(float i);

// ⑤ softplus アタック dB 形状 (onset=0 アンカー済み、i 軸線形補間)
float atk_softplus_db_i(float t_ms, float i_val);

// ⑥ パーセンタイル p [0,1] → dB。サステイン用 pooled CDF (y0<-55dB 除外済み, public_release正典)。
// i_val は引数互換のため残すが未使用 (2026-07-07 決定でクラス依存を撤廃済み)。
float percentile_to_db_i(float p, float i_val);

// ⑥ 弓速 [パルス/秒] → パーセンタイル (i_node の 2D テーブルから時刻 t 補間、最近傍ノード版)
float atk_bow_percentile(float i_node, float t_ms, float speed);

// ⑥ 弓速 [パルス/秒] → パーセンタイル (連続 i 版、隣接2ノードを線形ブレンド)
float atk_bow_percentile_i(float i_val, float t_ms, float speed);

// ⑥ (i,t) 音量パーセンタイルグリッド (2.5.2B) → dB。隣接2ノードを線形ブレンド。
float vol_db_grid(float p, float i_val, float t_ms);

// ⑥ アタック偏差 dev(t) = vol_db_grid(p,i,t) - vol_db_grid(0.5,i,t)。加算オフセットとして使う。
float atk_dev(float p, float i_val, float t_ms);

// ⑧ dB → CC#11 Expression 値 [0,127] (50F 較正の逆写像)
float db_to_expr_one(float db);

}  // namespace m62
