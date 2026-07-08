#include "cognitive_hint_engine.h"
#include <sstream>
#include <algorithm>

std::string CognitiveHintEngine::colorName(Color c) {
    switch (c) {
        case Color::RED:    return "紅";
        case Color::BLUE:   return "藍";
        case Color::YELLOW: return "黃";
        case Color::BLACK:  return "黑";
    }
    return "";
}

CognitiveHintEngine::FoundMove CognitiveHintEngine::findAttachableMove(
    const std::vector<Tile*>& hand,
    const std::vector<std::vector<Tile*>>& board_sets
) {
    FoundMove result;

    for (const auto& set : board_sets) {
        if (set.size() < 3) continue;

        // 判斷這個 set 是不是 Run（同色連續），不是 Group
        Tile* first_normal = nullptr;
        Tile* second_normal = nullptr;
        for (Tile* t : set) {
            if (t && !t->isJoker()) {
                if (!first_normal) first_normal = t;
                else if (!second_normal) { second_normal = t; break; }
            }
        }
        if (!first_normal || !second_normal) continue;
        if (first_normal->getColor() != second_normal->getColor()) continue; // 是 Group，跳過

        Tile* head = set.front();
        Tile* tail = set.back();
        if (!head || !tail || head->isJoker() || tail->isJoker()) continue;

        for (Tile* t : hand) {
            if (!t || t->isJoker()) continue;

            if (t->getColor() == head->getColor() && t->getNumber() == head->getNumber() - 1 && t->getNumber() >= 1) {
                result.found = true;
                result.tile = t;
                result.color = t->getColor();
                result.target_number = t->getNumber();
                result.attach_to_head = true;
                result.run_length = static_cast<int>(set.size());
                return result;
            }
            if (t->getColor() == tail->getColor() && t->getNumber() == tail->getNumber() + 1 && t->getNumber() <= 13) {
                result.found = true;
                result.tile = t;
                result.color = t->getColor();
                result.target_number = t->getNumber();
                result.attach_to_head = false;
                result.run_length = static_cast<int>(set.size());
                return result;
            }
        }
    }

    return result; // found = false
}

CognitiveHintEngine::FoundGroupMove CognitiveHintEngine::findGroupCompletionMove(
    const std::vector<Tile*>& hand,
    const std::vector<std::vector<Tile*>>& board_sets
) {
    FoundGroupMove result;

    for (const auto& set : board_sets) {
        if (set.size() != 3) continue; // 只處理「剛好 3 張」的 Group，4 張已補滿不需要提示

        // 判斷是不是 Group：同數字、顏色互不相同
        bool is_group = true;
        int number = -1;
        bool color_seen[4] = {false, false, false, false};
        for (Tile* t : set) {
            if (!t || t->isJoker()) { is_group = false; break; }
            if (number == -1) number = t->getNumber();
            else if (t->getNumber() != number) { is_group = false; break; }

            int ci = static_cast<int>(t->getColor());
            if (color_seen[ci]) { is_group = false; break; } // 同色重複，代表其實是別的東西
            color_seen[ci] = true;
        }
        if (!is_group) continue;

        // 找出缺的那個顏色
        int missing_color_idx = -1;
        for (int c = 0; c < 4; ++c) {
            if (!color_seen[c]) { missing_color_idx = c; break; }
        }
        if (missing_color_idx == -1) continue; // 理論上不會發生（3 張不可能 4 色都有）

        // 檢查手牌裡有沒有這個數字＋這個缺色的牌
        for (Tile* t : hand) {
            if (!t || t->isJoker()) continue;
            if (t->getNumber() == number && static_cast<int>(t->getColor()) == missing_color_idx) {
                result.found = true;
                result.tile = t;
                result.number = number;
                result.color = t->getColor();
                return result;
            }
        }
    }

    return result; // found = false
}

HintTier CognitiveHintEngine::tierFromStuckTurns(int stuck_turns) {
    // 示意性門檻，不是臨床驗證過的數字——之後有真實長輩測試數據，這裡要重新校準。
    if (stuck_turns < 2) return HintTier::GENTLE_NUDGE;
    if (stuck_turns < 4) return HintTier::POINT_TO_AREA;
    return HintTier::REVEAL_MOVE;
}

std::string CognitiveHintEngine::generateHint(
    const std::vector<Tile*>& hand,
    const std::vector<std::vector<Tile*>>& board_sets,
    HintTier tier
) {
    FoundMove move = findAttachableMove(hand, board_sets);
    if (move.found) {
        std::ostringstream oss;
        switch (tier) {
            case HintTier::GENTLE_NUDGE:
                oss << "手牌裡好像有牌可以出喔，仔細看看桌上的排列！";
                break;
            case HintTier::POINT_TO_AREA:
                oss << "可以留意一下桌上「" << colorName(move.color) << "色」那一排（目前有 "
                    << move.run_length << " 張連續的數字），你手上有牌可以接上去喔。";
                break;
            case HintTier::REVEAL_MOVE:
                oss << "試試看把手上的「" << colorName(move.color) << "色 " << move.target_number
                    << "」接到桌上那排" << colorName(move.color) << "色數字的"
                    << (move.attach_to_head ? "前面" : "後面") << "，應該接得上喔！";
                break;
        }
        return oss.str();
    }

    // 接龍找不到，換找「桌面上有沒有 3 張的 Group 可以補第 4 種顏色」
    FoundGroupMove group_move = findGroupCompletionMove(hand, board_sets);
    if (group_move.found) {
        std::ostringstream oss;
        switch (tier) {
            case HintTier::GENTLE_NUDGE:
                oss << "手牌裡好像有牌可以出喔，仔細看看桌上有沒有差一種顏色就補滿的組合！";
                break;
            case HintTier::POINT_TO_AREA:
                oss << "可以留意一下桌上數字「" << group_move.number
                    << "」那一組，好像只差一種顏色就能補滿，你手上剛好有相關的牌喔。";
                break;
            case HintTier::REVEAL_MOVE:
                oss << "試試看把手上的「" << colorName(group_move.color) << "色 " << group_move.number
                    << "」補到桌上那組數字 " << group_move.number << " 的群組裡，應該接得上喔！";
                break;
        }
        return oss.str();
    }

    // 兩種都掃描不到，誠實回報，不硬掰
    return "目前看起來手牌跟桌面接不太上，可以考慮先抽一張牌試試看喔！";
}

CognitiveHintEngine::MeldCandidate CognitiveHintEngine::findBestMeldCandidate(
    const std::vector<Tile*>& hand
) {
    MeldCandidate best;

    int total_jokers = 0;
    for (Tile* t : hand) {
        if (t && t->isJoker()) ++total_jokers;
    }

    // --- 找 Run 候選：依顏色分堆，掃描時允許用 Joker 動態填補內部缺口 ---
    std::vector<Tile*> by_color[4];
    for (Tile* t : hand) {
        if (t && !t->isJoker()) {
            by_color[static_cast<int>(t->getColor())].push_back(t);
        }
    }
    for (int c = 0; c < 4; ++c) {
        auto& bucket = by_color[c];
        std::sort(bucket.begin(), bucket.end(),
                  [](Tile* a, Tile* b) { return a->getNumber() < b->getNumber(); });

        for (size_t start = 0; start < bucket.size(); ++start) {
            std::vector<Tile*> real_tiles = { bucket[start] };
            int used_jokers = 0;
            int last_number = bucket[start]->getNumber();
            int joker_covered_score = 0;

            for (size_t k = start + 1; k < bucket.size(); ++k) {
                int gap = bucket[k]->getNumber() - last_number - 1;
                if (gap > 0) {
                    if (used_jokers + gap > total_jokers) break; // Joker 不夠填這個缺口
                    for (int g = 1; g <= gap; ++g) joker_covered_score += (last_number + g);
                    used_jokers += gap;
                }
                real_tiles.push_back(bucket[k]);
                last_number = bucket[k]->getNumber();
            }

            int length = static_cast<int>(real_tiles.size()) + used_jokers;
            if (length >= 3) {
                int score = joker_covered_score;
                for (Tile* t : real_tiles) score += t->getNumber();
                if (score > best.score) {
                    best.found = true;
                    best.score = score;
                    best.is_run = true;
                    best.tiles = real_tiles;
                    best.joker_count = used_jokers;
                }
            }
        }
    }

    // --- 找 Group 候選：依數字分堆，2 種顏色時可以用 Joker 補成 3 張 ---
    std::vector<Tile*> by_number[14]; // 1~13
    for (Tile* t : hand) {
        if (t && !t->isJoker() && t->getNumber() >= 1 && t->getNumber() <= 13) {
            by_number[t->getNumber()].push_back(t);
        }
    }
    for (int n = 1; n <= 13; ++n) {
        std::vector<Tile*> distinct_colors;
        bool seen[4] = {false, false, false, false};
        for (Tile* t : by_number[n]) {
            int ci = static_cast<int>(t->getColor());
            if (!seen[ci]) {
                seen[ci] = true;
                distinct_colors.push_back(t);
            }
        }

        int real_count = static_cast<int>(distinct_colors.size());
        int jokers_needed = (real_count >= 3) ? 0 : (3 - real_count);

        if (real_count >= 2 && jokers_needed <= total_jokers && real_count + jokers_needed >= 3) {
            int score = n * (real_count + jokers_needed);
            if (score > best.score) {
                best.found = true;
                best.score = score;
                best.is_run = false;
                best.tiles = distinct_colors;
                best.joker_count = jokers_needed;
            }
        }
    }

    return best;
}

std::string CognitiveHintEngine::generateMeldHint(
    const std::vector<Tile*>& hand,
    HintTier tier
) {
    MeldCandidate candidate = findBestMeldCandidate(hand);

    if (!candidate.found) {
        return "目前手牌裡還湊不出合法的組合（Run 或 Group），可以考慮先抽一張牌試試看喔！";
    }

    std::ostringstream oss;
    bool reaches_30 = candidate.score >= 30;
    std::string joker_note = candidate.joker_count > 0
        ? "（需要搭配 " + std::to_string(candidate.joker_count) + " 張 Joker）"
        : "";

    switch (tier) {
        case HintTier::GENTLE_NUDGE:
            oss << "手牌裡好像已經有一組不錯的牌可以試試看喔，仔細找找同色連續、或同數字不同色的組合！";
            break;
        case HintTier::POINT_TO_AREA:
            oss << "你手上有一組" << (candidate.is_run ? "同色連續的牌" : "同數字不同色的牌")
                << joker_note
                << "，目前大約可以湊到 " << candidate.score << " 分"
                << (reaches_30 ? "，已經達標囉！" : "，還差一點才到 30 分，可以看看有沒有牌能加進去。");
            break;
        case HintTier::REVEAL_MOVE: {
            oss << "試試看把手上的";
            for (size_t k = 0; k < candidate.tiles.size(); ++k) {
                Tile* t = candidate.tiles[k];
                oss << "「" << colorName(t->getColor()) << "色 " << t->getNumber() << "」";
                if (k + 1 < candidate.tiles.size()) oss << "、";
            }
            oss << joker_note
                << " 湊成一組" << (candidate.is_run ? "順組" : "群組")
                << "，總共 " << candidate.score << " 分"
                << (reaches_30 ? "，可以直接出牌破冰囉！" : "，但還不到 30 分，可能需要再抽牌等更好的組合。");
            break;
        }
    }
    return oss.str();
}
