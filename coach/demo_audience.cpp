#include "audience_profile.h"
#include <cstdio>
#include <string>

/* =========================================================================
   demo_audience.cpp —— 兩個族群並排比較
   ========================================================================= */

static const char* tierName(HintTier t) {
    return t == HintTier::GENTLE_NUDGE  ? "輕推"
         : t == HintTier::POINT_TO_AREA ? "指方向" : "講答案";
}

// 模擬一段遊玩：給定「每一回合有沒有成功」的序列，看提示怎麼變
static void simulate(Audience a, const char* pattern) {
    const AudienceProfile& p = AudienceProfiles::get(a);
    AdaptiveDifficulty adapt(p);

    printf("  《%s》 %s\n", p.name.c_str(),
           p.adaptive_difficulty ? "（動態難度）" : "（固定難度，由關卡決定）");
    printf("    回合  ");
    for (const char* c = pattern; *c; ++c) printf("%2d ", (int)(c - pattern) + 1);
    printf("\n    結果  ");
    for (const char* c = pattern; *c; ++c) printf(" %c ", *c == 'x' ? 'X' : 'O');
    printf("\n    提示  ");

    for (const char* c = pattern; *c; ++c) {
        if (*c == 'x') adapt.onStuck();
        else           adapt.onSuccess();

        if (adapt.needsGuarantee()) printf(" ! ");
        else printf("%s ", adapt.tier() == HintTier::GENTLE_NUDGE  ? "輕"
                         : adapt.tier() == HintTier::POINT_TO_AREA ? "指" : "答");
    }
    printf("\n\n");
}

int main() {
    printf("══════════════════════════════════════════════════════════\n");
    printf(" 兩個族群 · 同一套引擎\n");
    printf("══════════════════════════════════════════════════════════\n\n");

    for (Audience a : { Audience::KIDS, Audience::SENIORS }) {
        const AudienceProfile& p = AudienceProfiles::get(a);
        printf("【%s】\n", p.name.c_str());
        printf("  對象      %s\n", p.target_group.c_str());
        printf("  目標      %s\n", p.goal.c_str());
        printf("  關卡      %d 關\n", p.level_count);
        printf("  動態難度  %s\n", p.adaptive_difficulty ? "開啟" : "關閉");
        if (p.adaptive_difficulty)
            printf("            連續卡 %d 回合加強、連續成功 %d 次放鬆\n",
                   p.raise_after_stuck, p.lower_after_success);
        printf("  成功保證  %s\n",
               p.guarantee_success_after > 0
                 ? ("連續 " + std::to_string(p.guarantee_success_after) +
                    " 回合沒成功就直接給答案").c_str()
                 : "不啟用");
        printf("  單次時長  %d 分鐘%s\n", p.session_minutes,
               p.fatigue_reminder ? "，超過會提醒休息" : "");
        printf("  允許犯錯  %s\n", p.allow_errors ? "是——錯誤是學習的一部分"
                                                  : "盡量避免——呼應無錯學習");
        printf("  Recap     %s\n", p.recap_gates_progress
                 ? "要通過才能進下一關"
                 : "只是回顧，不擋進度（測驗會製造壓力）");
        printf("  措辭      「%s」\n\n", p.nudge_phrase.c_str());
    }

    printf("══════════════════════════════════════════════════════════\n");
    printf(" 動態難度：同樣的表現，兩個版本的反應不同\n");
    printf("══════════════════════════════════════════════════════════\n\n");

    printf("  情境一：連續卡關\n");
    simulate(Audience::KIDS,    "xxxxxxxx");
    simulate(Audience::SENIORS, "xxxxxxxx");

    printf("  情境二：卡兩次之後開始順手\n");
    simulate(Audience::KIDS,    "xxoooooo");
    simulate(Audience::SENIORS, "xxoooooo");

    printf("  情境三：時好時壞\n");
    simulate(Audience::KIDS,    "xoxxoxxo");
    simulate(Audience::SENIORS, "xoxxoxxo");

    printf("  （O 成功　X 卡關　輕/指/答 提示層級　! 成功保證觸發）\n\n");

    printf("══════════════════════════════════════════════════════════\n");
    printf(" 每一招訓練什麼認知功能\n");
    printf("══════════════════════════════════════════════════════════\n\n");
    for (const auto& c : CognitiveMap::all()) {
        const char* who = c.technique < 4 ? "兩者皆用" : "僅兒童版";
        printf("  L%d %-14s %-22s %s\n", c.technique + 1,
               c.name.c_str(), c.cognitive_domain.c_str(), who);
        printf("       %s\n", c.note.c_str());
    }

    printf("\n══════════════════════════════════════════════════════════\n");
    printf(" 疲勞追蹤（長者版）\n");
    printf("══════════════════════════════════════════════════════════\n\n");
    FatigueTracker f(AudienceProfiles::get(Audience::SENIORS));
    for (int m : { 10, 10, 10, 5 }) {
        f.addMinutes(m);
        printf("  已玩 %2d 分鐘  →  %s\n", f.elapsedMinutes(),
               f.shouldSuggestBreak() ? f.breakMessage().c_str() : "繼續");
    }

    printf("\n══════════════════════════════════════════════════════════\n");
    printf(" 不適用範圍\n\n");
    printf("  本系統不適用於中重度失智。\n");
    printf("  該族群的主流做法是無錯學習（errorless learning）——\n");
    printf("  盡量避免犯錯，因為他們有「學會錯誤」而非「從錯誤中學」的風險。\n");
    printf("  而本系統的核心是刻意留出犯錯與自主提取的空間。\n");
    printf("  **這是方法論的界線，不是難度的問題。**\n");
    return 0;
}
