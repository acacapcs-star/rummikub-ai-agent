#pragma once
#include <vector>
#include "tile.h"

/* -------------------------------------------------------
   Validator: static helpers that determine whether
   a collection of tiles forms a legal Rummikub set.
  
   Legal sets:
     run   – 3+ tiles, same color, consecutive numbers (e.g. R3 R4 R5).
     group – 3 or 4 tiles, same number, distinct colors (e.g. R5 B5 K5).
  
   Jokers act as wildcards and may stand in for any tile.
------------------------------------------------------- */
class Validator {
public:
    // TODO(L1): Returns true if 'tiles' forms a valid Run.
    static bool isValidRun(const std::vector<Tile*>& tiles);

    // TODO(L1): Returns true if 'tiles' forms a valid Group.
    static bool isValidGroup(const std::vector<Tile*>& tiles);

    // Returns true if 'tiles' is either a valid Run or a valid Group.
    static bool isValidSet(const std::vector<Tile*>& tiles);

    // Returns true when every set on the board is valid.
    static bool isValidBoard(const std::vector<std::vector<Tile*>>& sets);

    // Returns true when every old set is preserved exactly in new_sets.
    // Used to enforce "no rearrange" constraints (e.g. during initial meld).
    static bool preservesOldSets(
        const std::vector<std::vector<Tile*>>& old_sets,
        const std::vector<std::vector<Tile*>>& new_sets);

    // Sum of face values (joker = 30 as per standard rules).
    static int calculateScore(const std::vector<Tile*>& tiles);

    // Initial-meld scoring: jokers count as the value they represent in the
    // meld rather than the standard 30-point joker value.
    static int calculateInitialMeldScore(const std::vector<Tile*>& tiles);

    // Returns the value a tile contributes for initial-meld scoring inside
    // the given set.
    static int calculateInitialMeldTileValue(
        const std::vector<Tile*>& tiles,
        Tile* tile);
};
