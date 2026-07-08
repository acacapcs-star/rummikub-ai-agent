#pragma once
#include "ai_agent_0.h"

/* -------------------------------------------------------
   AIAgent_Conservative：保守型個性

   繼承自 AIAgent_0（混合戰術型），核心演算法（一條龍掃描、
   大風吹全域重組、安全回滾鎖）完全共用、不重寫。
   個性差異只透過覆寫三個參數鉤子來實現：

   - 對手要連續抽牌更多次，才判定「真的卡關」，才觸發反擊
   - 囤牌冷卻時間拉長，更捨不得把手上的戰術彈性用掉
   - 殘局模式觸發得比較晚，代表比較不輕易恐慌全力進攻
------------------------------------------------------- */
class AIAgent_Conservative : public AIAgent_0 {
public:
    explicit AIAgent_Conservative(const std::string& name) : AIAgent_0(name) {}

protected:
    int getEnemyDrawThreshold() const override { return 15; }   // 原本 8 → 15，更晚出手
    int getHoardCooldownTurns() const override { return 8; }    // 原本 4 → 8，冷卻更久
    int getAntiStalemateThreshold() const override { return 15; } // 原本 20 → 15，更晚進入全力模式
};
