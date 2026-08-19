#pragma once
#include "coach_engine.h"
#include <deque>
#include <string>
#include <vector>

/* =========================================================================
   audience_profile.h —— 兩個族群的客製化模組

   同一套引擎，兩種完全不同的使用者：

     兒童（8–12 歲）      認知還在建構 —— 目標是「習得」
     長者（65 歲以上）    認知可能在流失 —— 目標是「維持」

   這兩者不是「難度不同」，是**方法論相反**：

   ┌──────────────┬────────────────────┬────────────────────┐
   │              │ 兒童版             │ 長者版             │
   ├──────────────┼────────────────────┼────────────────────┤
   │ 目標         │ 習得新能力         │ 維持既有功能       │
   │ 犯錯         │ 有價值（從錯中學） │ 有風險（學會錯誤） │
   │ 引導方向     │ 100% → 40% 遞減    │ 依表現動態調整     │
   │ 卡關 16 回合 │ 挑戰               │ 挫折，可能就放棄   │
   │ 成功經驗     │ 錦上添花           │ 核心需求           │
   └──────────────┴────────────────────┴────────────────────┘

   最後一列是關鍵。失智照護的主流是**無錯學習**（errorless learning）——
   盡量不讓患者犯錯，因為他們有「學會錯誤」而非「從錯誤中學」的風險
   （Clare & Jones 2008; de Werd et al. 2013）。

   而本系統的核心是刻意留出犯錯與自主提取的空間。
   **兩者在方法論上是相反的**，所以不能用同一組參數。

   ── 適用範圍 ──────────────────────────────────────────
   長者版設計給「健康中高齡預防」與「輕度認知障礙（MCI）」。
   **不適用於中重度失智**——那個族群需要的是完整的無錯學習，
   而不是本系統的漸進撤除。這是方法論的界線，不是難度的問題。
   ========================================================================= */

enum class Audience { KIDS, SENIORS };

// ── 每一招對應到的認知功能 ───────────────────────────────
// 這個對照是設計時的推論，尚未經過認知評估工具驗證。
struct CognitiveTarget {
    int technique;
    std::string name;              // 拉密的技巧名稱
    std::string cognitive_domain;  // 對應的認知功能
    std::string note;
};

class CognitiveMap {
public:
    static const std::vector<CognitiveTarget>& all() {
        static const std::vector<CognitiveTarget> M = {
            { 0, "接龍頭尾",     "視覺搜尋、序列辨識",
              "在既有序列的兩端找出可延伸的位置" },
            { 1, "補第四色",     "集合分類、屬性比對",
              "辨識「同數字不同色」這個抽象規則" },
            { 2, "Joker 補缺口", "抽象替代、心智彈性",
              "把一個符號當成另一個符號使用" },
            { 3, "破冰湊 30 分", "計算、目標導向規劃",
              "在限制條件下搜尋滿足門檻的組合" },
            { 4, "大風吹重組",   "工作記憶、執行功能",
              "同時保持多組資訊並重新排列" },
            { 5, "長龍切斷",     "抑制控制",
              "放棄現成的完整結構，以換取後續的機會" },
        };
        return M;
    }

    static const CognitiveTarget* forTechnique(int t) {
        for (const auto& c : all()) if (c.technique == t) return &c;
        return nullptr;
    }
};

/* =========================================================================
   族群設定
   ========================================================================= */
struct AudienceProfile {
    Audience audience;
    std::string name;
    std::string target_group;      // 適用對象
    std::string goal;              // 這個版本的目標

    int level_count;               // 開放幾個關卡

    /* ── 動態難度 ─────────────────────────────────────────
       兒童版關閉：難度由關卡決定，卡關是挑戰的一部分。
       長者版開啟：難度跟著當下的表現走，因為認知功能會波動，
       今天做得到的事明天可能做不到。                              */
    bool adaptive_difficulty;
    int  raise_after_stuck;        // 連續卡幾回合就把提示調深一層
    int  lower_after_success;      // 連續成功幾次就把提示調淺一層

    /* ── 成功保證 ─────────────────────────────────────────
       連續這麼多回合沒有成功，就直接給答案。

       為什麼長者版需要這個：挫折累積會讓人放棄，
       而放棄就等於失去整個介入。一次成功的經驗，
       價值高於一次「自己想出來」的機會。
       -1 表示不啟用。                                            */
    int  guarantee_success_after;

    /* ── 疲勞提醒 ─────────────────────────────────────────
       認知訓練的常見做法是每次 30–45 分鐘。
       超過之後效果遞減，而且疲勞會讓表現看起來比實際差。          */
    int  session_minutes;
    bool fatigue_reminder;

    /* ── 錯誤處理 ─────────────────────────────────────────
       兒童版允許犯錯：錯誤是學習的一部分。
       長者版盡量避免：呼應無錯學習的原則。                        */
    bool allow_errors;
    bool confirm_before_invalid;   // 出不合法的牌之前先確認

    /* ── Recap ────────────────────────────────────────────
       兒童版是測驗，要過才進關。
       長者版是回顧，不擋——**測驗會製造壓力，而壓力本身損害表現**。 */
    bool recap_gates_progress;

    /* ── 措辭 ─────────────────────────────────────────────
       同樣一件事，兩個族群的說法不同。                            */
    std::string nudge_phrase;
    std::string encouragement;
};

class AudienceProfiles {
public:
    static const AudienceProfile& get(Audience a) {
        static const AudienceProfile PROFILES[] = {
            {
                Audience::KIDS, "兒童版",
                "8–12 歲",
                "培養邏輯思維——目標是習得新能力，所以引導要逐步撤除",
                6,                          // 六關全開
                false, 0, 0,                // 不用動態難度
                -1,                         // 不保證成功
                45, false,                  // 45 分鐘，不主動提醒
                true, false,                // 允許犯錯
                true,                       // Recap 要過才進關
                "再看一次周圍，有東西可以動",
                "很好，這一手想得不錯"
            },
            {
                Audience::SENIORS, "長者版",
                "65 歲以上健康中高齡者、輕度認知障礙（MCI）",
                "維持認知功能——目標是持續參與，所以挫折的成本高於挑戰的價值",
                4,                          // 只開前四關，跳過高難度的
                true, 2, 3,                 // 卡 2 回合加強、連 3 次成功放鬆
                4,                          // 連 4 回合沒成功就給答案
                30, true,                   // 30 分鐘，主動提醒休息
                false, true,                // 盡量避免犯錯，出錯前先確認
                false,                      // Recap 不擋進度
                "我們一起看看這裡",
                "對，就是這樣"
            },
        };
        return PROFILES[static_cast<int>(a)];
    }
};

/* =========================================================================
   動態難度調整器（長者版用）

   兒童版的難度由關卡決定——那是固定的課程。
   長者版的難度跟著當下的表現走，因為認知功能會波動：
   同一個人在早上與傍晚、休息前與疲勞後，能做到的事不一樣。

   狀態機很簡單，但兩個方向的門檻刻意不對稱：

     連續卡 2 回合   → 提示深一層（快速支援）
     連續成功 3 次   → 提示淺一層（緩慢撤除）

   為什麼不對稱：**加強要快，撤除要慢。**
   撤太快會造成挫折，而挫折的成本在這個族群高得多。
   ========================================================================= */
class AdaptiveDifficulty {
public:
    explicit AdaptiveDifficulty(const AudienceProfile& p)
        : profile_(p), level_(0) {}

    // 這一回合卡住了
    void onStuck() {
        if (!profile_.adaptive_difficulty) return;
        ++consecutive_stuck_;
        consecutive_success_ = 0;
        if (consecutive_stuck_ >= profile_.raise_after_stuck) {
            if (level_ < 2) ++level_;          // 往深的方向調
            consecutive_stuck_ = 0;
        }
        ++turns_without_success_;
    }

    // 這一回合成功了
    void onSuccess() {
        if (!profile_.adaptive_difficulty) return;
        ++consecutive_success_;
        consecutive_stuck_ = 0;
        turns_without_success_ = 0;
        if (consecutive_success_ >= profile_.lower_after_success) {
            if (level_ > 0) --level_;          // 往淺的方向調
            consecutive_success_ = 0;
        }
    }

    // 目前該給哪一層
    HintTier tier() const { return static_cast<HintTier>(level_); }

    /* 成功保證是否該啟動。

       連續這麼多回合沒有成功，就直接給答案——
       不管關卡設定、不管動態難度、不管模式。

       這是整個系統唯一一個「無條件覆蓋」的機制，
       因為它保護的不是學習效果，是**繼續參與的意願**。       */
    bool needsGuarantee() const {
        return profile_.guarantee_success_after > 0 &&
               turns_without_success_ >= profile_.guarantee_success_after;
    }

    int consecutiveStuck() const { return consecutive_stuck_; }
    int turnsWithoutSuccess() const { return turns_without_success_; }

    void reset() {
        level_ = 0;
        consecutive_stuck_ = 0;
        consecutive_success_ = 0;
        turns_without_success_ = 0;
    }

private:
    const AudienceProfile& profile_;
    int level_;                     // 0 輕推 / 1 指方向 / 2 講答案
    int consecutive_stuck_ = 0;
    int consecutive_success_ = 0;
    int turns_without_success_ = 0;
};

/* =========================================================================
   疲勞追蹤（長者版用）

   認知訓練的常見做法是每次 30–45 分鐘。超過之後：
     - 訓練效果遞減
     - 而且疲勞會讓表現看起來比實際差，
       那會誤導系統的難度判斷

   所以疲勞不只是使用者體驗的問題，它會污染評估。
   ========================================================================= */
class FatigueTracker {
public:
    explicit FatigueTracker(const AudienceProfile& p) : profile_(p) {}

    void addMinutes(int m) { elapsed_ += m; }

    bool shouldSuggestBreak() const {
        return profile_.fatigue_reminder && elapsed_ >= profile_.session_minutes;
    }

    // 已經超時多久
    int overtimeMinutes() const {
        int over = elapsed_ - profile_.session_minutes;
        return over > 0 ? over : 0;
    }

    std::string breakMessage() const {
        return "已經玩了 " + std::to_string(elapsed_) +
               " 分鐘了，休息一下再繼續吧。";
    }

    void reset() { elapsed_ = 0; }
    int elapsedMinutes() const { return elapsed_; }

private:
    const AudienceProfile& profile_;
    int elapsed_ = 0;
};
