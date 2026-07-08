#pragma once
#include "player.h"
#include <vector>

/* -------------------------------------------------------
   AIAgent_baseline1: Just add new complete sets from hand
------------------------------------------------------- */
class AIAgent_baseline1 : public Player {
public:
    explicit AIAgent_baseline1(const std::string& name);

    void playTurn(Board& board, int draw_pile_size) override;

private:
    std::vector<std::vector<Tile*>> findRuns(
        const std::vector<Tile*>& tiles) const;

    std::vector<std::vector<Tile*>> findGroups(
        const std::vector<Tile*>& tiles) const;

    bool tryInitialMeld(Board& board);
    int tryExtendBoard(Board& board);
};
