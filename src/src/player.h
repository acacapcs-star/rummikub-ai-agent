#pragma once
#include <string>
#include <vector>
#include "tile.h"
#include "board.h"

/* -------------------------------------------------------
   Player: abstract base class for all participants.
  
   Key teaching points:
     - Pure virtual playTurn() forces derived classes to
       provide their own strategy.
     - hand stores Tile* pointers; the Tile objects themselves
       live in GameManager's pool.
  
   PROTECTION NOTE:
     hand is private.  Read it through getHand() (returns a
     const reference).  Only the engine (Board / GameManager)
     can mutate it via the friend declarations below.
     Students must move tiles from hand to the board through
     Board::applyProposedSets().
------------------------------------------------------- */
class Player {
public:
    std::string name;
    bool        initial_meld_done;  // has played >= 30 pts from hand

    explicit Player(const std::string& name);
    virtual ~Player() = default;

    // Read-only view of the hand.
    const std::vector<Tile*>& getHand() const { return hand; }

    /* -------------------------------------------------------
       playTurn: the key virtual function.
        board           – the communal table (read via getSets;
                          mutate only through applyProposedSets).
        draw_pile_size  – how many tiles remain in the draw pile.
                           Useful as strategic information; you
                           cannot peek at or modify the pile.
      
       To do nothing this turn (skip / draw), simply return.
       GameManager detects no change and draws a tile for you.
    --------------------------------------------------------*/
    virtual void playTurn(Board& board, int draw_pile_size) = 0;

    // Serialise hand to JSON array.
    std::string handToJSON() const;

    // Sum of tile face values in hand (joker = 30).
    int handScore() const;

    void printHand() const;

private:
    std::vector<Tile*> hand;

    // Only the engine mutates hand.
    friend class Board;        // strips played tiles in applyProposedSets
    friend class GameManager;  // deals and draws tiles
};
