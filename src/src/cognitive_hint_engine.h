#pragma once
#include "tile.h"
#include <vector>
#include <string>

/* =========================================================================
   CognitiveHintEngine —— Section 3「認知教練型 AI」的核心元件

   跟 AIAgent_0／AIAgent_Conservative／AIAgent_Aggressive 完全不同的目標：
   那三個都是在「幫自己找出最好的一手，打贏對手」。
   這個模組要做的是「幫『對面的人類玩家』找出一個可行的動作，並且用由淺
   入深的方式提示他」——目標從「贏」變成「陪、引導」。

   技術上刻意沿用 AIAgent_0::findRuns() 的「接龍頭尾」掃描邏輯，因為那本
   來就是在回答同一個問題：「手牌裡有沒有牌可以接到桌面上的某個 Run」。
   差別只在於：原本找到就直接出牌，這裡找到後改成組成一段提示文字。
   ========================================================================= */

enum class HintTier {
    GENTLE_NUDGE,   // 第一層：只提醒「有機會出牌」，不指方向
    POINT_TO_AREA,  // 第二層：指出桌面上哪個顏色/區域
    REVEAL_MOVE     // 第三層：直接講出具體要出哪張牌、接在哪裡
};

class CognitiveHintEngine {
public:
    // 分析目前手牌＋桌面，回傳指定層級的提示文字。
    // 如果掃描不到任何可行動作，會誠實回傳「目前看起來沒有能出的牌，可以考慮抽一張」，
    // 不會硬掰一個不存在的提示。
    static std::string generateHint(
        const std::vector<Tile*>& hand,
        const std::vector<std::vector<Tile*>>& board_sets,
        HintTier tier
    );

    // 根據「已經連續卡關幾回合」，決定要給哪一層提示。
    // 門檻是刻意選得比較保守的示意值，不是臨床驗證過的數字，
    // 之後若有真實長輩測試數據，這裡的門檻應該要重新校準。
    static HintTier tierFromStuckTurns(int stuck_turns);

    // 破冰前（initial_meld_done == false）專用的提示：分析手牌裡最高分的
    // 候選組合（Run 或 Group），告訴玩家目前湊到幾分、離 30 分還差多少。
    // 這個版本納入了 Joker：Run 的內部缺口可以用 Joker 動態填補，
    // Group 只有 2 種顏色時也可以用 Joker 補成合法的 3 張。
    static std::string generateMeldHint(
        const std::vector<Tile*>& hand,
        HintTier tier
    );

private:
    struct FoundMove {
        bool found = false;
        Tile* tile = nullptr;
        Color color = Color::RED;
        int target_number = 0;
        bool attach_to_head = true;   // true = 接在龍頭前面，false = 接在龍尾後面
        int run_length = 0;           // 該 Run 目前長度，用來組「桌面上那排 N 張的線」這種描述
    };

    // 掃描邏輯跟 AIAgent_0::findRuns() 裡「找頭尾能接的牌」是同一個概念，
    // 差別是這裡只回傳「找到的第一個可行動作」用來組提示文字，不是用來真的出牌。
    static FoundMove findAttachableMove(
        const std::vector<Tile*>& hand,
        const std::vector<std::vector<Tile*>>& board_sets
    );

    struct FoundGroupMove {
        bool found = false;
        Tile* tile = nullptr;
        int number = 0;
        Color color = Color::RED;   // 手牌裡那張、能補上去的顏色
    };

    // 掃描桌面上「已經是 3 張的 Group」，找出手牌裡能補上第 4 種顏色的牌。
    // 只處理「桌面上剛好 3 張」這種情境——4 張的 Group 已經補滿，不需要提示。
    static FoundGroupMove findGroupCompletionMove(
        const std::vector<Tile*>& hand,
        const std::vector<std::vector<Tile*>>& board_sets
    );

    static std::string colorName(Color c);

    struct MeldCandidate {
        bool found = false;
        std::vector<Tile*> tiles;   // 只包含「真實牌」，不含 Joker 本身
        int score = 0;
        bool is_run = false;  // true = Run（同色連續）, false = Group（同數字不同色）
        int joker_count = 0;  // 這個候選組合需要用到幾張 Joker 才能湊成
    };

    // 掃描手牌找出分數最高的單一候選組合（Run 或 Group），這次納入 Joker：
    // Run 缺口可以用 Joker 動態填補；Group 只有 2 種顏色時，若有 Joker 可以補成 3 張。
    // 誠實限制：Joker 只用來填「內部缺口」，不會用來往兩端延伸——這是為了控制
    // 掃描複雜度刻意做的簡化，真正的大風吹演算法（AIAgent_0::tryExtendBoard）
    // 兩端延伸都有處理，但那是「幫自己出牌」，這裡是「給提示」，兩者的正確性
    // 要求不同，用簡化版本換取程式碼容易驗證是合理的取捨。
    static MeldCandidate findBestMeldCandidate(const std::vector<Tile*>& hand);
};
