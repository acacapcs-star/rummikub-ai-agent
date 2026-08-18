#include "recap.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

/* =========================================================================
   recap_experiment.cpp —— 三種過關條件的比較

   問題：關卡結束的五題 MCQ，要多少題對才算過？

     ALL_CORRECT   五題全對
     THRESHOLD     答對 3 題以上
     NO_GATE       不擋，只是給你看

   直覺上會覺得「越嚴越好」，但那要看它擋掉的是誰。
   一個把真的懂的人也擋在外面的門檻，只是在製造挫折。

   所以要量三件事：

     過關率     有多少人卡在這裡
     重試次數   平均試幾次才過
     **鑑別度** 真懂的人與不懂的人，過關率差多少   ← 最重要

   第三個是關鍵。如果三種條件下兩種人的過關率差不多，
   那這道關卡就沒有篩選作用——它只是在浪費所有人的時間。

   ── 這個實驗的限制 ────────────────────────────────────
   學習者的「理解程度」與答對機率的對應是我設的參數，不是量測值。
   真人答題不是獨立的伯努利試驗——他們會從前一題學到東西、
   會疲勞、會用刪去法。所以這只能回答「機制的篩選效果」，
   不能回答「真人會不會因此學得更好」。
   ========================================================================= */

// ── 模擬的學習者 ─────────────────────────────────────────
struct Learner {
    std::string name;
    double understanding;   // 0–1，對這一關的理解程度
    double hint_boost;      // 看了提示之後機率提升多少
};

static const Learner LEARNERS[] = {
    { "完全不懂",  0.15, 0.15 },   // 幾乎只能猜（四選一是 0.25，但會被誘答選項拉低）
    { "半懂",      0.45, 0.25 },
    { "大致懂了",  0.70, 0.20 },
    { "很清楚",    0.90, 0.08 },
};
static const int LEARNER_COUNT = 4;

// 難度越高，答對機率越低
static double answerProb(double understanding, int difficulty, bool with_hint,
                         double hint_boost) {
    // difficulty 1→×1.0, 2→×0.85, 3→×0.70
    static const double DIFF[] = { 1.0, 1.0, 0.85, 0.70 };
    double p = understanding * DIFF[difficulty];
    if (with_hint) p += hint_boost;
    if (p > 0.97) p = 0.97;    // 保留手滑的可能
    if (p < 0.05) p = 0.05;
    return p;
}

// 答錯時隨機挑一個錯的選項
static int wrongChoice(int correct, std::mt19937& rng) {
    std::uniform_int_distribution<int> pick(0, 3);
    int c;
    do { c = pick(rng); } while (c == correct);
    return c;
}

// ── 跑一次 recap ─────────────────────────────────────────
struct Attempt {
    RecapResult result;
    int tries = 1;
};

static Attempt simulateRecap(const Learner& L, std::vector<McqQuestion> qs,
                             RecapConfig cfg, std::mt19937& rng,
                             int max_tries = 5) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_int_distribution<int> pick(0, 3);
    Attempt a;

    for (int t = 1; t <= max_tries; ++t) {
        Recap recap(qs, cfg);
        for (int i = 0; i < recap.questionCount(); ++i) {
            const auto& q = recap.question(i);
            int correct = q.correctIndex();

            // 第一次作答
            bool right = u(rng) < answerProb(L.understanding, q.difficulty,
                                             false, L.hint_boost);
            int choice = right ? correct : wrongChoice(correct, rng);
            auto v = recap.answer(i, choice);

            // 錯了就看提示再試一次
            if (v == AnswerVerdict::WRONG_SHOW_HINT) {
                bool right2 = u(rng) < answerProb(L.understanding, q.difficulty,
                                                  true, L.hint_boost);
                recap.answer(i, right2 ? correct : wrongChoice(correct, rng));
            }
        }
        a.result = recap.finish();
        a.tries = t;
        if (a.result.passed) break;
        if (!cfg.allow_retry) break;
    }
    return a;
}

// ── 統計 ─────────────────────────────────────────────────
struct Stats {
    double pass_rate = 0;
    double avg_tries = 0;
    double avg_first_try_score = 0;
};

static Stats runTrials(const Learner& L, int level, RecapConfig cfg,
                       int trials, std::mt19937& rng) {
    auto qs = RecapBank::forLevel(level);
    int passed = 0, tries_sum = 0;
    double score_sum = 0;

    for (int i = 0; i < trials; ++i) {
        Attempt a = simulateRecap(L, qs, cfg, rng);
        if (a.result.passed) ++passed;
        tries_sum += a.tries;
        score_sum += a.result.score();
    }
    return { 100.0 * passed / trials,
             static_cast<double>(tries_sum) / trials,
             score_sum / trials };
}

// ── 主程式 ───────────────────────────────────────────────
static const char* gateName(RecapGate g) {
    switch (g) {
        case RecapGate::ALL_CORRECT: return "五題全對";
        case RecapGate::THRESHOLD:   return "答對 3 題";
        case RecapGate::NO_GATE:     return "不擋（只給看）";
    }
    return "?";
}

int main() {
    const int TRIALS = 3000;
    std::mt19937 rng(20260818);

    printf("═══════════════════════════════════════════════════════════════\n");
    printf(" Recap MCQ · 三種過關條件的比較\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("  模擬的學習者：\n");
    for (const auto& L : LEARNERS)
        printf("    %-10s 理解程度 %.2f   看提示後 +%.2f\n",
               L.name.c_str(), L.understanding, L.hint_boost);
    printf("\n  每種組合各跑 %d 次，固定隨機種子。\n", TRIALS);
    printf("  重試上限 5 次，答錯先給提示、第二次才給解答。\n\n");

    struct Variant { const char* name; RecapGate gate; int thr; bool retry; };
    Variant variants[] = {
        { "5/5 可重試",   RecapGate::ALL_CORRECT, 5, true  },
        { "5/5 不重試",   RecapGate::ALL_CORRECT, 5, false },
        { "4/5 可重試",   RecapGate::THRESHOLD,   4, true  },
        { "4/5 不重試",   RecapGate::THRESHOLD,   4, false },
        { "3/5 可重試",   RecapGate::THRESHOLD,   3, true  },
        { "3/5 不重試",   RecapGate::THRESHOLD,   3, false },
        { "不擋",         RecapGate::NO_GATE,     0, false },
    };

    // ═══ 逐關卡（取三關當代表）═══
    for (int level : { 1, 3, 6 }) {
        printf("  ───────────────────────────────────────────────────────────\n");
        printf("  Level %d\n\n", level);
        printf("  %-14s", "過關條件");
        for (const auto& L : LEARNERS) printf(" %10s", L.name.c_str());
        printf("   %8s\n", "鑑別度");
        printf("  %s\n", std::string(68, '-').c_str());

        for (const auto& vr : variants) {
            RecapConfig cfg;
            cfg.gate = vr.gate;
            cfg.threshold = vr.thr;
            cfg.allow_retry = vr.retry;

            printf("  %-14s", vr.name);
            double first = 0, last = 0;
            for (int i = 0; i < LEARNER_COUNT; ++i) {
                Stats st = runTrials(LEARNERS[i], level, cfg, TRIALS, rng);
                printf(" %9.1f%%", st.pass_rate);
                if (i == 0) first = st.pass_rate;
                if (i == LEARNER_COUNT - 1) last = st.pass_rate;
            }
            printf("   %7.1f\n", last - first);
        }
        printf("\n");
    }

    // ═══ 平均到六關 ═══
    printf("  ═══════════════════════════════════════════════════════════\n");
    printf("  六關平均\n\n");
    printf("  %-14s", "過關條件");
    for (const auto& L : LEARNERS) printf(" %10s", L.name.c_str());
    printf("   %8s %8s\n", "鑑別度", "平均重試");
    printf("  %s\n", std::string(80, '-').c_str());

    for (const auto& vr : variants) {
        RecapConfig cfg;
        cfg.gate = vr.gate;
        cfg.threshold = vr.thr;
        cfg.allow_retry = vr.retry;

        printf("  %-14s", vr.name);
        double sums[LEARNER_COUNT] = {0};
        double tries_sum = 0;
        for (int lv = 1; lv <= 6; ++lv)
            for (int i = 0; i < LEARNER_COUNT; ++i) {
                Stats st = runTrials(LEARNERS[i], lv, cfg, TRIALS / 3, rng);
                sums[i] += st.pass_rate / 6.0;
                tries_sum += st.avg_tries / (6.0 * LEARNER_COUNT);
            }
        for (int i = 0; i < LEARNER_COUNT; ++i) printf(" %9.1f%%", sums[i]);
        printf("   %7.1f %8.2f\n", sums[LEARNER_COUNT-1] - sums[0], tries_sum);
    }

    // ═══ 重試的負擔落在誰身上 ═══
    printf("\n  ═══════════════════════════════════════════════════════════\n");
    printf("  重試次數的分布（誰在重試？）\n\n");
    printf("  %-14s", "過關條件");
    for (const auto& L : LEARNERS) printf(" %10s", L.name.c_str());
    printf("\n  %s\n", std::string(60, '-').c_str());

    for (const auto& vr : variants) {
        if (!vr.retry) continue;
        RecapConfig cfg;
        cfg.gate = vr.gate;
        cfg.threshold = vr.thr;
        cfg.allow_retry = true;

        printf("  %-14s", vr.name);
        for (int i = 0; i < LEARNER_COUNT; ++i) {
            double t = 0;
            for (int lv = 1; lv <= 6; ++lv)
                t += runTrials(LEARNERS[i], lv, cfg, TRIALS / 3, rng).avg_tries / 6.0;
            printf(" %9.2f次", t);
        }
        printf("\n");
    }

    printf("\n  ── 怎麼讀 ──\n");
    printf("  鑑別度 = 「很清楚」的過關率 − 「完全不懂」的過關率。\n");
    printf("  它越高，代表這道關卡越能分出誰真的懂。\n");
    printf("  但過關率太低也不行——那會把懂的人一起擋掉。\n\n");

    printf("  ── 這個實驗不能宣稱什麼 ──\n");
    printf("  理解程度與答對機率的對應是設定的參數，不是量測值。\n");
    printf("  真人會從前一題學到東西、會用刪去法、會疲勞。\n");
    printf("  所以這只能回答「機制的篩選效果」，不能回答學習成效。\n");
    return 0;
}
