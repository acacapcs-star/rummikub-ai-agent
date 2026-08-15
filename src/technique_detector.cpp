#include "technique_detector.h"
#include "validator.h"
#include <algorithm>
#include <set>

// ─────────────────────────────────────────────────────────
// 內部工具
// ─────────────────────────────────────────────────────────
std::vector<Tile*> TechniqueDetector::flatten(
    const std::vector<std::vector<Tile*>>& sets) {
    std::vector<Tile*> out;
    for (const auto& s : sets)
        for (Tile* t : s) out.push_back(t);
    return out;
}

bool TechniqueDetector::sameTiles(const std::vector<Tile*>& a,
                                  const std::vector<Tile*>& b) {
    if (a.size() != b.size()) return false;
    std::set<Tile*> sa(a.begin(), a.end());
    std::set<Tile*> sb(b.begin(), b.end());
    return sa == sb;
}

bool TechniqueDetector::containsSet(const std::vector<std::vector<Tile*>>& sets,
                                    const std::vector<Tile*>& target) {
    for (const auto& s : sets)
        if (sameTiles(s, target)) return true;
    return false;
}

bool TechniqueDetector::isSupersetOf(const std::vector<Tile*>& candidate,
                                     const std::vector<Tile*>& target) {
    std::set<Tile*> c(candidate.begin(), candidate.end());
    for (Tile* t : target)
        if (c.find(t) == c.end()) return false;
    return true;
}

// ─────────────────────────────────────────────────────────
// 這一手實際打出去的牌：出牌前在手上、出牌後不在手上的那些。
// 用指標比對，因為所有 Tile 都在 GameManager 的單一 pool 裡，
// 指標本身就是身分（tile.h 的教學註解特別強調過這點）。
// ─────────────────────────────────────────────────────────
std::vector<Tile*> TechniqueDetector::playedTiles(const MoveSnapshot& snap) {
    std::set<Tile*> after(snap.hand_after.begin(), snap.hand_after.end());
    std::vector<Tile*> played;
    for (Tile* t : snap.hand_before)
        if (after.find(t) == after.end()) played.push_back(t);
    return played;
}

int TechniqueDetector::tilesPlayedCount(const MoveSnapshot& snap) {
    return static_cast<int>(playedTiles(snap).size());
}

// ─────────────────────────────────────────────────────────
// 有沒有動到桌面上原有的牌。
// 判準：出牌前的某一組，在出牌後找不到「原封不動」的對應組合，
// 也不是單純被延長（超集）——那就代表它被拆開重新分配了。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::touchedExistingTiles(const MoveSnapshot& snap) {
    for (const auto& before_set : snap.board_before) {
        bool intact_or_extended = false;
        for (const auto& after_set : snap.board_after) {
            if (sameTiles(before_set, after_set) ||
                isSupersetOf(after_set, before_set)) {
                intact_or_extended = true;
                break;
            }
        }
        if (!intact_or_extended) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// 接龍頭尾：某條既有的 Run 在出牌後變長了。
//
// 判準刻意嚴格——要求出牌後的那一組是出牌前那一組的「超集」，
// 也就是原本的牌全部還在、只是多了幾張。這樣可以排除
// 「整條被拆掉重拼、剛好看起來變長」的情況（那屬於大風吹）。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectAttachRun(const MoveSnapshot& snap) {
    std::vector<Tile*> played = playedTiles(snap);
    if (played.empty()) return false;

    for (const auto& before_set : snap.board_before) {
        if (!Validator::isValidRun(before_set)) continue;

        for (const auto& after_set : snap.board_after) {
            if (after_set.size() <= before_set.size()) continue;
            if (!isSupersetOf(after_set, before_set)) continue;
            if (!Validator::isValidRun(after_set)) continue;

            // 多出來的牌必須是這一手打出去的，否則不算玩家的功勞
            for (Tile* t : after_set) {
                bool was_in_before = false;
                for (Tile* b : before_set)
                    if (b == t) { was_in_before = true; break; }
                if (was_in_before) continue;

                for (Tile* p : played)
                    if (p == t) return true;
            }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// 補第四色：原本三張的 Group 變成四張。
// Rummikub 只有四色，所以「三張變四張」就是補滿，沒有其他可能。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectCompleteGroup(const MoveSnapshot& snap) {
    std::vector<Tile*> played = playedTiles(snap);
    if (played.empty()) return false;

    for (const auto& before_set : snap.board_before) {
        if (before_set.size() != 3) continue;
        if (!Validator::isValidGroup(before_set)) continue;

        for (const auto& after_set : snap.board_after) {
            if (after_set.size() != 4) continue;
            if (!isSupersetOf(after_set, before_set)) continue;
            if (!Validator::isValidGroup(after_set)) continue;

            for (Tile* t : after_set) {
                bool was_in_before = false;
                for (Tile* b : before_set)
                    if (b == t) { was_in_before = true; break; }
                if (was_in_before) continue;

                for (Tile* p : played)
                    if (p == t) return true;
            }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// Joker 補缺口：打出去的牌裡有 Joker，而且它不在該組合的頭尾。
//
// 為什麼要排除頭尾：Joker 放在 Run 的兩端只是延長，
// 任何一張普通牌也做得到；放在中間才是真正「填補缺口」，
// 那是這一關要教的東西。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectJokerFill(const MoveSnapshot& snap) {
    std::vector<Tile*> played = playedTiles(snap);

    bool played_joker = false;
    for (Tile* t : played)
        if (t->isJoker()) { played_joker = true; break; }
    if (!played_joker) return false;

    for (const auto& after_set : snap.board_after) {
        if (after_set.size() < 3) continue;
        if (!Validator::isValidRun(after_set)) continue;

        // 檢查中間位置（排除首尾）有沒有這一手打出去的 Joker
        for (std::size_t i = 1; i + 1 < after_set.size(); ++i) {
            if (!after_set[i]->isJoker()) continue;
            for (Tile* p : played)
                if (p == after_set[i]) return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// 破冰：這一手之前尚未破冰，且確實打出了牌。
// 分數是否達 30 由 Board::applyProposedSets() 把關，
// 這裡只要確認「破冰前的狀態 + 有出牌」即可。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectInitialMeld(const MoveSnapshot& snap) {
    if (snap.initial_meld_done_before) return false;
    return !playedTiles(snap).empty();
}

// ─────────────────────────────────────────────────────────
// 大風吹重組：桌面上原有的牌被重新分配。
//
// 判準：存在某一個出牌前的組合，在出牌後既找不到原封不動的版本，
// 也找不到把它整個包含進去的版本——那代表它被拆散了。
//
// 這裡刻意不去追蹤「每一張牌跑到哪裡」，因為那需要完整的
// 二分圖比對，複雜度高且容易誤判。拆散這個事實本身就足以判定。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectBoardReshuffle(const MoveSnapshot& snap) {
    if (snap.board_before.empty()) return false;
    return touchedExistingTiles(snap);
}

// ─────────────────────────────────────────────────────────
// 長龍切斷：出牌前有一條 ≥6 張的 Run，出牌後它不見了，
// 但它的牌全部出現在兩條各自合法的 Run 裡。
//
// 這是所有偵測裡最嚴格的一條——必須明確找到「一分為二」的證據，
// 只是「長 Run 消失」不算，因為那也可能是被併進更長的組合裡。
// ─────────────────────────────────────────────────────────
bool TechniqueDetector::detectRunSplit(const MoveSnapshot& snap) {
    for (const auto& before_set : snap.board_before) {
        if (before_set.size() < 6) continue;
        if (!Validator::isValidRun(before_set)) continue;

        // 這條長 Run 必須已經不存在於出牌後的盤面
        if (containsSet(snap.board_after, before_set)) continue;

        // 找出出牌後包含這條 Run 的牌的所有組合
        std::vector<const std::vector<Tile*>*> carriers;
        for (const auto& after_set : snap.board_after) {
            bool holds_any = false;
            for (Tile* t : after_set) {
                for (Tile* b : before_set)
                    if (b == t) { holds_any = true; break; }
                if (holds_any) break;
            }
            if (holds_any) carriers.push_back(&after_set);
        }

        // 必須剛好散落在兩個以上的組合裡，且每個都是合法的 Run
        if (carriers.size() < 2) continue;

        bool all_valid_runs = true;
        for (const auto* c : carriers)
            if (!Validator::isValidRun(*c)) { all_valid_runs = false; break; }

        if (all_valid_runs) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// 主入口：一次出牌可能同時符合多個技巧，全部回報。
// 例如「用 Joker 接到 Run 的中間」同時算 ATTACH_RUN 與 JOKER_FILL，
// 兩者各自記錄進度——玩家確實兩件事都做到了。
// ─────────────────────────────────────────────────────────
std::vector<Technique> TechniqueDetector::detect(const MoveSnapshot& snap) {
    std::vector<Technique> found;

    if (detectInitialMeld(snap))     found.push_back(Technique::INITIAL_MELD);
    if (detectAttachRun(snap))       found.push_back(Technique::ATTACH_RUN);
    if (detectCompleteGroup(snap))   found.push_back(Technique::COMPLETE_GROUP);
    if (detectJokerFill(snap))       found.push_back(Technique::JOKER_FILL);
    if (detectRunSplit(snap))        found.push_back(Technique::RUN_SPLIT);

    // 大風吹放最後判斷：長龍切斷本質上也動了桌面，
    // 但它是更明確的技巧，已經被上面認出來的話就不重複記為重組。
    bool already_split = std::find(found.begin(), found.end(),
                                   Technique::RUN_SPLIT) != found.end();
    if (!already_split && detectBoardReshuffle(snap))
        found.push_back(Technique::BOARD_RESHUFFLE);

    return found;
}
