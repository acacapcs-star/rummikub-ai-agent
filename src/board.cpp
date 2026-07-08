#include "board.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include "player.h"

const char* Board::describe(ApplyResult r) {
    switch (r) {
        case ApplyResult::Ok:                   return "ok";
        case ApplyResult::NotPlayerTile:        return "tile is not in your hand or on the board";
        case ApplyResult::DuplicateTile:        return "the same tile appears more than once";
        case ApplyResult::RemovedOldTile:       return "tiles already on the board cannot be removed";
        case ApplyResult::RearrangedDuringMeld: return "initial meld cannot rearrange existing sets";
        case ApplyResult::InvalidSet:           return "some set is not a valid Run or Group";
        case ApplyResult::MeldTooLow:           return "initial meld must score at least 30 points";
        case ApplyResult::NotCurrentPlayer:     return "not the current player";
    }
    return "unknown";
}

Board::ApplyResult Board::applyProposedSets(
        Player* player,
        const std::vector<std::vector<Tile*>>& newSets) {
    ++apply_call_count;

    // Snapshot the old board.
    const std::vector<std::vector<Tile*>> old_sets = sets;
    const std::vector<Tile*> old_board_tiles = allTiles();

    // ── 1. Build the set of tiles the player is legally allowed to put on the board.
    //       Anything else (a tile from the draw pile, from an opponent's hand, or a
    //       forged Tile* the student new'd themselves) is rejected.
    std::set<Tile*> legal_sources;
    for (Tile* t : old_board_tiles)        legal_sources.insert(t);
    for (Tile* t : player->getHand())      legal_sources.insert(t);

    // ── 2. Walk newSets once: verify provenance and detect duplicate Tile* usage.
    std::set<Tile*> seen;
    std::vector<Tile*> new_board_all;
    new_board_all.reserve(legal_sources.size());
    for (const auto& s : newSets) {
        for (Tile* t : s) {
            if (!t) {
                last_apply_result = ApplyResult::NotPlayerTile;
                return last_apply_result;
            }
            if (!legal_sources.count(t)) {
                last_apply_result = ApplyResult::NotPlayerTile;
                return last_apply_result;
            }
            if (!seen.insert(t).second) {
                last_apply_result = ApplyResult::DuplicateTile;
                return last_apply_result;
            }
            new_board_all.push_back(t);
        }
    }

    // ── 3. No old board tile may disappear from the new arrangement.
    for (Tile* t : old_board_tiles) {
        if (!seen.count(t)) {
            last_apply_result = ApplyResult::RemovedOldTile;
            return last_apply_result;
        }
    }

    // ── 4. Initial-meld players may not rearrange existing sets.
    if (!player->initial_meld_done) {
        if (!Validator::preservesOldSets(old_sets, newSets)) {
            last_apply_result = ApplyResult::RearrangedDuringMeld;
            return last_apply_result;
        }
    }

    // ── 5. Every set on the resulting board must be a valid Run or Group.
    if (!Validator::isValidBoard(newSets)) {
        last_apply_result = ApplyResult::InvalidSet;
        return last_apply_result;
    }

    // ── 6. Initial meld must contribute >= 30 points from the player's hand.
    if (!player->initial_meld_done) {
        int meld_score = 0;
        const auto& hand = player->getHand();
        const std::set<Tile*> hand_set(hand.begin(), hand.end());
        for (const auto& s : newSets) {
            for (Tile* t : s) {
                if (hand_set.count(t)) {
                    meld_score += Validator::calculateInitialMeldTileValue(s, t);
                }
            }
        }
        if (meld_score < 30) {
            last_apply_result = ApplyResult::MeldTooLow;
            return last_apply_result;
        }
    }

    // ── 7. All checks passed: commit.
    sets = newSets;

    // Strip from player's hand any tile that's now on the board.
    std::vector<Tile*> new_hand;
    new_hand.reserve(player->getHand().size());
    for (Tile* t : player->getHand()) {
        if (!seen.count(t)) new_hand.push_back(t);
    }
    player->hand = new_hand;

    if (!player->initial_meld_done) player->initial_meld_done = true;

    last_apply_result = ApplyResult::Ok;
    return last_apply_result;
}

bool Board::isValid() const {
    return Validator::isValidBoard(sets);
}

std::vector<Tile*> Board::allTiles() const {
    std::vector<Tile*> all;
    for (const auto& s : sets) {
        for (Tile* t : s) {
            all.push_back(t);
        }
    }
    return all;
}

std::string Board::toJSON() const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < sets.size(); ++i) {
        oss << "[";
        for (size_t j = 0; j < sets[i].size(); ++j) {
            oss << sets[i][j]->toJSON();
            if (j + 1 < sets[i].size()) oss << ",";
        }
        oss << "]";
        if (i + 1 < sets.size()) oss << ",";
    }
    oss << "]";
    return oss.str();
}

void Board::print() const {
    if (sets.empty()) {
        std::cout << "  (empty board)\n";
        return;
    }
    for (size_t i = 0; i < sets.size(); ++i) {
        std::cout << "  Set " << i << ": ";
        for (Tile* t : sets[i]) {
            std::cout << t->toString() << " ";
        }
        std::cout << "\n";
    }
}
