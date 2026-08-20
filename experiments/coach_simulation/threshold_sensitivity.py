"""認知教練 —— 提示門檻的敏感度實驗

## 這個實驗在回答什麼

`tierFromStuckTurns()` 目前是：

    if (stuck_turns < 2) return GENTLE_NUDGE;
    if (stuck_turns < 4) return POINT_TO_AREA;
    return REVEAL_MOVE;

設計文件自己寫了：「示意性門檻，不是臨床驗證過的數字」。

問題是——**沒有真人數據，這兩個數字要怎麼定？**

## 為什麼不能直接「用 AI 當受測者」

蘇教授建議用玩卡牌的 AI 當受測者。但有個結構性問題：
`AIAgent_0` 之類的 agent **不會對提示做出反應**，它們只會照自己的策略出牌。
給它提示，它的行為一模一樣。

那自己寫一個「學習者模型」呢？——那會變成循環論證：
用自己編的參數，驗證自己編的門檻。

## 所以這個實驗換一個問法

> 在**各種**可能的學習者假設下，門檻設在哪裡會不會改變結論？

- 如果不會 → 2/4 這個設定沒那麼要緊，可以先不管
- 如果會，而且差很多 → **結論是「沒有真人數據就不可能設對」**

第二種結果本身就是答案，而且比硬給一個數字誠實得多。

## 學習者模型

每個學習者有三個參數：

    base_skill      不靠提示時，一回合內找到解的機率
    hint_gain[tier] 各層提示讓機率上升多少
    learn_rate      看過答案後，base_skill 提升多少（會不會學起來）

模型本身是假設，不是事實 —— 所以才要掃過整個參數空間。

## 兩個互相拉扯的指標

教練型 AI 的目標不是「讓玩家快點出牌」，是「讓玩家自己學會找」。
所以要同時看：

    solve_rate      解出來的比例（太低 = 玩家會放棄）
    autonomy_rate   **沒看答案**就解出來的比例（這才是有沒有教會）

只看第一個的話，最好的策略是「第一回合就講答案」—— 但那什麼也沒教到。
"""

import itertools
import random
import statistics


# ---------------------------------------------------------------------------
# 提示層級（對應 C++ 的 HintTier）
# ---------------------------------------------------------------------------

GENTLE_NUDGE = 0
POINT_TO_AREA = 1
REVEAL_MOVE = 2

TIER_NAMES = {0: "輕推", 1: "指方向", 2: "講答案"}


def tier_from_stuck_turns(stuck_turns, t1, t2):
    """完全對應 cognitive_hint_engine.cpp:114-119

    目前程式碼裡 t1 = 2, t2 = 4。
    """
    if stuck_turns < t1:
        return GENTLE_NUDGE
    if stuck_turns < t2:
        return POINT_TO_AREA
    return REVEAL_MOVE


# ---------------------------------------------------------------------------
# 學習者模型
# ---------------------------------------------------------------------------

class Learner:
    """模擬的學習者。

    這是一個**模型**，不是真實玩家。所有參數都是假設。
    實驗的重點不是「用這組參數會怎樣」，而是「換一組參數結論會不會變」。
    """

    def __init__(self, base_skill, hint_gain, learn_rate, frustration, rng):
        self.base_skill = base_skill
        self.initial_skill = base_skill
        self.hint_gain = hint_gain      # {tier: 機率增量}
        self.learn_rate = learn_rate
        self.frustration = frustration  # 每多卡一回合，放棄機率上升多少
        self.rng = rng

    def attempt(self, tier):
        """這一回合有沒有找到解。回傳 (成功?, 是否靠看答案)。"""
        p = min(1.0, self.base_skill + self.hint_gain[tier])
        success = self.rng.random() < p

        # 看到答案就一定做得出來（那層提示直接講出要出哪張牌）
        if tier == REVEAL_MOVE:
            success = True

        return success, (tier == REVEAL_MOVE)

    def gives_up(self, stuck_turns, tier):
        """卡越久越可能放棄 —— 這是讓問題成立的關鍵。

        第一版實驗漏掉這一項，結果「永不給提示」在每種學習者身上都最好，
        因為模型裡的玩家可以無限嘗試、永遠不會挫折。

        設計文件自己寫過：「但也不能不給提示，卡住太久人就放棄了。」
        少了這個，教練就沒有存在的必要。

        提示會降低挫折感 —— 即使還沒解出來，知道「有路可走」就撐得住。
        """
        base = self.frustration * stuck_turns
        relief = {0: 0.3, 1: 0.6, 2: 1.0}[tier]   # 提示層級越深，越不容易放棄
        return self.rng.random() < base * (1.0 - relief * 0.7)

    def learn(self, saw_answer):
        """解完一題之後技能有沒有提升。

        假設：靠自己解出來學得比較多，看答案學得比較少。
        這個假設本身也是掃描的對象之一。
        """
        gain = self.learn_rate * (0.3 if saw_answer else 1.0)
        self.base_skill = min(0.95, self.base_skill + gain)


# ---------------------------------------------------------------------------
# 一題的流程
# ---------------------------------------------------------------------------

def run_puzzle(learner, t1, t2, max_turns=10):
    """跑一題。回傳 (解出來?, 花幾回合, 有沒有看答案)。

    對應 CognitiveCoachAgent::playTurn 的流程：
    桌面沒變 → stuck_turns++ → 依 stuck_turns 決定提示層級。
    """
    for stuck in range(max_turns):
        tier = tier_from_stuck_turns(stuck, t1, t2)

        if learner.gives_up(stuck, tier):
            return False, stuck + 1, False

        success, saw_answer = learner.attempt(tier)
        if success:
            return True, stuck + 1, saw_answer
    return False, max_turns, False


def run_session(learner, t1, t2, n_puzzles=20):
    """一個學習者連續解 n 題，回傳整體表現。"""
    solved = 0
    autonomous = 0
    turns = []

    for _ in range(n_puzzles):
        ok, n_turns, saw = run_puzzle(learner, t1, t2)
        turns.append(n_turns)
        if ok:
            solved += 1
            if not saw:
                autonomous += 1
            learner.learn(saw)

    return {
        "solve_rate": solved / n_puzzles,
        "autonomy_rate": autonomous / n_puzzles,
        "quit_rate": (n_puzzles - solved) / n_puzzles,
        "mean_turns": statistics.mean(turns),
        "skill_gain": learner.base_skill - learner.initial_skill,
    }


# ---------------------------------------------------------------------------
# 學習者的各種可能長相
# ---------------------------------------------------------------------------

LEARNER_PROFILES = {
    "完全新手": dict(base_skill=0.05, hint_gain={0: 0.05, 1: 0.20, 2: 1.0},
                 learn_rate=0.04, frustration=0.06),
    "一般玩家": dict(base_skill=0.25, hint_gain={0: 0.10, 1: 0.30, 2: 1.0},
                 learn_rate=0.03, frustration=0.05),
    "老手":   dict(base_skill=0.55, hint_gain={0: 0.10, 1: 0.25, 2: 1.0},
                 learn_rate=0.01, frustration=0.03),
    "提示無感": dict(base_skill=0.20, hint_gain={0: 0.01, 1: 0.03, 2: 1.0},
                 learn_rate=0.03, frustration=0.05),
    "提示敏感": dict(base_skill=0.15, hint_gain={0: 0.25, 1: 0.50, 2: 1.0},
                 learn_rate=0.05, frustration=0.05),
    "易放棄":  dict(base_skill=0.20, hint_gain={0: 0.10, 1: 0.30, 2: 1.0},
                 learn_rate=0.03, frustration=0.15),
    "有耐心":  dict(base_skill=0.20, hint_gain={0: 0.10, 1: 0.30, 2: 1.0},
                 learn_rate=0.03, frustration=0.01),
}


def evaluate(profile, t1, t2, n_seeds=200, n_puzzles=20):
    """同一組設定跑多個種子，回傳平均與標準差。

    多種子很重要 —— 單一種子的結論可能完全錯。
    """
    results = []
    for seed in range(n_seeds):
        rng = random.Random(seed)
        learner = Learner(rng=rng, **profile)
        results.append(run_session(learner, t1, t2, n_puzzles))

    out = {}
    for key in results[0]:
        vals = [r[key] for r in results]
        out[key] = statistics.mean(vals)
        out[key + "_sd"] = statistics.pstdev(vals)
    return out


# ---------------------------------------------------------------------------
# 實驗一：現有的 2/4 在各種學習者身上表現如何
# ---------------------------------------------------------------------------

def experiment_current_setting():
    print("=" * 78)
    print("實驗一：現行設定 (t1=2, t2=4) 對不同學習者的效果")
    print("=" * 78)
    print(f"{'學習者':<10} {'解出率':>10} {'自主率':>14} {'平均回合':>10} {'技能提升':>10}")
    print("-" * 78)

    for name, profile in LEARNER_PROFILES.items():
        r = evaluate(profile, 2, 4)
        print(f"{name:<10} {r['solve_rate']:>9.1%} "
              f"{r['autonomy_rate']:>8.1%} ±{r['autonomy_rate_sd']:.2f} "
              f"{r['mean_turns']:>10.2f} {r['skill_gain']:>+10.3f}")
    print()


# ---------------------------------------------------------------------------
# 實驗二：門檻掃描 —— 這是核心
# ---------------------------------------------------------------------------

def experiment_threshold_sweep():
    print("=" * 78)
    print("實驗二：門檻掃描 —— 每種學習者的最佳門檻是不是同一個？")
    print("=" * 78)
    print("（目標函數 = 自主率，因為教練的目的是教會而不是代勞）")
    print()

    grid = [(t1, t2) for t1 in range(1, 7) for t2 in range(t1 + 1, 9)]
    best_per_profile = {}

    for name, profile in LEARNER_PROFILES.items():
        scored = []
        for t1, t2 in grid:
            r = evaluate(profile, t1, t2, n_seeds=100)
            scored.append(((t1, t2), r["autonomy_rate"], r["solve_rate"]))

        scored.sort(key=lambda x: -x[1])
        best_per_profile[name] = scored[0]

        cur = [s for s in scored if s[0] == (2, 4)][0]
        rank = scored.index(cur) + 1

        print(f"{name}")
        print(f"  最佳門檻 t1={scored[0][0][0]}, t2={scored[0][0][1]}  "
              f"自主率 {scored[0][1]:.1%}  解出率 {scored[0][2]:.1%}")
        print(f"  現行 2/4              自主率 {cur[1]:.1%}  解出率 {cur[2]:.1%}  "
              f"（在 {len(grid)} 組裡排第 {rank}）")
        print()

    return best_per_profile


# ---------------------------------------------------------------------------
# 實驗三：最佳門檻分散得多開
# ---------------------------------------------------------------------------

def experiment_dispersion(best_per_profile):
    print("=" * 78)
    print("實驗三：最佳門檻的分散程度")
    print("=" * 78)

    t1s = [v[0][0] for v in best_per_profile.values()]
    t2s = [v[0][1] for v in best_per_profile.values()]

    print(f"  t1 範圍 {min(t1s)} ~ {max(t1s)}   (標準差 {statistics.pstdev(t1s):.2f})")
    print(f"  t2 範圍 {min(t2s)} ~ {max(t2s)}   (標準差 {statistics.pstdev(t2s):.2f})")
    print()

    if max(t1s) - min(t1s) <= 1 and max(t2s) - min(t2s) <= 1:
        print("  → 各種學習者的最佳門檻幾乎一致。")
        print("    代表門檻不是敏感參數，2/4 大致可用。")
    else:
        print("  → 不同學習者的最佳門檻差很多。")
        print("    代表**沒有真實使用者資料就無法設定這兩個數字**，")
        print("    而且單一組固定門檻本來就服務不了所有人 → 應該做自適應門檻。")
    print()


# ---------------------------------------------------------------------------
# 實驗四：門檻到底重不重要（跟兩個極端比）
# ---------------------------------------------------------------------------

def experiment_bounds():
    print("=" * 78)
    print("實驗四：跟兩個極端策略比較")
    print("=" * 78)
    print("  永遠講答案 = (0,0)   永遠只輕推 = (99,99)")
    print()
    print(f"{'學習者':<10} {'永遠講答案':>22} {'現行 2/4':>22} {'永不給答案':>22}")
    print(f"{'':10} {'解出/自主':>22} {'解出/自主':>22} {'解出/自主':>22}")
    print("-" * 78)

    for name, profile in LEARNER_PROFILES.items():
        a = evaluate(profile, 0, 0, n_seeds=100)
        b = evaluate(profile, 2, 4, n_seeds=100)
        c = evaluate(profile, 99, 99, n_seeds=100)
        print(f"{name:<10} "
              f"{a['solve_rate']:>11.1%}/{a['autonomy_rate']:<10.1%} "
              f"{b['solve_rate']:>11.1%}/{b['autonomy_rate']:<10.1%} "
              f"{c['solve_rate']:>11.1%}/{c['autonomy_rate']:<10.1%}")
    print()


if __name__ == "__main__":
    experiment_current_setting()
    best = experiment_threshold_sweep()
    experiment_dispersion(best)
    experiment_bounds()
