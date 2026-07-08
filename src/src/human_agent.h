#pragma once
#include "player.h"
#include <string>

/* -------------------------------------------------------
   HumanAgent: reads moves from action.json written by the web Visualizer.
  
   Communication flow:
     1. GameManager exports state.json (current board/hand).
     2. Browser loads state.json and shows drag-and-drop UI.
     3. Human arranges tiles and clicks Submit (or Draw).
     4. Browser POSTs action.json via server.py.
     5. HumanAgent polls for action.json, parses it, and either
        commits the proposed board through Board::applyProposedSets,
        or (for a "draw" action) returns immediately so that
        GameManager draws on the player's behalf.
  
   TODO(L2): file polling & parsing
------------------------------------------------------- */
class HumanAgent : public Player {
public:
    explicit HumanAgent(const std::string& name);

    void playTurn(Board& board, int draw_pile_size) override;

protected:
    // TODO(L2): Poll the filesystem until action.json appears.
    void waitForActionFile() const;

    // TODO(L2): Parse the contents of action.json and submit the move.
    // "draw" is treated as a no-op so GameManager handles drawing uniformly across all agents.
    void applyActionFile(Board& board);

private:

    // Simple JSON helper: parse an integer field  "key": <int>
    static int parseIntField(const std::string& json,
                             const std::string& key);

    // Sort a set in run order: non-jokers ascending, jokers placed in their
    // consecutive gap positions, remaining jokers appended at the right end
    // (or prepended if the right end would exceed 13).
    static void sortRunSet(std::vector<Tile*>& tiles);
};
