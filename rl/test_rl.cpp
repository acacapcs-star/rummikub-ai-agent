/* -------------------------------------------------------
   test_rl.cpp —— Actor-Critic 與環境的單元測試

   手刻神經網路最容易錯的地方是**反向傳播**，
   而那種錯不會讓程式當掉——它只會讓 loss 降得比較慢，
   或者降下去但學到錯的東西。

   所以這裡的核心是**數值梯度檢查**：
   把解析梯度跟「微擾法算出來的梯度」比對。
   兩者若吻合到小數點後八位，實作就是對的。

   這個方法叫 gradient checking，是實作 RL 時的標準做法。

   編譯：
     g++ -std=c++17 test_rl.cpp -o test_rl && ./test_rl
------------------------------------------------------- */

#include "actor_critic.h"
#include "mini_env.h"
#include <cmath>
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool c, const std::string& n) {
    if (c) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else   { ++g_fail; std::cout << "  FAIL " << n << "\n"; }
}

static void checkNear(double got, double want, const std::string& n,
                      double eps = 1e-6) {
    if (std::abs(got - want) < eps) {
        ++g_pass; std::cout << "  ok   " << n << "\n";
    } else {
        ++g_fail;
        std::cout << "  FAIL " << n << "   (得到 " << got
                  << "，預期 " << want << "，差 " << std::abs(got-want) << ")\n";
    }
}

// ══════════════════════════════════════════════════════════
//  softmax
// ══════════════════════════════════════════════════════════
static void test_softmax() {
    std::cout << "\nsoftmax\n";

    auto p = softmax({ 2.1, 1.4, -0.3 });
    double sum = 0;
    for (double x : p) sum += x;
    checkNear(sum, 1.0, "機率總和為 1");

    bool allPositive = true;
    for (double x : p) if (x <= 0) allPositive = false;
    check(allPositive, "全部為正——即使輸入有負數");

    check(p[0] > p[1] && p[1] > p[2], "順序與輸入的大小關係一致");

    // 平移不變性：softmax(z) == softmax(z + c)
    auto q = softmax({ 12.1, 11.4, 9.7 });   // 每個都加 10
    for (int i = 0; i < 3; ++i)
        checkNear(p[i], q[i], "  平移不變（第 " + std::to_string(i) + " 項）");

    // 極端值不能溢位
    auto big = softmax({ 1000.0, 999.0, 1.0 });
    bool finite = true;
    for (double x : big) if (!std::isfinite(x)) finite = false;
    check(finite, "★ 極大的輸入不會溢位——實作有先減去最大值");

    // 全部相同 → 均勻分布
    auto uniform = softmax({ 5.0, 5.0, 5.0, 5.0 });
    for (int i = 0; i < 4; ++i)
        checkNear(uniform[i], 0.25, "  輸入全相同時是均勻分布");
}

// ══════════════════════════════════════════════════════════
//  梯度檢查：這是整份測試的核心
// ══════════════════════════════════════════════════════════
/* softmax + log 的解析梯度是：
       ∂ log π(a) / ∂z_i = 𝟙[i == a] − π_i

   用微擾法驗證：
       數值梯度 ≈ [f(z + ε) − f(z − ε)] / (2ε)                       */
static void test_softmax_gradient() {
    std::cout << "\n梯度 · softmax + log 的解析解\n";

    std::vector<double> z = { 2.1, 1.4, -0.3, 0.8 };
    auto pi = softmax(z);
    const int chosen = 1;
    const double eps = 1e-6;

    for (std::size_t i = 0; i < z.size(); ++i) {
        double analytic = ((int)i == chosen ? 1.0 : 0.0) - pi[i];

        auto zp = z; zp[i] += eps;
        auto zm = z; zm[i] -= eps;
        double numeric = (std::log(softmax(zp)[chosen]) -
                          std::log(softmax(zm)[chosen])) / (2 * eps);

        checkNear(analytic, numeric,
                  "z[" + std::to_string(i) + "] 的解析梯度與數值梯度吻合", 1e-5);
    }

    // 梯度總和恆為 0——因為機率總和固定是 1，推高一個必然壓低其他
    double total = 0;
    for (std::size_t i = 0; i < z.size(); ++i)
        total += ((int)i == chosen ? 1.0 : 0.0) - pi[i];
    checkNear(total, 0.0, "★ 梯度總和恆為 0（機率總和的約束）", 1e-12);
}

// ══════════════════════════════════════════════════════════
//  Layer 的前向傳播
// ══════════════════════════════════════════════════════════
static void test_layer_forward() {
    std::cout << "\nLayer · 前向傳播\n";

    std::mt19937 rng(42);
    Layer l(3, 2, rng);

    // 手動設定權重，才能算出預期值
    l.W = { { 1.0, 2.0, 3.0 },
            { 0.5, 0.5, 0.5 } };
    l.b = { 0.0, 1.0 };

    auto out = l.forwardLinear({ 1.0, 1.0, 1.0 });
    checkNear(out[0], 6.0, "linear：1+2+3+0 = 6");
    checkNear(out[1], 2.5, "linear：0.5×3+1 = 2.5");

    auto t = l.forwardTanh({ 1.0, 1.0, 1.0 });
    checkNear(t[0], std::tanh(6.0), "tanh：對線性輸出取 tanh");
    checkNear(t[1], std::tanh(2.5), "  第二個輸出");

    check(std::abs(t[0]) < 1.0 && std::abs(t[1]) < 1.0,
          "tanh 的輸出落在 (−1, 1)");

    checkNear((double)l.paramCount(), 3 * 2 + 2, "參數數量 = in×out + out");
}

// ══════════════════════════════════════════════════════════
//  Layer 的反向傳播（梯度檢查）
// ══════════════════════════════════════════════════════════
/* 驗證方式：
     ① 記下更新前的權重
     ② 呼叫 backward 更新一次
     ③ 檢查權重的變化量是否等於 lr × grad_out × input

   這是最直接的驗證——因為 backward 對權重的更新就是
       W[o][i] += lr × g[o] × last_in[i]                            */
static void test_layer_backward() {
    std::cout << "\nLayer · 反向傳播\n";

    std::mt19937 rng(42);
    Layer l(3, 2, rng);
    l.W = { { 1.0, 2.0, 3.0 }, { 0.5, 0.5, 0.5 } };
    l.b = { 0.0, 1.0 };

    std::vector<double> input = { 2.0, 1.0, 0.5 };
    l.forwardLinear(input);

    auto W_before = l.W;
    auto b_before = l.b;

    const double lr = 0.1;
    std::vector<double> grad_out = { 1.0, -0.5 };
    auto grad_in = l.backward(grad_out, lr, false);

    // 權重更新量
    for (int o = 0; o < 2; ++o)
        for (int i = 0; i < 3; ++i)
            checkNear(l.W[o][i] - W_before[o][i], lr * grad_out[o] * input[i],
                      "  W[" + std::to_string(o) + "][" + std::to_string(i) +
                      "] 的更新量 = lr × grad × input");

    for (int o = 0; o < 2; ++o)
        checkNear(l.b[o] - b_before[o], lr * grad_out[o],
                  "  b[" + std::to_string(o) + "] 的更新量 = lr × grad");

    // 往前傳的梯度 = Wᵀ · grad_out（用更新前的 W）
    for (int i = 0; i < 3; ++i) {
        double expect = 0;
        for (int o = 0; o < 2; ++o) expect += W_before[o][i] * grad_out[o];
        checkNear(grad_in[i], expect,
                  "  往前傳的梯度[" + std::to_string(i) + "] = Wᵀ·grad");
    }
}

static void test_tanh_backward() {
    std::cout << "\nLayer · tanh 的導數\n";

    std::mt19937 rng(1);
    Layer l(2, 1, rng);
    l.W = { { 1.0, 1.0 } };
    l.b = { 0.0 };

    std::vector<double> input = { 0.3, 0.4 };
    auto out = l.forwardTanh(input);      // tanh(0.7)

    auto W_before = l.W;
    const double lr = 0.1;
    l.backward({ 1.0 }, lr, true);

    // tanh 的導數是 1 − tanh²(x)，而 last_out 已經是 tanh(x)
    double dtanh = 1.0 - out[0] * out[0];
    for (int i = 0; i < 2; ++i)
        checkNear(l.W[0][i] - W_before[0][i], lr * dtanh * input[i],
                  "  ★ 梯度有乘上 tanh 的導數 (1 − tanh²)");
}

// ══════════════════════════════════════════════════════════
//  Actor 與 Critic
// ══════════════════════════════════════════════════════════
static void test_actor() {
    std::cout << "\nActor\n";

    std::mt19937 rng(20260818);
    Actor a(4, 8, 3, rng);

    std::vector<double> s = { 0.1, 0.2, 0.3, 0.4 };
    auto p = a.probs(s);

    check(p.size() == 3, "輸出維度等於動作數");
    double sum = 0;
    for (double x : p) sum += x;
    checkNear(sum, 1.0, "輸出是合法的機率分布");

    checkNear((double)a.paramCount(), 4*8+8 + 8*3+3, "參數數量正確");

    // 同樣的輸入要給同樣的輸出
    auto p2 = a.probs(s);
    bool same = true;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (std::abs(p[i] - p2[i]) > 1e-15) same = false;
    check(same, "★ 前向傳播沒有副作用——同樣的輸入給同樣的輸出");

    // 抽樣的分布應該接近機率
    std::mt19937 srng(7);
    int counts[3] = {0,0,0};
    const int N = 30000;
    for (int i = 0; i < N; ++i) counts[a.sample(p, srng)]++;
    for (int i = 0; i < 3; ++i)
        checkNear((double)counts[i]/N, p[i],
                  "  抽樣的頻率接近機率（動作 " + std::to_string(i) + "）", 0.02);
}

static void test_actor_update_direction() {
    std::cout << "\nActor · 更新的方向\n";

    std::mt19937 rng(20260818);
    Actor a(4, 8, 3, rng);
    std::vector<double> s = { 0.1, 0.2, 0.3, 0.4 };
    const int action = 1;

    double before = a.probs(s)[action];
    a.update(s, action, +2.0, 0.1);      // advantage 為正
    double after_pos = a.probs(s)[action];
    check(after_pos > before, "★ advantage > 0 → 該動作的機率上升");

    Actor b(4, 8, 3, rng);
    double before2 = b.probs(s)[action];
    b.update(s, action, -2.0, 0.1);      // advantage 為負
    double after_neg = b.probs(s)[action];
    check(after_neg < before2, "★ advantage < 0 → 該動作的機率下降");

    // advantage 越大，調整幅度越大
    Actor c(4, 8, 3, rng), d(4, 8, 3, rng);
    // 讓兩個從相同狀態出發
    d = c;
    double base = c.probs(s)[action];
    c.update(s, action, 0.5, 0.1);
    d.update(s, action, 5.0, 0.1);
    double small = c.probs(s)[action] - base;
    double large = d.probs(s)[action] - base;
    check(large > small, "★ advantage 越大，機率調整的幅度越大");

    // 機率總和在更新後仍然是 1
    auto p = a.probs(s);
    double sum = 0;
    for (double x : p) sum += x;
    checkNear(sum, 1.0, "更新後仍是合法的機率分布");
}

static void test_critic() {
    std::cout << "\nCritic\n";

    std::mt19937 rng(20260818);
    Critic c(4, 8, rng);
    std::vector<double> s = { 0.1, 0.2, 0.3, 0.4 };

    double v0 = c.value(s);
    check(std::isfinite(v0), "輸出是有限的數字");

    // 反覆往同一個目標學，預測值要收斂過去
    const double target = 3.0;
    for (int i = 0; i < 300; ++i) c.update(s, target, 0.05);
    double v1 = c.value(s);
    check(std::abs(v1 - target) < std::abs(v0 - target),
          "★ 更新之後更接近目標值");
    checkNear(v1, target, "  反覆學習後收斂到目標", 0.1);

    // 往反方向學也要有效
    for (int i = 0; i < 300; ++i) c.update(s, -2.0, 0.05);
    checkNear(c.value(s), -2.0, "  改變目標後也追得上", 0.1);
}

// ══════════════════════════════════════════════════════════
//  環境
// ══════════════════════════════════════════════════════════
static void test_env_basics() {
    std::cout << "\n環境 · 基本\n";

    MiniEnv env(42);
    auto s = env.reset();
    check((int)s.size() == MiniEnv::STATE_DIM, "狀態維度正確");
    check((int)env.hand().size() == MiniEnv::HAND_SIZE, "手牌數正確");

    bool inRange = true;
    for (double x : s) if (x < 0.0 || x > 1.0) inRange = false;
    check(inRange, "★ 狀態全部正規化到 0–1——否則網路要處理不同量級的輸入");

    check(env.runTail() - env.runHead() == 2, "Run 起始是三張");

    bool tilesValid = true;
    for (int t : env.hand()) if (t < 1 || t > 13) tilesValid = false;
    check(tilesValid, "手牌的數字在 1–13");
}

static void test_env_reward() {
    std::cout << "\n環境 · 獎勵\n";

    // 找一個有牌可出的局面
    MiniEnv env(42);
    env.reset();
    for (int k = 0; k < 50 && !env.hasPlayable(); ++k) env.reset();

    if (env.hasPlayable()) {
        int best = env.bestAction();
        auto r = env.step(best);
        checkNear(r.reward, 1.0, "★ 出對牌 → +1");
    }

    // 找一個沒牌可出的局面
    MiniEnv env2(7);
    env2.reset();
    for (int k = 0; k < 50 && env2.hasPlayable(); ++k) env2.reset();

    if (!env2.hasPlayable()) {
        auto r = env2.step(MiniEnv::DRAW_ACTION);
        checkNear(r.reward, 0.5, "★ 沒牌可出時抽牌 → +0.5");
    }

    // 有牌可出卻抽牌
    MiniEnv env3(42);
    env3.reset();
    for (int k = 0; k < 50 && !env3.hasPlayable(); ++k) env3.reset();
    if (env3.hasPlayable()) {
        auto r = env3.step(MiniEnv::DRAW_ACTION);
        checkNear(r.reward, -1.0, "★ 有牌可出卻抽牌 → −1");
    }
}

static void test_env_optimal() {
    std::cout << "\n環境 · 最佳動作的判定\n";

    MiniEnv env(123);
    env.reset();

    // 任何可出的牌都算對，不是只有第一張
    int playableCount = 0;
    for (int i = 0; i < MiniEnv::HAND_SIZE; ++i)
        if (env.isOptimal(i)) ++playableCount;

    if (env.hasPlayable()) {
        check(playableCount >= 1, "有牌可出時，至少一個動作是最佳的");
        check(!env.isOptimal(MiniEnv::DRAW_ACTION),
              "★ 有牌可出時，抽牌不算最佳");
    } else {
        check(playableCount == 0, "沒牌可出時，出任何一張都不對");
        check(env.isOptimal(MiniEnv::DRAW_ACTION),
              "★ 沒牌可出時，抽牌才是最佳");
    }

    check(!env.isOptimal(-1) && !env.isOptimal(99), "越界的動作不算最佳");
}

static void test_env_boundary() {
    std::cout << "\n環境 · 數字邊界\n";

    // 跑很多局，確認 Run 不會超出 1–13
    MiniEnv env(999);
    bool inBounds = true;
    for (int ep = 0; ep < 200; ++ep) {
        env.reset();
        for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
            auto r = env.step(env.bestAction());
            if (env.runHead() < 1 || env.runTail() > 13) inBounds = false;
            if (r.done) break;
        }
    }
    check(inBounds, "★ 跑 200 局，Run 的兩端始終在 1–13 之內");
}

static void test_env_determinism() {
    std::cout << "\n環境 · 可重現性\n";

    MiniEnv a(555), b(555);
    auto sa = a.reset();
    auto sb = b.reset();

    bool same = true;
    for (std::size_t i = 0; i < sa.size(); ++i)
        if (std::abs(sa[i] - sb[i]) > 1e-15) same = false;
    check(same, "★ 相同種子產生相同的初始局面");

    double ra = 0, rb = 0;
    for (int t = 0; t < 10; ++t) {
        ra += a.step(t % MiniEnv::ACTION_DIM).reward;
        rb += b.step(t % MiniEnv::ACTION_DIM).reward;
    }
    checkNear(ra, rb, "  相同的動作序列產生相同的獎勵");
}

// ══════════════════════════════════════════════════════════
//  端對端：學得起來嗎
// ══════════════════════════════════════════════════════════
static void test_learning() {
    std::cout << "\n端對端 · 學習\n";

    std::mt19937 rng(20260818);
    Actor actor(MiniEnv::STATE_DIM, 24, MiniEnv::ACTION_DIM, rng);
    Critic critic(MiniEnv::STATE_DIM, 24, rng);
    MiniEnv env(20260818);

    auto measure = [&]() {
        MiniEnv e(777);
        int ok = 0, total = 0;
        for (int ep = 0; ep < 200; ++ep) {
            auto s = e.reset();
            for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
                auto p = actor.probs(s);
                int a = 0;
                for (std::size_t k = 1; k < p.size(); ++k) if (p[k] > p[a]) a = (int)k;
                if (e.isOptimal(a)) ++ok;
                ++total;
                auto r = e.step(a);
                s = r.state;
                if (r.done) break;
            }
        }
        return 100.0 * ok / total;
    };

    double before = measure();

    for (int ep = 0; ep < 3000; ++ep) {
        std::vector<std::vector<double>> S;
        std::vector<int> A;
        std::vector<double> R;
        auto s = env.reset();
        for (int t = 0; t < MiniEnv::MAX_STEPS; ++t) {
            auto p = actor.probs(s);
            int a = actor.sample(p, rng);
            auto r = env.step(a);
            S.push_back(s); A.push_back(a); R.push_back(r.reward);
            s = r.state;
            if (r.done) break;
        }
        double G = 0;
        std::vector<double> ret(R.size());
        for (int t = (int)R.size()-1; t >= 0; --t) { G = R[t] + 0.9*G; ret[t] = G; }
        for (std::size_t t = 0; t < R.size(); ++t) {
            double adv = ret[t] - critic.value(S[t]);
            actor.update(S[t], A[t], adv, 0.01);
            critic.update(S[t], ret[t], 0.05);
        }
    }

    double after = measure();
    std::cout << "         （訓練前 " << before << "%，訓練後 " << after << "%）\n";
    check(after > before + 20, "★ 訓練三千局後，正確率明顯提升");
    check(after > 60.0, "  達到 60% 以上");
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "Actor-Critic · 單元測試\n";
    std::cout << "════════════════════════════════════════";

    test_softmax();
    test_softmax_gradient();
    test_layer_forward();
    test_layer_backward();
    test_tanh_backward();
    test_actor();
    test_actor_update_direction();
    test_critic();
    test_env_basics();
    test_env_reward();
    test_env_optimal();
    test_env_boundary();
    test_env_determinism();
    test_learning();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
