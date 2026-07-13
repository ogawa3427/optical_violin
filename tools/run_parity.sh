#!/usr/bin/env bash
# native parity ゲート: C++ 推論コアの expr 波形が Python 参照 (notebook 62 相当) と一致するか検証。
#
#   ./tools/run_parity.sh            # 既存の焼き込み/参照で検証
#   ./tools/run_parity.sh --export   # 先に export_for_mcu.py で再生成してから検証
#
# 完了条件: 全 CSV で note 数一致・点数一致・max abs expr error <= 1.0
set -euo pipefail
cd "$(dirname "$0")/.."                      # projects/migration
ROOT="$(pwd)"
PY="$ROOT/../dataAnalysis/.venv/bin/python"

if [[ "${1:-}" == "--export" ]]; then
  echo "== regenerating models_62.h + reference =="
  (cd "$ROOT/../dataAnalysis" && "$PY" "$ROOT/tools/export_for_mcu.py")
fi

echo "== building native harness =="
BIN="$(mktemp -t m62_native.XXXX)"
g++ -std=c++17 -O2 -I include -I src/infer \
    test/main_native.cpp src/infer/m62.cpp -o "$BIN"

rc=0
for raw in reference/*.raw.csv; do
  stem="$(basename "$raw" .raw.csv)"

  # バッチ版: notebook 62 と数値一致 (max abs <= 1.0)
  if [[ -f "reference/$stem.expr.csv" ]]; then
    echo "== $stem [batch vs notebook] =="
    out="$(mktemp -t m62_out.XXXX)"
    "$BIN" "$raw" > "$out" 2>/dev/null
    "$PY" tools/compare_parity.py "$out" "reference/$stem.expr.csv" || rc=1
    rm -f "$out"
  fi

  # 因果版: C++ CausalEngine ↔ Python 因果リファレンス (float32 vs float64)
  if [[ -f "reference/$stem.causal.csv" ]]; then
    echo "== $stem [causal: C++ vs Python] =="
    out="$(mktemp -t m62_out.XXXX)"
    "$BIN" "$raw" --causal > "$out" 2>/dev/null
    "$PY" tools/compare_parity.py "$out" "reference/$stem.causal.csv" || rc=1
    rm -f "$out"
  fi
done
rm -f "$BIN"

if [[ $rc -eq 0 ]]; then echo "ALL PARITY PASS"; else echo "PARITY FAILURES"; fi
exit $rc
