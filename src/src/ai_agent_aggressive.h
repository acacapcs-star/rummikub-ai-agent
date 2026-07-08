#pragma once
#include "ai_agent_0.h"

/* -------------------------------------------------------
   AIAgent_Aggressive：進攻型個性

   繼承自 AIAgent_0（混合戰術型），核心演算法完全共用、不重寫。
   個性差異只透過覆寫三個參數鉤子來實現：

   - 對手只要連續抽牌少少幾次，就判定卡關、立刻觸發反擊
   - 幾乎不囤牌，冷卻時間壓到最短，抓到機會就想動手
   - 殘局模式提早觸發，更早進入全力進攻、不計代價出牌的狀態
------------------------------------------------------- */
class AIAgent_Aggressive : public AIAgent_0 {
public:
    explicit AIAgent_Aggressive(const std::string& name) : AIAgent_0(name) {}

protected:
    int getEnemyDrawThreshold() const override { return 3; }    // 原本 8 → 3，很快就出手
    int getHoardCooldownTurns() const override { return 1; }    // 原本 4 → 1，幾乎不囤牌
    int getAntiStalemateThreshold() const override { return 25; } // 原本 20 → 25，更早進入全力模式
};
