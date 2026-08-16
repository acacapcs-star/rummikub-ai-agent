/* -------------------------------------------------------
   coach_demo —— 把「同一個卡關情境，不同關卡講的話不一樣」印出來

   這支程式呼叫的是 CognitiveCoachAgent 內部完全相同的那兩行：
       campaign.shouldGiveHint(stuck_turns, tier)
       CognitiveHintEngine::generateHint(hand, board, tier)
   差別只在它不等 action.json，而是直接把每一格的結果印出來。

   用途有兩個：
     1. 驗證接線真的生效了——如果 shouldGiveHint 沒接上，
        六關會印出一模一樣的東西。
     2. 產生海報／簡報要用的對照表。

       g++ -std=c++17 -I src src/tile.cpp src/validator.cpp
           src/cognitive_hint_engine.cpp src/coach_campaign.cpp
           tools/coach_demo.cpp -o coach_demo
       ./coach_demo
------------------------------------------------------- */

#include <iostream>
#include <string>
#include <vector>

#include "tile.h"
#include "validator.h"
#include "cognitive_hint_engine.h"
#include "coach_campaign.h"

static std::vector<Tile*> g_pool;
static int g_next_id = 0;

static Tile* T(int number, Color color) {
    Tile* t = new Tile(g_next_id++, number, color);
    g_pool.push_back(t);
    return t;
}

static CoachCampaign atLevel(int level) {
    CoachCampaign c;
    while (c.currentLevel() < level && c.advance()) {}
    return c;
}

static const char* tierMark(HintTier t) {
    switch (t) {
        case HintTier::GENTLE_NUDGE:  return "輕";
        case HintTier::POINT_TO_AREA: return "指";
        case HintTier::REVEAL_MOVE:   return "答";
    }
    return "?";
}

int main() {
    // 一個固定的卡關情境：桌面有紅 4-5-6，玩家手上有紅 3（接得上）
    // 外加兩張接不上的牌。這一手永遠有解——所以每一次沉默都是
    // 「找得到但不說」，不是「找不到」。
    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* r3 = T(3, Color::RED);
    Tile* b9 = T(9, Color::BLUE);
    Tile* y2 = T(2, Color::YELLOW);

    std::vector<std::vector<Tile*>> board = {{r4, r5, r6}};
    std::vector<Tile*> hand = {r3, b9, y2};

    std::cout << "情境：桌面 [紅4 紅5 紅6]，手牌 [紅3 藍9 黃2]\n";
    std::cout << "紅3 接得上紅4 的前面——這一手從頭到尾都有解。\n";
    std::cout << "下面每一格是「卡關第 N 回合時，教練說了什麼」。\n";
    std::cout << "（· 代表不開口）\n\n";

    std::cout << "        卡關回合 → 0  1  2  3  4  5  6\n";
    std::cout << "        ─────────────────────────────\n";

    for (int lv = 1; lv <= CoachCampaign::totalLevels(); ++lv) {
        CoachCampaign c = atLevel(lv);
        const LevelConfig& cfg = CoachCampaign::levelConfig(lv);

        std::cout << "  L" << lv << " 引導" << cfg.guidance_percent << "%";
        if (cfg.guidance_percent < 100) std::cout << " ";
        std::cout << "  ";

        for (int stuck = 0; stuck <= 6; ++stuck) {
            HintTier tier;
            if (c.shouldGiveHint(stuck, tier)) std::cout << " " << tierMark(tier) << " ";
            else                                std::cout << " ·  ";
        }
        std::cout << "  " << cfg.name << "\n";
    }

    // 逐字對照：第一關與最後一關，卡到第 6 回合各說了什麼
    std::cout << "\n卡關第 6 回合，兩端的差別：\n\n";

    for (int lv : {1, 6}) {
        CoachCampaign c = atLevel(lv);
        HintTier tier;
        std::cout << "  [第 " << lv << " 關] ";
        if (c.shouldGiveHint(6, tier)) {
            std::cout << CognitiveHintEngine::generateHint(hand, board, tier) << "\n";
        } else {
            std::cout << "（不開口）\n";
        }
    }

    // 證明沉默不是因為找不到：直接問最深的那一層
    std::cout << "\n引擎其實知道答案——直接問 REVEAL_MOVE 層：\n  "
              << CognitiveHintEngine::generateHint(hand, board, HintTier::REVEAL_MOVE)
              << "\n";
    std::cout << "\n第 6 關的沉默是設計，不是能力上限。\n";

    for (Tile* t : g_pool) delete t;
    return 0;
}
