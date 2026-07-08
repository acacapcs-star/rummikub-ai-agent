#pragma once
#include <vector>
#include <string>
#include "tile.h"
#include "board.h"
#include "player.h"

// -------------------------------------------------------
// GameManager: owns the tile pool and orchestrates the
// complete game loop.
//
// Ownership model:
//   pool      – vector<Tile>   owns every Tile object.
//   draw_pile – vector<Tile*>  pointers into pool.
//   Players   – hold Tile* from pool (via Player::hand).
//   Board     – holds Tile* from pool (via Board::sets).
//
// Only GameManager (and Board, via friend) is allowed to
// mutate the draw pile or a player's hand.
// -------------------------------------------------------
class GameManager {
public:
    Board                board;
    std::vector<Player*> players;     // non-owning pointers
    int  current_player_idx;
    int  turn_number;
    bool game_over;
    std::string winner_name;

    // Where to write state.json (default = current directory).
    std::string state_file_path;

    GameManager();
    ~GameManager() = default;

    // Register a player (GameManager does NOT take ownership).
    void addPlayer(Player* player);

    // Build and shuffle the 106-tile pool, deal 14 tiles each.
    void initialize(unsigned int seed = 0);

    // Run the game until someone wins or the pool is exhausted.
    void run();

    // Write the current game state to state.json (or state_file_path).
    void exportState();

    // Print a human-readable summary to stdout.
    void printState() const;

private:
    void initializeTiles();
    void shufflePool(unsigned int seed);
    void dealTiles(int tiles_per_player = 14);
    bool checkWin(Player* player) const;
    std::string buildStateJSON() const;
    std::string buildStateHistoryJSON() const;
    std::string historyFilePath() const;
    void clearStateHistory();
    void computeAndAnnounceFinalScores();

    // All 106 tile objects live here for the game's lifetime.
    std::vector<Tile>   pool;
    std::vector<Tile*>  draw_pile;
    std::vector<std::string> state_history;

    // The result of the most recent agent submission (cleared every turn).
    // Surfaced in state.json as "last_action" so the visualizer can show
    // why a player's move was rejected.
    std::string last_action_player;
    std::string last_action_kind;     // "play" | "draw" | "skip"
    Board::ApplyResult last_action_result = Board::ApplyResult::Ok;
};
