
#include "ai_agent_0.h"
#include "validator.h"
#include <algorithm>
#include <iostream>
#include <cmath>
/* =========================================================================
   【內部隱藏全域變數與戰術追蹤器】
   ========================================================================= */
static int draw_cooldown = 0;
static bool overlap_retained = false;
static int last_draw_pile_size = 0;   // 追蹤上一回合牌堆數量
static int enemy_draw_count = 0;      // 紀錄對手連續抽牌次數
static bool is_endgame_global = false; // 💡 新增：不改標頭檔，用這個全域標記來通知大風吹
AIAgent_0::AIAgent_0(const std::string& name) : Player(name) {
    draw_cooldown = 0;
    overlap_retained = false;
    last_draw_pile_size = 0;
    enemy_draw_count = 0;
}
 
void AIAgent_0::sortRunSet(std::vector<Tile*>& tiles) {
    std::sort(tiles.begin(), tiles.end(), [](Tile* a, Tile* b) {
        return a->getNumber() < b->getNumber();
    });
}
 
/* -------------------------------------------------------
   【終極優化版】findRuns - Joker 智能動態順組生成器
   ------------------------------------------------------- */
std::vector<std::vector<Tile*>> AIAgent_0::findRuns(const std::vector<Tile*>& tiles) const {
    std::vector<std::vector<Tile*>> result;
    std::vector<Tile*> normal_tiles;
    std::vector<Tile*> jokers;
    
    // 分離普通牌與 Joker
    for (Tile* t : tiles) {
        if (!t) continue;
        if (t->isJoker()) jokers.push_back(t);
        else normal_tiles.push_back(t);
    }
    
    // 分顏色與數字入桶
    std::vector<std::vector<std::vector<Tile*>>> table(4, std::vector<std::vector<Tile*>>(14));
    for (Tile* t : normal_tiles) {
        int c = static_cast<int>(t->getColor());
        int n = t->getNumber();
        table[c][n].push_back(t);
    }
    
    // 深度動態滑動窗口掃描
    for (int c = 0; c < 4; ++c) {
        for (int start_n = 1; start_n <= 11; ++start_n) {
            std::vector<Tile*> current_run;
            std::vector<Tile*> temp_jokers = jokers;
            int current_n = start_n;
            
            while (current_n <= 13) {
                if (!table[c][current_n].empty()) {
                    current_run.push_back(table[c][current_n].back());
                    current_n++;
                }
                else if (!temp_jokers.empty()) {
                    if (current_run.size() >= 3 && (current_n + 1 > 13 || table[c][current_n + 1].empty())) {
                        break;
                    }
                    current_run.push_back(temp_jokers.back());
                    temp_jokers.pop_back();
                    current_n++;
                }
                else {
                    break;
                }
            }
            if (current_run.size() >= 3) {
                int normal_tiles_count = 0;
                for (Tile* t : current_run) {
                    if (!t->isJoker()) normal_tiles_count++;
                }
                if (normal_tiles_count > 0) {
                    int joker_used = 0;
                    for (Tile* t : current_run) {
                        if (!t->isJoker()) {
                            int n = t->getNumber();
                            if (!table[c][n].empty()) table[c][n].pop_back();
                        } else {
                            joker_used++;
                        }
                    }
                    for (int j = 0; j < joker_used; ++j) {
                        if (!jokers.empty()) jokers.pop_back();
                    }
                    result.push_back(current_run);
                    start_n = current_n - 1;
                }
            }
        }
    }
    return result;
}
 
/* -------------------------------------------------------
   findGroups - 尋找所有群組（經典高效實作）
   ------------------------------------------------------- */
std::vector<std::vector<Tile*>> AIAgent_0::findGroups(const std::vector<Tile*>& tiles) const {
    std::vector<std::vector<Tile*>> result;
    std::vector<std::vector<std::vector<Tile*>>> table(4, std::vector<std::vector<Tile*>>(14));
    
    for (Tile* t : tiles) {
        if (!t || t->isJoker()) continue;
        int c = static_cast<int>(t->getColor());
        int n = t->getNumber();
        table[c][n].push_back(t);
    }
    
    for (int n = 1; n <= 13; ++n) {
        std::vector<Tile*> current_group;
        for (int c = 0; c < 4; ++c) {
            if (!table[c][n].empty()) {
                current_group.push_back(table[c][n].back());
            }
        }
        if (current_group.size() >= 3) {
            result.push_back(current_group);
            for (int c = 0; c < 4; ++c) {
                if (!table[c][n].empty()) {
                    table[c][n].pop_back();
                }
            }
        }
    }
    return result;
}
 
/* -------------------------------------------------------
   【核心修正版】checkRunsWithJokers (殘餘 Joker 搶救機制)
   ------------------------------------------------------- */
void checkRunsWithJokers(const std::vector<Tile*>& color_tiles, std::vector<Tile*>& jokers, std::vector<std::vector<Tile*>>& result) {
    if (color_tiles.empty()) return;
    std::vector<Tile*> current_run;
    
    for (size_t i = 0; i < color_tiles.size(); ++i) {
        if (current_run.empty()) {
            current_run.push_back(color_tiles[i]);
        } else {
            int last_num = current_run.back()->isJoker() ?
                (current_run[current_run.size() - 2]->getNumber() +
                (int)(current_run.end() - std::find_if(current_run.rbegin(), current_run.rend(), [](Tile* t) { return !t->isJoker(); }).base()))
                : current_run.back()->getNumber();
            int current_num = color_tiles[i]->getNumber();
            
            if (current_num == last_num) {
                continue;
            }
            if (current_num == last_num + 1) {
                current_run.push_back(color_tiles[i]);
            }
            else {
                int gap = current_num - last_num - 1;
                if (gap > 0 && gap <= (int)jokers.size()) {
                    for (int g = 0; g < gap; ++g) {
                        current_run.push_back(jokers.back());
                        jokers.pop_back();
                    }
                    current_run.push_back(color_tiles[i]);
                }
                else {
                    while (current_run.size() < 3 && !jokers.empty()) {
                        current_run.push_back(jokers.back());
                        jokers.pop_back();
                    }
                    if (current_run.size() >= 3) {
                        result.push_back(current_run);
                    }
                    current_run.clear();
                    current_run.push_back(color_tiles[i]);
                }
            }
        }
    }
    
    while (current_run.size() < 3 && !jokers.empty()) {
        current_run.push_back(jokers.back());
        jokers.pop_back();
    }
    if (current_run.size() >= 3) {
        result.push_back(current_run);
    }
}
static int internalMeldScoreSafe(const std::vector<Tile*>& set) {
    int score = 0;
    for (Tile* t : set) {
        if (!t) continue;
        if (t->isJoker()) score += 10;
        else score += t->getNumber();
    }
    return score;
}
 
static bool internalSetConflictsWithUsed(const std::vector<Tile*>& set, const std::vector<Tile*>& used) {
    for (Tile* t : set) {
        if (std::find(used.begin(), used.end(), t) != used.end()) {
            return true;
        }
    }
    return false;
}
 
static std::vector<std::vector<Tile*>> internalFindGroupsWithJokersForMeld(const std::vector<Tile*>& tiles) {
    std::vector<std::vector<Tile*>> result;
    std::vector<Tile*> jokers;
    std::vector<std::vector<std::vector<Tile*>>> table(4, std::vector<std::vector<Tile*>>(14));
 
    for (Tile* t : tiles) {
        if (!t) continue;
        if (t->isJoker()) {
            jokers.push_back(t);
        } else {
            int c = static_cast<int>(t->getColor());
            int n = t->getNumber();
            if (c >= 0 && c < 4 && n >= 1 && n <= 13) {
                table[c][n].push_back(t);
            }
        }
    }
 
    for (int n = 13; n >= 1; --n) {
        std::vector<Tile*> normal_group;
        for (int c = 0; c < 4; ++c) {
            if (!table[c][n].empty()) {
                normal_group.push_back(table[c][n].back());
            }
        }
 
        if (normal_group.size() >= 3) {
            result.push_back(normal_group);
        }
 
        if (normal_group.size() == 2 && !jokers.empty()) {
            std::vector<Tile*> joker_group = normal_group;
            joker_group.push_back(jokers[0]);
            result.push_back(joker_group);
        }
 
        if (normal_group.size() == 1 && jokers.size() >= 2) {
            std::vector<Tile*> joker_group = normal_group;
            joker_group.push_back(jokers[0]);
            joker_group.push_back(jokers[1]);
            result.push_back(joker_group);
        }
    }
 
    return result;
}
/* -------------------------------------------------------
   【全新安全破冰版】tryInitialMeld(破冰三十點)
   ------------------------------------------------------- */
bool AIAgent_0::tryInitialMeld(Board& board) {
    std::vector<Tile*> my_hand = getHand();
    if (my_hand.empty()) return false;
 
    auto countJokersInSet = [](const std::vector<Tile*>& set) {
        int cnt = 0;
        for (Tile* t : set) {
            if (t && t->isJoker()) cnt++;
        }
        return cnt;
    };
 
    auto attemptMeld = [&](std::vector<std::vector<Tile*>> candidates) -> bool {
        std::sort(candidates.begin(), candidates.end(),
            [&](const std::vector<Tile*>& a, const std::vector<Tile*>& b) {
                int ja = countJokersInSet(a);
                int jb = countJokersInSet(b);
                if (ja != jb) return ja < jb; // baseline 先保留 Joker
                int sa = internalMeldScoreSafe(a);
                int sb = internalMeldScoreSafe(b);
                if (sa != sb) return sa > sb;
                return a.size() > b.size();
            }
        );
 
        std::vector<std::vector<Tile*>> proposed_meld;
        std::vector<Tile*> used_tiles;
        int accum_score = 0;
 
        for (const auto& set : candidates) {
            if (set.size() < 3) continue;
            if (internalSetConflictsWithUsed(set, used_tiles)) continue;
 
            proposed_meld.push_back(set);
            accum_score += internalMeldScoreSafe(set);
 
            for (Tile* t : set) {
                used_tiles.push_back(t);
            }
 
            if (accum_score >= 30) break;
        }
 
        if (accum_score >= 30 && !proposed_meld.empty()) {
            std::vector<std::vector<Tile*>> new_board_state = board.getSets();
            new_board_state.insert(new_board_state.end(), proposed_meld.begin(), proposed_meld.end());
 
            if (board.applyProposedSets(this, new_board_state) == Board::ApplyResult::Ok) {
                initial_meld_done = true;
                return true;
            }
        }
 
        return false;
    };
 
    // 第一層：完全不用 Joker 破冰，保留 baseline 爆發力
    std::vector<Tile*> normal_hand;
    for (Tile* t : my_hand) {
        if (t && !t->isJoker()) {
            normal_hand.push_back(t);
        }
    }
 
    std::vector<std::vector<Tile*>> normal_candidates = findRuns(normal_hand);
    std::vector<std::vector<Tile*>> normal_groups = findGroups(normal_hand);
    normal_candidates.insert(normal_candidates.end(), normal_groups.begin(), normal_groups.end());
 
    if (attemptMeld(normal_candidates)) {
        return true;
    }
 
    // 第二層：不用 Joker 破不了，才允許 Joker 參與 run
    std::vector<std::vector<Tile*>> joker_backup_candidates = findRuns(my_hand);
    std::vector<std::vector<Tile*>> joker_backup_groups = findGroups(my_hand);
    joker_backup_candidates.insert(joker_backup_candidates.end(), joker_backup_groups.begin(), joker_backup_groups.end());
 
    if (attemptMeld(joker_backup_candidates)) {
        return true;
    }
 
    return false;
}
/* -------------------------------------------------------
   【絕對安全防回退版】tryExtendBoard - 終極大風吹重組
   ------------------------------------------------------- */
int AIAgent_0::tryExtendBoard(Board& board) {
    // 💡 1. 隔離保護：一進來先完整保存 100% 合法的最初桌面狀態
    //先暫時刪掉std::vector<std::vector<Tile*>> absolute_backup_sets = board.getSets();
 
    int total_tiles_played = 0;
    bool tiles_were_played_this_round = true;
    int safety_counter = 0;
    
    // 階段一：基礎尾端追加貼牌
    while (tiles_were_played_this_round && safety_counter < 20) {
        safety_counter++;
        tiles_were_played_this_round = false;
        std::vector<Tile*> my_hand = getHand();
        std::vector<std::vector<Tile*>> current_sets = board.getSets();
        
        for (size_t i = 0; i < current_sets.size(); ++i) {
            std::vector<Tile*> am_set = current_sets[i];
            if (am_set.empty()) continue;
            bool is_run = true;
            Tile* first_normal = nullptr;
            Tile* second_normal = nullptr;
            
            for (size_t j = 0; j < am_set.size(); ++j) {
                if (!am_set[j]->isJoker()) {
                    if (first_normal == nullptr) first_normal = am_set[j];
                    else if (second_normal == nullptr) { second_normal = am_set[j]; break; }
                }
            }
            if (first_normal != nullptr && second_normal != nullptr) {
                if (first_normal->getColor() != second_normal->getColor()) {
                    is_run = false;
                }
            }
            
            for (size_t k = 0; k < my_hand.size(); ++k) {
                Tile* my_tile = my_hand[k];
                if (my_tile == nullptr) continue;
                bool played_this_tile = false;
                
            if (is_run) {
    Tile* tail = am_set.back();
    if (!tail->isJoker() && my_tile->getColor() == tail->getColor()) {
        if (my_tile->getNumber() == tail->getNumber() + 1 && my_tile->getNumber() <= 13) {
            am_set.push_back(my_tile);
            played_this_tile = true;
        }
    }
    // 💡 新增：檢查能不能接在順組「頭端」（數字減1），原本只檢查尾端
    if (!played_this_tile) {
        Tile* front = am_set.front();
        if (!front->isJoker() && my_tile->getColor() == front->getColor()) {
            if (my_tile->getNumber() == front->getNumber() - 1 && my_tile->getNumber() >= 1) {
                am_set.insert(am_set.begin(), my_tile);
                played_this_tile = true;
                std::cout << "👈 [頭端接牌] 成功把牌接在順組前面！\n";
            }
        }
    }
}
                else {
                    int group_num = (first_normal != nullptr) ? first_normal->getNumber() : 0;
                    if (group_num > 0 && my_tile->getNumber() == group_num && am_set.size() < 4) {
                        bool color_dup = false;
                        for (size_t m = 0; m < am_set.size(); ++m) {
                            if (!am_set[m]->isJoker() && am_set[m]->getColor() == my_tile->getColor()) {
                                color_dup = true;
                                break;
                            }
                        }
                        if (!color_dup) {
                            am_set.push_back(my_tile);
                            played_this_tile = true;
                        }
                    }
                }
                
                if (played_this_tile) {
                    std::vector<std::vector<Tile*>> single_propose = current_sets;
                    single_propose[i] = am_set;
                    if (board.applyProposedSets(this, single_propose) == Board::ApplyResult::Ok) {
                        total_tiles_played++;
                        tiles_were_played_this_round = true;
                        break;
                    } 
                }
            }
            if (tiles_were_played_this_round) break;
        }
    }
    std::vector<std::vector<Tile*>> safe_after_basic_extend_sets = board.getSets();//2~~
    // 統計桌面與手牌資源
    std::vector<Tile*> board_tiles_pool;
    std::vector<std::vector<Tile*>> current_board_sets = board.getSets();
    for (const auto& b_set : current_board_sets) {
        for (Tile* t : b_set) {
            if (t) board_tiles_pool.push_back(t);
        }
    }
    
    std::vector<Tile*> my_hand = getHand();
    std::vector<Tile*> global_pool = board_tiles_pool;
    for (Tile* h_tile : my_hand) {
        if (h_tile != nullptr) global_pool.push_back(h_tile);
    }
    
    // 階段三：全域重組
    std::vector<std::vector<Tile*>> fresh_solution;
    std::vector<Tile*> r_, b_, y_, c_, j_;
 
    /* =========================================================================
       【新增保護機制】桌面上原本就是合法 Group 的部分，整組保留不拆解
       原本的邏輯會把所有牌（包含桌面 Group）一律按顏色拆散重組，
       但 Group 本來就是「同數字不同色」，拆進顏色堆後很難剛好湊回合法組合，
       只要湊不回去，整次重組就會被安全鎖擋下、全部回滾。
       這裡先把桌面上的 Group 整組保留，不放進顏色堆，其餘（Run + 手牌）才照原本邏輯重組。
       ========================================================================= */
    std::vector<Tile*> protected_tiles;
    for (const auto& board_set : current_board_sets) {
        if (board_set.size() < 3) continue;
        Tile* first_normal = nullptr;
        Tile* second_normal = nullptr;
        for (Tile* t : board_set) {
            if (!t->isJoker()) {
                if (first_normal == nullptr) first_normal = t;
                else if (second_normal == nullptr) { second_normal = t; break; }
            }
        }
        bool set_is_run = true;
        if (first_normal != nullptr && second_normal != nullptr) {
            if (first_normal->getColor() != second_normal->getColor()) {
                set_is_run = false;
            }
        }
        if (!set_is_run) {
            // 這是 Group，整組保留，不拆解進顏色堆
            fresh_solution.push_back(board_set);
            for (Tile* t : board_set) protected_tiles.push_back(t);
        }
    }
    /* ========================================================================= */
 
    for (Tile* t : global_pool) {
        if (!t) continue;
        // 💡 已經被保護起來的 Group 牌，跳過，不重複丟進顏色堆
        if (std::find(protected_tiles.begin(), protected_tiles.end(), t) != protected_tiles.end()) continue;
        if (t->isJoker()) j_.push_back(t);
        else {
            if (t->getColor() == Color::RED) r_.push_back(t);
            else if (t->getColor() == Color::BLUE) b_.push_back(t);
            else if (t->getColor() == Color::YELLOW) y_.push_back(t);
            else if (t->getColor() == Color::BLACK) c_.push_back(t);
        }
    }
    
    auto sortByNumber = [](Tile* a, Tile* b) { return a->getNumber() < b->getNumber(); };
    std::sort(r_.begin(), r_.end(), sortByNumber);
    std::sort(b_.begin(), b_.end(), sortByNumber);
    std::sort(y_.begin(), y_.end(), sortByNumber);
    std::sort(c_.begin(), c_.end(), sortByNumber);
    
    checkRunsWithJokers(r_, j_, fresh_solution);
    checkRunsWithJokers(b_, j_, fresh_solution);
    checkRunsWithJokers(y_, j_, fresh_solution);
    checkRunsWithJokers(c_, j_, fresh_solution);
    
    // 保留 Olivia 的長龍切斷重組實作
    std::vector<std::vector<Tile*>> optimized_solution;
    for (size_t i = 0; i < fresh_solution.size(); ++i) {
        std::vector<Tile*> current_set = fresh_solution[i];
        bool is_run = false;
        if (current_set.size() >= 3) {
            Tile* t0 = current_set[0];
            Tile* t1 = current_set[1];
            if (t0 != nullptr && t1 != nullptr && !t0->isJoker() && !t1->isJoker() && t0->getColor() == t1->getColor()) {
                is_run = true;
            }
        }
        if (is_run && current_set.size() >= 6) {
            Color run_color = current_set[0]->getColor();
            int best_cut_point = -1;
            int max_hand_tiles_matching = 0;
            for (size_t cut = 3; cut <= current_set.size() - 3; ++cut) {
                int current_cut_matching = 0;
                int p1_head = current_set[0]->getNumber();
                int p1_tail = current_set[cut - 1]->getNumber();
                int p2_head = current_set[cut]->getNumber();
                int p2_tail = current_set.back()->getNumber();
                
                std::vector<bool> hand_tile_simulated_used(my_hand.size(), false);
                bool simulated_matched = true;
                while (simulated_matched) {
                    simulated_matched = false;
                    for (size_t h = 0; h < my_hand.size(); ++h) {
                        if (hand_tile_simulated_used[h] || my_hand[h] == nullptr || my_hand[h]->isJoker()) continue;
                        if (my_hand[h]->getColor() != run_color) continue;
                        int h_num = my_hand[h]->getNumber();
                        if (h_num == p1_head - 1 && p1_head > 1) {
                            p1_head--; current_cut_matching++;
                            hand_tile_simulated_used[h] = true; simulated_matched = true;
                            break;
                        }
                        if (h_num == p1_tail + 1 && p1_tail < 13) {
                            p1_tail++; current_cut_matching++;
                            hand_tile_simulated_used[h] = true; simulated_matched = true;
                            break;
                        }
                    }
                }
                
                simulated_matched = true;
                while (simulated_matched) {
                    simulated_matched = false;
                    for (size_t h = 0; h < my_hand.size(); ++h) {
                        if (hand_tile_simulated_used[h] || my_hand[h] == nullptr || my_hand[h]->isJoker()) continue;
                        if (my_hand[h]->getColor() != run_color) continue;
                        int h_num = my_hand[h]->getNumber();
                        if (h_num == p2_head - 1 && p2_head > 1) {
                            p2_head--; current_cut_matching++;
                            hand_tile_simulated_used[h] = true; simulated_matched = true;
                            break;
                        }
                        if (h_num == p2_tail + 1 && p2_tail < 13) {
                            p2_tail++; current_cut_matching++;
                            hand_tile_simulated_used[h] = true; simulated_matched = true;
                            break;
                        }
                    }
                }
                
                if (current_cut_matching > max_hand_tiles_matching) {
                    max_hand_tiles_matching = current_cut_matching;
                    best_cut_point = static_cast<int>(cut);
                }
            }
            if (best_cut_point != -1) {
                std::vector<Tile*> part1(current_set.begin(), current_set.begin() + best_cut_point);
                std::vector<Tile*> part2(current_set.begin() + best_cut_point, current_set.end());
                optimized_solution.push_back(part1);
                optimized_solution.push_back(part2);
            } else {
                optimized_solution.push_back(current_set);
            }
        } else {
            optimized_solution.push_back(current_set);
        }
    }
    fresh_solution = optimized_solution;
    
    // 餘牌湊群組
    std::vector<Tile*> remain;
    for (Tile* t : global_pool) {
        if (!t) continue;
        bool used = false;
        for (const auto& s : fresh_solution) {
            if (std::find(s.begin(), s.end(), t) != s.end()) { used = true; break; }
        }
        if (!used && !t->isJoker()) remain.push_back(t);
    }
    
    std::vector<std::vector<Tile*>> fresh_groups = findGroups(remain);
    fresh_solution.insert(fresh_solution.end(), fresh_groups.begin(), fresh_groups.end());
    
    /* =========================================================================
       【殘局 Joker 強塞救濟區塊】
       ========================================================================= */
    if (is_endgame_global && !j_.empty()) {
        for (auto& s : fresh_solution) {
            if (s.size() >= 3 && s.size() < 4 && !j_.empty()) {
                s.push_back(j_.back());
                j_.pop_back(); // 成功把手上扣 30 分的 Joker 消耗上桌！
            }
        }
    }
    /* ========================================================================= */
    
    // 過濾出長度 >= 3 的合法組合 (以下完全維持妳原本的邏輯)
    std::vector<std::vector<Tile*>> final_legal_board;
    for (const auto& proposed_set : fresh_solution) {
        if (proposed_set.size() >= 3) {
            final_legal_board.push_back(proposed_set);
        }
    } 
    
    // 💡【安全鎖】檢查原本在桌面的牌，是否「全部」都完美待在 >= 3 張的合法新牌組中
    bool all_board_tiles_safe = true;
    for (Tile* b_tile : board_tiles_pool) {
        bool still_exists = false;
        for (const auto& f_set : final_legal_board) {
            if (std::find(f_set.begin(), f_set.end(), b_tile) != f_set.end()) {
                still_exists = true;
                break;
            }
        }
        if (!still_exists) {
            // 只要有一張桌面老牌落單、湊不滿 3 張，這次大風吹直接宣告失敗！
            all_board_tiles_safe = false;
            break;
        }
    }
    
    // 計算最終留在桌上的總張數
    int final_board_tiles_count = 0;
    for (const auto& s : final_legal_board) {
        final_board_tiles_count += s.size();
    }
    
    // 💡 只有當「原本桌面的老牌全部安全」且「桌上總張數變多（代表打出新牌）」才上傳
    if (all_board_tiles_safe && final_board_tiles_count > (int)board_tiles_pool.size()) {
        if (board.applyProposedSets(this, final_legal_board) == Board::ApplyResult::Ok) {
            total_tiles_played += (final_board_tiles_count - board_tiles_pool.size());
            std::cout << "🔥 [心機收割] 大風吹重組完美成功，打出了 " << total_tiles_played << " 張手牌！\n";
            return total_tiles_played;
        }
    }
    
    // 🚨 任何一絲不完美，立刻強制回滾最初乾淨狀態，絕不上傳髒盤面給裁判，不跳重複指針紅字！
    board.applyProposedSets(this, safe_after_basic_extend_sets);
    //board.applyProposedSets(this, absolute_backup_sets);這行暫時替換成上面那一行！
    return total_tiles_played;
}
 
/* =========================================================================
   【隱藏內部統計與保留機制】
   ========================================================================= */
int internalCountMissing(const std::vector<Tile*>& hand) {
    int count = 0;
    std::vector<std::vector<int>> table(4, std::vector<int>(14, 0));
    std::vector<int> num_counts(14, 0);
    for (Tile* t : hand) {
        if (!t || t->isJoker()) continue;
        int c = static_cast<int>(t->getColor());
        int n = t->getNumber();
        if (n >= 1 && n <= 13 && c >= 0 && c < 4) {
            if (table[c][n] == 0) { 
                table[c][n] = 1;
                num_counts[n]++; 
            }
        }
    }
    for (int n = 1; n <= 13; ++n) { 
        if (num_counts[n] == 2) count++; 
    }
    for (int c = 0; c < 4; ++c) {
        for (int n = 1; n <= 12; ++n) {
            if (table[c][n] == 1 && table[c][n + 1] == 1) { 
                count++;
                n++; 
            }
            else if (n <= 11 && table[c][n] == 1 && table[c][n + 1] == 0 && table[c][n + 2] == 1) { 
                count++; 
            }
        }
    }
    return count;
}
 
bool internalCheckOverlap(const std::vector<Tile*>& hand) {
    std::vector<std::vector<int>> table(4, std::vector<int>(14, 0));
    for (Tile* t : hand) {
        if (!t || t->isJoker()) continue;
        table[static_cast<int>(t->getColor())][t->getNumber()] = 1;
    }
    for (int c = 0; c < 4; ++c) {
        for (int n = 1; n <= 13; ++n) {
            if (table[c][n] == 1) {
                int same_num = 0;
                for (int oc = 0; oc < 4; ++oc) {
                    if (table[oc][n] == 1) same_num++;
                }
                bool run_p = false;
                if (n >= 2 && table[c][n - 1] == 1) run_p = true;
                if (n <= 12 && table[c][n + 1] == 1) run_p = true;
                if (same_num >= 2 && run_p) return true;
            }
        }
    }
    return false;
}
 
void AIAgent_0::solveRummikub(std::vector<std::vector<Tile*>> current_table, std::vector<Tile*> remaining_hand, int joker_count) {
    return;
}
 
 
/* -------------------------------------------------------
   playTurn - 決策大腦端（對手連續抽牌 3 次反擊機制）
   ------------------------------------------------------- */
void AIAgent_0::playTurn(Board& board, int draw_pile_size) {
    std::cout << "\n--- " << name << "'s turn ---\n";
    
    /* =========================================================================
       【殘局開關與防污染重置區塊】
       ========================================================================= */
    is_endgame_global = (draw_pile_size == 0);//final change
    
    if (last_draw_pile_size == 0) {
        last_draw_pile_size = draw_pile_size;
    }
    
    if (draw_pile_size > last_draw_pile_size && last_draw_pile_size != 0) {
        enemy_draw_count = 0;
        overlap_retained = false;
        draw_cooldown = 0;
    }
    
    if (draw_pile_size < last_draw_pile_size) {
        enemy_draw_count++;
        std::cout << "👀 [戰術偵測] 對手正在囤牌/卡牌中！連續抽牌次數: " << enemy_draw_count << "\n";
    } else if (draw_pile_size > last_draw_pile_size) {
        enemy_draw_count = 0;
    } else {
        enemy_draw_count = 0;
    }
 
    last_draw_pile_size = draw_pile_size;
    
    if (!initial_meld_done) {
        if (tryInitialMeld(board)) return;
        return; 
    }
 
    std::vector<Tile*> my_hand = getHand();
 
    // 殘局暴風戰術分流
    if (is_endgame_global) {
        std::cout << "⚔ [殘局暴風模式啟動] 牌堆剩 " << draw_pile_size << " 張！解除囤牌限制，全力打牌消負分！\n";
        overlap_retained = false;
        draw_cooldown = 0;
 
        int total_played = 0;
        int safety = 0;
        while (safety++ < 10) {
            int played = tryExtendBoard(board);
            total_played += played;
            if (played <= 0) break;  // 打不動了，停止重複呼叫
        }
        std::cout << "🌪️ [殘局迴圈] 本回合總共透過重組打出 " << total_played << " 張手牌\n";
        return;
    }
 
    /* ========================================================================= */
 
    if (enemy_draw_count >= getEnemyDrawThreshold()) {  // 🎭 個性化：由子類別決定門檻
        std::cout << "⚔ [心機反擊] 敵人連續抽牌達 8 次！抓到卡牌空檔，立刻發動全域大風吹調動位置！\n";
        int result = tryExtendBoard(board);
        if (result == 0) {
            std::cout << "🔍 [除錯] tryExtendBoard 沒打出任何牌，目前手牌：";
            for (Tile* t : getHand()) {
                std::cout << "[" << (t->isJoker() ? "JOKER" : std::to_string(t->getNumber())) << "] ";
            }
            std::cout << "\n";
        }
        return;
    }
    
    if (draw_pile_size <= 5) {
        tryExtendBoard(board);
        return;
    }
    
    /* =========================================================================
       【常規局：心機囤牌與冷卻防守區塊】
       ========================================================================= */
    if (draw_pile_size <= getAntiStalemateThreshold()) {  // 🎭 個性化：由子類別決定門檻
        std::cout << "⚠️ [警告] 牌堆剩餘 " << draw_pile_size << " 張，接近殘局！放棄囤牌，全力進攻！\n";
        int result = tryExtendBoard(board);
        if (result == 0) {
            std::cout << "🔍 [除錯] tryExtendBoard 沒打出任何牌，目前手牌：";
            for (Tile* t : getHand()) {
                std::cout << "[" << (t->isJoker() ? "JOKER" : std::to_string(t->getNumber())) << "] ";
            }
            std::cout << "\n";
        }
        return;
    }
    
    if (internalCheckOverlap(my_hand) && !overlap_retained) {
        overlap_retained = true;
        draw_cooldown = getHoardCooldownTurns();  // 🎭 個性化：由子類別決定冷卻長度
        std::cout << "🤫 [心機牽制] 觸發重疊牌型，啟動囤牌戰術，冷卻 4 回合！\n";
        return; 
    }
    
    if (overlap_retained && draw_cooldown > 0) {
        draw_cooldown--;
        std::cout << "⏳ [戰術冷卻中] 剩餘冷卻回合: " << draw_cooldown << "\n";
        return; 
    }
    
    /* 診斷測試：先關閉安全防守，看勝率變化
    if (internalCountMissing(my_hand) >= 5) {
        std::cout << "🛡️ [安全防守] 缺牌過多，這局先不拆牌，安全 pass\n";
        return; 
    }
    */
    
    int result = tryExtendBoard(board);
    if (result == 0) {
        std::cout << "🔍 [除錯] tryExtendBoard 沒打出任何牌，目前手牌：";
        for (Tile* t : getHand()) {
            std::cout << "[" << (t->isJoker() ? "JOKER" : std::to_string(t->getNumber())) << "] ";
        }
        std::cout << "\n";
    }
} 