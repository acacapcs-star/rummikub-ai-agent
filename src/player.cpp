#include "player.h"
#include <iostream>
#include <sstream>

Player::Player(const std::string& name)
    : name(name), initial_meld_done(false) {}

std::string Player::handToJSON() const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < hand.size(); ++i) {
        oss << hand[i]->toJSON();
        if (i + 1 < hand.size()) oss << ",";
    }
    oss << "]";
    return oss.str();
}

int Player::handScore() const {
    int score = 0;
    for (Tile* t : hand) {
        score += t->isJoker() ? 30 : t->getNumber();
    }
    return score;
}

void Player::printHand() const {
    std::cout << name << "'s hand (" << hand.size() << " tiles): ";
    for (Tile* t : hand) {
        std::cout << t->toString() << " ";
    }
    std::cout << "\n";
}
