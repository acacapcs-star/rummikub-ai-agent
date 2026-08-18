#include "coach_modes.h"
#include <cstdio>
#include <string>

/* =========================================================================
   demo_modes.cpp —— 五種模式的音量對照

   同一個關卡設定，套上五種模式，看系統在卡關 0–15 回合時各說什麼。
   ========================================================================= */

// 用 L1（引導 100%）與 L6（引導 40%）當代表
static const LevelSpec L1 = {
    1, 0, "接龍頭尾", 100,
    0, 1, 2, HintTier::REVEAL_MOVE, 3, 1, -1, HintTier::REVEAL_MOVE
};
static const LevelSpec L6 = {
    6, 5, "長龍切斷", 40,
    3, -1, -1, HintTier::GENTLE_NUDGE, 3, 3, 12, HintTier::POINT_TO_AREA
};

// 依 spec 判斷在卡 N 回合時給哪一層（跟引擎的邏輯相同）
static const char* tierAt(const LevelSpec& c, int stuck) {
    if (c.safety_net_after_turns >= 0 && stuck >= c.safety_net_after_turns)
        return "◆";
    if (c.reveal_after_turns >= 0 && stuck >= c.reveal_after_turns &&
        c.max_tier == HintTier::REVEAL_MOVE) return "答";
    if (c.point_after_turns >= 0 && stuck >= c.point_after_turns &&
        c.max_tier != HintTier::GENTLE_NUDGE) return "指";
    if (c.nudge_after_turns >= 0 && stuck >= c.nudge_after_turns) return "輕";
    return "·";
}

static void showRow(const LevelSpec& base, CoachMode m) {
    const ModeProfile& p = CoachModes::get(m);
    LevelSpec s = CoachModes::apply(base, m);
    printf("  %-12s", p.name.c_str());
    for (int stuck = 0; stuck <= 15; ++stuck) printf("%s", tierAt(s, stuck));
    printf("\n");
}

int main() {
    printf("═══════════════════════════════════════════════════════\n");
    printf(" 五種模式 · 同一個引擎，不同的音量\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    for (CoachMode m : CoachModes::all()) {
        const ModeProfile& p = CoachModes::get(m);
        printf("  【%s】\n", p.name.c_str());
        printf("    %s\n", p.description.c_str());
        printf("    門檻 ×%.1f   音量上限 %s   保底 %s   偵測自創招數 %s\n\n",
               p.turn_multiplier,
               p.volume_cap == HintTier::REVEAL_MOVE  ? "講答案"
             : p.volume_cap == HintTier::POINT_TO_AREA ? "指方向" : "輕推",
               p.safety_net ? "開" : "關",
               p.detect_private ? "是" : "否");
    }

    printf("═══════════════════════════════════════════════════════\n");
    printf(" 第 1 關（引導 100%%）在五種模式下\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    printf("  %-12s%s\n", "模式", "卡關 0 → 15 回合");
    printf("  %s\n", std::string(58, '-').c_str());
    for (CoachMode m : CoachModes::all()) showRow(L1, m);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf(" 第 6 關（引導 40%%）在五種模式下\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    printf("  %-12s%s\n", "模式", "卡關 0 → 15 回合");
    printf("  %s\n", std::string(58, '-').c_str());
    for (CoachMode m : CoachModes::all()) showRow(L6, m);

    printf("\n  · 沉默   輕 輕推   指 指方向   答 講答案   ◆ 保底\n");
    printf("  卡關回合  0123456789...15\n\n");

    printf("═══════════════════════════════════════════════════════\n");
    printf(" 兩個軸\n\n");
    printf("  關卡決定「這一招該教多少」——那是內容的深淺。\n");
    printf("  模式決定「你想要多少幫助」——那是使用者的選擇。\n\n");
    printf("  第 1 關在挑戰極限組，比第 6 關在新手練組還安靜。\n");
    return 0;
}
