#include "tile.h"
#include <sstream>

Tile::Tile(int id, int number, Color color)
    : id(id), number(number), color(color), is_joker(false) {}

Tile::Tile(int id)
    : id(id), number(0), color(Color::RED), is_joker(true) {}

std::string Tile::colorName() const {
    if (isJoker()) return "JOKER";
    switch (getColor()) {
        case Color::RED:    return "RED";
        case Color::BLUE:   return "BLUE";
        case Color::YELLOW: return "YELLOW";
        case Color::BLACK:  return "BLACK";
    }
    return "UNKNOWN";
}

std::string Tile::toString() const {
    if (isJoker()) return "[JOKER]";
    return "[" + colorName() + " " + std::to_string(getNumber()) + "]";
}

std::string Tile::toJSON() const {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":"       << getId()       << ","
        << "\"number\":"   << getNumber()   << ","
        << "\"color\":\""  << colorName() << "\","
        << "\"is_joker\":" << (isJoker() ? "true" : "false")
        << "}";
    return oss.str();
}
