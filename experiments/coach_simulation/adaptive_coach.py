"""三個新機制：難度分級、舉一反三、鷹架淡出

1. 簡單概念 —— 看一次進展多，而且會舉一反三（同家族的概念跟著提升）
2. 困難概念 —— 看一次進展少，但每次都累積細節，穩定爬升
3. 兩難情況 —— 學習者已經有多個候選走法時，逐步降低提示層級（撤鷹架）
"""
import random, statistics
from collections import Counter
from knowledge_learner import (
    Knowledge, KnowledgeLearner, make_puzzles, CONCEPTS,
    MASTERY_THRESHOLD, GENTLE_NUDGE, POINT_TO_AREA, REVEAL_MOVE, STARTING_POINTS,
)
from concept_hints import PREREQ, missing_link

# --- 1. 難度分級 -----------------------------------------------------------
EASY = {"RUN_BASIC", "GROUP_BASIC", "ATTACH_TAIL", "GROUP_COMPLETE"}
HARD = {"ATTACH_HEAD", "RUN_LONG", "JOKER_AS_TILE", "JOKER_FILL_GAP"}

GAIN      = {"easy": 0.50, "hard": 0.20}   # 簡單的看兩次會，困難的要五次
TRANSFER  = {"easy": 0.25, "hard": 0.05}   # 舉一反三的幅度

def level(c):
    return "easy" if c in EASY else "hard"

# --- 2. 舉一反三：同家族的概念會沾光 ----------------------------------------
FAMILIES = [
    {"RUN_BASIC", "ATTACH_TAIL", "ATTACH_HEAD", "RUN_LONG"},
    {"GROUP_BASIC", "GROUP_COMPLETE"},
    {"JOKER_AS_TILE", "JOKER_FILL_GAP"},
]

def family_of(c):
    for f in FAMILIES:
        if c in f:
            return f
    return {c}


class GradedKnowledge(Knowledge):
    def expose(self, concept, transfer=True):
        if concept not in self.familiarity:
            return
        lv = level(concept)
        self.familiarity[concept] = min(
            MASTERY_THRESHOLD, self.familiarity[concept] + GAIN[lv])

        if transfer:
            # 舉一反三：簡單概念帶動同家族，困難概念幾乎不帶
            for sib in family_of(concept):
                if sib != concept:
                    self.familiarity[sib] = min(
                        MASTERY_THRESHOLD,
                        self.familiarity[sib] + TRANSFER[lv])


# --- 3. 鷹架淡出：兩難時降低提示 -------------------------------------------
class FadingCoach:
    """學習者最近表現好、或這題他本來就有多個候選 → 提示往下降一級。

    「兩難」= enumerate_moves 回傳多於一個走法。
    那代表他不是不會，是在選 —— 這時候給提示是代勞，不是教學。
    """
    def __init__(self, t1, t2, fade=True, window=5):
        self.t1, self.t2 = t1, t2
        self.fade = fade
        self.window = window
        self.recent = []

    def record(self, autonomous):
        self.recent.append(autonomous)
        if len(self.recent) > self.window:
            self.recent.pop(0)

    def tier(self, stuck, n_candidates):
        base = GENTLE_NUDGE if stuck < self.t1 else (
            POINT_TO_AREA if stuck < self.t2 else REVEAL_MOVE)
        if not self.fade:
            return base

        step_down = 0
        # 兩難：他已經想得到不只一種走法 → 不要幫他選
        if n_candidates > 1:
            step_down += 1
        # 最近都自己解出來 → 撤鷹架
        if len(self.recent) == self.window and all(self.recent):
            step_down += 1

        return max(GENTLE_NUDGE, base - step_down)


def solve(learner, puzzle, coach, max_turns=12, frustration=0.05, rng=None):
    k = learner.k
    granted = set()
    for stuck in range(max_turns):
        n_cand = len(learner.enumerate_moves(puzzle))
        tier = coach.tier(stuck, n_cand)

        if rng and rng.random() < frustration * stuck:
            return False, stuck + 1, False

        if tier == REVEAL_MOVE:
            k.expose(puzzle.required_concept)
            if puzzle.required_concept in PREREQ:
                k.expose(PREREQ[puzzle.required_concept], transfer=False)
            return True, stuck + 1, True

        if tier == POINT_TO_AREA:
            gap = missing_link(k, puzzle.required_concept)
            if gap and gap not in granted:
                k.expose(gap)
                granted.add(gap)
                saved = k.familiarity[gap]
                k.familiarity[gap] = MASTERY_THRESHOLD
                ok = learner.attempt(puzzle, GENTLE_NUDGE, None)
                k.familiarity[gap] = saved
                if ok:
                    return True, stuck + 1, False
                continue

        if learner.attempt(puzzle, GENTLE_NUDGE, None):
            return True, stuck + 1, False
    return False, max_turns, False


def session(start, puzzles, t1, t2, fade, seed=0):
    rng = random.Random(seed)
    k = GradedKnowledge(start); lr = KnowledgeLearner(k)
    coach = FadingCoach(t1, t2, fade=fade)
    solved = auto = 0; turns = []
    curve = []
    for i, p in enumerate(puzzles):
        ok, n, saw = solve(lr, p, coach, rng=rng)
        turns.append(n)
        a = ok and not saw
        coach.record(a)
        if ok:
            solved += 1
            auto += 1 if a else 0
        if (i+1) % 30 == 0:
            curve.append(len(k.mastered()))
    N = len(puzzles)
    return dict(solve_rate=solved/N, autonomy_rate=auto/N,
                mean_turns=statistics.mean(turns),
                n_mastered=len(k.mastered()), mastered=k.mastered(),
                curve=curve, fam=dict(k.familiarity))


if __name__ == "__main__":
    puzzles = make_puzzles(random.Random(11), 150)
    M = lambda rs, key: statistics.mean(r[key] for r in rs)

    print("="*76)
    print("加入三個機制後的效果（完全不會起步，150 題）")
    print("="*76)
    print(f"  {'設定':<22} {'解出率':>8} {'自主率':>8} {'學會概念':>10} {'平均回合':>10}")
    print("  "+"-"*60)
    for label, t1, t2, fade in [
        ("概念型 2/4（無淡出）", 2, 4, False),
        ("概念型 2/4 + 鷹架淡出", 2, 4, True),
        ("概念型 1/3 + 鷹架淡出", 1, 3, True),
        ("概念型 1/2 + 鷹架淡出", 1, 2, True),
    ]:
        rs = [session((), puzzles, t1, t2, fade, seed=s) for s in range(5)]
        print(f"  {label:<22} {M(rs,'solve_rate'):>7.0%} {M(rs,'autonomy_rate'):>8.0%} "
              f"{M(rs,'n_mastered'):>7.1f}/8 {M(rs,'mean_turns'):>10.2f}")
    print()

    print("="*76)
    print("簡單 vs 困難概念的學習曲線（熟悉度，1.0 = 精通）")
    print("="*76)
    rs = [session((), puzzles, 1, 3, True, seed=s) for s in range(5)]
    fam = {c: statistics.mean(r['fam'][c] for r in rs) for c in CONCEPTS}
    for c in sorted(CONCEPTS, key=lambda x: -fam[x]):
        bar = "█" * int(fam[c]*24)
        print(f"  {c:<16} [{level(c):<4}] {fam[c]:.2f} {bar}")
    print()

    print("="*76)
    print("精通概念數的成長（每 30 題一個點）")
    print("="*76)
    for label, t1, t2, fade in [("無淡出 2/4",2,4,False), ("有淡出 1/3",1,3,True)]:
        rs = [session((), puzzles, t1, t2, fade, seed=s) for s in range(5)]
        curve = [statistics.mean(r['curve'][i] for r in rs) for i in range(len(rs[0]['curve']))]
        print(f"  {label:<12} " + "  ".join(f"{v:.1f}" for v in curve))
    print()

    print("="*76)
    print("不同起點：鷹架淡出有沒有幫助")
    print("="*76)
    print(f"  {'起點':<18} {'無淡出':>10} {'有淡出':>10} {'差':>8}")
    print("  "+"-"*48)
    for name, known in STARTING_POINTS.items():
        a = M([session(known, puzzles,2,4,False,seed=s) for s in range(5)], 'autonomy_rate')
        b = M([session(known, puzzles,2,4,True, seed=s) for s in range(5)], 'autonomy_rate')
        print(f"  {name:<18} {a:>9.0%} {b:>10.0%} {b-a:>+8.0%}")
