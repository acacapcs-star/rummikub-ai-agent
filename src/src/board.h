#pragma once
#include <vector>
#include <string>
#include "tile.h"
#include "validator.h"

class Player; // forward declaration

/* -------------------------------------------------------
   Board: the communal table where all played sets live.
  
   Terminology (matches spec.md):
     set   – a played pile of tiles (either a Run or a Group).
     run   – 3+ tiles, same colour, consecutive numbers.
     group – 3 or 4 tiles, same number, distinct colours.
  
   Design note (pointer usage):
     `sets` is a vector of vectors of Tile*.  No Tile objects
     are owned here – ownership stays in GameManager's pool.
     Moving a tile to/from the board is an O(1) pointer op.
  
   PROTECTION NOTE:
     `sets` is private.  Read it through getSets(); the only
     way to mutate the board is via applyProposedSets().
     That function validates everything an agent might try:
       - tiles must come from the player's hand or the board;
       - no duplicate Tile* across the proposed sets;
       - no existing board tile may disappear;
       - every resulting set must be a valid Run or Group;
       - if the player hasn't melded, no rearranging existing
         sets and the meld score from hand must be >= 30.
   ---------------------------------------------------- */
class Board {
public:
    // Outcome of an applyProposedSets() call. Students should
    // compare against ApplyResult::Ok; anything else means the
    // submission was rejected (and the board left unchanged).
    enum class ApplyResult {
        Ok = 0,
        NotPlayerTile,        // a tile in newSets is not from player's hand or the existing board
        DuplicateTile,        // the same Tile* appears more than once in newSets
        RemovedOldTile,       // a tile that was on the board is missing from newSets
        RearrangedDuringMeld, // initial meld attempt rearranged an existing set
        InvalidSet,           // some set in newSets is not a valid Run or Group
        MeldTooLow,           // initial meld score from hand < 30
        NotCurrentPlayer      // unused for now; reserved for future engine checks
    };

    // Human-readable label for an ApplyResult (English short string).
    static const char* describe(ApplyResult r);

    Board() = default;

    // Read the current sets on the board (each inner vector is one Run or Group).
    const std::vector<std::vector<Tile*>>& getSets() const { return sets; }

    // The only way to mutate the board.
    //
    // Returns ApplyResult::Ok on success (board and player.hand are updated).
    // Any other value means the submission was rejected and state is unchanged.
    // The returned reason is also cached and retrievable via lastApplyResult().
    ApplyResult applyProposedSets(Player* player,
                                  const std::vector<std::vector<Tile*>>& newSets);

    // Result of the most recent applyProposedSets() call.
    ApplyResult lastApplyResult() const { return last_apply_result; }

    // Returns true when every set on the board is valid.
    bool isValid() const;

    // Collect every Tile* currently on the board.
    std::vector<Tile*> allTiles() const;

    // Serialise to a JSON array-of-arrays string.
    std::string toJSON() const;

    void print() const;

private:
    std::vector<std::vector<Tile*>> sets;
    ApplyResult last_apply_result = ApplyResult::Ok;
    // Incremented every time applyProposedSets is invoked, regardless of outcome.
    // GameManager reads the delta across a turn to decide if the agent actually
    // tried to submit a play.
    unsigned long apply_call_count = 0;

    friend class GameManager;   // GM samples apply_call_count around playTurn
};
