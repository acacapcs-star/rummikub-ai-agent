#pragma once
#include "coach_engine.h"
#include "coach_modes.h"
#include "battle/battle_parser.h"
#include <map>
#include "recap/recap.h"
#include <optional>

/* =========================================================================
   coach_session.h —— 把四個模組接成一個完整的系統

   在這之前，四個模組各自能跑、各自有測試，但彼此不知道對方存在：

     CoachEngine   什麼時候說、說多深
     CoachModes    使用者想要多少幫助
     mini_battle   玩家自訂的限制條件
     Recap         關卡結束的五題 MCQ

   這一層負責把它們接起來，同時保持一個原則：
   **每個模組仍然能單獨測試。** 整合層只做接線，不放邏輯。

   接線的三條路：

     ① 模式 → 引擎
        建構時套用門檻倍率與音量上限。
        模式不能中途換——否則「這個玩家有多熟」會失去意義。

     ② 挑戰 → 出牌檢查
        出牌前呼叫 validateMove()。引擎只回報結果，不管 UI 流程。

     ③ Recap → 過關
        canAdvance() 只管出牌次數，recapPassed() 另外判斷。
        分開是為了讓介面能顯示「出牌條件已達成，接下來是 recap」。
   ========================================================================= */

// ── 一次出牌的完整結果 ───────────────────────────────────
struct MoveOutcome {
    bool accepted = true;                    // 這一手能不能出
    int  bonus = 0;                          // 挑戰規則給的額外分
    std::vector<std::string> violations;     // 違反了哪些自訂規則
    std::vector<int> techniques;             // 偵測到用了哪些技巧
};

// ── 過關的完整狀態 ───────────────────────────────────────
struct AdvanceStatus {
    bool moves_done = false;      // 出牌次數與自主次數都達標了嗎
    bool recap_done = false;      // recap 過了嗎
    bool can_advance = false;     // 兩個都是才能進下一關

    int uses = 0, uses_needed = 0;
    int unassisted = 0, unassisted_needed = 0;
};

template <typename State, typename Move>
class CoachSession {
public:
    CoachSession(const CoachDomain<State, Move>& domain, CoachMode mode)
        : engine_(domain), domain_(domain), mode_(mode) {}

    // ═════════════════════════════════════════════════════
    //  ① 模式
    // ═════════════════════════════════════════════════════
    CoachMode mode() const { return mode_; }
    const ModeProfile& modeProfile() const { return CoachModes::get(mode_); }

    // 套用模式之後的關卡設定——引擎原本的 currentSpec() 不含模式調整
    LevelSpec effectiveSpec() const {
        return CoachModes::apply(engine_.currentSpec(), mode_);
    }

    /* 提示決策：先問引擎，再套用模式的音量上限。

       為什麼不直接把調整後的 spec 餵給引擎：
       因為引擎持有的是 domain 的 levels()，那是 const 參考。
       在這一層做調整，引擎本身完全不用改——
       **這正是抽象化的好處：新增一個維度不需要動核心。**              */
    Advice tick(const State& s, int stuck_turns) {
        const ModeProfile& p = modeProfile();
        Advice a;

        /* 先問領域有沒有解，再決定要不要開口。

           順序很重要：「現在真的沒有任何牌可以出」是事實，不是提示。
           讓玩家在無解的局面乾等，不是安靜，是故障。
           所以這一句不受模式限制——連高手過招也會說。               */
        auto move = domain_.solve(s);
        if (!move.has_value()) {
            a.speak = true;
            a.tier = HintTier::REVEAL_MOVE;
            a.text = "現在真的沒有能做的動作。";
            return a;
        }

        // 高手過招：有解的情況下，遊戲中完全不出聲
        if (!p.hint_during_play) { a.speak = false; return a; }

        LevelSpec spec = effectiveSpec();
        HintTier tier;
        bool from_net = false;
        if (!decide(spec, stuck_turns, tier, from_net)) { a.speak = false; return a; }

        a.speak = true;
        a.tier = tier;
        a.from_safety_net = from_net;
        a.text = domain_.hint(tier, move.value(), s);

        if (tier == HintTier::REVEAL_MOVE) saw_reveal_ = true;
        if (tier == HintTier::POINT_TO_AREA) saw_point_ = true;
        return a;
    }

    // ═════════════════════════════════════════════════════
    //  ② 自訂挑戰
    // ═════════════════════════════════════════════════════
    void setBattle(const Battle& b) { battle_ = b; }
    void clearBattle() { battle_.reset(); }
    bool hasBattle() const { return battle_.has_value(); }
    const std::string& battleName() const {
        static const std::string none = "";
        return battle_ ? battle_->name : none;
    }

    // 出牌前檢查。沒有設定挑戰時一律通過。
    BattleVerdict validateMove(const MoveMetrics& m) const {
        if (!battle_) return BattleVerdict{};
        return BattleChecker::check(*battle_, m);
    }

    // ═════════════════════════════════════════════════════
    //  出牌：檢查 → 偵測技巧 → 更新掌握度
    // ═════════════════════════════════════════════════════
    MoveOutcome play(const State& before, const State& after,
                     const MoveMetrics& metrics) {
        MoveOutcome out;

        BattleVerdict v = validateMove(metrics);
        out.accepted = v.allowed;
        out.bonus = v.bonus;
        out.violations = v.violations;

        // 違規的一手不算數——不記技巧、不升掌握度。
        // 否則玩家可以靠違規的出牌累積進度。
        if (!out.accepted) return out;

        out.techniques = domain_.classify(before, after);
        engine_.observe(before, after);

        // 自創招數的偵測（只有部分模式啟用）
        const ModeProfile& p = modeProfile();
        if (p.detect_private && metrics.tiles_played >= p.private_threshold)
            private_candidates_.push_back(metrics);

        saw_reveal_ = saw_point_ = false;
        return out;
    }

    // ═════════════════════════════════════════════════════
    //  ③ 過關與 Recap
    // ═════════════════════════════════════════════════════
    AdvanceStatus advanceStatus() const {
        AdvanceStatus st;
        const LevelSpec& spec = engine_.currentSpec();
        const auto& prog = engine_.progressOf(spec.technique);

        st.uses = prog.total_uses;
        st.uses_needed = spec.required_uses;
        st.unassisted = prog.unassisted_uses;
        st.unassisted_needed = spec.required_unassisted;
        st.moves_done = engine_.canAdvance();
        st.recap_done = recap_passed_;
        st.can_advance = st.moves_done && st.recap_done;
        return st;
    }

    // 出牌條件達成後，取得這一關的 recap 題目
    std::optional<Recap> startRecap(unsigned shuffle_seed) const {
        if (!engine_.canAdvance()) return std::nullopt;   // 還沒資格
        return Recap(RecapBank::forLevel(engine_.currentLevel()),
                     RecapConfig{}, shuffle_seed);
    }

    void submitRecap(const RecapResult& r) {
        recap_passed_ = r.passed;
        recap_scores_[engine_.currentLevel()] = r.score();
    }

    double recapScore(int level) const {
        auto it = recap_scores_.find(level);
        return it == recap_scores_.end() ? -1.0 : it->second;
    }

    bool advance() {
        if (!advanceStatus().can_advance) return false;
        bool ok = engine_.advance();
        if (ok) recap_passed_ = false;      // 新的一關要重新過 recap
        return ok;
    }

    // ═════════════════════════════════════════════════════
    //  轉發給引擎
    // ═════════════════════════════════════════════════════
    int currentLevel() const { return engine_.currentLevel(); }
    const LevelSpec& currentSpec() const { return engine_.currentSpec(); }
    const TechniqueProgress& progressOf(int t) const {
        return engine_.progressOf(t);
    }

    // 玩家目前的能力——寫 mini_battle 腳本時要用
    PlayerState playerState() const {
        PlayerState ps;
        ps.level = engine_.currentLevel();
        for (int i = 0; i < domain_.techniqueCount(); ++i)
            ps.mastery.push_back(
                static_cast<int>(engine_.progressOf(i).mastery));
        return ps;
    }

    const std::vector<MoveMetrics>& privateCandidates() const {
        return private_candidates_;
    }

private:
    CoachEngine<State, Move> engine_;
    const CoachDomain<State, Move>& domain_;
    CoachMode mode_;

    std::optional<Battle> battle_;
    bool recap_passed_ = false;
    std::map<int, double> recap_scores_;
    std::vector<MoveMetrics> private_candidates_;

    bool saw_reveal_ = false;
    bool saw_point_ = false;

    // 跟引擎相同的判斷邏輯，但吃的是套用模式之後的 spec
    static bool decide(const LevelSpec& c, int stuck,
                       HintTier& out, bool& from_net) {
        from_net = false;
        if (c.safety_net_after_turns >= 0 && stuck >= c.safety_net_after_turns) {
            out = c.safety_net_tier;
            from_net = true;
            return true;
        }
        if (c.reveal_after_turns >= 0 && stuck >= c.reveal_after_turns &&
            c.max_tier == HintTier::REVEAL_MOVE) {
            out = HintTier::REVEAL_MOVE;
            return true;
        }
        if (c.point_after_turns >= 0 && stuck >= c.point_after_turns &&
            c.max_tier != HintTier::GENTLE_NUDGE) {
            out = HintTier::POINT_TO_AREA;
            return true;
        }
        if (c.nudge_after_turns >= 0 && stuck >= c.nudge_after_turns) {
            out = HintTier::GENTLE_NUDGE;
            return true;
        }
        return false;
    }
};
