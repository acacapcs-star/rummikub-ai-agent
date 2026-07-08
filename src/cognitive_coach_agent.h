#pragma once
#include "human_agent.h"
#include "cognitive_hint_engine.h"
#include "coach_hint_bridge.h"
#include "board.h"
#include <string>
#include <iostream>
#include <cstdio>

/* =========================================================================
   CognitiveCoachAgent —— 把 CognitiveHintEngine 真正接進可以玩的回合流程

   繼承自 HumanAgent，重用它既有的檔案輪詢／解析邏輯（waitForActionFile、
   applyActionFile 改成 protected 讓這裡能呼叫），只在「等待玩家動作之前」
   多插入一步：檢查玩家卡關幾回合、印出對應層級的提示。

   卡關判斷邏輯：比對這次呼叫 playTurn 時的桌面，跟上一次呼叫時的桌面
   是否相同（用 Board::toJSON() 序列化後直接比對字串）。
   桌面沒變 = 上一輪沒有人（包括對手）成功推進盤面 = 累加卡關計數；
   桌面變了 = 有人出牌了 = 歸零重算。

   誠實限制：這個「卡關」偵測看的是「整個桌面有沒有變化」，不是精確地
   只針對這個玩家本人。在 1 對 1 對局下兩者等價；4 人局的話語意會需要
   調整（跟 docs/strategies/03 裡提過的限制是同一類問題）。
   ========================================================================= */
class CognitiveCoachAgent : public HumanAgent {
public:
    explicit CognitiveCoachAgent(const std::string& name) : HumanAgent(name) {}

    void playTurn(Board& board, int draw_pile_size) override {
        std::cout << "\n--- " << name << "'s turn (Cognitive Coach Mode) ---\n";
        printHand();

        std::string current_snapshot = board.toJSON();
        if (!last_snapshot_.empty() && current_snapshot == last_snapshot_) {
            stuck_turns_++;
        } else {
            stuck_turns_ = 0;
        }
        last_snapshot_ = current_snapshot;

        HintTier tier = CognitiveHintEngine::tierFromStuckTurns(stuck_turns_);

        // 🎓 破冰前後用不同的提示邏輯：還沒破冰時，該關心的是「湊不湊得到 30 分」，
        // 不是「能不能接桌面的 Run」——這兩件事對玩家來說是完全不同的問題。
        std::string hint = initial_meld_done
            ? CognitiveHintEngine::generateHint(getHand(), board.getSets(), tier)
            : CognitiveHintEngine::generateMeldHint(getHand(), tier);

        std::cout << "\n💡 [教練提示] " << hint << "\n";
        g_current_coach_hint = hint;  // 寫進信箱
        if (g_trigger_state_reexport) g_trigger_state_reexport();  // 立刻重新匯出，讓這輪 state.json 就反映最新提示

        // 重用 HumanAgent 既有的檔案輪詢與提交邏輯，不重寫
        waitForActionFile();
        try {
            applyActionFile(board);
        } catch (const std::exception& e) {
            std::cerr << "[CognitiveCoachAgent] Error: " << e.what() << " – turn skipped.\n";
        }
        std::remove("action.json");
    }

private:
    std::string last_snapshot_;
    int stuck_turns_ = 0;
};
