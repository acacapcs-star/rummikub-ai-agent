#include "spaced_cycle.h"
#include <cstdio>
#include <random>

/* 模擬一個使用者走過三輪循環，看報告怎麼變 */

static const char* techName(int t) {
    static const char* N[] = { "接龍頭尾", "補第四色", "Joker補缺口",
                               "破冰湊30分", "大風吹重組", "長龍切斷" };
    return (t >= 0 && t < 6) ? N[t] : "?";
}

static void printReport(const CycleReport& r) {
    printf("  ┌─ 第 %d 輪報告 ─────────────────────────────────\n", r.cycle_number);
    printf("  │ 練習次數   %d\n", r.total_attempts);
    printf("  │ 整體自主率 %.0f%%", r.overall_autonomy * 100);
    if (r.hasPrevious()) {
        double d = r.improvement() * 100;
        printf("   （上一輪 %.0f%%，%s%.0f 個百分點）",
               r.previous_autonomy * 100, d >= 0 ? "+" : "", d);
    }
    printf("\n");
    printf("  │ 平均回合   %.1f\n", r.avg_turns);
    printf("  │ 穩定度     %.2f %s\n", r.stability,
           r.stability < 0.15 ? "（各技巧表現接近）"
                              : "（技巧之間落差大）");
    printf("  │\n");

    printf("  │ 學會的     ");
    if (r.learned.empty()) printf("（這一輪沒有升級）");
    else for (int t : r.learned) printf("%s ", techName(t));
    printf("\n");

    printf("  │ 強項       ");
    if (r.strengths.empty()) printf("（尚無）");
    else for (int t : r.strengths) printf("%s ", techName(t));
    printf("\n");

    printf("  │ 待補強     ");
    if (r.needs_work.empty()) printf("（沒有明顯弱項）");
    else for (int t : r.needs_work) printf("%s ", techName(t));
    printf("\n");
    printf("  │\n");

    printf("  │ 下一輪難題比例 %.0f%%\n", r.next_difficulty_ratio * 100);
    printf("  │   %s\n", r.difficulty_note.c_str());
    printf("  │ 下一輪的出現權重\n");
    for (const auto& [t, w] : r.next_weights)
        printf("  │   %-12s ×%.1f %s\n", techName(t), w,
               w >= 2.0 ? "← 加強" : (w <= 0.6 ? "← 已熟，降低但不歸零" : ""));
    printf("  └───────────────────────────────────────────────\n\n");
}

int main() {
    printf("══════════════════════════════════════════════════════\n");
    printf(" 間隔重複 · 費氏循環\n");
    printf("══════════════════════════════════════════════════════\n\n");

    printf("  複習間隔按費氏數列拉長，走到頂之後循環回起點：\n\n");
    for (int cap : { 8, 13, 21 }) {
        printf("    頂點 %2d 天  ", cap);
        for (int step = 0; step < 14; ++step) {
            printf("%d", FibonacciSchedule::intervalAt(step, cap));
            if (step < 13) printf(",");
        }
        printf("...\n");
    }
    printf("\n    長者版用 8（循環密集）、兒童版用 21（循環寬鬆）\n");
    printf("    因為長者要的不是「記得住」，是持續使用那個認知功能。\n\n");

    printf("══════════════════════════════════════════════════════\n");
    printf(" 三輪循環的報告\n");
    printf("══════════════════════════════════════════════════════\n\n");

    SpacedCycle cycle(AudienceProfiles::get(Audience::SENIORS), 4);
    std::mt19937 rng(20260818);
    std::uniform_real_distribution<double> u(0, 1);

    // 模擬一個使用者：接龍很強、Joker 很弱，隨著輪次整體進步
    double skill[4] = { 0.85, 0.55, 0.25, 0.45 };
    int mastery[4] = { 0, 0, 0, 0 };

    for (int round = 1; round <= 3; ++round) {
        for (int rep = 0; rep < 20; ++rep)
            for (int t = 0; t < 4; ++t) {
                bool ok = u(rng) < skill[t];
                int turns = ok ? (1 + (int)(u(rng) * 2)) : (3 + (int)(u(rng) * 3));
                int before = mastery[t];
                if (ok && mastery[t] < 3) ++mastery[t];
                cycle.record(t, ok, turns, before, mastery[t]);
            }
        printReport(cycle.finishCycle());

        // 每一輪之後能力小幅提升（弱的進步比較多）
        for (int t = 0; t < 4; ++t)
            skill[t] = std::min(0.95, skill[t] + (0.9 - skill[t]) * 0.25);
    }

    printf("══════════════════════════════════════════════════════\n");
    printf(" 難度自動調節\n");
    printf("══════════════════════════════════════════════════════\n\n");
    printf("  通過率高「而且」速度快 → 提高難題比例\n");
    printf("  兩個條件都要滿足——只有通過率高可能是題目太簡單，\n");
    printf("  只有速度快可能是在亂猜。\n\n");

    printf("══════════════════════════════════════════════════════\n");
    printf(" 一個實作上要注意的事\n\n");
    printf("  這個示範每輪每技巧練 20 次。次數少於 10 次時，\n");
    printf("  單輪的自主率會有很大的隨機波動——\n");
    printf("  能力沒變也可能看起來掉了十幾個百分點。\n\n");
    printf("  所以實際使用時，**練習次數太少的那一輪不該產生強烈的調整建議**。\n");
    printf("  這跟先前那個 Actor-Critic 實驗的教訓一樣：\n");
    printf("  單次結果的變異可能大過你想量測的效果。\n");
    return 0;
}
