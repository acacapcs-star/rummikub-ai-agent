#include "validator.h"
#include <algorithm>
#include <set>

namespace {

bool computeRunValues(const std::vector<Tile*>& tiles, std::vector<int>& values) {
    values.assign(tiles.size(),0);
    std::size_t first_known = tiles.size();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (!tiles[i]->isJoker()){
            values[i] = tiles[i]->getNumber();
            if (first_known == tiles.size()) {
                first_known = i;
            }
        }
    }

    if (first_known == tiles.size()) {
        return false;
    }

    for (std::size_t i = first_known; i > 0; --i) {
        values[i - 1] = values[i] - 1;
    }
    for (std::size_t i = first_known + 1; i < tiles.size(); ++i) {
        if (tiles[i]->isJoker()){
            values[i] = values[i - 1] + 1;
        } else {
            if (tiles[i]->getNumber() != values[i - 1] + 1) {
                return false;
            }
            values[i] = tiles[i]->getNumber();
        }
    }
    return true;
}

bool computeGroupValues(const std::vector<Tile*>& tiles, std::vector<int>& values) {
    values.assign(tiles.size(), 0);
    int group_number = -1;
    for (Tile* t : tiles) {
        if (t->isJoker()) {
            continue;
        }
        if (group_number == -1) {
            group_number = t->getNumber();
        } else if(t->getNumber()!= group_number) {
            return false;
        }
    }

    if (group_number==-1) {
        return false;
    }
    for (std::size_t i=0;i<tiles.size();++i) {
        values[i]=tiles[i]->isJoker() ? group_number : tiles[i]->getNumber();
    }
    return true;
}

} 

/* -------------------------------------------------------
   TODO(L1): isValidRun
     A Run needs at least 3 tiles, all the same color,
     with consecutive numbers (1..13).  Jokers fill gaps.
     Tiles must already be in sorted order: numbers ascending
     left-to-right with each Joker occupying the consecutive
     slot it represents.
------------------------------------------------------- */
bool Validator::isValidRun(const std::vector<Tile*>& tiles) {
    // TODO - START
    
    // 1. 檢查牌組張數是否至少 3 張
    if (tiles.size() < 3) {
        return false;
    }

    // 2. 檢查顏色：所有非鬼牌的牌，顏色必須相同
    Color runColor;
    bool foundFirstColor = false;
    for (Tile* tile : tiles) {
        if (tile->isJoker()) {
            continue; // 鬼牌可以變任意色，跳過
        }
        if (!foundFirstColor) {
            runColor = tile->getColor(); // 記錄第一張非鬼牌的顏色
            foundFirstColor = true;
        } else {
            if (tile->getColor() != runColor) {
                return false; // 顏色不相同，不合法
            }
        }
    }

    // 3. 檢查數字是否連續（使用助教工具）
    std::vector<int> values;
    if (!computeRunValues(tiles, values)) {
        return false;
    }

    // 4. 檢查範圍：數字必須在 1 到 13 之間
    for (int val : values) {
        if (val < 1 || val > 13) {
            return false;
        }
    }

    // TODO - END
    return true;
}

/* -------------------------------------------------------
   TODO(L1): isValidGroup
     A Group needs exactly 3 or 4 tiles, all the same number,
     each with a DIFFERENT color.
     Jokers fill in for any missing color.
------------------------------------------------------- */
bool Validator::isValidGroup(const std::vector<Tile*>& tiles) {
    // TODO - START
    
    // 1. 檢查張數：群組只能是固定 3 張或 4 張
    if (tiles.size() < 3 || tiles.size() > 4) {
        return false;
    }

    // 2. 檢查數字是否完全相同（使用助教工具）
    std::vector<int> values;
    if (!computeGroupValues(tiles, values)) {
        return false;
    }

    // 3. 檢查顏色：群組內的顏色絕對不能重複
    std::set<Color> seenColors;
    for (Tile* tile : tiles) {
        if (tile->isJoker()) {
            continue; // 鬼牌可以頂替任何缺的顏色，跳過
        }
        if (seenColors.count(tile->getColor()) > 0) {
            return false; // 顏色重複出現，不合法！
        }
        seenColors.insert(tile->getColor());
    }

    // TODO - END
    return true;
}

bool Validator::isValidSet(const std::vector<Tile*>& tiles){
    return isValidRun(tiles) || isValidGroup(tiles);
}

bool Validator::isValidBoard(const std::vector<std::vector<Tile*>>& sets) {
    for (const auto& s : sets) {
        if (!isValidSet(s)) return false;
    }
    return true;
}

/* -------------------------------------------------------
   preservesOldSets
     Ensure that every old set appears unchanged (as a multiset
     of tile ids) in new_sets. Order of sets does not matter;
     comparison is by tile id contents.
------------------------------------------------------- */
bool Validator::preservesOldSets(
        const std::vector<std::vector<Tile*>>& old_sets,
        const std::vector<std::vector<Tile*>>& new_sets) {
    // Helper: produce sorted vector of ids for a set
    auto ids_of = [](const std::vector<Tile*>& s) {
        std::vector<int> ids;
        ids.reserve(s.size());
        for (Tile* t : s) ids.push_back(t->getId());
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    std::vector<std::vector<int>> new_ids;
    new_ids.reserve(new_sets.size());
    for (const auto& s : new_sets) new_ids.push_back(ids_of(s));

    for (const auto& olds : old_sets) {
        std::vector<int> oid = ids_of(olds);
        bool found = false;
        for (const auto& nid : new_ids) {
            if (nid == oid) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

int Validator::calculateScore(const std::vector<Tile*>& tiles) {
    int score = 0;
    for (Tile* t : tiles) {
        score += t->isJoker() ? 30 : t->getNumber();
    }
    return score;
}

int Validator::calculateInitialMeldScore(const std::vector<Tile*>& tiles) {
    std::vector<int> values;
    if (isValidRun(tiles)) {
        computeRunValues(tiles, values);
    } else if (isValidGroup(tiles)) {
        computeGroupValues(tiles, values);
    } else {
        return 0;
    }

    int score = 0;
    for (int value : values) {
        score += value;
    }
    return score;
}

int Validator::calculateInitialMeldTileValue(
        const std::vector<Tile*>& tiles,
        Tile* tile) {
    if (!tile->isJoker()) {
        return tile->getNumber();
    }

    std::vector<int> values;
    if (isValidRun(tiles)) {
        computeRunValues(tiles, values);
    } else if (isValidGroup(tiles)) {
        computeGroupValues(tiles, values);
    } else {
        return 0;
    }

    auto it = std::find(tiles.begin(), tiles.end(), tile);
    if (it == tiles.end()) {
        return 30;
    }

    return values[static_cast<std::size_t>(std::distance(tiles.begin(), it))];
}
