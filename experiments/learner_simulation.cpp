/* -------------------------------------------------------
   learner_simulation.cpp —— 用 AI 學習者驗證引導門檻

   為什麼要做這個：
     六個關卡的引導門檻（卡幾回合才開口、最多給到哪一層）
     全部是設計時憑判斷定下的數字，沒有任何實證支持。
     找真人試用成本太高，而且樣本小、環境不受控。

     成大蘇文鈺教授的建議是：找 AI 當受測者。
     好處是樣本可以無限多、完全受控、可重現，
     而且不需要招募與倫理程序。

   這支程式做什麼：
     模擬五種不同能力的學習者，各自跑完六個關卡，
     記錄每一關實際觸發的提示層級分布、過關所需回合數，
     以及兩種異常：
       - 太早給答案（學習者其實找得到，系統卻先講了）
       - 卡死沒人救（學習者找不到，系統也不開口）

   模擬的是什麼、不是什麼：
     這裡模擬的是「學習者在每個回合有多大機率自己找到那手牌」，
     不是真的在玩拉密。因為要驗證的是引導邏輯的時序，
     不是遊戲規則本身——規則已經由 validator 的 40 項測試把關。

     因此本實驗無法回答「真人是否會因此學得更好」，
     只能回答「在給定的能力假設下，門檻會不會太早或太晚」。
     這是刻意的範圍限制，不是疏漏。

   編譯：
     g++ -std=c++17 -I src experiments/learner_simulation.cpp \
         src/coach_campaign.cpp -o learner_sim
     ./learner_sim
------------------------------------------------------- */

#include "coach_campaign.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// ═════════════════════════════════════════════════════════
//  學習者模型
// ═════════════════════════════════════════════════════════
/*
   每個學習者用兩個參數描述：

   base_skill      在沒有任何提示下，單一回合自己找到那手牌的機率
   hint_boost[3]   收到各層提示後，機率提升多少
                   （輕推 / 指方向 / 講答案）

   「講答案」那一層對所有學習者都是 1.0——
   系統把牌講出來了，照做就是了。這一點不因能力而異。

   base_skill 隨關卡難度遞減：後面的招數本來就比較難想到。
   這裡用一個簡單的線性衰減，係數同樣是憑判斷設的——
   這是本實驗最大的假設，結論的可信度不會超過這個假設的可信度。
*/
struct Learner {
    std::string name;
    std::string description;
    double base_skill;
    double hint_boost[3];   // GENTLE / POINT / REVEAL
};

static const Learner kLearners[] = {
    {"完全新手", "幾乎找不到任何組合，需要大量引導",
     0.05, {0.10, 0.35, 1.00}},
    {"學得慢的", "只看得到最明顯的一手，複雜的想不到",
     0.15, {0.25, 0.55, 1.00}},
    {"一般玩家", "大部分找得到，遇到重組類的會卡",
     0.30, {0.45, 0.70, 1.00}},
    {"學得快的", "幾乎都找得到，偶爾需要提醒",
     0.50, {0.65, 0.85, 1.00}},
    {"已經會的", "幾乎不需要幫助",
     0.75, {0.85, 0.95, 1.00}},
};

static const int kLearnerCount = sizeof(kLearners) / sizeof(kLearners[0]);

// 後面的關卡比較難，能力要打折。
// 這個 0.85 的衰減係數是假設，不是量測值。
static double skillAtLevel(const Learner& l, int level) {
    double decay = 1.0;
    for (int i = 1; i < level; ++i) decay *= 0.85;
    double s = l.base_skill * decay;
    return s < 0.01 ? 0.01 : s;   // 保底，避免完全不可能過關
}

// ═════════════════════════════════════════════════════════
//  單一關卡的模擬結果
// ═════════════════════════════════════════════════════════
struct LevelResult {
    int turns_used = 0;          // 過關花了幾個回合
    int silent_turns = 0;        // 系統沒開口的回合數
    int nudge_turns = 0;
    int point_turns = 0;
    int reveal_turns = 0;
    int safety_net_turns = 0;    // 由保底機制觸發的回合數
    int solved_unassisted = 0;   // 沒看 REVEAL 就做出來的次數
    int solved_total = 0;
    bool completed = false;
    int longest_stuck = 0;       // 最長連續卡關回合數
};

// 模擬一位學習者打一個關卡。
// max_turns 是安全上限——超過就視為卡死，這本身就是一個要觀察的訊號。
static LevelResult simulateLevel(const Learner& learner, int level,
                                 std::mt19937& rng, int max_turns = 200) {
    LevelResult r;
    CoachCampaign campaign;
    while (campaign.currentLevel() < level) campaign.advance();

    const LevelConfig& cfg = CoachCampaign::levelConfig(level);
    double skill = skillAtLevel(learner, level);
    std::uniform_real_distribution<double> dice(0.0, 1.0);

    int stuck = 0;              // 這一手已經卡了幾個回合
    bool saw_point = false;     // 這一手看過指方向了嗎
    bool saw_reveal = false;    // 這一手看過答案了嗎

    for (int turn = 0; turn < max_turns; ++turn) {
        ++r.turns_used;

        // 系統決定要不要開口、給哪一層
        HintTier tier;
        bool from_net = false;
        bool speaks = campaign.shouldGiveHint(stuck, tier, from_net);
        if (speaks && from_net) ++r.safety_net_turns;

        double p = skill;
        if (speaks) {
            switch (tier) {
                case HintTier::GENTLE_NUDGE:
                    ++r.nudge_turns;
                    p += learner.hint_boost[0];
                    break;
                case HintTier::POINT_TO_AREA:
                    ++r.point_turns;
                    saw_point = true;
                    p += learner.hint_boost[1];
                    break;
                case HintTier::REVEAL_MOVE:
                    ++r.reveal_turns;
                    saw_reveal = true;
                    p += learner.hint_boost[2];
                    break;
            }
        } else {
            ++r.silent_turns;
        }
        if (p > 1.0) p = 1.0;

        if (dice(rng) < p) {
            // 找到了——記錄這一次的掌握程度
            TechniqueUse use;
            use.technique = cfg.technique;
            use.saw_reveal = saw_reveal;
            use.saw_point = saw_point;
            campaign.recordUse(use);

            ++r.solved_total;
            if (!saw_reveal) ++r.solved_unassisted;

            if (stuck > r.longest_stuck) r.longest_stuck = stuck;
            stuck = 0;
            saw_point = saw_reveal = false;

            if (campaign.canAdvance()) {
                r.completed = true;
                break;
            }
        } else {
            ++stuck;
            if (stuck > r.longest_stuck) r.longest_stuck = stuck;
        }
    }
    return r;
}

// ═════════════════════════════════════════════════════════
//  彙總
// ═════════════════════════════════════════════════════════
struct Aggregate {
    double avg_turns = 0;
    double avg_longest_stuck = 0;
    double pct_silent = 0;
    double pct_nudge = 0;
    double pct_point = 0;
    double pct_reveal = 0;
    double completion_rate = 0;
    double unassisted_ratio = 0;   // 自主解出佔全部解出的比例
    double pct_safety_net = 0;     // 保底觸發佔比
};

static Aggregate runTrials(const Learner& learner, int level,
                           int trials, std::mt19937& rng) {
    Aggregate a;
    long total_turns = 0, silent = 0, nudge = 0, point = 0, reveal = 0;
    long solved = 0, unassisted = 0, completed = 0, stuck_sum = 0, net = 0;

    for (int i = 0; i < trials; ++i) {
        LevelResult r = simulateLevel(learner, level, rng);
        total_turns += r.turns_used;
        silent += r.silent_turns;
        nudge  += r.nudge_turns;
        point  += r.point_turns;
        reveal += r.reveal_turns;
        solved += r.solved_total;
        unassisted += r.solved_unassisted;
        stuck_sum += r.longest_stuck;
        net += r.safety_net_turns;
        if (r.completed) ++completed;
    }

    double t = static_cast<double>(total_turns);
    a.avg_turns = t / trials;
    a.avg_longest_stuck = static_cast<double>(stuck_sum) / trials;
    a.pct_silent = 100.0 * silent / t;
    a.pct_nudge  = 100.0 * nudge  / t;
    a.pct_point  = 100.0 * point  / t;
    a.pct_reveal = 100.0 * reveal / t;
    a.completion_rate = 100.0 * completed / trials;
    a.unassisted_ratio = solved > 0 ? 100.0 * unassisted / solved : 0.0;
    a.pct_safety_net = 100.0 * net / t;
    return a;
}

// ═════════════════════════════════════════════════════════
int main() {
    const int TRIALS = 2000;
    std::mt19937 rng(20260815);   // 固定種子，結果可重現

    printf("學習者模擬實驗\n");
    printf("每種學習者 × 每個關卡各跑 %d 次，隨機種子固定\n\n", TRIALS);

    printf("受測者設定：\n");
    for (const auto& l : kLearners)
        printf("  %-8s base_skill=%.2f  %s\n",
               l.name.c_str(), l.base_skill, l.description.c_str());
    printf("\n");

    // ── 主表：各關卡 × 各學習者 ──────────────────────────
    for (int level = 1; level <= CoachCampaign::totalLevels(); ++level) {
        const LevelConfig& cfg = CoachCampaign::levelConfig(level);
        printf("─────────────────────────────────────────────────────────────\n");
        printf("Level %d  %s（引導 %d%%）\n", level, cfg.name.c_str(),
               cfg.guidance_percent);
        printf("  門檻：輕推@%d  指方向@%d  講答案@%s  上限=%s\n",
               cfg.nudge_after_turns, cfg.point_after_turns,
               cfg.reveal_after_turns < 0 ? "永不" :
                   std::to_string(cfg.reveal_after_turns).c_str(),
               cfg.max_tier == HintTier::GENTLE_NUDGE ? "輕推" :
               cfg.max_tier == HintTier::POINT_TO_AREA ? "指方向" : "講答案");
        printf("  過關：用出 %d 次，其中 %d 次需自主\n\n",
               cfg.required_uses, cfg.required_unassisted);

        printf("  %-8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
               "學習者", "過關率", "平均回合", "最久卡關",
               "沉默%", "輕推%", "指方向%", "講答案%", "保底%");

        for (const auto& l : kLearners) {
            Aggregate a = runTrials(l, level, TRIALS, rng);
            printf("  %-8s %7.1f%% %8.1f %8.1f %7.1f%% %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n",
                   l.name.c_str(), a.completion_rate, a.avg_turns,
                   a.avg_longest_stuck, a.pct_silent, a.pct_nudge,
                   a.pct_point, a.pct_reveal, a.pct_safety_net);
        }
        printf("\n");
    }

    // ── 異常偵測 ─────────────────────────────────────────
    printf("═════════════════════════════════════════════════════════════\n");
    printf("異常偵測\n\n");

    printf("【卡死風險】平均最久連續卡關 ≥ 8 回合的組合：\n");
    bool any_stuck = false;
    for (int level = 1; level <= CoachCampaign::totalLevels(); ++level) {
        for (const auto& l : kLearners) {
            Aggregate a = runTrials(l, level, TRIALS, rng);
            if (a.avg_longest_stuck >= 8.0) {
                printf("  L%d %-8s 平均卡 %.1f 回合，過關率 %.1f%%\n",
                       level, l.name.c_str(), a.avg_longest_stuck,
                       a.completion_rate);
                any_stuck = true;
            }
        }
    }
    if (!any_stuck) printf("  （無）\n");

    printf("\n【過度介入】講答案佔比 ≥ 25%% 的組合：\n");
    bool any_over = false;
    for (int level = 1; level <= CoachCampaign::totalLevels(); ++level) {
        for (const auto& l : kLearners) {
            Aggregate a = runTrials(l, level, TRIALS, rng);
            if (a.pct_reveal >= 25.0) {
                printf("  L%d %-8s 講答案佔 %.1f%%，自主解出僅 %.1f%%\n",
                       level, l.name.c_str(), a.pct_reveal,
                       a.unassisted_ratio);
                any_over = true;
            }
        }
    }
    if (!any_over) printf("  （無）\n");

    printf("\n【過關率過低】低於 90%% 的組合：\n");
    bool any_fail = false;
    for (int level = 1; level <= CoachCampaign::totalLevels(); ++level) {
        for (const auto& l : kLearners) {
            Aggregate a = runTrials(l, level, TRIALS, rng);
            if (a.completion_rate < 90.0) {
                printf("  L%d %-8s 過關率 %.1f%%（200 回合上限內）\n",
                       level, l.name.c_str(), a.completion_rate);
                any_fail = true;
            }
        }
    }
    if (!any_fail) printf("  （無）\n");

    printf("\n═════════════════════════════════════════════════════════════\n");
    printf("這個實驗不能宣稱什麼：\n");
    printf("  - 學習者的能力參數與關卡難度衰減係數都是假設，不是量測值\n");
    printf("  - 模擬的是「每回合找到解的機率」，不是真的在玩拉密\n");
    printf("  - 因此只能回答門檻的時序是否合理，\n");
    printf("    不能回答真人是否會因此學得更好\n");
    return 0;
}
