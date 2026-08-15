#pragma once
#include "tile.h"
#include "coach_campaign.h"
#include <vector>

/* =========================================================================
   TechniqueDetector —— 從一次出牌的前後盤面，反推玩家用了哪些技巧

   為什麼需要這個模組：
     CoachCampaign 需要知道「玩家剛剛用出了哪一招」才能記錄進度，
     但玩家不會自己回報。唯一可靠的資訊來源是盤面的變化——
     出牌前的桌面、出牌後的桌面、以及手上少了哪些牌。

   偵測原則（刻意保守）：
     寧可漏判，不可誤判。
     漏判只是少記一次進度，玩家再用一次就補回來了；
     誤判會讓玩家在「還沒真的學會」的情況下被判定過關，
     那等於系統在騙他。所以每一條規則都要求明確的盤面證據。

   已知限制：
     - 一次出牌可能同時符合多個技巧（例如接龍的同時也用了 Joker），
       此時會全部回報，由 CoachCampaign 各自記錄。
     - 「大風吹重組」與「長龍切斷」都需要桌面既有的牌被移動過，
       但無法百分之百區分「玩家刻意重組」與「剛好排列順序不同」，
       因此採用最嚴格的判準（見各偵測函式的說明）。
   ========================================================================= */

// 一次出牌的前後快照
struct MoveSnapshot {
    std::vector<std::vector<Tile*>> board_before;
    std::vector<std::vector<Tile*>> board_after;
    std::vector<Tile*> hand_before;
    std::vector<Tile*> hand_after;
    bool initial_meld_done_before = true;   // 這一手之前是否已破冰
};

class TechniqueDetector {
public:
    // 主要入口：回傳這一手用到的所有技巧。
    // 沒有偵測到任何技巧時回傳空 vector——這是正常情況，
    // 例如玩家只是抽了一張牌。
    static std::vector<Technique> detect(const MoveSnapshot& snap);

    // ── 個別偵測（公開以便單獨測試）─────────────────────

    // 這一手實際打出去的牌（手牌中消失、且出現在桌面上的）
    static std::vector<Tile*> playedTiles(const MoveSnapshot& snap);

    // 接龍頭尾：某條既有的 Run 變長了，且新增的牌接在原本的頭或尾
    static bool detectAttachRun(const MoveSnapshot& snap);

    // 補第四色：某個原本三張的 Group 變成四張
    static bool detectCompleteGroup(const MoveSnapshot& snap);

    // Joker 補缺口：打出去的牌裡含 Joker，且它落在某條 Run 的中間位置
    static bool detectJokerFill(const MoveSnapshot& snap);

    // 破冰：這一手之前尚未破冰，之後桌面增加了組合
    static bool detectInitialMeld(const MoveSnapshot& snap);

    // 大風吹重組：桌面上原有的牌被重新分配到不同的組合裡
    static bool detectBoardReshuffle(const MoveSnapshot& snap);

    // 長龍切斷：出牌前有一條 ≥6 張的 Run，出牌後它被拆成兩條合法的 Run
    static bool detectRunSplit(const MoveSnapshot& snap);

    // ── 給 Private 招數用的「戰果」判斷 ──────────────────

    // 這一手出了幾張牌
    static int tilesPlayedCount(const MoveSnapshot& snap);

    // 這一手有沒有動到桌面上原有的牌（同樣張數下，動到桌面難度高得多）
    static bool touchedExistingTiles(const MoveSnapshot& snap);

private:
    // 把一堆組合攤平成單一牌集合，用來比對「有沒有牌消失」
    static std::vector<Tile*> flatten(const std::vector<std::vector<Tile*>>& sets);

    // 兩個組合是否由完全相同的牌構成（順序無關）
    static bool sameTiles(const std::vector<Tile*>& a, const std::vector<Tile*>& b);

    // 在 sets 中尋找一個「與 target 的牌完全相同」的組合
    static bool containsSet(const std::vector<std::vector<Tile*>>& sets,
                            const std::vector<Tile*>& target);

    // 某個組合是否為 target 的超集（target 的每張牌都在裡面）
    static bool isSupersetOf(const std::vector<Tile*>& candidate,
                             const std::vector<Tile*>& target);
};
