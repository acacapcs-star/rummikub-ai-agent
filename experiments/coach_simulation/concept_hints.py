"""提示設計比較：位置型 vs 概念型

前一個實驗發現：現行的「輕推」與「指方向」對卡住的人效果是零，
因為它們傳遞的是**位置資訊**，而學習者缺的是**概念資訊**。

這支程式比較三種提示設計，其他條件完全相同。
"""
import random, statistics
from collections import Counter
from knowledge_learner import (
    Tile, joker, is_valid_run, is_valid_group, Knowledge, KnowledgeLearner,
    make_puzzles, CONCEPTS, MASTERY_THRESHOLD, EXPOSURE_GAIN,
    GENTLE_NUDGE, POINT_TO_AREA, REVEAL_MOVE, STARTING_POINTS,
)

# 先備概念：辨認 ≠ 操作
PREREQ = {
    "ATTACH_TAIL":    "RUN_BASIC",
    "ATTACH_HEAD":    "RUN_BASIC",
    "RUN_LONG":       "RUN_BASIC",
    "GROUP_COMPLETE": "GROUP_BASIC",
    "JOKER_FILL_GAP": "JOKER_AS_TILE",
}

def missing_link(k, concept):
    """這個學習者要解這題，最缺的是哪個概念。"""
    pre = PREREQ.get(concept)
    if pre and not k.knows(pre):
        return pre
    if not k.knows(concept):
        return concept
    return None


# --- 三種提示設計 -----------------------------------------------------------
LOCATION = "位置型（現行）"   # 輕推 / 指方向 / 講答案
CONCEPT  = "概念型"          # 輕推 / 教概念 / 講答案
LEAN     = "精簡概念型"      # 教概念 / 講答案（拿掉無效的輕推）


def solve(learner, puzzle, t1, t2, design, max_turns=12, frustration=0.05, rng=None):
    area = puzzle.board_sets[0][0].color
    k = learner.k
    granted = set()          # 這題被暫時開通的概念

    for stuck in range(max_turns):
        if design == LEAN:
            tier = REVEAL_MOVE if stuck >= t1 else POINT_TO_AREA
        else:
            tier = GENTLE_NUDGE if stuck < t1 else (POINT_TO_AREA if stuck < t2 else REVEAL_MOVE)

        if rng and rng.random() < frustration * stuck:
            return False, stuck + 1, False, tier

        if tier == REVEAL_MOVE:
            k.expose(puzzle.required_concept)
            if puzzle.required_concept in PREREQ:
                k.expose(PREREQ[puzzle.required_concept])
            return True, stuck + 1, True, tier

        if tier == POINT_TO_AREA and design in (CONCEPT, LEAN):
            # 教概念：說出規則，不說出這一手
            gap = missing_link(k, puzzle.required_concept)
            if gap and gap not in granted:
                k.expose(gap)
                granted.add(gap)
                # 被告知規則後，這題可以試著用 —— 但還沒真的學會
                saved = k.familiarity[gap]
                k.familiarity[gap] = MASTERY_THRESHOLD
                ok = learner.attempt(puzzle, GENTLE_NUDGE, None)
                k.familiarity[gap] = saved
                if ok:
                    return True, stuck + 1, False, tier
                continue

        hint_color = area if (tier == POINT_TO_AREA and design == LOCATION) else None
        if learner.attempt(puzzle, GENTLE_NUDGE, hint_color):
            return True, stuck + 1, False, tier

    return False, max_turns, False, REVEAL_MOVE


def session(start, puzzles, t1, t2, design, seed=0):
    rng = random.Random(seed)
    k = Knowledge(start); lr = KnowledgeLearner(k)
    solved = auto = 0; turns=[]; tiers=Counter()
    for p in puzzles:
        ok, n, saw, tier = solve(lr, p, t1, t2, design, rng=rng)
        turns.append(n); tiers[tier]+=1
        if ok:
            solved += 1
            if not saw: auto += 1
    N=len(puzzles)
    return dict(solve_rate=solved/N, autonomy_rate=auto/N,
                mean_turns=statistics.mean(turns),
                n_mastered=len(k.mastered()), mastered=k.mastered(),
                tiers=dict(tiers))


if __name__ == "__main__":
    puzzles = make_puzzles(random.Random(7), 150)

    print("="*76)
    print("三種提示設計比較（從完全不會開始，150 題，門檻 2/4）")
    print("="*76)
    print(f"  {'設計':<16} {'解出率':>8} {'自主率':>8} {'學會概念':>10} {'平均回合':>10}")
    print("  "+"-"*54)
    for design, t1, t2 in [(LOCATION,2,4), (CONCEPT,2,4), (LEAN,3,0)]:
        rs = [session((), puzzles, t1, t2, design, seed=s) for s in range(5)]
        m = lambda key: statistics.mean(r[key] for r in rs)
        print(f"  {design:<16} {m('solve_rate'):>7.0%} {m('autonomy_rate'):>8.0%} "
              f"{m('n_mastered'):>7.1f}/8 {m('mean_turns'):>10.2f}")
    print()

    print("="*76)
    print("概念型設計下，門檻掃描")
    print("="*76)
    print(f"  {'門檻':<10} {'解出率':>8} {'自主率':>8} {'學會概念':>10} {'平均回合':>10}")
    print("  "+"-"*50)
    for t1,t2 in [(1,2),(1,3),(2,4),(2,5),(3,5),(3,6)]:
        rs = [session((), puzzles, t1, t2, CONCEPT, seed=s) for s in range(5)]
        m = lambda key: statistics.mean(r[key] for r in rs)
        tag = " 現行" if (t1,t2)==(2,4) else ""
        print(f"  {str(t1)+'/'+str(t2)+tag:<10} {m('solve_rate'):>7.0%} "
              f"{m('autonomy_rate'):>8.0%} {m('n_mastered'):>7.1f}/8 {m('mean_turns'):>10.2f}")
    print()

    print("="*76)
    print("不同起點的學習者，兩種設計的差距")
    print("="*76)
    print(f"  {'起點':<18} {'位置型自主率':>14} {'概念型自主率':>14} {'差':>8}")
    print("  "+"-"*56)
    for name, known in STARTING_POINTS.items():
        a = statistics.mean(session(known, puzzles, 2,4, LOCATION, seed=s)['autonomy_rate'] for s in range(5))
        b = statistics.mean(session(known, puzzles, 2,4, CONCEPT,  seed=s)['autonomy_rate'] for s in range(5))
        print(f"  {name:<18} {a:>13.0%} {b:>14.0%} {b-a:>+8.0%}")
