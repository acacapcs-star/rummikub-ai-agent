#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include "game_manager.h"
// 【這裡改了】：拿掉了 baseline0 和 baseline1 的 include，避免編譯找不到檔案
#include "ai_agent_0.h"
#include "ai_agent_conservative.h"
#include "ai_agent_aggressive.h"
#include "human_agent.h"
#include "cognitive_coach_agent.h"

int main(int argc, char* argv[]) {
    std::string mode = "ai";
    if (argc >= 2) mode = argv[1];

    GameManager gm;
    std::vector<std::unique_ptr<Player>> ownedPlayers;

    auto makePlayerFromToken = [&](const std::string& t, int idx) -> std::unique_ptr<Player> {
        std::string name_suffix = "_" + std::to_string(idx);
        if (t == "human" || t == "h")      return std::make_unique<HumanAgent>("Human" + name_suffix);
        if (t == "coach")                  return std::make_unique<CognitiveCoachAgent>("Human_Coached" + name_suffix);
        
        // 【這裡改了】：只要助教的環境想呼叫 b0 或 b1，在妳的 Mac 上通通變成建立 AIAgent_0
        if (t == "b0" || t == "baseline0") return std::make_unique<AIAgent_0>("AI_baseline0" + name_suffix);
        if (t == "b1" || t == "baseline1") return std::make_unique<AIAgent_0>("AI_baseline1" + name_suffix);
        
        if (t == "0"  || t == "ai0")       return std::make_unique<AIAgent_0>("AI_0" + name_suffix);

        // 🎭 個性化子類別 token
        if (t == "cons" || t == "conservative") return std::make_unique<AIAgent_Conservative>("AI_Conservative" + name_suffix);
        if (t == "aggr" || t == "aggressive")    return std::make_unique<AIAgent_Aggressive>("AI_Aggressive" + name_suffix);

        return nullptr;
    };

    // --- 下面的程式碼都不用動，保留原本的即可 ---

    if (argc >= 3) {
        int count = 0;
        for (int i = 1; i < argc; ++i) {
            std::string tok = argv[i];
            auto up = makePlayerFromToken(tok, count);
            if (!up) {
                std::cerr << "Unknown player token: " << tok << "\n";
                std::cerr << "Usage examples: human b0   or   b0 0   or   cons aggr   or   coach b0\n";
                return 1;
            }
            gm.addPlayer(up.get());
            ownedPlayers.push_back(std::move(up));
            ++count;
        }
    } else if (mode == "human") {
        // human vs baseline0
        ownedPlayers.push_back(makePlayerFromToken("human", 0));
        ownedPlayers.push_back(makePlayerFromToken("b0", 1));
        gm.addPlayer(ownedPlayers[0].get());
        gm.addPlayer(ownedPlayers[1].get());
    } else if (mode == "ai4") {
        // 4 AI players: b0, b1, 0, 0
        ownedPlayers.push_back(makePlayerFromToken("b0", 0));
        ownedPlayers.push_back(makePlayerFromToken("b1", 1));
        ownedPlayers.push_back(makePlayerFromToken("0", 2));
        ownedPlayers.push_back(makePlayerFromToken("0", 3));
        for (auto &p : ownedPlayers) gm.addPlayer(p.get());
    } else {
        // Default: two AI players (baseline vs AI_0)
        ownedPlayers.push_back(makePlayerFromToken("b0", 0));
        ownedPlayers.push_back(makePlayerFromToken("0", 1));
        gm.addPlayer(ownedPlayers[0].get());
        gm.addPlayer(ownedPlayers[1].get());
    }

    try {
        gm.initialize();  // shuffle & deal
        gm.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
