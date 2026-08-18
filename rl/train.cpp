#include "actor_critic.h"
#include "mini_env.h"
#include <cstdio>
#include <string>
#include <vector>

/* =========================================================================
   train.cpp —— 用 Actor-Critic 學會簡化版拉密

   訓練迴圈的骨架：

     for 每一局:
       ① 跑完一局，記下每一步的 (狀態, 動作, 獎勵)
       ② 從後往前算「折扣回報」G
       ③ advantage = G − Critic(狀態)
       ④ Actor 依 advantage 更新
       ⑤ Critic 學著把預測靠近 G

   為什麼 ② 要從後往前：
     G_t = r_t + γ·r_{t+1} + γ²·r_{t+2} + ...
     整理一下就是 G_t = r_t + γ·G_{t+1}
     所以從最後一步往回推，每一步只要一次乘加。

   γ（折扣因子）在做什麼：
     它決定「未來的獎勵值多少」。
       γ = 0    只看眼前這一步
       γ = 0.99 幾乎跟眼前一樣重視二十步之後
     這個環境的獎勵是即時的（出對牌馬上 +1），
     所以 γ 設低一點就夠。

   編譯：
     g++ -std=c++17 -O2 train.cpp -o train && ./train
   ========================================================================= */

// ── 超參數 ───────────────────────────────────────────────
static const int    HIDDEN        = 24;
static const double LR_ACTOR      = 0.01;
static const double LR_CRITIC     = 0.05;   // Critic 學快一點，
                                            // 因為 Actor 依賴它的估計；
                                            // 基準線不準的話，Actor 會被誤導
static const double GAMMA         = 0.9;
static const int    EPISODES      = 20000;
static const int    EVAL_EVERY    = 2000;
static const int    EVAL_EPISODES = 500;

// ── 評估：跟「最佳動作」比對正確率 ───────────────────────
// 這個環境簡單到可以算出正解，所以能直接量「AI 學得多準」。
// 完整拉密沒有這個奢侈——那也是簡化版的價值之一。
struct EvalResult {
    double accuracy;      // 選到最佳動作的比例
    double avg_reward;    // 平均每步獎勵
};

static EvalResult evaluate(Actor& actor, unsigned seed) {
    MiniEnv env(seed);
    std::mt19937 rng(seed + 999);
    int correct = 0, total = 0;
    double reward_sum = 0.0;

    for (int ep = 0; ep < EVAL_EPISODES; ++ep) {
        auto s = env.reset();
        for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
            auto p = actor.probs(s);

            // 評估時取機率最高的（不抽籤）——
            // 我們要量的是「學到什麼」，不是「探索得多好」
            int a = 0;
            for (std::size_t i = 1; i < p.size(); ++i)
                if (p[i] > p[a]) a = static_cast<int>(i);

            if (env.isOptimal(a)) ++correct;
            ++total;

            auto r = env.step(a);
            reward_sum += r.reward;
            s = r.state;
            if (r.done) break;
        }
    }
    return { 100.0 * correct / total, reward_sum / total };
}

// ── 隨機策略的基準線 ─────────────────────────────────────
// 沒有這個基準，「正確率 60%」是好是壞說不準。
static EvalResult randomBaseline(unsigned seed) {
    MiniEnv env(seed);
    std::mt19937 rng(seed + 555);
    std::uniform_int_distribution<int> pick(0, MiniEnv::ACTION_DIM - 1);
    int correct = 0, total = 0;
    double reward_sum = 0.0;

    for (int ep = 0; ep < EVAL_EPISODES; ++ep) {
        env.reset();
        for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
            int a = pick(rng);
            if (env.isOptimal(a)) ++correct;
            ++total;
            auto r = env.step(a);
            reward_sum += r.reward;
            if (r.done) break;
        }
    }
    return { 100.0 * correct / total, reward_sum / total };
}

int main() {
    std::mt19937 rng(20260818);      // 固定種子，結果可重現

    Actor  actor (MiniEnv::STATE_DIM, HIDDEN, MiniEnv::ACTION_DIM, rng);
    Critic critic(MiniEnv::STATE_DIM, HIDDEN, rng);
    MiniEnv env(20260818);

    printf("═══════════════════════════════════════════════════════\n");
    printf(" Actor-Critic · 簡化版拉密\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    printf("  狀態維度   %d\n", MiniEnv::STATE_DIM);
    printf("  動作數     %d （出五張其中一張，或抽牌）\n", MiniEnv::ACTION_DIM);
    printf("  Actor 參數 %d\n", actor.paramCount());
    printf("  Critic參數 %d\n", critic.paramCount());
    printf("  γ = %.2f   lr_actor = %.3f   lr_critic = %.3f\n\n",
           GAMMA, LR_ACTOR, LR_CRITIC);

    EvalResult base = randomBaseline(777);
    printf("  隨機策略基準：正確率 %.1f%%   平均獎勵 %+.3f\n\n",
           base.accuracy, base.avg_reward);

    printf("  ── 訓練開始 ──\n");
    printf("  %8s  %10s  %12s  %10s\n",
           "episode", "正確率", "平均獎勵", "Critic誤差");

    double critic_err_sum = 0.0;
    int critic_err_n = 0;

    for (int ep = 1; ep <= EPISODES; ++ep) {
        // ── ① 跑完一局，記錄軌跡 ──────────────────────────
        std::vector<std::vector<double>> states;
        std::vector<int> actions;
        std::vector<double> rewards;

        auto s = env.reset();
        for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
            auto p = actor.probs(s);
            int a = actor.sample(p, rng);        // 訓練時抽籤 → 保留探索

            auto r = env.step(a);
            states.push_back(s);
            actions.push_back(a);
            rewards.push_back(r.reward);
            s = r.state;
            if (r.done) break;
        }

        // ── ② 從後往前算折扣回報 ─────────────────────────
        int T = static_cast<int>(rewards.size());
        std::vector<double> returns(T);
        double G = 0.0;
        for (int t = T - 1; t >= 0; --t) {
            G = rewards[t] + GAMMA * G;
            returns[t] = G;
        }

        // ── ③④⑤ 更新 ────────────────────────────────────
        for (int t = 0; t < T; ++t) {
            double v = critic.value(states[t]);
            double advantage = returns[t] - v;       // ← 核心：相對好壞

            critic_err_sum += std::abs(advantage);
            ++critic_err_n;

            actor.update(states[t], actions[t], advantage, LR_ACTOR);
            critic.update(states[t], returns[t], LR_CRITIC);
        }

        // ── 定期評估 ─────────────────────────────────────
        if (ep % EVAL_EVERY == 0) {
            EvalResult e = evaluate(actor, 777);
            printf("  %8d  %9.1f%%  %+11.3f  %10.3f\n",
                   ep, e.accuracy, e.avg_reward,
                   critic_err_sum / critic_err_n);
            critic_err_sum = 0.0; critic_err_n = 0;
        }
    }

    // ── 最終結果 ─────────────────────────────────────────
    EvalResult final = evaluate(actor, 777);
    printf("\n  ── 結果 ──\n");
    printf("  %-12s %8s  %10s\n", "", "正確率", "平均獎勵");
    printf("  %-12s %7.1f%%  %+10.3f\n", "隨機策略", base.accuracy, base.avg_reward);
    printf("  %-12s %7.1f%%  %+10.3f\n", "訓練後",   final.accuracy, final.avg_reward);
    printf("  %-12s %7.1f%%\n", "提升",
           final.accuracy - base.accuracy);

    // ── 看它在具體局面上怎麼選 ───────────────────────────
    printf("\n  ── 抽樣檢查 ──\n");
    MiniEnv demo(12345);
    for (int i = 0; i < 5; ++i) {
        auto st = demo.reset();
        auto p  = actor.probs(st);
        int chosen = 0;
        for (std::size_t k = 1; k < p.size(); ++k)
            if (p[k] > p[chosen]) chosen = static_cast<int>(k);
        int best = demo.bestAction();
        bool ok = demo.isOptimal(chosen);

        printf("\n  桌面 %d-%d   手牌 ", demo.runHead(), demo.runTail());
        for (int t : demo.hand()) printf("%d ", t);

        printf("\n    機率 ");
        for (std::size_t k = 0; k < p.size(); ++k) {
            if (k == MiniEnv::DRAW_ACTION) printf("[抽牌 %.0f%%] ", p[k]*100);
            else printf("[第%zu張 %.0f%%] ", k+1, p[k]*100);
        }
        printf("\n    選擇 %s   正解 %s   %s\n",
               chosen == MiniEnv::DRAW_ACTION ? "抽牌"
                   : ("第" + std::to_string(chosen+1) + "張").c_str(),
               best == MiniEnv::DRAW_ACTION ? "抽牌"
                   : ("第" + std::to_string(best+1) + "張").c_str(),
               ok ? "✓" : "✗");
    }

    printf("\n═══════════════════════════════════════════════════════\n");
    printf(" 這個實驗證明的是「Actor-Critic 能學會拉密的子問題」，\n");
    printf(" 不是「AI 學會打拉密」。完整拉密的動作空間有數萬種，\n");
    printf(" 需要處理變長狀態與稀疏獎勵，超出這個階段的範圍。\n");
    return 0;
}
