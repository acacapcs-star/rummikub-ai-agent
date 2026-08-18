/* -------------------------------------------------------
   test_recap.cpp —— Recap 系統的單元測試
------------------------------------------------------- */
#include "recap.h"
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool c, const std::string& n) {
    if (c) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else   { ++g_fail; std::cout << "  FAIL " << n << "\n"; }
}
static void checkEq(int got, int want, const std::string& n) {
    if (got == want) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else { ++g_fail; std::cout << "  FAIL " << n
                               << "  (得到 " << got << "，預期 " << want << ")\n"; }
}

static std::vector<McqQuestion> fakeQs(int n) {
    std::vector<McqQuestion> qs;
    for (int i = 0; i < n; ++i)
        qs.push_back({ "題目" + std::to_string(i),
                       {{"A",false},{"B",true},{"C",false},{"D",false}},
                       "提示", "解答", 2 });
    return qs;   // 正解一律是索引 1
}

static void test_題庫() {
    std::cout << "\n題庫\n";
    for (int lv = 1; lv <= 6; ++lv) {
        auto qs = RecapBank::forLevel(lv);
        checkEq((int)qs.size(), 5, "Level " + std::to_string(lv) + " 有五題");

        bool allOk = true;
        for (const auto& q : qs) {
            if (q.options.size() != 4) allOk = false;
            if (q.correctIndex() < 0) allOk = false;
            int nCorrect = 0;
            for (const auto& o : q.options) if (o.correct) ++nCorrect;
            if (nCorrect != 1) allOk = false;          // 必須恰好一個正解
            if (q.hint.empty() || q.explanation.empty()) allOk = false;
            if (q.difficulty < 1 || q.difficulty > 3) allOk = false;
        }
        check(allOk, "  四選一、恰好一個正解、提示與解答都有、難度在 1–3");
    }

    // 題庫本身正解集中在第二個選項（30 題有 24 題），
    // 所以發題時必須洗牌——否則玩家會發現「不知道就選 B」。
    int raw[4] = {0,0,0,0};
    for (int lv = 1; lv <= 6; ++lv)
        for (const auto& q : RecapBank::forLevel(lv))
            raw[q.correctIndex()]++;
    check(raw[1] > 15, "題庫原始狀態確實偏向第二個選項（這正是要洗牌的理由）");

    int mixed[4] = {0,0,0,0};
    for (int lv = 1; lv <= 6; ++lv) {
        Recap r(RecapBank::forLevel(lv), {}, 12345u + lv);
        for (int i = 0; i < r.questionCount(); ++i)
            mixed[r.question(i).correctIndex()]++;
    }
    bool spread = true;
    for (int i = 0; i < 4; ++i) if (mixed[i] == 0) spread = false;
    check(spread, "★ 洗牌後正解分散在四個位置——玩家找不到規律");
}

static void test_答題流程() {
    std::cout << "\n作答 · 兩階段\n";
    Recap r(fakeQs(5));

    check(r.answer(0, 1) == AnswerVerdict::CORRECT_FIRST, "第一次就對");
    check(r.answer(1, 0) == AnswerVerdict::WRONG_SHOW_HINT, "第一次錯 → 給提示");
    check(r.answer(1, 1) == AnswerVerdict::CORRECT_AFTER_HINT, "看提示後對了");
    check(r.answer(2, 0) == AnswerVerdict::WRONG_SHOW_HINT, "第一次錯");
    check(r.answer(2, 3) == AnswerVerdict::WRONG_SHOW_ANSWER, "第二次還錯 → 給解答");
    check(r.answer(2, 1) == AnswerVerdict::WRONG_SHOW_ANSWER,
          "★ 已定案的題目不能再改——否則可以一直試到對");
}

static void test_計分() {
    std::cout << "\n計分\n";
    Recap r(fakeQs(5));
    r.answer(0, 1);              // 一次對
    r.answer(1, 1);              // 一次對
    r.answer(2, 0); r.answer(2, 1);   // 提示後對
    r.answer(3, 0); r.answer(3, 2);   // 兩次都錯
    r.answer(4, 1);              // 一次對

    auto res = r.finish();
    checkEq(res.correct_first_try, 3, "第一次就對 3 題");
    checkEq(res.correct_with_hint, 1, "看提示後對 1 題");
    checkEq(res.wrong, 1, "全錯 1 題");
    checkEq((int)res.score(), 60, "★ 分數只算第一次就對的（3/5 = 60）");
}

static void test_三種過關條件() {
    std::cout << "\n過關條件\n";

    // firstTry 題一次答對、withHint 題看提示後答對、其餘全錯
    auto play = [](RecapGate g, int thr, int firstTry, int withHint) {
        RecapConfig c;
        c.gate = g;
        c.threshold = thr;
        Recap r(fakeQs(5), c);          // 不傳 seed → 不洗牌，正解固定在索引 1
        int i = 0;
        for (int k = 0; k < firstTry; ++k, ++i) r.answer(i, 1);
        for (int k = 0; k < withHint; ++k, ++i) { r.answer(i, 0); r.answer(i, 1); }
        for (; i < 5; ++i) { r.answer(i, 0); r.answer(i, 2); }
        return r.finish().passed;
    };

    check( play(RecapGate::ALL_CORRECT, 5, 5, 0), "全對 → 5/5 通過");
    check( play(RecapGate::ALL_CORRECT, 5, 3, 2),
          "★ 3 題一次對 + 2 題看提示 → 5/5 仍通過（最終都答對了）");
    check(!play(RecapGate::ALL_CORRECT, 5, 4, 0), "錯 1 題 → 5/5 不過");

    check( play(RecapGate::THRESHOLD, 3, 3, 0), "3 題對 → 3/5 通過");
    check(!play(RecapGate::THRESHOLD, 3, 2, 0), "2 題對 → 3/5 不過");

    check( play(RecapGate::NO_GATE, 0, 0, 0), "全錯 → 不擋模式仍通過");
}

static void test_預設值() {
    std::cout << "\n預設設定（來自實驗結論）\n";
    RecapConfig c;
    check(c.gate == RecapGate::ALL_CORRECT, "★ 預設是五題全對——鑑別度 96.6");
    check(c.allow_retry, "★ 預設允許重試——不重試會誤傷懂的人（只有 75.7% 過關）");
    checkEq(c.max_retries, 5, "重試上限 5 次");
}

static void test_reset() {
    std::cout << "\n重試\n";
    Recap r(fakeQs(5));
    for (int i = 0; i < 5; ++i) { r.answer(i, 0); r.answer(i, 2); }
    checkEq(r.finish().wrong, 5, "全錯");

    r.reset();
    for (int i = 0; i < 5; ++i) r.answer(i, 1);
    auto res = r.finish();
    checkEq(res.correct_first_try, 5, "reset 之後重新計分");
    check(res.passed, "重試後通過");
}

int main() {
    std::cout << "Recap · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_題庫();
    test_答題流程();
    test_計分();
    test_三種過關條件();
    test_預設值();
    test_reset();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
