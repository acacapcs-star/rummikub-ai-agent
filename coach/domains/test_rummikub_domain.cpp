/* -------------------------------------------------------
   test_rummikub_domain.cpp —— 真實拉密領域的單元測試

   為什麼這一層特別需要測試：
     抽象引擎測的是「什麼時候說」，領域測的是「說什麼」。
     **教練型 AI 講錯話的代價比對戰型高得多**——
     新手最沒有判斷力，系統講什麼他就照做，
     然後被引擎打回，他會以為是自己錯了。

     信裡提到的那個 bug 就是這一層的：
     玩家手上有紅11、紅12 和一張 Joker（36 分足以破冰），
     引擎卻說「湊不出來，去抽牌」。

   測試分兩塊：
     solve()    找得到解嗎？找到的解真的合法嗎？
     classify() 認得出玩家用了哪一招嗎？會不會誤判？

   編譯：
     g++ -std=c++17 test_rummikub_domain.cpp \
         ../../src/tile.cpp ../../src/validator.cpp -o test_domain
------------------------------------------------------- */

#include "rummikub_domain.h"
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const std::string& name) {
    if (cond) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else      { ++g_fail; std::cout << "  FAIL " << name << "\n"; }
}

static void checkEqInt(int got, int want, const std::string& name) {
    if (got == want) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else { ++g_fail; std::cout << "  FAIL " << name
                               << "   (得到 " << got << "，預期 " << want << ")\n"; }
}

// ── 造牌 ─────────────────────────────────────────────────
static std::vector<Tile*> g_pool;
static int g_id = 0;
static Tile* T(int n, Color c) { auto* t = new Tile(g_id++, n, c); g_pool.push_back(t); return t; }
static Tile* J()               { auto* t = new Tile(g_id++);       g_pool.push_back(t); return t; }
static void cleanup() { for (auto* t : g_pool) delete t; g_pool.clear(); }

static bool hasTechnique(const std::vector<int>& v, RummiTechnique t) {
    for (int x : v) if (x == static_cast<int>(t)) return true;
    return false;
}

// ══════════════════════════════════════════════════════════
//  solve：找得到解，而且解要是對的
// ══════════════════════════════════════════════════════════
static void test_solve_attach_run() {
    std::cout << "\nsolve · 接龍頭尾\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s.hand  = { T(3,Color::RED) };
    auto m = d.solve(s);
    check(m.has_value(), "紅3 能接在 紅4-5-6 前面");
    if (m) {
        check(m->kind == RummiMove::ATTACH_RUN, "判定為接龍");
        check(m->at_head, "接在頭部（因為 3 < 4）");
    }

    RummiState s2;
    s2.board = {{ T(4,Color::BLUE), T(5,Color::BLUE), T(6,Color::BLUE) }};
    s2.hand  = { T(7,Color::BLUE) };
    auto m2 = d.solve(s2);
    check(m2.has_value() && !m2->at_head, "藍7 接在尾部（因為 7 > 6）");

    RummiState s3;
    s3.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s3.hand  = { T(3,Color::BLUE) };     // 顏色不對
    check(!d.solve(s3).has_value(), "藍3 接不上紅色的龍——顏色要一致");
}

static void test_solve_boundary() {
    std::cout << "\nsolve · 數字邊界 1–13\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(11,Color::RED), T(12,Color::RED), T(13,Color::RED) }};
    s.hand  = { T(10,Color::RED) };
    check(d.solve(s).has_value(), "紅10 可以接在 11 前面");

    RummiState s2;
    s2.board = {{ T(1,Color::RED), T(2,Color::RED), T(3,Color::RED) }};
    s2.hand  = { T(13,Color::RED) };
    auto m = s2.hand.empty() ? std::nullopt : d.solve(s2);
    check(!m.has_value() || m->kind != RummiMove::ATTACH_RUN,
          "13 不能接在 1 前面——不會繞回去");
}

static void test_solve_complete_group() {
    std::cout << "\nsolve · 補第四色\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(7,Color::RED), T(7,Color::BLUE), T(7,Color::BLACK) }};
    s.hand  = { T(7,Color::YELLOW) };
    auto m = d.solve(s);
    check(m.has_value() && m->kind == RummiMove::COMPLETE_GROUP, "黃7 補進三色組");

    RummiState s2;
    s2.board = {{ T(7,Color::RED), T(7,Color::BLUE), T(7,Color::BLACK) }};
    s2.hand  = { T(7,Color::RED) };      // 顏色重複
    auto m2 = d.solve(s2);
    check(!m2.has_value() || m2->kind != RummiMove::COMPLETE_GROUP,
          "重複的紅7 不能補——Group 顏色必須互異");

    RummiState s3;
    s3.board = {{ T(7,Color::RED), T(7,Color::BLUE),
                  T(7,Color::BLACK), T(7,Color::YELLOW) }};   // 已經四張
    s3.hand  = { T(7,Color::RED) };
    auto m3 = d.solve(s3);
    check(!m3.has_value() || m3->kind != RummiMove::COMPLETE_GROUP,
          "四張的 Group 已滿，不能再補");
}

static void test_solve_initial_meld() {
    std::cout << "\nsolve · 破冰（信裡那個 bug 的迴歸測試）\n";
    RummikubDomain d;

    // ★ 這是回報過的 bug：紅11 + 紅12 + Joker = 36 分，足以破冰
    RummiState s;
    s.initial_meld_done = false;
    s.hand = { T(11,Color::RED), T(12,Color::RED), J() };
    auto m = d.solve(s);
    check(m.has_value(), "★ 紅11、紅12、Joker 必須找得到破冰組合");
    if (m) {
        check(m->kind == RummiMove::INITIAL_MELD, "判定為破冰");
        checkEqInt(m->score, 36, "分數是 36（Joker 算它代表的 13）");
        check(m->score >= 30, "達到 30 分門檻");
    }

    RummiState s2;
    s2.initial_meld_done = false;
    s2.hand = { T(1,Color::RED), T(2,Color::RED), T(3,Color::RED) };
    auto m2 = d.solve(s2);
    check(!m2.has_value(), "紅1-2-3 只有 6 分，不能破冰");

    RummiState s3;
    s3.initial_meld_done = false;
    s3.board = {{ T(4,Color::BLUE), T(5,Color::BLUE), T(6,Color::BLUE) }};
    s3.hand  = { T(3,Color::BLUE) };     // 接得上，但還沒破冰
    auto m3 = d.solve(s3);
    check(!m3.has_value(), "破冰前不能接桌面的牌——只能用純手牌湊 30 分");
}

static void test_solve_joker_fill() {
    std::cout << "\nsolve · Joker 補缺口\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(1,Color::BLACK), T(2,Color::BLACK), T(3,Color::BLACK) }};
    s.hand  = { T(5,Color::RED), T(7,Color::RED), J() };
    auto m = d.solve(s);
    check(m.has_value() && m->kind == RummiMove::JOKER_FILL,
          "紅5 + Joker + 紅7 湊成一組");
    if (m) checkEqInt(static_cast<int>(m->tiles.size()), 3, "三張牌");

    RummiState s2;
    s2.board = {{ T(1,Color::BLACK), T(2,Color::BLACK), T(3,Color::BLACK) }};
    s2.hand  = { T(5,Color::RED), T(9,Color::RED), J() };   // 差 4，補不起來
    auto m2 = d.solve(s2);
    check(!m2.has_value() || m2->kind != RummiMove::JOKER_FILL,
          "紅5 和紅9 差太遠，一張 Joker 補不了");
}

static void test_solve_no_answer() {
    std::cout << "\nsolve · 真的沒有解時要誠實\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s.hand  = { T(9,Color::BLUE), T(11,Color::YELLOW) };
    check(!d.solve(s).has_value(), "接不上時回傳 nullopt，不硬掰");

    RummiState s2;
    check(!d.solve(s2).has_value(), "空盤面也不會當掉");
}

// ══════════════════════════════════════════════════════════
//  hint：三層必須真的有深淺差別
// ══════════════════════════════════════════════════════════
static void test_hint_tiers_differ() {
    std::cout << "\nhint · 三層的深淺\n";
    RummikubDomain d;

    RummiState s;
    s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s.hand  = { T(3,Color::RED) };
    auto m = d.solve(s);
    check(m.has_value(), "先找到解");
    if (!m) return;

    std::string nudge  = d.hint(HintTier::GENTLE_NUDGE,  *m, s);
    std::string point  = d.hint(HintTier::POINT_TO_AREA, *m, s);
    std::string reveal = d.hint(HintTier::REVEAL_MOVE,   *m, s);

    check(!nudge.empty() && !point.empty() && !reveal.empty(), "三層都有內容");
    check(nudge != point && point != reveal, "三層的說法互不相同");

    // 輕推不該透露具體是哪張牌
    check(nudge.find("3") == std::string::npos,
          "輕推不提具體數字——那是講答案才做的事");
    // 講答案必須包含具體的牌
    check(reveal.find("3") != std::string::npos, "講答案有提到 紅3");
}

// ══════════════════════════════════════════════════════════
//  classify：認得出用了哪一招，而且不誤判
// ══════════════════════════════════════════════════════════
static void test_classify_attach() {
    std::cout << "\nclassify · 接龍\n";
    RummikubDomain d;

    Tile *r3 = T(3,Color::RED), *r4 = T(4,Color::RED),
         *r5 = T(5,Color::RED), *r6 = T(6,Color::RED);

    RummiState before, after;
    before.board = {{ r4, r5, r6 }};  before.hand = { r3 };
    after.board  = {{ r3, r4, r5, r6 }}; after.hand = {};

    auto t = d.classify(before, after);
    check(hasTechnique(t, RT_ATTACH_RUN), "認出接龍頭尾");
    check(!hasTechnique(t, RT_RUN_SPLIT), "不會誤判成長龍切斷");
}

static void test_classify_group() {
    std::cout << "\nclassify · 補第四色\n";
    RummikubDomain d;

    Tile *a = T(7,Color::RED),  *b = T(7,Color::BLUE),
         *c = T(7,Color::BLACK), *y = T(7,Color::YELLOW);

    RummiState before, after;
    before.board = {{ a, b, c }};  before.hand = { y };
    after.board  = {{ a, b, c, y }}; after.hand = {};

    auto t = d.classify(before, after);
    check(hasTechnique(t, RT_COMPLETE_GROUP), "認出補第四色");
}

static void test_classify_joker_position() {
    std::cout << "\nclassify · Joker 位置決定是否算「補缺口」\n";
    RummikubDomain d;

    // Joker 在中間 → 算補缺口
    {
        Tile *r4 = T(4,Color::RED), *r5 = T(5,Color::RED),
             *r7 = T(7,Color::RED); Tile* j = J();
        RummiState before, after;
        before.board = {{ r4, r5 }};       before.hand = { j, r7 };
        after.board  = {{ r4, r5, j, r7 }}; after.hand = {};
        check(hasTechnique(d.classify(before, after), RT_JOKER_FILL),
              "Joker 在中間 → 算補缺口");
    }
    // Joker 在尾端 → 只是延長，不算
    {
        Tile *r4 = T(4,Color::RED), *r5 = T(5,Color::RED),
             *r6 = T(6,Color::RED); Tile* j = J();
        RummiState before, after;
        before.board = {{ r4, r5, r6 }};   before.hand = { j };
        after.board  = {{ r4, r5, r6, j }}; after.hand = {};
        check(!hasTechnique(d.classify(before, after), RT_JOKER_FILL),
              "★ Joker 接在尾端不算補缺口——那只是延長，普通牌也做得到");
    }
}

static void test_classify_split_vs_attach() {
    std::cout << "\nclassify · 區分「切斷」與「被併進更長的組合」\n";
    RummikubDomain d;

    // 真的切斷：一條長 Run 散成兩條
    {
        std::vector<Tile*> run;
        for (int i = 1; i <= 8; ++i) run.push_back(T(i, Color::BLACK));
        RummiState before, after;
        before.board = { run };
        after.board  = { { run[0],run[1],run[2],run[3] },
                         { run[4],run[5],run[6],run[7] } };
        check(hasTechnique(d.classify(before, after), RT_RUN_SPLIT),
              "散成兩條合法 Run → 認出切斷");
    }
    // 不是切斷：長 Run 消失了，但只是被接長
    {
        std::vector<Tile*> run;
        for (int i = 1; i <= 8; ++i) run.push_back(T(i, Color::BLACK));
        Tile* nine = T(9, Color::BLACK);
        std::vector<Tile*> longer = run; longer.push_back(nine);

        RummiState before, after;
        before.board = { run };     before.hand = { nine };
        after.board  = { longer };  after.hand = {};

        auto t = d.classify(before, after);
        check(!hasTechnique(t, RT_RUN_SPLIT),
              "★ 只是接長不算切斷——這是最容易誤判的一組");
        check(hasTechnique(t, RT_ATTACH_RUN), "應該被認成接龍");
    }
}

static void test_classify_nothing() {
    std::cout << "\nclassify · 什麼都沒做\n";
    RummikubDomain d;

    Tile* r3 = T(3, Color::RED);
    RummiState before, after;
    before.hand = {};  after.hand = { r3 };     // 只是抽了一張牌

    check(d.classify(before, after).empty(),
          "只抽牌時不回報任何技巧——寧可漏判，不可誤判");
}

static void test_classify_multiple() {
    std::cout << "\nclassify · 一手同時符合多招\n";
    RummikubDomain d;

    // Joker 補在中間，同時也讓那條 Run 變長
    Tile *r4 = T(4,Color::RED), *r5 = T(5,Color::RED), *r7 = T(7,Color::RED);
    Tile* j = J();
    RummiState before, after;
    before.board = {{ r4, r5 }};        before.hand = { j, r7 };
    after.board  = {{ r4, r5, j, r7 }};  after.hand = {};

    auto t = d.classify(before, after);
    check(t.size() >= 1, "至少認出一招");
    check(hasTechnique(t, RT_JOKER_FILL), "包含 Joker 補缺口");
}

// ══════════════════════════════════════════════════════════
//  關卡設定的一致性
// ══════════════════════════════════════════════════════════
static void test_level_specs() {
    std::cout << "\n關卡設定\n";
    RummikubDomain d;
    const auto& L = d.levels();

    checkEqInt(static_cast<int>(L.size()), 6, "六個關卡");
    checkEqInt(d.techniqueCount(), RT_COUNT, "六招");

    // 引導強度必須單調遞減
    bool decreasing = true;
    for (std::size_t i = 1; i < L.size(); ++i)
        if (L[i].guidance_percent >= L[i-1].guidance_percent) decreasing = false;
    check(decreasing, "引導強度逐關遞減（100% → 40%）");
    checkEqInt(L.front().guidance_percent, 100, "第一關 100%");
    checkEqInt(L.back().guidance_percent, 40,  "最後一關 40%");

    // 自主次數要求必須不遞減
    bool stricter = true;
    for (std::size_t i = 1; i < L.size(); ++i)
        if (L[i].required_unassisted < L[i-1].required_unassisted) stricter = false;
    check(stricter, "自主次數的要求逐關收緊");
    checkEqInt(L.back().required_unassisted, L.back().required_uses,
               "最後一關要求全部自主");

    // 最後一關永遠不給答案
    check(L.back().max_tier == HintTier::GENTLE_NUDGE, "最後一關上限只到輕推");
    check(L.back().safety_net_tier != HintTier::REVEAL_MOVE,
          "★ 最後一關的保底也不給答案——核心約束不能被保底破壞");

    // 每一招都有對應的關卡
    for (int t = 0; t < RT_COUNT; ++t) {
        bool found = false;
        for (const auto& spec : L) if (spec.technique == t) found = true;
        check(found, "技巧 " + d.techniqueName(t) + " 有對應的關卡");
    }
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "真實拉密領域 · 單元測試\n";
    std::cout << "════════════════════════════════════════";

    test_solve_attach_run();
    test_solve_boundary();
    test_solve_complete_group();
    test_solve_initial_meld();
    test_solve_joker_fill();
    test_solve_no_answer();
    test_hint_tiers_differ();
    test_classify_attach();
    test_classify_group();
    test_classify_joker_position();
    test_classify_split_vs_attach();
    test_classify_nothing();
    test_classify_multiple();
    test_level_specs();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    cleanup();
    return g_fail == 0 ? 0 : 1;
}
