"""穩健性複驗

單一組題目跑出來的結論不能信 —— 可能只是那組題目剛好有利。
這支程式換 10 組不同的題目種子重跑，看核心結論站不站得住。

執行：python3 robustness_check.py
"""
import random
import statistics

from knowledge_learner import (
    make_puzzles, Knowledge, KnowledgeLearner,
    GENTLE_NUDGE, POINT_TO_AREA, STARTING_POINTS,
)
from concept_hints import session, LOCATION, CONCEPT, LEAN


def check_designs(n_puzzle_seeds=10, n_session_seeds=5, n_puzzles=150):
    print("=" * 74)
    print(f"複驗一：三種提示設計（{n_puzzle_seeds} 組題目 × {n_session_seeds} 個 session 種子）")
    print("=" * 74)
    print()

    agg = {LOCATION: [], CONCEPT: [], LEAN: []}
    for pz in range(n_puzzle_seeds):
        puzzles = make_puzzles(random.Random(100 + pz), n_puzzles)
        for design, t1, t2 in [(LOCATION, 2, 4), (CONCEPT, 2, 4), (LEAN, 3, 0)]:
            rs = [session((), puzzles, t1, t2, design, seed=s)
                  for s in range(n_session_seeds)]
            agg[design].append(statistics.mean(r["autonomy_rate"] for r in rs))

    print(f"  {'設計':<16} {'自主率平均':>12} {'標準差':>10} {'最低':>8} {'最高':>8}")
    print("  " + "-" * 58)
    for d in (LOCATION, CONCEPT, LEAN):
        v = agg[d]
        print(f"  {d:<16} {statistics.mean(v):>11.1%} {statistics.pstdev(v):>10.3f} "
              f"{min(v):>7.1%} {max(v):>8.1%}")
    print()

    w1 = sum(1 for a, b in zip(agg[LOCATION], agg[CONCEPT]) if b > a)
    w2 = sum(1 for a, b in zip(agg[LOCATION], agg[LEAN]) if b > a)
    print(f"  概念型贏過位置型：{w1}/{n_puzzle_seeds} 組題目")
    print(f"  精簡型贏過位置型：{w2}/{n_puzzle_seeds} 組題目")
    print()
    return agg


def check_finding_one(n_puzzle_seeds=10, n_puzzles=60):
    """發現一：「指方向」對缺乏概念的學習者有沒有用。"""
    print("=" * 74)
    print("複驗二：前兩層提示的差異")
    print("=" * 74)
    print()

    diffs = []
    for pz in range(n_puzzle_seeds):
        puzzles = make_puzzles(random.Random(200 + pz), n_puzzles)
        for name, known in STARTING_POINTS.items():
            lr = KnowledgeLearner(Knowledge(known))
            a = sum(1 for p in puzzles
                    if lr.attempt(p, GENTLE_NUDGE, p.board_sets[0][0].color))
            b = sum(1 for p in puzzles
                    if lr.attempt(p, POINT_TO_AREA, p.board_sets[0][0].color))
            diffs.append(b - a)

    n = len(diffs)
    print(f"  {n} 次比較（{n_puzzle_seeds} 組題目 × {len(STARTING_POINTS)} 種起點）")
    print(f"  「指方向」比「輕推」多解出的題數：最小 {min(diffs)}、最大 {max(diffs)}")
    print()
    if all(d == 0 for d in diffs):
        print("  → 全部為 0。這不是統計上的接近，是結構上的必然：")
        print("    位置資訊對缺乏概念的學習者不產生任何作用。")
    else:
        print("  → 有非零值，發現一需要修正。")
    print()
    return diffs


if __name__ == "__main__":
    check_designs()
    check_finding_one()

    print("=" * 74)
    print("這個複驗沒有驗證到的事")
    print("=" * 74)
    print("""
  以上只證明：**在這個學習者模型底下，結論對題目分布不敏感。**

  它沒有證明模型像真人。10 組題目換來換去，模型的假設從頭到尾沒變 ——
  「教概念之後馬上能用」、「曝光 0.34 累積三次會」這些數字仍然是設定的，
  不是量出來的。

  正確的說法是：
      在「學習者的能力受限於概念而非運氣」這個假設下，
      概念型提示優於位置型，且此結論對題目分布穩健。

  前提本身仍需真人驗證。
""")
