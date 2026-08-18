/* -------------------------------------------------------
   test_session.cpp —— 整合層的測試

   四個模組各自都有測試了，這裡測的是**接線**：
     模式有沒有真的影響提示、挑戰有沒有擋住違規的出牌、
     recap 有沒有成為進關的必要條件。
------------------------------------------------------- */
#include "coach_session.h"
#include "domains/rummikub_domain.h"
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool c, const std::string& n) {
    if (c) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else   { ++g_fail; std::cout << "  FAIL " << n << "\n"; }
}

static std::vector<Tile*> g_pool;
static int g_id = 0;
static Tile* T(int n, Color c) { auto* t = new Tile(g_id++, n, c); g_pool.push_back(t); return t; }
static void cleanup() { for (auto* t : g_pool) delete t; g_pool.clear(); }

using Session = CoachSession<RummiState, RummiMove>;

static RummiState makeState() {
    RummiState s;
    s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s.hand  = { T(3,Color::RED) };
    return s;
}

// ── ① 模式影響提示 ───────────────────────────────────────
static void test_模式接線() {
    std::cout << "\n① 模式 → 引擎\n";
    RummikubDomain d;
    auto st = makeState();

    Session beginner(d, CoachMode::BEGINNER);
    check(beginner.tick(st, 0).speak, "新手練組卡 0 回合就開口");

    Session extreme(d, CoachMode::EXTREME);
    check(!extreme.tick(st, 0).speak, "★ 挑戰極限組卡 0 回合保持沉默");

    Session sparring(d, CoachMode::SPARRING);
    bool silent = true;
    for (int k = 0; k <= 50; ++k) if (sparring.tick(st, k).speak) silent = false;
    check(silent, "★ 高手過招卡再久都不出聲");

    // 音量上限
    Session comp(d, CoachMode::COMPETITIVE);
    bool everDeep = false;
    for (int k = 0; k <= 50; ++k) {
        Advice a = comp.tick(st, k);
        if (a.speak && a.tier != HintTier::GENTLE_NUDGE) everDeep = true;
    }
    check(!everDeep, "★ 較量組永遠只給輕推——L1 的關卡設定被模式蓋過");

    // 沒有解的時候不受模式限制
    RummiState dead;
    dead.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    dead.hand  = { T(9,Color::BLUE) };
    Advice a = extreme.tick(dead, 0);
    check(a.speak && a.text.find("沒有") != std::string::npos,
          "★ 真的沒牌可出時，連挑戰極限組也會說——那是事實不是提示");
}

// ── ② 挑戰擋下違規 ───────────────────────────────────────
static void test_挑戰接線() {
    std::cout << "\n② 挑戰 → 出牌檢查\n";
    RummikubDomain d;
    Session s(d, CoachMode::BEGINNER);

    MoveMetrics ok;   ok.tiles_played = 5;
    MoveMetrics bad;  bad.tiles_played = 2;

    check(s.validateMove(bad).allowed, "沒設挑戰時一律通過");

    PlayerState ps;
    ps.level = 6;
    ps.mastery.assign(6, 3);
    std::vector<ParseError> errs;
    auto b = BattleParser::parse(
        R"(battle "t" { require tilesPlayed >= 3; })", ps, errs);
    check(b.has_value(), "腳本解析成功");
    if (!b) return;

    s.setBattle(*b);
    check(s.hasBattle() && s.battleName() == "t", "挑戰已套用");
    check( s.validateMove(ok).allowed,  "出 5 張 → 通過");
    check(!s.validateMove(bad).allowed, "★ 出 2 張 → 被擋下");

    // 違規的一手不該累積進度
    auto before = makeState();
    RummiState after;
    after.board = {{ before.hand[0], before.board[0][0],
                     before.board[0][1], before.board[0][2] }};
    int uses_before = s.progressOf(RT_ATTACH_RUN).total_uses;
    MoveOutcome mo = s.play(before, after, bad);
    check(!mo.accepted, "違規的出牌被拒絕");
    check(s.progressOf(RT_ATTACH_RUN).total_uses == uses_before,
          "★ 違規的一手不記進度——否則可以靠違規累積過關");

    s.clearBattle();
    check(!s.hasBattle(), "可以清除挑戰");
}

// ── ③ Recap 是進關的必要條件 ─────────────────────────────
static void test_recap接線() {
    std::cout << "\n③ Recap → 過關\n";
    RummikubDomain d;
    Session s(d, CoachMode::BEGINNER);

    auto before = makeState();
    RummiState after;
    after.board = {{ before.hand[0], before.board[0][0],
                     before.board[0][1], before.board[0][2] }};

    check(!s.startRecap(1).has_value(), "★ 出牌條件未達成時，拿不到 recap");

    MoveMetrics mm; mm.tiles_played = 1;
    for (int i = 0; i < 3; ++i) s.play(before, after, mm);

    auto st1 = s.advanceStatus();
    check(st1.moves_done, "出牌三次 → 出牌條件達成");
    check(!st1.recap_done, "但 recap 還沒過");
    check(!st1.can_advance, "★ 所以還不能進下一關");
    check(!s.advance(), "advance() 被拒絕");

    auto recap = s.startRecap(777);
    check(recap.has_value(), "現在拿得到 recap 了");
    if (!recap) return;

    // 先故意全錯
    for (int i = 0; i < recap->questionCount(); ++i) {
        int wrong = (recap->question(i).correctIndex() + 1) % 4;
        recap->answer(i, wrong);
        recap->answer(i, wrong);
    }
    s.submitRecap(recap->finish());
    check(!s.advanceStatus().recap_done, "全錯 → recap 沒過");
    check(!s.advance(), "★ recap 沒過就不能進關");

    // 重考全對
    recap->reset();
    for (int i = 0; i < recap->questionCount(); ++i)
        recap->answer(i, recap->question(i).correctIndex());
    s.submitRecap(recap->finish());
    check(s.advanceStatus().recap_done, "全對 → recap 通過");
    check(s.advanceStatus().can_advance, "兩個條件都達成");

    int lv = s.currentLevel();
    check(s.advance(), "advance() 成功");
    check(s.currentLevel() == lv + 1, "進到下一關");
    check(!s.advanceStatus().recap_done,
          "★ 新的一關要重新過 recap——不能一次考試通關全部");
}

// ── recap 洗牌 ───────────────────────────────────────────
static void test_洗牌() {
    std::cout << "\nRecap 選項洗牌\n";
    RummikubDomain d;
    int pos[4] = {0,0,0,0};

    for (unsigned seed = 1; seed <= 20; ++seed) {
        Session s(d, CoachMode::BEGINNER);
        auto before = makeState();
        RummiState after;
        after.board = {{ before.hand[0], before.board[0][0],
                         before.board[0][1], before.board[0][2] }};
        MoveMetrics mm; mm.tiles_played = 1;
        for (int i = 0; i < 3; ++i) s.play(before, after, mm);

        auto r = s.startRecap(seed * 7919u);
        if (!r) continue;
        for (int i = 0; i < r->questionCount(); ++i)
            pos[r->question(i).correctIndex()]++;
    }
    bool spread = true;
    for (int i = 0; i < 4; ++i) if (pos[i] == 0) spread = false;
    check(spread, "★ 正解分散在四個位置——否則玩家會發現「不知道就選 B」");
}

// ── 玩家狀態轉換 ─────────────────────────────────────────
static void test_玩家狀態() {
    std::cout << "\n玩家狀態（給腳本解析用）\n";
    RummikubDomain d;
    Session s(d, CoachMode::BEGINNER);

    PlayerState ps = s.playerState();
    check(ps.level == 1, "起始關卡是 1");
    check(static_cast<int>(ps.mastery.size()) == RT_COUNT, "六招的掌握度都有");

    bool allZero = true;
    for (int m : ps.mastery) if (m != 0) allZero = false;
    check(allZero, "一開始全部未解鎖");

    // 出牌之後掌握度會升
    auto before = makeState();
    RummiState after;
    after.board = {{ before.hand[0], before.board[0][0],
                     before.board[0][1], before.board[0][2] }};
    MoveMetrics mm; mm.tiles_played = 1;
    s.play(before, after, mm);

    ps = s.playerState();
    check(ps.mastery[RT_ATTACH_RUN] > 0, "★ 用出招數後掌握度上升，腳本能用的欄位跟著解鎖");
}

// ── 自創招數的偵測 ───────────────────────────────────────
static void test_自創招數() {
    std::cout << "\n自創招數的偵測\n";
    RummikubDomain d;
    auto before = makeState();
    RummiState after;
    after.board = {{ before.hand[0], before.board[0][0],
                     before.board[0][1], before.board[0][2] }};

    MoveMetrics big; big.tiles_played = 6;

    Session stylish(d, CoachMode::STYLISH);
    stylish.play(before, after, big);
    check(!stylish.privateCandidates().empty(),
          "★ 炫酷組會記錄漂亮的一手");

    Session beginner(d, CoachMode::BEGINNER);
    beginner.play(before, after, big);
    check(beginner.privateCandidates().empty(),
          "新手練組不記錄——那個階段先把基本功學會");
}

int main() {
    std::cout << "整合層 · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_模式接線();
    test_挑戰接線();
    test_recap接線();
    test_洗牌();
    test_玩家狀態();
    test_自創招數();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    cleanup();
    return g_fail == 0 ? 0 : 1;
}
