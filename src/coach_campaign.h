#pragma once
#include "tile.h"
#include "cognitive_hint_engine.h"
#include <string>
#include <vector>

/* =========================================================================
   CoachCampaign —— 認知教練型 AI 的教學關卡系統

   跟 CognitiveHintEngine 的分工：
     CognitiveHintEngine  回答「這一手該怎麼下」——單次、無狀態。
     CoachCampaign        回答「這個玩家現在該學什麼、還需要多少幫助」
                          ——跨回合、跨關卡，有狀態。

   六個關卡，每關教一個技巧，引導強度從 100% 遞減到 40%：

     Level 1  接龍頭尾        100%   提示立刻給，可以講到答案
     Level 2  補第四色         88%
     Level 3  Joker 補缺口     76%
     Level 4  破冰湊 30 分     64%   最多只給到「指方向」
     Level 5  大風吹重組       52%
     Level 6  長龍切斷         40%   只剩「輕推」，答案要自己找

   「引導強度」不是一個抽象的百分比，它落實成兩件可執行的事：
     1. 要卡關幾回合，系統才開口（越後面越晚）
     2. 最多只給到哪一層提示（越後面越不給答案）

   過關條件刻意設成「用出 3 次，其中至少 1 次沒看過 REVEAL_MOVE」：
   前者確保熟練，後者確保是真的懂了而不是照抄。
   ========================================================================= */

// ── 官方技巧清單 ─────────────────────────────────────────
enum class Technique {
    ATTACH_RUN,        // Level 1：把手牌接到桌面 Run 的頭或尾
    COMPLETE_GROUP,    // Level 2：桌面 3 張的 Group 補上第四色
    JOKER_FILL,        // Level 3：用 Joker 填 Run 的內部缺口
    INITIAL_MELD,      // Level 4：破冰湊滿 30 分
    BOARD_RESHUFFLE,   // Level 5：拆桌面重拼（大風吹）
    RUN_SPLIT,         // Level 6：長 Run 切斷製造接點
    TECHNIQUE_COUNT
};

// ── 一項技巧的掌握程度 ───────────────────────────────────
// 同一招可以重複升級：第一次靠答案做出來拿一星，
// 之後某天沒提示又用了一次，就升到三星。那個升級的瞬間才是真的學會。
enum class Mastery {
    LOCKED,        // 還沒用過
    COPIED,        // ⭐   看了 REVEAL_MOVE 才做出來
    PROMPTED,      // ⭐⭐  只給到 POINT_TO_AREA 就自己找到
    DISCOVERED     // ⭐⭐⭐ 完全沒提示，自己用出來
};

// ── 單一關卡的設定 ───────────────────────────────────────
struct LevelConfig {
    int level;                  // 1–6
    Technique technique;        // 這一關要教的招數
    std::string name;           // 顯示用名稱
    std::string description;    // 教學文字

    int guidance_percent;       // 引導強度，純粹用來顯示給玩家看

    // 引導強度落實成的兩件事：
    int nudge_after_turns;      // 卡關幾回合後給 GENTLE_NUDGE
    int point_after_turns;      // 卡關幾回合後給 POINT_TO_AREA
    int reveal_after_turns;     // 卡關幾回合後給 REVEAL_MOVE（-1 = 這關永不給）
    HintTier max_tier;          // 這一關最多只給到哪一層

    int required_uses;          // 過關需要用出幾次
    int required_unassisted;    // 其中至少幾次不能是看了 REVEAL 才做的

    // 保底機制：卡關超過這個回合數時，破例往下多給一層。
    // 存在的理由是模擬實驗跑出來的——L6 的完全新手平均連續卡 16 回合，
    // 因為那一關上限只到「輕推」，而輕推對能力不足者幾乎沒有幫助。
    //
    // 保底不是「你太笨了給你答案」，而是「你已經試了這麼久，
    // 這裡確實有東西，讓我把範圍縮小一點」——系統仍然不講答案，
    // 只是把音量調高一格。-1 表示這一關不啟用保底。
    int safety_net_after_turns;
    HintTier safety_net_tier;   // 保底時最多給到哪一層
};

// ── 玩家在某一項技巧上的進度 ─────────────────────────────
struct TechniqueProgress {
    Technique technique;
    Mastery mastery = Mastery::LOCKED;
    int total_uses = 0;          // 總共用出幾次
    int unassisted_uses = 0;     // 其中沒看 REVEAL_MOVE 的次數
};

// ── 一次出牌被偵測到用了哪一招，以及當時看過的最高提示層 ──
struct TechniqueUse {
    Technique technique;
    bool saw_reveal = false;     // 這一手之前是否已經看過 REVEAL_MOVE
    bool saw_point = false;      // 是否看過 POINT_TO_AREA
};

// ── Recap MCQ ────────────────────────────────────────────
struct McqOption {
    std::string text;
    bool correct = false;
};

struct McqQuestion {
    std::string prompt;
    std::vector<McqOption> options;
    std::string hint;        // 答錯第一次給的提示——不直接講答案
    std::string explanation; // 答錯第二次才給的完整解釋
};

class CoachCampaign {
public:
    CoachCampaign();

    // ── 關卡設定 ──────────────────────────────────────────
    static const LevelConfig& levelConfig(int level);   // level 1–6
    static int totalLevels();

    // ── 目前狀態 ──────────────────────────────────────────
    int currentLevel() const { return current_level_; }
    const LevelConfig& currentConfig() const { return levelConfig(current_level_); }

    // ── 提示層級決策 ──────────────────────────────────────
    // 依「目前關卡的引導強度」與「已經卡關幾回合」決定要給哪一層提示。
    // 回傳 false 表示這個時候還不該開口——沉默也是一種設計，
    // 在後段關卡尤其重要：太早給提示，玩家就沒有自行發掘的空間。
    bool shouldGiveHint(int stuck_turns, HintTier& out_tier) const;

    // 這一次的提示是不是由保底機制觸發的。
    // 分開回報是為了讓上層能記錄「這個玩家用到了幾次保底」——
    // 那是一個值得看的指標：保底用得太頻繁，代表關卡難度配置有問題。
    bool shouldGiveHint(int stuck_turns, HintTier& out_tier,
                        bool& out_from_safety_net) const;

    // ── 記錄一次技巧使用 ──────────────────────────────────
    void recordUse(const TechniqueUse& use);

    // ── 進度查詢 ──────────────────────────────────────────
    const TechniqueProgress& progressOf(Technique t) const;
    bool canAdvance() const;      // 目前關卡的過關條件是否已達成
    bool advance();               // 進入下一關；已是最後一關則回傳 false

    // ── Recap ─────────────────────────────────────────────
    // 每關結束的 5 題 MCQ。題型固定為：
    //   1. 辨識——給桌面，問哪張手牌接得上
    //   2. 判斷合法——這個組合成不成立
    //   3. 選最佳——兩個都能出，哪個對後續有利
    //   4. 理解機制——為什麼規則是這樣設計的
    //   5. 本關招數——換個情境問同一個技巧
    // 第 5 題是關鍵：它測的是「換場景還會不會用」，不是「記不記得剛才那步」。
    static std::vector<McqQuestion> recapFor(int level);

    // 答錯處理：第一次給提示、第二次才給解答——
    // 跟遊戲內的引導邏輯一致，不讓玩家用猜的過關。
    enum class AnswerResult { CORRECT, WRONG_FIRST_TRY, WRONG_SHOW_ANSWER };
    static AnswerResult judge(const McqQuestion& q, int chosen_index, int attempt);

    // ── 顯示用 ────────────────────────────────────────────
    static std::string techniqueName(Technique t);
    static std::string masteryStars(Mastery m);

private:
    int current_level_ = 1;
    std::vector<TechniqueProgress> progress_;

    static Mastery masteryFromUse(const TechniqueUse& use);
    static bool isUpgrade(Mastery current, Mastery candidate);
};
