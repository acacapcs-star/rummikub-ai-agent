"""認知教練 —— 知識型學習者模擬

## 跟第一版（機率模型）的差別

第一版把學習者寫成「有 P 的機率找到解」，P 是手調的數字。
那會變成循環論證：用自己編的參數，驗證自己編的門檻。

這一版改成**知識型**：

    學習者持有一組「已知概念」
    每回合窮舉所有它「想得到」的走法
    想得到的範圍由已知概念決定

能不能解出來是**推導出來的**，不是抽籤決定的。
一個不知道「Group」這個概念的人，窮舉時根本不會產生 Group 的候選 ——
不是他運氣不好，是那個念頭不存在。

## 概念清單

對應拉密實際需要的知識，由淺入深：

    RUN_BASIC        同色連續 3 張叫 Run
    GROUP_BASIC      同數字不同色 3 張叫 Group
    ATTACH_TAIL      可以把牌接在桌面 Run 的大數字那端
    ATTACH_HEAD      也可以接在小數字那端        ← 比 TAIL 難想到
    GROUP_COMPLETE   桌面 3 張的 Group 可以補第 4 色
    JOKER_AS_TILE    Joker 可以當任何一張牌
    JOKER_FILL_GAP   Joker 可以填 Run 中間的缺口  ← 比當普通牌難
    RUN_LONG         Run 可以超過 3 張

## 提示如何轉成知識

    GENTLE_NUDGE   「有牌可以出喔」
                   → 不含任何概念資訊。只是叫他再想一次。
                     對「已經窮舉過所有想得到的走法」的人來說，完全無效。

    POINT_TO_AREA  「留意桌上紅色那一排」
                   → 縮小搜尋範圍，但**不教概念**。
                     如果他不知道「可以接在 Run 尾巴」，
                     叫他看紅色那排也沒用 —— 他不知道要拿那排做什麼。

    REVEAL_MOVE    「把紅 7 接到那排後面」
                   → 展示了一次概念的實例。看過會累積熟悉度，
                     但**一次不會學會**（只往後進展一點點）。

## 學習：一次進展一點點

每個概念有 0.0~1.0 的熟悉度。看一次 REVEAL 加 EXPOSURE_GAIN，
熟悉度到 MASTERY_THRESHOLD 才算真的會、能自己用出來。
"""

import itertools
import random
import statistics
from collections import Counter

# ---------------------------------------------------------------------------
# 牌
# ---------------------------------------------------------------------------

RED, BLUE, YELLOW, BLACK = range(4)
COLOR_NAME = {RED: "紅", BLUE: "藍", YELLOW: "黃", BLACK: "黑"}


class Tile:
    __slots__ = ("number", "color", "is_joker")

    def __init__(self, number, color, is_joker=False):
        self.number = number
        self.color = color
        self.is_joker = is_joker

    def __repr__(self):
        return "J" if self.is_joker else f"{COLOR_NAME[self.color]}{self.number}"


def joker():
    return Tile(0, RED, True)


# ---------------------------------------------------------------------------
# 規則判定（對應 validator.h）
# ---------------------------------------------------------------------------

def is_valid_run(tiles):
    """同色連續 3+ 張，Joker 可代任意牌。"""
    if len(tiles) < 3:
        return False
    real = [t for t in tiles if not t.is_joker]
    n_joker = len(tiles) - len(real)
    if not real:
        return False
    if len({t.color for t in real}) != 1:
        return False

    nums = sorted(t.number for t in real)
    if len(set(nums)) != len(nums):
        return False

    # 需要幾張 Joker 填滿 nums 的缺口
    gaps = (nums[-1] - nums[0] + 1) - len(nums)
    if gaps > n_joker:
        return False
    # 剩下的 Joker 接在兩端，需要放得下
    leftover = n_joker - gaps
    span = nums[-1] - nums[0] + 1 + leftover
    return span <= 13 and nums[0] >= 1


def is_valid_group(tiles):
    """同數字、不同色，3 或 4 張。"""
    if len(tiles) not in (3, 4):
        return False
    real = [t for t in tiles if not t.is_joker]
    n_joker = len(tiles) - len(real)
    if not real:
        return False
    if len({t.number for t in real}) != 1:
        return False
    colors = {t.color for t in real}
    if len(colors) != len(real):
        return False
    return len(real) + n_joker <= 4


def is_valid_set(tiles):
    return is_valid_run(tiles) or is_valid_group(tiles)


# ---------------------------------------------------------------------------
# 概念
# ---------------------------------------------------------------------------

CONCEPTS = [
    "RUN_BASIC",
    "GROUP_BASIC",
    "ATTACH_TAIL",
    "ATTACH_HEAD",
    "GROUP_COMPLETE",
    "JOKER_AS_TILE",
    "JOKER_FILL_GAP",
    "RUN_LONG",
]

EXPOSURE_GAIN = 0.34        # 看一次 REVEAL 增加的熟悉度 → 大約要看三次才會
MASTERY_THRESHOLD = 1.0     # 熟悉度到這個值才算真的會


class Knowledge:
    def __init__(self, known=(), rng=None):
        self.familiarity = {c: 0.0 for c in CONCEPTS}
        for c in known:
            self.familiarity[c] = MASTERY_THRESHOLD

    def knows(self, concept):
        return self.familiarity[concept] >= MASTERY_THRESHOLD - 1e-9

    def expose(self, concept):
        """看過一次示範 —— 只往後進展一點點。"""
        if concept in self.familiarity:
            self.familiarity[concept] = min(
                MASTERY_THRESHOLD, self.familiarity[concept] + EXPOSURE_GAIN)

    def mastered(self):
        return [c for c in CONCEPTS if self.knows(c)]


# ---------------------------------------------------------------------------
# 題目：桌面 + 手牌，有唯一一種「所需概念」
# ---------------------------------------------------------------------------

class Puzzle:
    def __init__(self, board_sets, hand, required_concept, description):
        self.board_sets = board_sets
        self.hand = hand
        self.required_concept = required_concept
        self.description = description


def make_puzzles(rng, n=200):
    """產生題目。每題都保證有解，且解需要某個特定概念。"""
    puzzles = []
    for _ in range(n):
        kind = rng.choice([
            "ATTACH_TAIL", "ATTACH_HEAD", "GROUP_COMPLETE",
            "JOKER_FILL_GAP", "RUN_LONG",
        ])
        c = rng.randrange(4)
        base = rng.randint(2, 9)

        if kind == "ATTACH_TAIL":
            run = [Tile(base + i, c) for i in range(3)]
            answer = Tile(base + 3, c)
            board = [run]
            hand = [answer, Tile(rng.randint(1, 13), (c + 1) % 4)]
            desc = f"把 {answer} 接在 {run[0]}-{run[-1]} 後面"

        elif kind == "ATTACH_HEAD":
            run = [Tile(base + i, c) for i in range(3)]
            answer = Tile(base - 1, c)
            board = [run]
            hand = [answer, Tile(rng.randint(1, 13), (c + 2) % 4)]
            desc = f"把 {answer} 接在 {run[0]}-{run[-1]} 前面"

        elif kind == "GROUP_COMPLETE":
            num = rng.randint(1, 13)
            cols = rng.sample(range(4), 3)
            grp = [Tile(num, x) for x in cols]
            missing = [x for x in range(4) if x not in cols][0]
            answer = Tile(num, missing)
            board = [grp]
            hand = [answer, Tile(rng.randint(1, 13), cols[0])]
            desc = f"把 {answer} 補進 {num} 的 Group"

        elif kind == "JOKER_FILL_GAP":
            run = [Tile(base + i, c) for i in range(3)]
            answer = joker()
            board = [run]
            hand = [answer, Tile(rng.randint(1, 13), (c + 3) % 4)]
            desc = f"用 Joker 接在 {run[0]}-{run[-1]} 上"

        else:  # RUN_LONG
            run = [Tile(base + i, c) for i in range(4)]
            answer = Tile(base + 4, c)
            board = [run]
            hand = [answer, Tile(rng.randint(1, 13), (c + 1) % 4)]
            desc = f"把 {answer} 接上去（Run 會變 5 張）"

        puzzles.append(Puzzle(board, hand, kind, desc))
    return puzzles


# ---------------------------------------------------------------------------
# 學習者：窮舉「它想得到的」走法
# ---------------------------------------------------------------------------

class KnowledgeLearner:
    """關鍵：這個 agent 不擲骰子。

    它窮舉所有走法，但只窮舉「已知概念涵蓋得到的」那些。
    不知道 ATTACH_HEAD 的人，永遠不會去試把牌放在 Run 前面 ——
    那個念頭不存在，不是他運氣不好。
    """

    def __init__(self, knowledge):
        self.k = knowledge

    def enumerate_moves(self, puzzle, area_hint=None):
        """回傳所有這個學習者「想得到」的走法。

        area_hint 是 POINT_TO_AREA 給的顏色限制 —— 它縮小範圍，
        但**不會讓學習者想到原本想不到的走法**。
        """
        moves = []
        for si, s in enumerate(puzzle.board_sets):
            if area_hint is not None:
                colors = {t.color for t in s if not t.is_joker}
                if area_hint not in colors:
                    continue

            is_run = is_valid_run(s)
            is_grp = is_valid_group(s)

            for tile in puzzle.hand:
                # --- 接在 Run 尾巴 ---
                if is_run and self.k.knows("ATTACH_TAIL"):
                    cand = s + [tile]
                    if len(cand) > 4 and not self.k.knows("RUN_LONG"):
                        pass
                    elif tile.is_joker and not self.k.knows("JOKER_AS_TILE"):
                        pass
                    elif is_valid_run(cand):
                        moves.append(("ATTACH_TAIL", si, tile))

                # --- 接在 Run 頭 ---
                if is_run and self.k.knows("ATTACH_HEAD"):
                    cand = [tile] + s
                    if len(cand) > 4 and not self.k.knows("RUN_LONG"):
                        pass
                    elif tile.is_joker and not self.k.knows("JOKER_AS_TILE"):
                        pass
                    elif is_valid_run(cand):
                        moves.append(("ATTACH_HEAD", si, tile))

                # --- 補滿 Group ---
                if is_grp and self.k.knows("GROUP_COMPLETE"):
                    cand = s + [tile]
                    if tile.is_joker and not self.k.knows("JOKER_AS_TILE"):
                        pass
                    elif is_valid_group(cand):
                        moves.append(("GROUP_COMPLETE", si, tile))

        return moves

    def attempt(self, puzzle, tier, area_hint=None):
        """這一回合能不能解出來 —— 完全由知識決定，沒有隨機性。"""
        if tier == REVEAL_MOVE:
            return True     # 直接被告知答案，照做就對了

        hint_color = area_hint if tier == POINT_TO_AREA else None
        return len(self.enumerate_moves(puzzle, hint_color)) > 0


# ---------------------------------------------------------------------------
# 教練（對應 tierFromStuckTurns）
# ---------------------------------------------------------------------------

GENTLE_NUDGE, POINT_TO_AREA, REVEAL_MOVE = 0, 1, 2
TIER_NAME = {0: "輕推", 1: "指方向", 2: "講答案"}


def tier_from_stuck_turns(stuck, t1, t2):
    if stuck < t1:
        return GENTLE_NUDGE
    if stuck < t2:
        return POINT_TO_AREA
    return REVEAL_MOVE


# ---------------------------------------------------------------------------
# 一題
# ---------------------------------------------------------------------------

def solve_puzzle(learner, puzzle, t1, t2, max_turns=12, frustration=0.0, rng=None):
    """回傳 (解出來?, 回合數, 是否靠看答案, 用到的最高提示層級)。"""
    area = puzzle.board_sets[0][0].color

    for stuck in range(max_turns):
        tier = tier_from_stuck_turns(stuck, t1, t2)

        if frustration and rng and rng.random() < frustration * stuck:
            return False, stuck + 1, False, tier

        if learner.attempt(puzzle, tier, area):
            saw_answer = (tier == REVEAL_MOVE)
            if saw_answer:
                # 看過一次示範 —— 只往後進展一點點
                learner.k.expose(puzzle.required_concept)
                if puzzle.required_concept == "JOKER_FILL_GAP":
                    learner.k.expose("JOKER_AS_TILE")
            return True, stuck + 1, saw_answer, tier

    return False, max_turns, False, REVEAL_MOVE


def run_session(start_known, puzzles, t1, t2, frustration=0.0, seed=0):
    rng = random.Random(seed)
    k = Knowledge(start_known)
    learner = KnowledgeLearner(k)

    solved = autonomous = quit_count = 0
    turns = []
    tier_used = Counter()
    per_concept = {c: {"n": 0, "auto": 0} for c in CONCEPTS}

    for p in puzzles:
        ok, n, saw, tier = solve_puzzle(learner, p, t1, t2,
                                        frustration=frustration, rng=rng)
        turns.append(n)
        tier_used[tier] += 1
        per_concept[p.required_concept]["n"] += 1
        if ok:
            solved += 1
            if not saw:
                autonomous += 1
                per_concept[p.required_concept]["auto"] += 1
        else:
            quit_count += 1

    n = len(puzzles)
    return {
        "solve_rate": solved / n,
        "autonomy_rate": autonomous / n,
        "quit_rate": quit_count / n,
        "mean_turns": statistics.mean(turns),
        "mastered": learner.k.mastered(),
        "n_mastered": len(learner.k.mastered()),
        "tier_used": tier_used,
        "per_concept": per_concept,
    }


# ---------------------------------------------------------------------------
# 實驗
# ---------------------------------------------------------------------------

STARTING_POINTS = {
    "完全不會": (),
    "只認得 Run/Group": ("RUN_BASIC", "GROUP_BASIC"),
    "會接尾巴": ("RUN_BASIC", "GROUP_BASIC", "ATTACH_TAIL"),
    "只差 Joker": ("RUN_BASIC", "GROUP_BASIC", "ATTACH_TAIL",
                 "ATTACH_HEAD", "GROUP_COMPLETE", "RUN_LONG"),
}


def exp1_gentle_nudge_is_useless():
    print("=" * 76)
    print("實驗一：「輕推」對知識型學習者有沒有用？")
    print("=" * 76)
    print("輕推說的是「你手上有牌可以出喔」—— 不含任何概念資訊。")
    print("對一個已經窮舉過所有想得到的走法的人來說，這句話帶來什麼？")
    print()

    rng = random.Random(0)
    puzzles = make_puzzles(rng, 60)

    for name, known in STARTING_POINTS.items():
        k = Knowledge(known)
        learner = KnowledgeLearner(k)
        solvable = sum(1 for p in puzzles
                       if learner.attempt(p, GENTLE_NUDGE, p.board_sets[0][0].color))
        print(f"  {name:<16} 靠輕推能解出 {solvable:3d}/{len(puzzles)} 題 "
              f"({solvable/len(puzzles):.0%})")

    print()
    print("  → 輕推能解的題目，本來不給提示也解得出來。")
    print("    它不改變任何事，只是延後了真正有用的提示。")
    print()


def exp2_point_to_area():
    print("=" * 76)
    print("實驗二：「指方向」對不同起點的人效果一樣嗎？")
    print("=" * 76)
    print()

    rng = random.Random(1)
    puzzles = make_puzzles(rng, 60)

    print(f"  {'起點':<16} {'輕推':>8} {'指方向':>10} {'差異':>8}")
    print("  " + "-" * 46)
    for name, known in STARTING_POINTS.items():
        k = Knowledge(known)
        learner = KnowledgeLearner(k)
        a = sum(1 for p in puzzles
                if learner.attempt(p, GENTLE_NUDGE, p.board_sets[0][0].color))
        b = sum(1 for p in puzzles
                if learner.attempt(p, POINT_TO_AREA, p.board_sets[0][0].color))
        print(f"  {name:<16} {a:>8} {b:>10} {b-a:>+8}")

    print()
    print("  → 指方向縮小的是搜尋範圍，不是知識邊界。")
    print("    不知道『可以接在 Run 前面』的人，")
    print("    叫他看紅色那排也沒用 —— 他不知道要拿那排做什麼。")
    print()


def exp3_learning_curve():
    print("=" * 76)
    print("實驗三：從完全不會開始，門檻怎麼影響學習")
    print("=" * 76)
    print()

    rng = random.Random(2)
    puzzles = make_puzzles(rng, 120)

    print(f"  {'門檻':<10} {'解出率':>8} {'自主率':>8} {'學會幾個概念':>14} {'平均回合':>10}")
    print("  " + "-" * 56)

    for t1, t2 in [(0, 0), (1, 2), (2, 4), (3, 6), (4, 8), (99, 99)]:
        r = run_session((), puzzles, t1, t2, frustration=0.05, seed=0)
        label = f"{t1}/{t2}"
        if (t1, t2) == (0, 0):
            label += " 直接講"
        elif (t1, t2) == (2, 4):
            label += " 現行"
        elif t1 == 99:
            label += " 不給"
        print(f"  {label:<10} {r['solve_rate']:>7.0%} {r['autonomy_rate']:>8.0%} "
              f"{r['n_mastered']:>11d}/8 {r['mean_turns']:>10.2f}")

    print()


def exp4_concept_order():
    print("=" * 76)
    print("實驗四：哪些概念學得起來、哪些學不起來")
    print("=" * 76)
    print()

    rng = random.Random(3)
    puzzles = make_puzzles(rng, 200)
    r = run_session((), puzzles, 2, 4, frustration=0.05, seed=0)

    print(f"  現行門檻 2/4，從完全不會開始解 200 題")
    print(f"  最後學會：{'、'.join(r['mastered']) if r['mastered'] else '（無）'}")
    print()
    print(f"  {'概念':<18} {'出現題數':>10} {'自主解出':>10} {'自主率':>10}")
    print("  " + "-" * 52)
    for c, d in r["per_concept"].items():
        if d["n"]:
            print(f"  {c:<18} {d['n']:>10} {d['auto']:>10} "
                  f"{d['auto']/d['n']:>9.0%}")
    print()
    print(f"  提示層級使用次數：", dict(r["tier_used"]))
    print()


if __name__ == "__main__":
    exp1_gentle_nudge_is_useless()
    exp2_point_to_area()
    exp3_learning_curve()
    exp4_concept_order()
