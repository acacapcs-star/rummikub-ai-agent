#pragma once
#include <random>
#include <vector>

/* =========================================================================
   mini_env.h —— 簡化版拉密環境

   為什麼要簡化：
     完整拉密的動作空間有數萬種（出哪幾組牌、怎麼重組桌面），
     狀態是變長的（桌面組數不固定、手牌數不固定），
     而且獎勵稀疏（只有贏了才有分）。
     那三件事任何一件都足以讓 Actor-Critic 訓不起來。

     所以先做一個縮到最小、但保留核心決策的版本：
       局面：桌面一條 Run，手上 5 張牌
       動作：出第 1~5 張，或抽牌（共 6 種）
       目標：接得上就出，接不上就抽

   這個題目簡單到人一眼看得出正確答案——
   **正因為如此，才能驗證 AI 是不是真的學會了。**
   如果連這個都學不會，放大到完整拉密只會更糟。

   狀態表示（12 維）：
     [0]    桌面 Run 的頭（正規化到 0~1）
     [1]    桌面 Run 的尾
     [2..6] 五張手牌的數字（正規化）
     [7..11] 五張手牌「接不接得上」的旗標（0 或 1）

   最後五維是刻意加的：
     它等於把「這張接不接得上」直接告訴網路。
     沒有它的話，網路要自己從數字關係學出「差 1 才接得上」，
     那是另一個學習問題，會混淆我們想驗證的東西。
     **這是一個為了讓實驗聚焦而做的簡化，不是因為那樣比較好。**
   ========================================================================= */

class MiniEnv {
public:
    static const int HAND_SIZE   = 5;
    static const int STATE_DIM   = 2 + HAND_SIZE * 2;   // 12
    static const int ACTION_DIM  = HAND_SIZE + 1;       // 出 5 張其中一張，或抽牌
    static const int DRAW_ACTION = HAND_SIZE;           // 動作 5 = 抽牌
    static const int MAX_STEPS   = 20;

    explicit MiniEnv(unsigned seed = 42) : rng_(seed) {}

    // ── 重置：隨機生成一個局面 ────────────────────────────
    std::vector<double> reset() {
        std::uniform_int_distribution<int> pick(2, 10);
        run_head_ = pick(rng_);
        run_tail_ = run_head_ + 2;          // 固定三張的 Run

        hand_.clear();
        std::uniform_int_distribution<int> tile(1, 13);
        for (int i = 0; i < HAND_SIZE; ++i) hand_.push_back(tile(rng_));

        steps_ = 0;
        return observe();
    }

    // ── 一步 ──────────────────────────────────────────────
    // 回傳 { 新狀態, 獎勵, 是否結束 }
    struct StepResult {
        std::vector<double> state;
        double reward;
        bool done;
    };

    StepResult step(int action) {
        ++steps_;
        double reward = 0.0;

        if (action == DRAW_ACTION) {
            // 抽牌：如果手上其實有能出的，這是浪費一個回合
            reward = hasPlayable() ? -1.0 : 0.5;
            std::uniform_int_distribution<int> tile(1, 13);
            hand_.push_back(tile(rng_));
            if (hand_.size() > HAND_SIZE) hand_.erase(hand_.begin());
        } else {
            int t = hand_[action];
            if (t == run_head_ - 1 && t >= 1) {
                run_head_ = t;
                reward = 1.0;                 // 接對了
                replaceTile(action);
            } else if (t == run_tail_ + 1 && t <= 13) {
                run_tail_ = t;
                reward = 1.0;
                replaceTile(action);
            } else {
                reward = -1.0;                // 出了接不上的牌
            }
        }

        bool done = (steps_ >= MAX_STEPS);
        return { observe(), reward, done };
    }

    // ── 給人看的：目前有沒有可以出的牌 ───────────────────
    bool hasPlayable() const {
        for (int t : hand_) if (playable(t)) return true;
        return false;
    }

    // ── 最佳動作（用來評估 AI 學得如何）─────────────────
    //
    // 注意：手上可能同時有好幾張接得上，那些都同樣正確。
    // 早期版本的評估只認「第一張可出的」，把「選了第三張」判成錯——
    // **那是量錯了，不是 agent 學錯了。**
    // 這種指標上的瑕疵會讓真實表現被低估，而且不會有任何錯誤訊息。
    int bestAction() const {
        for (int i = 0; i < HAND_SIZE; ++i)
            if (playable(hand_[i])) return i;
        return DRAW_ACTION;
    }

    // 判斷一個動作是不是「正確」——任何可出的牌都算對
    bool isOptimal(int action) const {
        if (action == DRAW_ACTION) return !hasPlayable();
        if (action < 0 || action >= static_cast<int>(hand_.size())) return false;
        return playable(hand_[action]);
    }

    int runHead() const { return run_head_; }
    int runTail() const { return run_tail_; }
    const std::vector<int>& hand() const { return hand_; }

private:
    std::mt19937 rng_;
    int run_head_ = 4, run_tail_ = 6;
    std::vector<int> hand_;
    int steps_ = 0;

    bool playable(int t) const {
        return (t == run_head_ - 1 && t >= 1) ||
               (t == run_tail_ + 1 && t <= 13);
    }

    void replaceTile(int idx) {
        std::uniform_int_distribution<int> tile(1, 13);
        hand_[idx] = tile(rng_);
    }

    std::vector<double> observe() const {
        std::vector<double> s(STATE_DIM, 0.0);
        s[0] = run_head_ / 13.0;
        s[1] = run_tail_ / 13.0;
        for (int i = 0; i < HAND_SIZE; ++i) {
            s[2 + i] = (i < static_cast<int>(hand_.size()))
                     ? hand_[i] / 13.0 : 0.0;
            s[2 + HAND_SIZE + i] = (i < static_cast<int>(hand_.size()) &&
                                    playable(hand_[i])) ? 1.0 : 0.0;
        }
        return s;
    }
};
