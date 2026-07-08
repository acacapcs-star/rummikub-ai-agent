#pragma once
#include <string>

/* -------------------------------------------------------
   Color enum: defines the four tile colours in Rummikub.
------------------------------------------------------- */
enum class Color { RED, BLUE, YELLOW, BLACK };

/* -------------------------------------------------------
   Tile: the fundamental game piece.
     - number : face value 1-13  (0 for joker)
     - color  : one of the four colours  (ignored for joker)
     - is_joker: wildcard flag
     - id     : unique identifier used in JSON communication
  
   IMPORTANT (teaching note):
     All Tile objects live in a single "pool" (vector<Tile>) owned by GameManager.
     Everything else – player hands, board groups – stores Tile* pointers, NOT copies.
     Moving a tile between locations means swapping a pointer,
     which is O(1) and avoids object copying.
  
   PROTECTION NOTE:
     Tile members are private to prevent directly modifying tile data.
     Use const getter methods instead.
------------------------------------------------------- */
class Tile {
private:
    int   id;
    int   number;
    Color color;
    bool  is_joker;

public:
    // Normal tile constructor
    Tile(int id, int number, Color color);

    // Joker constructor
    explicit Tile(int id);

    // Const getters to safely read tile properties
    int getId() const { return id; }
    int getNumber() const { return number; }
    Color getColor() const { return color; }
    bool isJoker() const { return is_joker; }

    std::string colorName() const;
    std::string toString()  const;
    std::string toJSON()    const;
};
