#pragma once
#include "audience_profile.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

/* =========================================================================
   companions.h —— 三個角色

   同一個引擎，三種不同時機出現的角色。它們的差別不在語氣，
   在**誰觸發、給什麼、以及界線在哪**：

   ┌──────┬────────────┬──────────────┬────────────────┐
   │      │ 誰觸發     │ 給什麼       │ 界線           │
   ├──────┼────────────┼──────────────┼────────────────┤
   │ 引路 │ 系統       │ 三層提示     │ 不出牌         │
   │ 見證 │ 系統       │ 認出你用的招 │ 事後，不干預   │
   │ 回望 │ **玩家**   │ 你以前用過的 │ 只講規則不指牌 │
   └──────┴────────────┴──────────────┴────────────────┘

   ── 回望為什麼特別 ──────────────────────────────────────

   它是唯一一個由玩家主動叫出來的，而且它給的不是「這一手的答案」，
   是「你以前怎麼做的」。

       「你上次遇到類似的局面，用過大風吹重組。
         那一招是：把桌上的組合拆開重排，讓手上的牌接得上。
         你用過 3 次。要試試看嗎？」

   注意它做了三件事，但沒有一件是解題：
     ① 依局面相似度挑出一招
     ② 重述那一招的**規則**（不是這一手的答案）
     ③ 報告使用次數

   **它從頭到尾沒說哪張牌。**

   這跟引路的差別不只是內容，是心理狀態：
   引路是「系統發現你卡住了」，回望是「你自己想找靈感」。
   後者保留了主動權，而主動權正是這個系統想守住的東西。
   ========================================================================= */

enum class Companion { GUIDE, WITNESS, RECALL };

// ── 每個族群的角色設定 ───────────────────────────────────
struct CompanionPersona {
    Companion role;
    Audience audience;
    std::string code_name;        // 內部代號
    std::string display_name;     // 顯示用（暫定，之後可改）
    std::string appearance;       // 形象
    std::string tone;             // 語氣
    bool enabled;
};

class Companions {
public:
    static const std::vector<CompanionPersona>& all() {
        static const std::vector<CompanionPersona> P = {
            // ── 兒童版 ──
            { Companion::GUIDE, Audience::KIDS, "kid_guide", "小燈",
              "一盞會飄的小燈籠，只照亮一小塊地方",
              "好奇、不急——「欸，這邊好像有東西」", true },
            { Companion::WITNESS, Audience::KIDS, "kid_witness", "印章仙",
              "拿著印章的小精靈，認出招數就蓋一個章",
              "興奮、誇張——「這招我認得！蓋章！」", true },
            { Companion::RECALL, Audience::KIDS, "kid_recall", "筆記人",
              "抱著一本厚筆記本的小人，會翻到某一頁指給你看",
              "翻書的語氣——「等等，你上次⋯⋯找到了！」", true },

            // ── 長者版 ──
            { Companion::GUIDE, Audience::SENIORS, "senior_guide", "老友",
              "坐在旁邊一起看牌的人，不會湊過來",
              "從容、不催——「我們一起看看這裡」", true },
            /* 長者版的見證不是成就系統。

               「收集徽章」對兒童有效，因為那是外在動機；
               但對長者，**單純被看見就夠了**——
               而且成就系統暗示著「你還沒收集完」，那是一種壓力。            */
            { Companion::WITNESS, Audience::SENIORS, "senior_witness", "點頭",
              "只是點頭示意，不做別的",
              "簡短的肯定——「對，就是這樣」", true },
            { Companion::RECALL, Audience::SENIORS, "senior_recall", "舊照片",
              "一張泛黃的照片，翻過來背面寫著那一招",
              "回憶的語氣——「你以前這樣做過」", true },
        };
        return P;
    }

    static const CompanionPersona& get(Companion c, Audience a) {
        for (const auto& p : all())
            if (p.role == c && p.audience == a) return p;
        return all()[0];
    }
};

/* =========================================================================
   對手風格偵測

   看對手最近幾回合的行為，判斷他在打什麼。
   用滑動視窗而不是全場統計——**對手的策略會隨局勢改變**，
   全場平均會把「前期保守、後期爆發」抹平成「穩健」。

   只有兒童版啟用。長者版的重點是維持認知功能，不是競技；
   多一個要追蹤的維度只會增加認知負荷。
   ========================================================================= */
enum class OpponentStyle {
    UNKNOWN,      // 還沒看夠回合數
    AGGRESSIVE,   // 常重組桌面、單手出多張
    CONSERVATIVE, // 常抽牌、只出安全的
    HOARDING,     // 手牌一直增加、不太出
    STEADY        // 穩定出牌、不冒險
};

// 對手一回合的行為
struct OpponentTurn {
    int tiles_played = 0;
    bool drew = false;
    bool touched_board = false;
    int hand_size_after = 0;
};

class OpponentReader {
public:
    static const int WINDOW = 8;      // 滑動視窗大小

    void record(const OpponentTurn& t) {
        history_.push_back(t);
        if (static_cast<int>(history_.size()) > WINDOW) history_.pop_front();
    }

    int observedTurns() const { return static_cast<int>(history_.size()); }

    OpponentStyle style() const {
        // 看不夠回合就別亂猜——**過早的判斷比不判斷更糟**，
        // 因為它會讓角色給出誤導的建議。
        if (static_cast<int>(history_.size()) < 4) return OpponentStyle::UNKNOWN;

        int draws = 0, board_touches = 0, total_played = 0;
        int hand_start = history_.front().hand_size_after;
        int hand_end   = history_.back().hand_size_after;

        for (const auto& t : history_) {
            if (t.drew) ++draws;
            if (t.touched_board) ++board_touches;
            total_played += t.tiles_played;
        }
        int n = static_cast<int>(history_.size());
        double draw_rate = static_cast<double>(draws) / n;
        double avg_played = static_cast<double>(total_played) / n;
        double touch_rate = static_cast<double>(board_touches) / n;

        // 手牌持續增加而且很少出牌 → 囤積
        if (hand_end > hand_start + 2 && avg_played < 0.8)
            return OpponentStyle::HOARDING;

        // 常動桌面或單手出很多 → 積極
        if (touch_rate >= 0.35 || avg_played >= 2.5)
            return OpponentStyle::AGGRESSIVE;

        // 抽牌比例高 → 保守
        if (draw_rate >= 0.5)
            return OpponentStyle::CONSERVATIVE;

        return OpponentStyle::STEADY;
    }

    static std::string styleName(OpponentStyle s) {
        switch (s) {
            case OpponentStyle::AGGRESSIVE:   return "積極型";
            case OpponentStyle::CONSERVATIVE: return "保守型";
            case OpponentStyle::HOARDING:     return "囤積型";
            case OpponentStyle::STEADY:       return "穩健型";
            default:                          return "還在觀察";
        }
    }

    /* 依對手風格，角色該加一句什麼。

       這一句是**戰術層面的提醒**，不是解題提示——
       它不會告訴你出哪張牌，只告訴你現在的局勢節奏。            */
    static std::string advice(OpponentStyle s) {
        switch (s) {
            case OpponentStyle::AGGRESSIVE:
                return "他一直在動桌面，牌會越來越少——你要快。";
            case OpponentStyle::CONSERVATIVE:
                return "他好像卡住了，現在重組桌面他來不及用。";
            case OpponentStyle::HOARDING:
                return "他手上牌越來越多，在等大的——別留好接的位置給他。";
            case OpponentStyle::STEADY:
                return "";      // 穩健型不特別提醒
            default:
                return "";
        }
    }

    void reset() { history_.clear(); }

private:
    std::deque<OpponentTurn> history_;
};

/* =========================================================================
   回望：從歷史紀錄挑一招

   相似度用局面特徵算。特徵刻意選「結構性」的而不是「內容性」的——
   桌面有幾組、最長的順子多長、手上有沒有 Joker⋯⋯
   而不是「有沒有紅 5」。

   因為玩家記得的是**局面的形狀**，不是具體哪張牌。
   ========================================================================= */
struct BoardFingerprint {
    int set_count = 0;          // 桌面組數
    int longest_run = 0;        // 最長順子的長度
    int hand_size = 0;          // 手牌數
    bool has_joker = false;     // 手上有沒有 Joker
    int colors_on_board = 0;    // 桌面出現幾種顏色
    int playable_count = 0;     // 目前有幾張接得上

    // 兩個局面的相似度 0–1
    double similarityTo(const BoardFingerprint& o) const {
        double diff = 0.0;
        // 每一項先正規化再算差距，避免手牌數（0–20）壓過組數（0–8）
        diff += std::abs(set_count - o.set_count) / 8.0;
        diff += std::abs(longest_run - o.longest_run) / 13.0;
        diff += std::abs(hand_size - o.hand_size) / 20.0;
        diff += (has_joker != o.has_joker) ? 1.0 : 0.0;
        diff += std::abs(colors_on_board - o.colors_on_board) / 4.0;
        diff += std::abs(playable_count - o.playable_count) / 10.0;
        double avg = diff / 6.0;
        return 1.0 - std::min(1.0, avg);
    }
};

// 一筆歷史紀錄
struct TechniqueMemory {
    int technique = 0;
    BoardFingerprint context;   // 當時的局面
    int times_used = 0;         // 這一招總共用了幾次
    int cycle_recorded = 0;     // 在第幾輪記下的
};

struct RecallSuggestion {
    bool found = false;
    int technique = 0;
    double similarity = 0.0;
    int times_used = 0;
    std::string rule_text;      // 重述規則，不是答案
    std::string opening;        // 角色的開場白
};

class RecallCompanion {
public:
    explicit RecallCompanion(Audience a) : audience_(a) {}

    // 玩家用出一招時記下來
    void remember(int technique, const BoardFingerprint& fp, int cycle) {
        for (auto& m : memories_)
            if (m.technique == technique) {
                ++m.times_used;
                m.context = fp;              // 記最近一次的局面
                m.cycle_recorded = cycle;
                return;
            }
        memories_.push_back({ technique, fp, 1, cycle });
    }

    /* 玩家主動求助時，挑一招建議。

       挑的依據是**局面相似度**——找出「你以前在最像的局面用過哪一招」。

       相似度低於門檻就不建議。硬要給一個不相關的招數，
       比不給更糟：它會讓玩家往錯的方向想，
       而那個時間成本是玩家付的。                                  */
    RecallSuggestion suggest(const BoardFingerprint& now,
                             double threshold = 0.6) const {
        RecallSuggestion s;

        /* 被叫出來就要說話。

           早期版本在「沒有紀錄」與「不夠像」兩種情況直接 return，
           結果角色跳出來卻一句話都沒有——**那看起來像壞掉，不像克制。**

           沉默是一種設計選擇，但那要用在「系統判斷現在不該開口」的時候。
           這裡是玩家主動叫它出來的，不回應就只是失禮。               */
        if (memories_.empty()) {
            s.opening = noRecordLine();
            return s;
        }

        double best = -1.0;
        const TechniqueMemory* pick = nullptr;
        for (const auto& m : memories_) {
            double sim = m.context.similarityTo(now);
            if (sim > best) { best = sim; pick = &m; }
        }

        if (!pick || best < threshold) {
            s.similarity = best < 0 ? 0.0 : best;
            s.opening = noMatchLine();
            return s;
        }

        s.found = true;
        s.technique = pick->technique;
        s.similarity = best;
        s.times_used = pick->times_used;
        s.rule_text = ruleOf(pick->technique);
        s.opening = openingLine();
        return s;
    }

    int rememberedCount() const { return static_cast<int>(memories_.size()); }

    // 還沒有任何紀錄
    std::string noRecordLine() const {
        return audience_ == Audience::SENIORS
             ? "還沒有可以翻的紀錄，我們先玩玩看。"
             : "筆記本還是空的，先去累積幾招吧。";
    }

    // 有紀錄但都不夠像
    std::string noMatchLine() const {
        return audience_ == Audience::SENIORS
             ? "這次的牌跟以前都不太一樣，我們重新想想。"
             : "翻遍了也沒有很像的局面，這次要自己開路了。";
    }

    int timesUsed(int technique) const {
        for (const auto& m : memories_)
            if (m.technique == technique) return m.times_used;
        return 0;
    }

    // 規則的重述——注意這裡沒有任何「哪張牌」的資訊
    static std::string ruleOf(int t) {
        switch (t) {
            case 0: return "把手上的牌接在桌面某條順子的最前面或最後面。";
            case 1: return "桌上有三張同數字不同色的組合時，補上第四種顏色。";
            case 2: return "用 Joker 代替中間缺掉的那個數字，把兩段接起來。";
            case 3: return "還沒破冰時，用純手牌湊出總分 30 以上的合法組合。";
            case 4: return "把桌上既有的組合拆開重新排列，讓手上的牌接得上。";
            case 5: return "把六張以上的長順子切成兩段，製造出新的接點。";
            default: return "";
        }
    }

    void clear() { memories_.clear(); }

private:
    Audience audience_;
    std::vector<TechniqueMemory> memories_;

    std::string openingLine() const {
        return audience_ == Audience::KIDS
             ? "等等——你上次好像遇過差不多的局面。"
             : "你以前這樣做過。";
    }
};

/* =========================================================================
   見證：認出玩家用了哪一招

   兒童版是成就收集，長者版是單純的肯定。

   為什麼要分：**收集徽章對兒童有效，因為那是外在動機**；
   但對長者，成就系統暗示著「你還沒收集完」——那是一種壓力，
   而壓力本身損害認知表現。
   ========================================================================= */
struct WitnessResponse {
    bool speak = false;
    std::string text;
    bool is_first_time = false;   // 第一次用出這一招
    int badge_count = 0;          // 兒童版：已收集幾個
};

class WitnessCompanion {
public:
    explicit WitnessCompanion(Audience a) : audience_(a) {}

    WitnessResponse onTechnique(int technique, int tiles_played) {
        WitnessResponse r;
        bool first = seen_.find(technique) == seen_.end();
        ++seen_[technique];

        r.speak = true;
        r.is_first_time = first;
        r.badge_count = static_cast<int>(seen_.size());

        if (audience_ == Audience::KIDS) {
            if (first)
                r.text = "這招我認得——蓋章！你收集到第 " +
                         std::to_string(r.badge_count) + " 個了。";
            else if (tiles_played >= 5)
                r.text = "一次出這麼多張，漂亮。";
            else
                r.text = "又用出來了，第 " +
                         std::to_string(seen_[technique]) + " 次。";
        } else {
            // 長者版：不報數字、不談收集
            r.text = first ? "很好，這一手做得對。" : "對，就是這樣。";
        }
        return r;
    }

    int badgeCount() const { return static_cast<int>(seen_.size()); }
    int countOf(int technique) const {
        auto it = seen_.find(technique);
        return it == seen_.end() ? 0 : it->second;
    }
    void reset() { seen_.clear(); }

private:
    Audience audience_;
    std::map<int, int> seen_;
};

/* =========================================================================
   引路：三層提示 + 對手風格的戰術提醒

   對手風格只有兒童版啟用。
   ========================================================================= */
struct GuideResponse {
    bool speak = false;
    HintTier tier = HintTier::GENTLE_NUDGE;
    std::string text;
    std::string tactical;      // 對手風格的提醒（可能為空）
};

class GuideCompanion {
public:
    explicit GuideCompanion(Audience a) : audience_(a) {}

    GuideResponse speak(HintTier tier, const std::string& hint_text,
                        OpponentStyle opp = OpponentStyle::UNKNOWN) const {
        GuideResponse r;
        r.speak = true;
        r.tier = tier;
        r.text = hint_text;

        // 對手風格的提醒只有兒童版有，而且只在較淺的提示層級加——
        // 已經在講答案了還提醒戰術，只是多餘的資訊。
        if (audience_ == Audience::KIDS && tier != HintTier::REVEAL_MOVE)
            r.tactical = OpponentReader::advice(opp);

        return r;
    }

    bool usesOpponentStyle() const { return audience_ == Audience::KIDS; }

private:
    Audience audience_;
};
