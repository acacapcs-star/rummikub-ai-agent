#pragma once
#include "audience_profile.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

/* =========================================================================
   spaced_cycle.h —— 間隔重複與循環報告

   原本的系統有一個洞：**技巧學會之後就不管了。**

       掌握度升到 ⭐⭐⭐ → 過關 → 再也不會回來練

   對兒童版還算可以（他在往前學新東西），
   但對長者版是致命的——**認知功能會退化，學會的東西三週沒碰就退回去了。**

   ── 間隔重複 ────────────────────────────────────────────

   複習的間隔按費氏數列拉長，但走到頂之後**循環回到起點**：

       1 → 2 → 3 → 5 → 8 → 13 → 1 → 2 → 3 → 5 → 8 → 13 → 1 ...

   為什麼是循環而不是封頂維持：

       封頂在 13   學會之後每 13 天碰一次     → 目標是「記得住」
       循環回 1    每 13 天之後重新密集一輪   → 目標是「持續使用」

   **對延緩失智這個定位，循環才對。** 那個族群要的不是記憶保存，
   是讓那個認知功能持續被使用。

   而且循環有個副作用：已經學會的技巧永遠不會消失在系統裡，
   不會出現「這招我三個月沒碰過」的情況。

   循環的頂點可以自訂：

       頂點 8    1,2,3,5,8,1,2,3,5,8...        循環密集 → 長者版
       頂點 13   1,2,3,5,8,13,1,2,3,5,8,13...  預設
       頂點 21   1,2,3,5,8,13,21,1,2,3...      循環寬鬆 → 兒童版

   ── 循環報告 ────────────────────────────────────────────

   每走完一輪，產出一份報告，然後依報告調整下一輪的訓練重點。
   這讓系統從「一次性的課程」變成「持續的循環」。
   ========================================================================= */

// ── 費氏間隔 ─────────────────────────────────────────────
class FibonacciSchedule {
public:
    // 完整的費氏間隔序列
    static const std::vector<int>& base() {
        static const std::vector<int> F = { 1, 2, 3, 5, 8, 13, 21, 34 };
        return F;
    }

    /* 依「目前在序列的第幾步」與「循環頂點」算出間隔天數。

       step 會一直往上加，不會歸零——歸零的是它在序列裡的位置。
       這樣才能區分「第一輪的第 1 天」與「第三輪的第 1 天」，
       後者的意義完全不同（那是一個已經練過兩輪的人）。            */
    static int intervalAt(int step, int cap_days) {
        const auto& F = base();
        int cap_index = capIndex(cap_days);
        if (cap_index < 0) return F.back();
        int cycle_len = cap_index + 1;
        return F[step % cycle_len];
    }

    // 走完一輪需要幾步
    static int cycleLength(int cap_days) {
        int ci = capIndex(cap_days);
        return ci < 0 ? static_cast<int>(base().size()) : ci + 1;
    }

    // 這一步是第幾輪（從 1 開始）
    static int cycleNumber(int step, int cap_days) {
        return step / cycleLength(cap_days) + 1;
    }

    // 這一步在該輪裡的第幾個位置
    static int positionInCycle(int step, int cap_days) {
        return step % cycleLength(cap_days);
    }

    // 這一步是不是一輪的最後一步（該產出報告了）
    static bool isCycleEnd(int step, int cap_days) {
        return positionInCycle(step, cap_days) == cycleLength(cap_days) - 1;
    }

private:
    static int capIndex(int cap_days) {
        const auto& F = base();
        for (std::size_t i = 0; i < F.size(); ++i)
            if (F[i] == cap_days) return static_cast<int>(i);
        return -1;      // 不在序列裡就用完整序列
    }
};

// ── 單一技巧的複習排程 ───────────────────────────────────
struct ReviewSchedule {
    int technique = 0;
    int step = 0;              // 走到序列的第幾步（不歸零）
    int days_until_due = 1;    // 還有幾天到期
    int reviews_done = 0;      // 總複習次數
    int reviews_passed = 0;    // 其中成功幾次

    double passRate() const {
        return reviews_done == 0 ? 0.0
             : static_cast<double>(reviews_passed) / reviews_done;
    }
    bool isDue() const { return days_until_due <= 0; }
};

/* =========================================================================
   一輪的統計
   ========================================================================= */
struct TechniqueRoundStat {
    int technique = 0;
    int attempts = 0;          // 這一輪練了幾次
    int unassisted = 0;        // 其中幾次自主解出
    int total_turns = 0;       // 花了幾個回合（用來算速度）
    int mastery_before = 0;
    int mastery_after = 0;

    double autonomyRate() const {
        return attempts == 0 ? 0.0
             : static_cast<double>(unassisted) / attempts;
    }
    double avgTurns() const {
        return attempts == 0 ? 0.0
             : static_cast<double>(total_turns) / attempts;
    }
    bool improved() const { return mastery_after > mastery_before; }
};

/* ── 一輪結束時的報告 ─────────────────────────────────────

   六個面向：

     進步      這一輪的自主率相較上一輪
     學會      這一輪升級的掌握度
     強項      自主率最高的技巧
     待補強    自主率最低、或退步的技巧
     穩定度    表現的波動 ← 對長者版特別重要
     參與度    練了幾次

   **穩定度是刻意加的。** 認知功能退化的早期徵兆常常是
   「波動變大」而不是「平均下降」——一個人平均分數沒變，
   但好的時候更好、壞的時候更壞，那本身就是訊號。           */
struct CycleReport {
    int cycle_number = 0;
    int total_attempts = 0;

    std::vector<TechniqueRoundStat> stats;

    double overall_autonomy = 0.0;      // 這一輪整體自主率
    double previous_autonomy = -1.0;    // 上一輪（-1 = 沒有上一輪）
    double avg_turns = 0.0;             // 平均花幾回合
    double stability = 0.0;             // 各技巧自主率的標準差，越小越穩

    std::vector<int> learned;           // 這一輪升級的技巧
    std::vector<int> strengths;         // 強項
    std::vector<int> needs_work;        // 待補強

    // 給下一輪的建議
    double next_difficulty_ratio = 0.5; // 難題的比例 0–1
    std::string difficulty_note;
    std::map<int, double> next_weights; // 各技巧的出現權重

    bool hasPrevious() const { return previous_autonomy >= 0.0; }
    double improvement() const {
        return hasPrevious() ? overall_autonomy - previous_autonomy : 0.0;
    }
};

/* =========================================================================
   循環管理器
   ========================================================================= */
class SpacedCycle {
public:
    SpacedCycle(const AudienceProfile& profile, int technique_count)
        : profile_(profile), technique_count_(technique_count) {
        // 長者版循環密集（頂點 8），兒童版寬鬆（頂點 21）
        cap_days_ = (profile.audience == Audience::SENIORS) ? 8 : 21;

        for (int t = 0; t < technique_count; ++t) {
            ReviewSchedule s;
            s.technique = t;
            s.days_until_due = FibonacciSchedule::intervalAt(0, cap_days_);
            schedules_.push_back(s);

            TechniqueRoundStat st;
            st.technique = t;
            current_.push_back(st);
        }
    }

    int capDays() const { return cap_days_; }
    void setCapDays(int d) { cap_days_ = d; }     // 客製化

    // 一輪需要幾步
    int cycleLength() const { return FibonacciSchedule::cycleLength(cap_days_); }

    // ── 時間推進 ──────────────────────────────────────────
    void advanceDays(int days) {
        for (auto& s : schedules_) s.days_until_due -= days;
    }

    std::vector<int> dueTechniques() const {
        std::vector<int> out;
        for (const auto& s : schedules_)
            if (s.isDue()) out.push_back(s.technique);
        return out;
    }

    const ReviewSchedule& scheduleOf(int t) const { return schedules_[t]; }

    // ── 記錄一次練習 ──────────────────────────────────────
    void record(int technique, bool unassisted, int turns_taken,
                int mastery_before, int mastery_after) {
        if (technique < 0 || technique >= technique_count_) return;

        auto& st = current_[technique];
        if (st.attempts == 0) st.mastery_before = mastery_before;
        ++st.attempts;
        if (unassisted) ++st.unassisted;
        st.total_turns += turns_taken;
        st.mastery_after = mastery_after;

        // 更新排程
        auto& sc = schedules_[technique];
        ++sc.reviews_done;
        if (unassisted) ++sc.reviews_passed;

        /* 成功就往序列的下一步走，失敗就退一格。

           為什麼是退一格而不是打回原點：
           打回原點是 Anki 的做法，對記憶卡片合理，
           但對這個族群挫折感太重——**一次失敗不代表全部忘光。**   */
        if (unassisted) ++sc.step;
        else if (sc.step > 0) --sc.step;

        sc.days_until_due = FibonacciSchedule::intervalAt(sc.step, cap_days_);
    }

    // ── 這一輪走完了嗎 ────────────────────────────────────
    // 判準：所有技巧的 step 都至少走完一輪
    bool cycleComplete() const {
        int len = cycleLength();
        for (const auto& s : schedules_)
            if (s.step < len * completed_cycles_ + len) return false;
        return true;
    }

    int completedCycles() const { return completed_cycles_; }

    // ── 產出報告並開啟下一輪 ──────────────────────────────
    CycleReport finishCycle() {
        CycleReport r;
        r.cycle_number = completed_cycles_ + 1;
        r.stats = current_;

        int total_attempts = 0, total_unassisted = 0, total_turns = 0;
        for (const auto& s : current_) {
            total_attempts   += s.attempts;
            total_unassisted += s.unassisted;
            total_turns      += s.total_turns;
            if (s.improved()) r.learned.push_back(s.technique);
        }
        r.total_attempts = total_attempts;
        r.overall_autonomy = total_attempts == 0 ? 0.0
            : static_cast<double>(total_unassisted) / total_attempts;
        r.avg_turns = total_attempts == 0 ? 0.0
            : static_cast<double>(total_turns) / total_attempts;
        r.previous_autonomy = previous_autonomy_;

        // ── 強項與待補強 ──
        std::vector<std::pair<double,int>> ranked;
        for (const auto& s : current_)
            if (s.attempts > 0) ranked.push_back({ s.autonomyRate(), s.technique });
        std::sort(ranked.begin(), ranked.end(),
                  [](auto& a, auto& b){ return a.first > b.first; });

        for (std::size_t i = 0; i < ranked.size(); ++i) {
            if (ranked[i].first >= 0.7) r.strengths.push_back(ranked[i].second);
            if (ranked[i].first < 0.4)  r.needs_work.push_back(ranked[i].second);
        }

        // ── 穩定度：各技巧自主率的標準差 ──
        if (!ranked.empty()) {
            double mean = 0;
            for (auto& p : ranked) mean += p.first;
            mean /= ranked.size();
            double var = 0;
            for (auto& p : ranked) var += (p.first - mean) * (p.first - mean);
            r.stability = std::sqrt(var / ranked.size());
        }

        computeNextRound(r);

        // ── 開啟下一輪 ──
        previous_autonomy_ = r.overall_autonomy;
        ++completed_cycles_;
        for (auto& s : current_) {
            s.attempts = s.unassisted = s.total_turns = 0;
            s.mastery_before = s.mastery_after;
        }
        return r;
    }

private:
    const AudienceProfile& profile_;
    int technique_count_;
    int cap_days_;
    int completed_cycles_ = 0;
    double previous_autonomy_ = -1.0;
    std::vector<ReviewSchedule> schedules_;
    std::vector<TechniqueRoundStat> current_;

    /* 依這一輪的表現，決定下一輪怎麼調。

       兩件事：

       ① 難度
          通過率高**而且**速度快 → 提高難題比例
          兩個條件都要滿足——只有通過率高可能是題目太簡單，
          只有速度快可能是在亂猜。

       ② 各技巧的出現權重
          弱的技巧出現頻率提高，強的降低。
          但不會歸零——那會讓已學會的技巧退化，
          而「持續使用」正是這個系統的目的。                        */
    void computeNextRound(CycleReport& r) const {
        // ── ① 難度 ──
        bool fast = r.avg_turns > 0 && r.avg_turns <= 2.5;
        bool accurate = r.overall_autonomy >= 0.75;

        if (accurate && fast) {
            r.next_difficulty_ratio = 0.75;
            r.difficulty_note = "通過率與速度都高 → 提高難題比例";
        } else if (r.overall_autonomy < 0.4) {
            r.next_difficulty_ratio = 0.25;
            r.difficulty_note = "通過率偏低 → 降低難題比例，先把基礎穩住";
        } else if (accurate && !fast) {
            r.next_difficulty_ratio = 0.55;
            r.difficulty_note = "答得準但慢 → 難度維持，先練熟練度";
        } else {
            r.next_difficulty_ratio = 0.5;
            r.difficulty_note = "表現平穩 → 難度維持";
        }

        // 長者版的難度上限較保守
        if (profile_.audience == Audience::SENIORS)
            r.next_difficulty_ratio = std::min(r.next_difficulty_ratio, 0.6);

        // ── ② 權重 ──
        for (const auto& s : r.stats) {
            double w;
            if (s.attempts == 0)               w = 1.0;   // 沒練到的，正常權重
            else if (s.autonomyRate() < 0.4)   w = 2.0;   // 弱項加倍
            else if (s.autonomyRate() < 0.7)   w = 1.2;
            else                               w = 0.6;   // 強項降低但不歸零
            r.next_weights[s.technique] = w;
        }
    }
};
