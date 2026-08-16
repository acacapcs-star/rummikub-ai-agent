#pragma once
#include "human_agent.h"
#include "cognitive_hint_engine.h"
#include "coach_hint_bridge.h"
#include "coach_campaign.h"
#include "technique_detector.h"
#include "board.h"
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cstdio>

/* =========================================================================
   CognitiveCoachAgent —— 把教練型 AI 真正接進可以玩的回合流程

   繼承自 HumanAgent，重用它既有的檔案輪詢／解析邏輯（waitForActionFile、
   applyActionFile 為 protected），在玩家動作的前後各插入一步：
   動作之前決定要不要開口、開口到哪一層；
   動作之後反推玩家用了哪一招、記進關卡進度。

   ── 這一版改了什麼，以及為什麼 ────────────────────────

   上一版用的是 CognitiveHintEngine::tierFromStuckTurns()：一條固定的
   三段門檻（卡 2 回合指方向、卡 4 回合給答案），跟關卡無關，而且
   **永遠會回傳一個層級**——沒有「不開口」這個選項。

   結果是 CoachCampaign 的設計在實際對局裡一次都沒生效：
   六關的遞減曲線沒有用上，max_tier 上限沒有用上，
   「Level 6 卡再久也只給輕推」這個承諾沒有用上，
   而「該閉嘴的時候閉嘴」——這個作品最核心的主張——從來沒有發生過。

   這一版改成問 campaign_.shouldGiveHint()。它回傳 false 就是真的什麼都不印，
   連信箱都清空，讓前端也跟著安靜。沉默是這裡唯一新增的「功能」，
   但它才是重點：教練型 AI 跟對戰型 AI 的差別不在會不會解題，
   在知不知道現在不該說話。

   ── 「看過提示」怎麼算 ────────────────────────────────

   CoachCampaign 判斷過關時要知道玩家這一手是不是靠看答案做出來的。
   這裡採用的定義是：**從上一次成功出牌到現在，這段卡關期間看過的最深層級**。

   理由是玩家卡了五回合、第四回合看到答案、第五回合才動手，
   那一手當然算「看了答案才做出來的」——提示不會因為隔了一回合就失效。
   所以 saw_reveal_ / saw_point_ 累加整段卡關期，出牌成功後才歸零。

   ── 已知限制 ─────────────────────────────────────────

   1. 卡關偵測看的是「整個桌面有沒有變化」，不是精確地只針對這個玩家本人。
      1 對 1 對局下兩者等價；4 人局的語意需要調整。（沿用上一版的限制）

   2. 過關後的 5 題 MCQ 走 g_current_recap 信箱 + action.json 的
      {"action":"answer","choice":N} 回覆。因為舊版前端不認得這個協定，
      預設是關閉的：要設環境變數 COACH_RECAP=1 才會進入互動作答，
      否則只把題目印在終端機、不等待輸入，舊前端不會卡死在等答案。
      逾時（預設 180 秒，COACH_RECAP_TIMEOUT 可調）也會放棄複習繼續遊戲。
      前端顯示 recap 的部分尚未實作，這是缺口。

   3. 技巧偵測沿用 TechniqueDetector 的保守判準（寧可漏判不可誤判），
      所以玩家實際用出的招數可能比記錄下來的多。少記一次進度，
      玩家再用一次就補回來；誤記則會讓他在沒學會的情況下被判過關。
   ========================================================================= */
class CognitiveCoachAgent : public HumanAgent {
public:
    explicit CognitiveCoachAgent(const std::string& name) : HumanAgent(name) {}

    void playTurn(Board& board, int draw_pile_size) override {
        (void)draw_pile_size;

        std::cout << "\n--- " << name << "'s turn (Cognitive Coach Mode) ---\n";
        printLevelBanner();
        printHand();

        // ── 1. 卡關幾回合了 ─────────────────────────────
        std::string current_snapshot = board.toJSON();
        if (!last_snapshot_.empty() && current_snapshot == last_snapshot_) {
            stuck_turns_++;
        } else {
            stuck_turns_ = 0;
        }
        last_snapshot_ = current_snapshot;

        // ── 2. 這一關，現在，該不該開口 ─────────────────
        //
        // 這一行就是整個改動的核心。回傳 false 代表「這一關的設計
        // 就是要玩家自己想」，不是系統壞掉，也不是找不到解——
        // 引擎照樣找得到，只是不說。
        HintTier tier;
        if (campaign_.shouldGiveHint(stuck_turns_, tier)) {
            // 破冰前後關心的是不同的問題：還沒破冰時該想的是
            // 「湊不湊得到 30 分」，不是「能不能接桌面的 Run」。
            std::string hint = initial_meld_done
                ? CognitiveHintEngine::generateHint(getHand(), board.getSets(), tier)
                : CognitiveHintEngine::generateMeldHint(getHand(), tier);

            std::cout << "\n💡 [教練提示] " << hint << "\n";
            g_current_coach_hint = hint;

            if (tier == HintTier::REVEAL_MOVE)  saw_reveal_ = true;
            if (tier == HintTier::POINT_TO_AREA) saw_point_ = true;
        } else {
            // 沉默。信箱也要清掉，否則前端會繼續顯示上一輪的提示，
            // 畫面上就看不出「這一關不說話」這件事。
            g_current_coach_hint.clear();
        }
        if (g_trigger_state_reexport) g_trigger_state_reexport();

        // ── 3. 出牌前拍快照 ─────────────────────────────
        MoveSnapshot snap;
        snap.board_before = board.getSets();   // 複製外層結構，Tile* 指向同一批牌
        snap.hand_before  = getHand();
        snap.initial_meld_done_before = initial_meld_done;

        // ── 4. 重用 HumanAgent 既有的檔案輪詢與提交邏輯 ──
        waitForActionFile();
        try {
            applyActionFile(board);
        } catch (const std::exception& e) {
            std::cerr << "[CognitiveCoachAgent] Error: " << e.what()
                      << " – turn skipped.\n";
        }
        std::remove("action.json");

        // ── 5. 出牌後拍快照，反推用了哪一招 ─────────────
        snap.board_after = board.getSets();
        snap.hand_after  = getHand();

        recordTechniques(snap);
    }

private:
    CoachCampaign campaign_;
    std::string last_snapshot_;
    int stuck_turns_ = 0;

    // 這一段卡關期間看過的最深提示層級。出牌成功後歸零。
    bool saw_reveal_ = false;
    bool saw_point_  = false;

    // ── 關卡狀態列 ───────────────────────────────────────
    void printLevelBanner() const {
        const LevelConfig& cfg = campaign_.currentConfig();
        const TechniqueProgress& p = campaign_.progressOf(cfg.technique);

        std::cout << "[第 " << cfg.level << " 關] " << cfg.name
                  << "  引導 " << cfg.guidance_percent << "%"
                  << "  進度 " << p.total_uses << "/" << cfg.required_uses
                  << "（自主 " << p.unassisted_uses << "/"
                  << cfg.required_unassisted << "）";

        std::string stars = CoachCampaign::masteryStars(p.mastery);
        if (!stars.empty()) std::cout << "  " << stars;
        std::cout << "\n" << cfg.description << "\n";
    }

    // ── 偵測 → 記錄 → 星等 → 過關 ───────────────────────
    void recordTechniques(const MoveSnapshot& snap) {
        std::vector<Technique> used = TechniqueDetector::detect(snap);
        if (used.empty()) return;   // 只抽牌或什麼都沒做，卡關計數留著

        for (Technique t : used) {
            Mastery before = campaign_.progressOf(t).mastery;

            TechniqueUse use;
            use.technique  = t;
            use.saw_reveal = saw_reveal_;
            use.saw_point  = saw_point_;
            campaign_.recordUse(use);

            Mastery after = campaign_.progressOf(t).mastery;
            std::cout << "  o " << CoachCampaign::techniqueName(t);
            if (after != before) {
                std::cout << "  -> " << CoachCampaign::masteryStars(after)
                          << masteryLabel(after);
            }
            std::cout << "\n";
        }

        // 成功出牌了，提示的效力到此為止，下一段卡關重新算。
        saw_reveal_ = false;
        saw_point_  = false;

        if (campaign_.canAdvance()) announceLevelUp();
    }

    void announceLevelUp() {
        const LevelConfig& done = campaign_.currentConfig();
        std::cout << "\n[過關] 第 " << done.level << " 關「" << done.name
                  << "」達成——用出 " << done.required_uses << " 次，其中 "
                  << done.required_unassisted << " 次沒看答案。\n";

        runRecap(done.level, done.name);

        if (campaign_.advance()) {
            const LevelConfig& next = campaign_.currentConfig();
            std::cout << "-> 進入第 " << next.level << " 關：" << next.name
                      << "  引導降到 " << next.guidance_percent << "%\n";
            if (next.reveal_after_turns < 0) {
                std::cout << "   這一關開始不再給出具體答案，卡再久也一樣。\n";
            }
        } else {
            std::cout << "-> 六關全部完成。之後的提示層級維持在最後一關的設定。\n";
        }
        std::cout << "\n";
    }

    // ── 過關複習：5 題 MCQ ───────────────────────────────
    //
    // 答錯第一次給 hint、第二次之後給 explanation，然後**繼續問到答對為止**。
    // 刻意不做「看完解答就跳下一題」：那樣玩家可以一路亂按看完五題解答，
    // 複習就變成翻答案本。解答看過還是要自己選一次。
    void runRecap(int level, const std::string& level_name) {
        std::vector<McqQuestion> qs = CoachCampaign::recapFor(level);
        if (qs.empty()) return;

        if (!recapEnabled()) {
            // 舊前端不認得 recap 協定，只印在終端機、不等作答。
            std::cout << "（本關 " << qs.size()
                      << " 題複習未啟用互動作答，設 COACH_RECAP=1 開啟）\n";
            for (std::size_t i = 0; i < qs.size(); ++i)
                std::cout << "  " << (i + 1) << ". " << qs[i].prompt << "\n";
            return;
        }

        for (std::size_t i = 0; i < qs.size(); ++i) {
            int attempt = 1;
            std::string feedback;

            while (true) {
                g_current_recap = buildRecapJSON(level, level_name, i, qs.size(),
                                                 attempt, qs[i], feedback);
                if (g_trigger_state_reexport) g_trigger_state_reexport();

                std::cout << "  [複習 " << (i + 1) << "/" << qs.size() << "] "
                          << qs[i].prompt << "\n";
                for (std::size_t k = 0; k < qs[i].options.size(); ++k)
                    std::cout << "    " << k << ") " << qs[i].options[k].text << "\n";

                int choice = -1;
                if (!waitForAnswer(choice)) {
                    std::cout << "  （等不到作答，略過本關複習）\n";
                    clearRecap();
                    return;
                }

                CoachCampaign::AnswerResult r =
                    CoachCampaign::judge(qs[i], choice, attempt);

                if (r == CoachCampaign::AnswerResult::CORRECT) {
                    std::cout << "  正確。" << qs[i].explanation << "\n\n";
                    break;
                }
                if (r == CoachCampaign::AnswerResult::WRONG_FIRST_TRY) {
                    feedback = qs[i].hint;          // 第一次只給提示，不給答案
                    std::cout << "  再想一次：" << feedback << "\n";
                } else {
                    feedback = qs[i].explanation;   // 第二次之後給完整解釋
                    std::cout << "  " << feedback << "\n";
                }
                ++attempt;
            }
        }

        clearRecap();
        std::cout << "  本關複習完成。\n";
    }

    void clearRecap() {
        g_current_recap.clear();
        if (g_trigger_state_reexport) g_trigger_state_reexport();
    }

    static bool recapEnabled() {
        const char* v = std::getenv("COACH_RECAP");
        return v && std::string(v) == "1";
    }

    static int recapTimeoutSeconds() {
        const char* v = std::getenv("COACH_RECAP_TIMEOUT");
        if (!v) return 180;
        int n = std::atoi(v);
        return n > 0 ? n : 180;
    }

    // 等待 action.json 出現且含 "choice"。逾時回傳 false 讓遊戲繼續，
    // 不讓一場對局因為沒人作答而永遠停住。
    static bool waitForAnswer(int& out_choice) {
        const int timeout = recapTimeoutSeconds();
        for (int elapsed = 0; elapsed < timeout * 2; ++elapsed) {
            std::ifstream f("action.json");
            if (f.good()) {
                std::ostringstream buf;
                buf << f.rdbuf();
                f.close();
                std::string content = buf.str();
                std::remove("action.json");

                int choice;
                if (extractChoice(content, choice)) {
                    out_choice = choice;
                    return true;
                }
                // 收到的不是作答（可能是誤送的出牌指令）——丟掉繼續等，
                // 不當機、也不把它當成答案。
                std::cout << "  （收到非作答的 action.json，已忽略）\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return false;
    }

    static bool extractChoice(const std::string& json, int& out) {
        const std::string key = "\"choice\"";
        std::size_t pos = json.find(key);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        std::size_t start = pos;
        if (pos < json.size() && json[pos] == '-') ++pos;
        while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
        if (start == pos) return false;
        out = std::atoi(json.substr(start, pos - start).c_str());
        return true;
    }

    static std::string escapeJSON(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    }

    static std::string buildRecapJSON(int level, const std::string& level_name,
                                      std::size_t index, std::size_t total,
                                      int attempt, const McqQuestion& q,
                                      const std::string& feedback) {
        std::ostringstream o;
        o << "{"
          << "\"level\":" << level << ","
          << "\"level_name\":\"" << escapeJSON(level_name) << "\","
          << "\"question_index\":" << index << ","
          << "\"question_total\":" << total << ","
          << "\"attempt\":" << attempt << ","
          << "\"prompt\":\"" << escapeJSON(q.prompt) << "\","
          << "\"options\":[";
        for (std::size_t i = 0; i < q.options.size(); ++i) {
            if (i) o << ",";
            o << "\"" << escapeJSON(q.options[i].text) << "\"";
        }
        o << "],";
        if (feedback.empty()) o << "\"feedback\":null";
        else                  o << "\"feedback\":\"" << escapeJSON(feedback) << "\"";
        o << "}";
        return o.str();
    }

    static std::string masteryLabel(Mastery m) {
        switch (m) {
            case Mastery::COPIED:     return " 照做";
            case Mastery::PROMPTED:   return " 提點";
            case Mastery::DISCOVERED: return " 自行發掘";
            case Mastery::LOCKED:     return "";
        }
        return "";
    }
};
