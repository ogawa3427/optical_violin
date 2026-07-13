// 逐次セグメンタ: イベントを 1 個ずつ push し、ノート境界を検出した時点で
// 完成ノートをコールバックする。バッチ segment() と同一の境界判定・フィルタを使うので
// 出力ノート列はバッチと完全一致する (native parity で検証済み)。
//
// 実機では push() に dataSender 由来の (device_ms, m, note_num) を流し込み、
// コールバックで build_note_data → note_expr_waveform → 出力、とつなぐ。
//
// 注: 1 ノート分の遅延がある (ノート完成時にまとめて expr を吐く)。
//     真の「1 パルス 1 expr」逐次化 (LPF の因果化等) は causal.h が担う。
//     このクラスは parity 検証と「ノート単位処理が許容できる」ユースケース向け。
#pragma once
#include "m62.h"
#include "models_62.h"  // SEG
#include <vector>
#include <functional>

namespace m62 {

class StreamSegmenter {
 public:
  using NoteCb = std::function<void(const Note&)>;

  // コールバック cb はノート完成時に呼ばれる。
  explicit StreamSegmenter(NoteCb cb) : cb_(std::move(cb)) {}

  // 1 イベント投入。境界検出時に直前ノートを finalize してコールバック。
  //
  // batch との対応: distances[k]=dist(ev[k],ev[k+1]); label[k]=start なら row k+1 が
  // 新ノート先頭。先頭ノートのみ row0 を捨てる規約 (start_indices[0]=0)。
  void push(int device_ms, int m, int note_num) {
    Event e{device_ms, m, note_num};
    long g = g_++;                   // 受信カウンタを取得して進める

    if (g == 0) {                    // ev0: 先頭ノートのマーカー行 (batch の start_indices[0]=0 に相当)
      buf_.clear();
      buf_.push_back(e);             // buf_ に積んでおく (finalize で drop_first_=true により捨てられる)
      return;
    }

    if (g >= 2) {                    // batch の k=1..n-2 ループに相当 (最初の距離評価は g=2 から)
      if (transition_distance(buf_.back(), e) > SEG.threshold) {
        finalize();                  // 直前ノート完成 → コールバックを呼ぶ
        buf_.clear();
        buf_.push_back(e);           // e は次ノートの実先頭行としてバッファに積む
        return;                      // この e は次ノートに属するので今回の buf_ 追加はここで完了
      }
    }
    buf_.push_back(e);               // 境界なし → 現ノートに追加
  }

  // 入力終端で残バッファを finalize (実機では一定時間無音で呼ぶ)。
  void flush() {
    finalize();                      // 残っているバッファをノートとして確定
    buf_.clear();                    // 次の push に備えてバッファをクリア
    drop_first_ = false;             // 先頭 drop フラグをリセット (再利用時の安全策)
  }

 private:
  // バッファ内のイベントをノートとして確定し、make_note を通してコールバックへ渡す。
  void finalize() {
    // 先頭ノートのみ buf_[0]=ev0 を捨てる (batch の iloc[si+1:] に相当)
    // 2 ノート目以降は buf_ 全体が実ノート行なので drop しない。
    size_t begin = drop_first_ ? 1 : 0;
    drop_first_ = false;             // 以降のノートは drop 不要
    if (buf_.size() <= begin) return; // 有効行がゼロなら何もしない
    std::vector<Event> rows(buf_.begin() + begin, buf_.end());
    Note note;
    if (make_note(rows, note)) cb_(note); // フィルタを通過したノートのみコールバック
  }

  std::vector<Event> buf_;           // 現在積み上げ中のイベントバッファ
  long g_ = 0;                       // グローバル受信カウンタ
  bool drop_first_ = true;           // true = 次の finalize で buf_[0] を捨てる (先頭ノートのみ)
  NoteCb cb_;                        // ノート完成時コールバック
};

}  // namespace m62
