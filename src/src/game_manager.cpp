#include "game_manager.h"
#include "coach_hint_bridge.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <stdexcept>
#include <cstdio>
#include <climits>

GameManager::GameManager()
    : current_player_idx(0), turn_number(0),
      game_over(false), state_file_path("state.json") {
    g_trigger_state_reexport = [this]() { exportState(); };
}

void GameManager::addPlayer(Player* player) {
    players.push_back(player);
}

/* -------------------------------------------------------
   initializeTiles
     Create 106 tiles: 2 sets of 1-13 in 4 colours (= 104)
     plus 2 jokers, stored contiguously in 'pool'.
     After construction, 'draw_pile' holds Tile* pointers
     to every element of 'pool'.
   ---------------------------------------------------- */
void GameManager::initializeTiles() {
    pool.reserve(106);
    int id = 0;

    for (int copy = 0; copy < 2; ++copy) {
        for (int c = 0; c < 4; ++c) {
            Color col = static_cast<Color>(c);
            for (int num = 1; num <= 13; ++num) {
                pool.emplace_back(id++, num, col);
            }
        }
    }
    pool.emplace_back(id++);   // joker 1
    pool.emplace_back(id++);   // joker 2

    draw_pile.clear();
    draw_pile.reserve(pool.size());
    for (Tile& t : pool) {
        draw_pile.push_back(&t);
    }
}

void GameManager::shufflePool(unsigned int seed) {
    std::mt19937 rng(seed ? seed : std::random_device{}());
    std::shuffle(draw_pile.begin(), draw_pile.end(), rng);
}

void GameManager::dealTiles(int tiles_per_player) {
    for (Player* p : players) {
        for (int i = 0; i < tiles_per_player; ++i) {
            if (draw_pile.empty()) {
                throw std::runtime_error("Not enough tiles to deal!");
            }
            p->hand.push_back(draw_pile.back());
            draw_pile.pop_back();
        }
    }
}

void GameManager::initialize(unsigned int seed) {
    if (players.empty()) {
        throw std::runtime_error("Add players before calling initialize()");
    }
    initializeTiles();
    shufflePool(seed);
    dealTiles();
}

bool GameManager::checkWin(Player* player) const {
    return player->getHand().empty();
}

/* -------------------------------------------------------
   run
     Main game loop: round-robin turns until someone wins
     or the draw pile and all hands are empty.
   ---------------------------------------------------- */
void GameManager::run() {
    std::cout << "=== Rummikub Game Start ===\n";
    std::cout << "Players: ";
    for (Player* p : players) std::cout << p->name << " ";
    std::cout << "\n\n";

    clearStateHistory();

    int no_action_streak = 0;

    while (!game_over) {
        ++turn_number;
        Player* current = players[current_player_idx];

        size_t hand_before  = current->getHand().size();
        size_t draw_before  = draw_pile.size();
        std::string board_before = board.toJSON();

        // Export state at the START of each turn.  last_action_* still holds
        // whatever the PREVIOUS turn's player did — that's the value we want
        // the visualizer to surface here.
        exportState();

        // Sample call count so we can tell whether the agent actually invoked
        // applyProposedSets this turn (vs. simply returning to skip/draw).
        const unsigned long calls_before = board.apply_call_count;

        current->playTurn(board, static_cast<int>(draw_pile.size()));

        size_t hand_after  = current->getHand().size();
        size_t draw_after  = draw_pile.size();
        std::string board_after = board.toJSON();

        bool board_changed  = (board_before != board_after);
        bool hand_changed   = (hand_before  != hand_after);
        bool draw_changed   = (draw_before  != draw_after);
        bool player_did_action = board_changed || hand_changed || draw_changed;

        const bool agent_tried_play   = (board.apply_call_count > calls_before);
        const Board::ApplyResult last = board.lastApplyResult();

        // Record what this player just did so the NEXT exportState surfaces it.
        last_action_player = current->name;
        if (board_changed) {
            last_action_kind   = "play";
            last_action_result = Board::ApplyResult::Ok;
        } else if (agent_tried_play && last != Board::ApplyResult::Ok) {
            last_action_kind   = "play";
            last_action_result = last;
        } else {
            last_action_kind   = "skip";
            last_action_result = Board::ApplyResult::Ok;
        }

        // If the player took no action, draw a tile for them.
        if (!player_did_action) {
            if (!draw_pile.empty()) {
                Tile* drawn = draw_pile.back();
                draw_pile.pop_back();
                current->hand.push_back(drawn);
                std::cout << current->name << " draws " << drawn->toString() << "\n";
                if (last_action_kind == "skip") last_action_kind = "draw";
            }
        }

        if (draw_pile.empty()) {
            if (player_did_action) no_action_streak = 0;
            else                   ++no_action_streak;
        } else {
            no_action_streak = 0;
        }

        printState();

        if (checkWin(current)) {
            game_over = true;
            computeAndAnnounceFinalScores();
            exportState();
            break;
        }

        bool all_empty = draw_pile.empty();
        if (all_empty) {
            for (Player* p : players) {
                if (!p->getHand().empty()) { all_empty = false; break; }
            }
        }
        if (all_empty) {
            game_over = true;
            std::cout << "\n=== No tiles left - game over! ===\n";
            computeAndAnnounceFinalScores();
            exportState();
            break;
        }

        if (draw_pile.empty() &&
            no_action_streak >= static_cast<int>(players.size())) {
            game_over = true;
            std::cout << "\n=== No actions in a full round with empty draw pile – game over! ===\n";
            computeAndAnnounceFinalScores();
            exportState();
            break;
        }

        current_player_idx =
            (current_player_idx + 1) % static_cast<int>(players.size());
    }
}

// -------------------------------------------------------
// buildStateJSON
//   Serialise the complete game state to a JSON string.
// -------------------------------------------------------
std::string GameManager::buildStateJSON() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"turn\": " << turn_number << ",\n";
    oss << "  \"current_player\": \"" << players[current_player_idx]->name << "\",\n";
    oss << "  \"game_over\": " << (game_over ? "true" : "false") << ",\n";

    if (game_over && !winner_name.empty()) {
        oss << "  \"winner\": \"" << winner_name << "\",\n";
    } else {
        oss << "  \"winner\": null,\n";
    }

    oss << "  \"draw_pile_count\": " << draw_pile.size() << ",\n";

    // 🎓 Section 3：教練提示（如果目前這位玩家是 CognitiveCoachAgent 才會有內容）
    if (!g_current_coach_hint.empty()) {
        std::string escaped;
        for (char c : g_current_coach_hint) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        oss << "  \"coach_hint\": \"" << escaped << "\",\n";
    } else {
        oss << "  \"coach_hint\": null,\n";
    }

    // Result of the previous turn's action.  null on turn 1.
    if (!last_action_player.empty()) {
        bool ok = (last_action_result == Board::ApplyResult::Ok);
        oss << "  \"last_action\": {"
            << "\"player\":\""  << last_action_player << "\","
            << "\"kind\":\""    << last_action_kind   << "\","
            << "\"ok\":"        << (ok ? "true" : "false") << ","
            << "\"reason\":\""  << Board::describe(last_action_result) << "\""
            << "},\n";
    } else {
        oss << "  \"last_action\": null,\n";
    }

    oss << "  \"players\": [\n";
    for (size_t i = 0; i < players.size(); ++i) {
        Player* p = players[i];
        oss << "    {\n";
        oss << "      \"name\": \"" << p->name << "\",\n";
        oss << "      \"hand_count\": " << p->getHand().size() << ",\n";
        oss << "      \"hand\": " << p->handToJSON() << ",\n";
        oss << "      \"initial_meld_done\": "
            << (p->initial_meld_done ? "true" : "false") << "\n";
        oss << "    }";
        if (i + 1 < players.size()) oss << ",";
        oss << "\n";
    }
    oss << "  ],\n";

    oss << "  \"board\": " << board.toJSON() << "\n";
    oss << "}\n";
    return oss.str();
}

std::string GameManager::buildStateHistoryJSON() const {
    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < state_history.size(); ++i) {
        oss << state_history[i];
        if (i + 1 < state_history.size()) {
            oss << ",\n";
        }
    }
    oss << "\n]\n";
    return oss.str();
}

std::string GameManager::historyFilePath() const {
    size_t sep = state_file_path.find_last_of("/\\");
    if (sep == std::string::npos) {
        return "state_history.json";
    }
    return state_file_path.substr(0, sep + 1) + "state_history.json";
}

void GameManager::clearStateHistory() {
    state_history.clear();
    const std::string path = historyFilePath();
    const std::string tmp = path + ".tmp";
    std::ofstream hf(tmp);
    if (!hf.is_open()) {
        std::cerr << "[GameManager] Warning: cannot write temp " << tmp << "\n";
        return;
    }
    hf << "[]\n";
    hf.close();
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::cerr << "[GameManager] Warning: rename failed for " << tmp << " -> " << path << "\n";
        std::ofstream hf2(path);
        if (hf2.is_open()) hf2 << "[]\n";
    }
}

void GameManager::computeAndAnnounceFinalScores() {
    std::cout << "\nFinal scores:\n";
    int best_score = INT_MAX;
    std::string best_player;
    for (Player* p : players) {
        int s = p->handScore();
        std::cout << p->name << ": " << s << "\n";
        if (s < best_score) { best_score = s; best_player = p->name; }
    }
    winner_name = best_player;
    std::cout << "\nWinner: " << winner_name << " (" << best_score << ")\n";
}

void GameManager::exportState() {
    std::string state_json = buildStateJSON();
    const std::string tmp = state_file_path + ".tmp";
    std::ofstream f(tmp);
    if (!f.is_open()) {
        std::cerr << "[GameManager] Warning: cannot write temp " << tmp << "\n";
        return;
    }
    f << state_json;
    f.close();
    if (std::rename(tmp.c_str(), state_file_path.c_str()) != 0) {
        std::cerr << "[GameManager] Warning: rename failed for " << tmp << " -> " << state_file_path << "\n";
        std::ofstream f2(state_file_path);
        if (f2.is_open()) {
            f2 << state_json;
            f2.close();
        }
    } else {
        std::cout << "[GameManager] State exported to " << state_file_path << "\n";
    }

    state_history.push_back(state_json);

    const std::string hist_path = historyFilePath();
    const std::string hist_tmp  = hist_path + ".tmp";
    std::ofstream hf(hist_tmp);
    if (!hf.is_open()) {
        std::cerr << "[GameManager] Warning: cannot write temp " << hist_tmp << "\n";
        return;
    }
    hf << buildStateHistoryJSON();
    hf.close();
    if (std::rename(hist_tmp.c_str(), hist_path.c_str()) != 0) {
        std::cerr << "[GameManager] Warning: rename failed for " << hist_tmp << " -> " << hist_path << "\n";
        std::ofstream hf2(hist_path);
        if (hf2.is_open()) hf2 << buildStateHistoryJSON();
    }
}

void GameManager::printState() const {
    std::cout << "Turn " << turn_number << " | Draw pile: "
              << draw_pile.size() << " tiles\n";
    std::cout << "Board:\n";
    board.print();
    for (Player* p : players) {
        p->printHand();
    }
    std::cout << "\n";
}
