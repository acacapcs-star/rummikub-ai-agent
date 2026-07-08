#pragma once
#include "player.h"
#include <vector>

/* -------------------------------------------------------
   AIAgent_0: computer-controlled player.
  
   Lecture 2 task  – inherit Player, implement basic playTurn
     that plays sets from hand without touching the board.
   Lecture 3 task  – extend playTurn to rearrange board tiles
     using std::find_if and other STL algorithms.
------------------------------------------------------- */
class AIAgent_0 : public Player {
public:
    explicit AIAgent_0(const std::string& name);
    virtual ~AIAgent_0() = default;  // 之後會用基底類別指標操作不同個性子類別，需要 virtual 解構
    void playTurn(Board& board, int draw_pile_size) override;

protected:
    /* ---------------------------------------------------
       🎭 個性化參數鉤子（給子類別覆寫用）
       AIAgent_0 本身是「混合戰術型」，這三個數字是它的預設值。
       子類別（例如保守型、進攻型）只要覆寫這幾個函式回傳不同
       數字，playTurn() 內部的決策邏輯完全不用重寫。
    --------------------------------------------------- */
    virtual int getEnemyDrawThreshold() const { return 8; }   // 對手連續抽牌幾次才觸發反擊
    virtual int getHoardCooldownTurns() const { return 4; }    // 囤牌冷卻幾回合
    virtual int getAntiStalemateThreshold() const { return 20; } // 牌堆剩幾張時放棄囤牌、全力進攻

private:
    std::vector<std::vector<Tile*>> findRuns(
        const std::vector<Tile*>& tiles) const;
    std::vector<std::vector<Tile*>> findGroups(
        const std::vector<Tile*>& tiles) const;
    /* ---------------------------------------------------
       TODO(L2) Helper: attempt initial meld (>= 30 pts from hand).
       Returns true and modifies board+hand if successful.
    --------------------------------------------------- */
    bool tryInitialMeld(Board& board);
    /* ---------------------------------------------------
       Sort a set in run order: non-jokers ascending, jokers
       placed in their consecutive gap positions, remaining
       jokers appended at the right end (or prepended if the
       right end would exceed 13).
    --------------------------------------------------- */
    static void sortRunSet(std::vector<Tile*>& tiles);
    /* ---------------------------------------------------
       TODO(L3) Helper: play tiles onto existing board sets
       or add new sets.  Returns number of tiles played.
    --------------------------------------------------- */
    int tryExtendBoard(Board& board);

    // ===================================================
    // 🚀 妳的大風吹遞迴演算法核心（補在這裡！）
    // ===================================================
    void solveRummikub(
        std::vector<std::vector<Tile*>> current_table, 
        std::vector<Tile*> remaining_hand, 
        int joker_count
    );

    // 🏆 排行榜記錄器（遞迴用來找最優解的老本）
    std::vector<std::vector<Tile*>> best_solution;
    int max_tiles_played;
};