#include "human_agent.h"
#include "validator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <stdexcept>
#include <cstdio>   // std::remove
HumanAgent::HumanAgent(const std::string& name) : Player(name) {}
void HumanAgent::sortRunSet(std::vector<Tile*>& tiles) {
    std::vector<Tile*> non_jokers, jokers;
    for (Tile* t : tiles) {
        (t->isJoker() ? jokers : non_jokers).push_back(t);
    }
    if (non_jokers.empty()) return;

    std::sort(non_jokers.begin(), non_jokers.end(),
        [](Tile* a, Tile* b) { return a->getNumber() < b->getNumber(); });

    std::vector<Tile*> result;
    std::size_t ji = 0;
    for (std::size_t i = 0; i < non_jokers.size(); ++i) {
        if (i > 0) {
            int gap = non_jokers[i]->getNumber() - non_jokers[i-1]->getNumber() - 1;
            for (int g = 0; g < gap && ji < jokers.size(); ++g, ++ji)
                result.push_back(jokers[ji]);
        }
        result.push_back(non_jokers[i]);
    }

    // Remaining jokers: append at right if still within [1,13], else prepend
    if (ji < jokers.size()) {
        int extra = static_cast<int>(jokers.size() - ji);
        if (non_jokers.back()->getNumber() + extra <= 13)
            while (ji < jokers.size()) result.push_back(jokers[ji++]);
        else
            result.insert(result.begin(), jokers.begin() + static_cast<int>(ji), jokers.end());
    }

    tiles = result;
}

/* -------------------------------------------------------
   TODO(L2): waitForActionFile
     Block until the file "action.json" appears on disk.
------------------------------------------------------- */
void HumanAgent::waitForActionFile() const {
    std::cout << "[HumanAgent] Waiting for action.json ...\n" << std::flush; // 強制沖刷
    while (true) {
        // TODO - START
        std::ifstream f("action.json");
        if (f.good()) {
            f.close();
            std::cout << std::flush; // 確保狀態有吐出去
            break; // 檔案找到了，立刻跳出迴圈！
        }
        // TODO - END
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

/* -------------------------------------------------------
   parseIntField
     Minimal JSON helper – extracts the integer value for
     a given key from a flat JSON string.
     e.g. parseIntField("{\"id\":7}", "id")  => 7
------------------------------------------------------- */
int HumanAgent::parseIntField(const std::string& json,
                               const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        throw std::runtime_error("Key not found: " + key);
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    size_t end = pos;
    if (json[end] == '-') ++end;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) ++end;
    return std::stoi(json.substr(pos, end - pos));
}

/* -------------------------------------------------------
   TODO(L2): applyActionFile
     Parse action.json and submit the move through the engine.
  
     {
       "action": "play" | "draw",
       "board": [              // new arrangement of ALL board tiles
         [ {"id": 5}, ... ],   // one set per inner array
         ...
       ]
     }
  
     "draw" is treated as a no-op: HumanAgent returns immediately
     and GameManager will draw a tile for the player as the
     fallback for any agent that didn't change the board.
  
     For "play", we look each tile id up among (current board ∪ our
     hand), build the proposed sets, and hand them to
     Board::applyProposedSets which performs all engine checks.
------------------------------------------------------- */
void HumanAgent::applyActionFile(Board& board) {
    std::ostringstream buf;
    // TODO - START
    std::ifstream f("action.json");
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open action.json");
    }
    buf << f.rdbuf();
    f.close();
    // TODO - END
    const std::string content = buf.str();

    // Treat "draw" as a no-op; GameManager will draw for us.
    if (content.find("\"draw\"") != std::string::npos) {
        std::cout << name << " chose to draw (no action).\n" << std::flush; // 強制沖刷
        return;
    }

    // Build id → Tile* lookup over the tiles the human is allowed to
    // touch this turn: current board tiles plus this player's hand.
    std::vector<Tile*> lookup = board.allTiles();
    for (Tile* t : getHand()) lookup.push_back(t);

    // Walk the JSON character-by-character to extract the nested board array.
    size_t board_pos = content.find("\"board\"");
    if (board_pos == std::string::npos) {
        throw std::runtime_error("action.json missing 'board' field");
    }
    size_t arr_start = content.find('[', board_pos);
    if (arr_start == std::string::npos) {
        throw std::runtime_error("action.json 'board' field malformed");
    }

    std::vector<std::vector<Tile*>> new_sets;
    std::vector<Tile*> current_set;

    size_t pos = arr_start + 1;
    int depth = 1;

    while (pos < content.size() && depth > 0) {
        char ch = content[pos];
        if (ch == '[') {
            depth++;
            if (depth == 2) current_set.clear();
        } else if (ch == ']') {
            depth--;
            if (depth == 1 && !current_set.empty()) {
                new_sets.push_back(current_set);
                current_set.clear();
            }
        } else if (ch == '{') {
            size_t obj_end = content.find('}', pos);
            if (obj_end == std::string::npos) break;
            std::string tile_json = content.substr(pos, obj_end - pos + 1);
            int id = parseIntField(tile_json, "id");

            auto it = std::find_if(lookup.begin(), lookup.end(),
                [id](Tile* t) { return t->getId() == id; });
            if (it != lookup.end()) {
                current_set.push_back(*it);
            }
            pos = obj_end;
        }
        ++pos;
    }

    // Sort each set into canonical run order before handing off.
    for (auto& s : new_sets) sortRunSet(s);

    // Hand off to the engine.  All ownership / validity / meld checks
    // happen inside applyProposedSets.
    Board::ApplyResult result = board.applyProposedSets(this, new_sets);
    if (result != Board::ApplyResult::Ok) {
        throw std::runtime_error(std::string("Proposed board rejected: ")
                                 + Board::describe(result));
    }

    std::cout << name << " plays move from action.json\n" << std::flush; // 強制沖刷
}
/* -------------------------------------------------------
   playTurn
     Wait for the human's action.json, then apply it.
     On any error the turn is skipped and GameManager will
     draw a tile on the human's behalf.
------------------------------------------------------- */
void HumanAgent::playTurn(Board& board, int /*draw_pile_size*/) {
    std::cout << "\n--- " << name << "'s turn (Human) ---\n";
    printHand();

    waitForActionFile();

    try {
        applyActionFile(board);
    } catch (const std::exception& e) {
        std::cerr << "[HumanAgent] Error: " << e.what()
                  << " – turn skipped.\n";
    }
    std::remove("action.json");
}
