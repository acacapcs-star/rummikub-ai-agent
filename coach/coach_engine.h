#pragma once
#include <optional>
#include <string>
#include <vector>

/* =========================================================================
   coach_engine.h —— 領域無關的教練引擎

   為什麼要抽象化：
     原本的 CognitiveHintEngine 綁死在拉密上——它知道什麼是 Run、
     什麼是 Joker、怎麼湊 30 分。但「什麼時候該說、說到多深、
     什麼時候閉嘴」這件事跟拉密沒有關係。

     把那件事抽出來之後，同一個引擎可以教下棋、教解題、
     甚至教機器人路徑規劃——只要那個領域能回答三個問題。

   引擎需要領域提供的三件事：
     1. solve()     給我現在的狀態，找出一個解（找不到就回 nullopt）
     2. hint()      把那個解翻譯成三種深淺的說法
     3. classify()  從動作前後的狀態，反推使用者用了哪些技巧

   引擎自己負責的：
     - 依關卡與卡關回合數，決定要不要開口、給哪一層
     - 記錄掌握度、判斷過關
     - 保底機制

   分工的界線很清楚：
     **領域知道「答案是什麼」，引擎決定「要不要說、說多少」。**
   ========================================================================= */

// ── 提示的三個深度 ───────────────────────────────────────
enum class HintTier {
    GENTLE_NUDGE,    // 只說「這裡有東西」
    POINT_TO_AREA,   // 指出是哪一區
    REVEAL_MOVE      // 講出具體該做什麼
};

// ── 掌握程度 ─────────────────────────────────────────────
// 只升不降：學會了就是學會了，不因為某次偷懶而變成不會。
enum class Mastery {
    LOCKED,        // 還沒用過
    COPIED,        // ⭐   看了答案才做出來
    PROMPTED,      // ⭐⭐  只給到方向就自己找到
    DISCOVERED     // ⭐⭐⭐ 完全沒提示，自己用出來
};

// ── 一個關卡的設定 ───────────────────────────────────────
struct LevelSpec {
    int level;
    int technique;              // 這一關教哪一招（領域自己編號）
    std::string name;
    int guidance_percent;       // 顯示用

    // 引導強度落實成的兩個參數
    int nudge_after_turns;      // 卡幾回合後給輕推（-1 = 不給）
    int point_after_turns;      // 卡幾回合後給指方向
    int reveal_after_turns;     // 卡幾回合後給答案
    HintTier max_tier;          // 這一關最多給到哪一層

    int required_uses;          // 過關要用出幾次
    int required_unassisted;    // 其中幾次不能是看了答案才做的

    // 保底：卡太久時破例把音量調高一格。
    // 這個機制是模擬實驗跑出來才加的——L6 的完全新手平均卡 16 回合。
    int safety_net_after_turns; // -1 = 不啟用
    HintTier safety_net_tier;
};

// ── 進度 ─────────────────────────────────────────────────
struct TechniqueProgress {
    int technique = 0;
    Mastery mastery = Mastery::LOCKED;
    int total_uses = 0;
    int unassisted_uses = 0;
};

// ── 引擎給出的建議 ───────────────────────────────────────
struct Advice {
    bool speak = false;              // 要不要開口
    HintTier tier = HintTier::GENTLE_NUDGE;
    bool from_safety_net = false;    // 是不是保底觸發的
    std::string text;                // 領域翻譯出來的說法
};

/* =========================================================================
   領域必須實作的介面

   State  當前狀態（拉密的桌面+手牌 / 機器人的位置+地圖）
   Move   一個動作（出一組牌 / 移動一步）
   ========================================================================= */
template <typename State, typename Move>
class CoachDomain {
public:
    virtual ~CoachDomain() = default;

    // 找出一個解。找不到就回 std::nullopt——
    // 引擎會據此告訴使用者「現在真的沒有」，而不是硬掰。
    virtual std::optional<Move> solve(const State& s) const = 0;

    // 把解翻譯成指定深度的說法。
    // 同一個解，三種說法：輕推不透露位置、指方向給範圍、講答案給具體動作。
    virtual std::string hint(HintTier tier, const Move& m, const State& s) const = 0;

    // 從動作前後的狀態，反推使用了哪些技巧。
    // 原則是寧可漏判不可誤判——誤判會讓使用者在沒學會的情況下被判過關。
    virtual std::vector<int> classify(const State& before,
                                      const State& after) const = 0;

    virtual int techniqueCount() const = 0;
    virtual std::string techniqueName(int t) const = 0;
    virtual const std::vector<LevelSpec>& levels() const = 0;
};

/* =========================================================================
   引擎本體：完全不知道 State 和 Move 是什麼
   ========================================================================= */
template <typename State, typename Move>
class CoachEngine {
public:
    explicit CoachEngine(const CoachDomain<State, Move>& domain)
        : domain_(domain) {
        progress_.resize(domain.techniqueCount());
        for (int i = 0; i < domain.techniqueCount(); ++i)
            progress_[i].technique = i;
    }

    // ── 每個回合呼叫一次 ──────────────────────────────────
    // stuck_turns = 使用者在這一步已經卡了幾個回合
    Advice tick(const State& s, int stuck_turns) {
        Advice a;

        // ① 先問領域：現在到底有沒有解？
        auto move = domain_.solve(s);
        if (!move.has_value()) {
            // 沒有解就誠實說沒有，不硬掰一個提示出來。
            a.speak = true;
            a.tier = HintTier::REVEAL_MOVE;
            a.text = "現在真的沒有能做的動作。";
            return a;
        }

        // ② 決定要不要開口、給哪一層
        HintTier tier;
        bool from_net = false;
        if (!decideTier(stuck_turns, tier, from_net)) {
            return a;   // speak = false —— 沉默也是一種設計
        }

        // ③ 請領域把解翻譯成那個深度的說法
        a.speak = true;
        a.tier = tier;
        a.from_safety_net = from_net;
        a.text = domain_.hint(tier, move.value(), s);

        // 記住這一步看過的最深提示，供 observe() 判斷掌握度
        if (tier == HintTier::REVEAL_MOVE) saw_reveal_ = true;
        if (tier == HintTier::POINT_TO_AREA) saw_point_ = true;
        return a;
    }

    // ── 使用者做完一個動作後呼叫 ──────────────────────────
    void observe(const State& before, const State& after) {
        for (int t : domain_.classify(before, after)) {
            if (t < 0 || t >= static_cast<int>(progress_.size())) continue;
            auto& p = progress_[t];
            ++p.total_uses;
            if (!saw_reveal_) ++p.unassisted_uses;

            Mastery cand = saw_reveal_ ? Mastery::COPIED
                         : saw_point_  ? Mastery::PROMPTED
                                       : Mastery::DISCOVERED;
            if (static_cast<int>(cand) > static_cast<int>(p.mastery))
                p.mastery = cand;      // 只升不降
        }
        saw_reveal_ = saw_point_ = false;   // 這一步結束，重置
    }

    // ── 關卡 ──────────────────────────────────────────────
    int currentLevel() const { return level_; }

    const LevelSpec& currentSpec() const {
        const auto& L = domain_.levels();
        int idx = level_ - 1;
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(L.size())) idx = static_cast<int>(L.size()) - 1;
        return L[idx];
    }

    bool canAdvance() const {
        const auto& spec = currentSpec();
        const auto& p = progress_[spec.technique];
        return p.total_uses >= spec.required_uses &&
               p.unassisted_uses >= spec.required_unassisted;
    }

    bool advance() {
        if (level_ >= static_cast<int>(domain_.levels().size())) return false;
        ++level_;
        return true;
    }

    const TechniqueProgress& progressOf(int t) const { return progress_[t]; }

    static std::string stars(Mastery m) {
        switch (m) {
            case Mastery::COPIED:     return "*";
            case Mastery::PROMPTED:   return "**";
            case Mastery::DISCOVERED: return "***";
            default:                  return "";
        }
    }

private:
    // 由深到淺檢查——順序反了的話，淺層會先命中，深層永遠執行不到。
    bool decideTier(int stuck, HintTier& out, bool& from_net) const {
        const auto& c = currentSpec();
        from_net = false;

        // 保底優先：例外規則必須在一般規則之前判斷
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

    const CoachDomain<State, Move>& domain_;
    std::vector<TechniqueProgress> progress_;
    int level_ = 1;
    bool saw_reveal_ = false;
    bool saw_point_ = false;
};
