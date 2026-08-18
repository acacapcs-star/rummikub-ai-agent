#pragma once
#include <cmath>
#include <random>
#include <vector>

/* =========================================================================
   actor_critic.h —— 手刻的 Actor-Critic

   為什麼不用 PyTorch：
     這個網路只有幾百個參數。手刻的話，每一步梯度都看得見——
     呼叫 loss.backward() 學不到那些東西。

   兩個網路，共用同樣的結構但輸出不同：

     Actor   狀態 → 每個動作的機率      （決定做什麼）
     Critic  狀態 → 一個數字            （這個局面值多少分）

   兩者的分工：
     Critic 提供「基準線」，Actor 依「實際比基準線好多少」調整。
     那個差值叫 advantage：

       advantage = 實際回報 − Critic 的預期

     advantage > 0  這一步比預期好 → 提高該動作的機率
     advantage < 0  比預期差       → 降低

   為什麼要基準線：
     如果直接用「實際回報」當訊號，那麼在一個每步都能拿高分的局面裡，
     所有動作都會被鼓勵——包括不好的那些。
     **基準線讓訊號變成「相對好壞」而不是「絕對分數」。**
   ========================================================================= */

// ── 一層全連接：out = act(W·in + b) ──────────────────────
struct Layer {
    int in_dim, out_dim;
    std::vector<std::vector<double>> W;   // out_dim × in_dim
    std::vector<double> b;

    // 反向傳播時要用的暫存
    std::vector<double> last_in;
    std::vector<double> last_out;         // 經過 activation 之後

    Layer(int i, int o, std::mt19937& rng) : in_dim(i), out_dim(o) {
        // Xavier 初始化：讓每一層的輸出變異數維持穩定，
        // 否則深一點的網路會出現梯度消失或爆炸。
        double scale = std::sqrt(2.0 / (i + o));
        std::normal_distribution<double> nd(0.0, scale);
        W.assign(o, std::vector<double>(i));
        for (auto& row : W) for (double& w : row) w = nd(rng);
        b.assign(o, 0.0);
    }

    // tanh 版（隱藏層用）
    std::vector<double> forwardTanh(const std::vector<double>& x) {
        last_in = x;
        std::vector<double> out(out_dim);
        for (int o = 0; o < out_dim; ++o) {
            double s = b[o];
            for (int i = 0; i < in_dim; ++i) s += W[o][i] * x[i];
            out[o] = std::tanh(s);
        }
        last_out = out;
        return out;
    }

    // 線性版（輸出層用）
    std::vector<double> forwardLinear(const std::vector<double>& x) {
        last_in = x;
        std::vector<double> out(out_dim);
        for (int o = 0; o < out_dim; ++o) {
            double s = b[o];
            for (int i = 0; i < in_dim; ++i) s += W[o][i] * x[i];
            out[o] = s;
        }
        last_out = out;
        return out;
    }

    // 反向傳播：吃「對本層輸出的梯度」，回傳「對本層輸入的梯度」，
    // 同時把 W 和 b 的梯度累加進去。
    std::vector<double> backward(const std::vector<double>& grad_out,
                                 double lr, bool through_tanh) {
        std::vector<double> g = grad_out;

        // tanh 的導數：1 − tanh²(x)，而 last_out 已經是 tanh(x)
        if (through_tanh)
            for (int o = 0; o < out_dim; ++o)
                g[o] *= (1.0 - last_out[o] * last_out[o]);

        // 對輸入的梯度（要往前一層傳）
        std::vector<double> grad_in(in_dim, 0.0);
        for (int i = 0; i < in_dim; ++i)
            for (int o = 0; o < out_dim; ++o)
                grad_in[i] += W[o][i] * g[o];

        // 更新權重
        for (int o = 0; o < out_dim; ++o) {
            for (int i = 0; i < in_dim; ++i)
                W[o][i] += lr * g[o] * last_in[i];
            b[o] += lr * g[o];
        }
        return grad_in;
    }

    int paramCount() const { return in_dim * out_dim + out_dim; }
};

// ── softmax：把任意實數轉成合法機率 ─────────────────────
inline std::vector<double> softmax(const std::vector<double>& z) {
    // 先減去最大值再取指數，避免 e^大數 溢位。
    // 這不改變結果，因為 softmax 對輸入的平移不變。
    double mx = z[0];
    for (double v : z) if (v > mx) mx = v;

    std::vector<double> e(z.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < z.size(); ++i) {
        e[i] = std::exp(z[i] - mx);
        sum += e[i];
    }
    for (double& v : e) v /= sum;
    return e;
}

/* =========================================================================
   Actor：狀態 → 動作機率
   ========================================================================= */
class Actor {
public:
    Actor(int state_dim, int hidden, int action_dim, std::mt19937& rng)
        : l1_(state_dim, hidden, rng), l2_(hidden, action_dim, rng) {}

    std::vector<double> probs(const std::vector<double>& s) {
        auto h = l1_.forwardTanh(s);
        auto z = l2_.forwardLinear(h);
        last_probs_ = softmax(z);
        return last_probs_;
    }

    // 依機率抽一個動作
    int sample(const std::vector<double>& p, std::mt19937& rng) const {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        double r = u(rng), acc = 0.0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            acc += p[i];
            if (r <= acc) return static_cast<int>(i);
        }
        return static_cast<int>(p.size()) - 1;
    }

    /* 更新：往「讓這個動作機率變高」的方向走，幅度由 advantage 決定。

       softmax + log 的梯度有一個異常簡潔的形式：
         ∂ log π(a) / ∂z_i  =  𝟙[i == a] − π_i

       乘上 advantage 之後：
         advantage > 0 → 被選中的動作往上推，其他往下壓
         advantage < 0 → 整個翻轉

       注意梯度總和恆為 0——因為機率總和固定是 1，
       推高一個必然壓低其他。                                        */
    void update(const std::vector<double>& s, int action,
                double advantage, double lr) {
        auto p = probs(s);      // 重新前向一次，填好 last_in / last_out
        std::vector<double> grad_z(p.size());
        for (std::size_t i = 0; i < p.size(); ++i)
            grad_z[i] = advantage * (((int)i == action ? 1.0 : 0.0) - p[i]);

        auto grad_h = l2_.backward(grad_z, lr, false);
        l1_.backward(grad_h, lr, true);
    }

    int paramCount() const { return l1_.paramCount() + l2_.paramCount(); }

private:
    Layer l1_, l2_;
    std::vector<double> last_probs_;
};

/* =========================================================================
   Critic：狀態 → 一個數字（這個局面值多少分）
   ========================================================================= */
class Critic {
public:
    Critic(int state_dim, int hidden, std::mt19937& rng)
        : l1_(state_dim, hidden, rng), l2_(hidden, 1, rng) {}

    double value(const std::vector<double>& s) {
        auto h = l1_.forwardTanh(s);
        return l2_.forwardLinear(h)[0];
    }

    /* 更新：讓預測值靠近實際回報。

       這是單純的迴歸——用均方誤差，梯度就是 (target − pred)。
       跟 Actor 不同，Critic 不需要 advantage，
       因為它要學的就是「實際上值多少」。                            */
    void update(const std::vector<double>& s, double target, double lr) {
        double pred = value(s);
        std::vector<double> grad = { target - pred };
        auto grad_h = l2_.backward(grad, lr, false);
        l1_.backward(grad_h, lr, true);
    }

    int paramCount() const { return l1_.paramCount() + l2_.paramCount(); }

private:
    Layer l1_, l2_;
};
