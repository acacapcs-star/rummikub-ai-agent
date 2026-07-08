#pragma once
#include "player.h"
#include <vector>

/* -------------------------------------------------------
   AIAgent_baseline0: the "do nothing" baseline agent.
   Every turn it returns immediately and GameManager draws
   a tile on its behalf.  Useful as a starting opponent and
   a reference for the minimum playTurn implementation.
------------------------------------------------------- */
class AIAgent_baseline0 : public Player {
public:
    explicit AIAgent_baseline0(const std::string& name);

    void playTurn(Board& board, int draw_pile_size) override;

private:
    std::vector<std::vector<Tile*>> findRuns(
        const std::vector<Tile*>& tiles) const;

    std::vector<std::vector<Tile*>> findGroups(
        const std::vector<Tile*>& tiles) const;

    bool tryInitialMeld(Board& board);
    int tryExtendBoard(Board& board);
};
