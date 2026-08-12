#!/usr/bin/env python3
"""
從 turn_metrics.csv 計算行為指標的分佈與相關矩陣。

用途：檢查憑直覺選出的指標裡，哪些其實在測同一件事。
用法：
    for i in $(seq 1 100); do ./test_build 0 b0 > /dev/null; done
    python3 analysis/correlate_metrics.py runs100.csv
"""
import csv, sys, statistics as st

COLS = ['us','regroup','tiles','meld_attempts',
        'extend_calls','failed_applies','had_option']

def corr(a, b):
    ma, mb = st.mean(a), st.mean(b)
    va = sum((x-ma)**2 for x in a) ** .5
    vb = sum((y-mb)**2 for y in b) ** .5
    if va == 0 or vb == 0:
        return float('nan')
    return sum((x-ma)*(y-mb) for x, y in zip(a, b)) / (va*vb)

def main(path):
    rows = list(csv.DictReader(open(path)))
    D = {c: [float(r[c]) for r in rows] for c in COLS}
    print(f"n = {len(rows)} 手\n")

    print("== 分佈 ==")
    for c in COLS:
        v = D[c]
        print(f"{c:16s} mean={st.mean(v):9.2f}  sd={st.pstdev(v):9.2f}"
              f"  min={min(v):6.0f}  max={max(v):8.0f}")

    print("\n== 相關矩陣 ==")
    print(" "*16 + "".join(f"{c[:7]:>9s}" for c in COLS))
    for a in COLS:
        print(f"{a:16s}" + "".join(f"{corr(D[a],D[b]):9.2f}" for b in COLS))

    print("\n== |r| > 0.8 的配對（可能重複）==")
    hit = False
    for i, a in enumerate(COLS):
        for b in COLS[i+1:]:
            r = corr(D[a], D[b])
            if r == r and abs(r) > 0.8:
                print(f"  {a} × {b} = {r:.3f}")
                hit = True
    if not hit:
        print("  無")

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'runs100.csv')
