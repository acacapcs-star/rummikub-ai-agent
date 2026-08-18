#include "coach_session.h"
#include "domains/rummikub_domain.h"
#include <cstdio>
#include <string>

/* =========================================================================
   demo_session.cpp —— 四個模組接起來之後的樣子

   一個玩家從第一關開始：
     選模式 → 出牌拿提示 → 設自訂挑戰 → 達成過關條件 → 考 recap → 進下一關
   ========================================================================= */

// ── 造牌 ─────────────────────────────────────────────────
static std::vector<Tile*> g_pool;
static int g_id = 0;
static Tile* T(int n, Color c) { auto* t = new Tile(g_id++, n, c); g_pool.push_back(t); return t; }
static void cleanup() { for (auto* t : g_pool) delete t; g_pool.clear(); }

static const char* tierName(HintTier t) {
    return t == HintTier::GENTLE_NUDGE  ? "輕推"
         : t == HintTier::POINT_TO_AREA ? "指方向" : "講答案";
}

using Session = CoachSession<RummiState, RummiMove>;

/* 每一關對應的出牌。

   這是整合之後才浮現的一件事：**玩家必須真的用出那一關教的招數**，
   出別的招不算數。所以示範也得為每一關準備對應的盤面變化。      */
struct MovePair { RummiState before, after; };

static MovePair moveForTechnique(int tech) {
    MovePair mp;
    switch (tech) {
        case RT_ATTACH_RUN: {
            Tile *a = T(4,Color::RED), *b = T(5,Color::RED), *c = T(6,Color::RED);
            Tile* n = T(3, Color::RED);
            mp.before.board = {{ a, b, c }};  mp.before.hand = { n };
            mp.after.board  = {{ n, a, b, c }};
            break;
        }
        case RT_COMPLETE_GROUP: {
            Tile *r = T(7,Color::RED), *bl = T(7,Color::BLUE), *k = T(7,Color::BLACK);
            Tile* y = T(7, Color::YELLOW);
            mp.before.board = {{ r, bl, k }};  mp.before.hand = { y };
            mp.after.board  = {{ r, bl, k, y }};
            break;
        }
        case RT_JOKER_FILL: {
            Tile *a = T(4,Color::BLUE), *b = T(5,Color::BLUE), *d = T(7,Color::BLUE);
            Tile* j = new Tile(g_id++); g_pool.push_back(j);
            mp.before.board = {{ a, b }};  mp.before.hand = { j, d };
            mp.after.board  = {{ a, b, j, d }};
            break;
        }
        case RT_INITIAL_MELD: {
            Tile *a = T(11,Color::RED), *b = T(12,Color::RED), *c = T(13,Color::RED);
            mp.before.initial_meld_done = false;
            mp.before.hand = { a, b, c };
            mp.after.initial_meld_done = true;
            mp.after.board = {{ a, b, c }};
            break;
        }
        case RT_BOARD_RESHUFFLE: {
            // 一組被拆散重新分配 → 偵測為大風吹
            Tile *a = T(1,Color::BLACK), *b = T(2,Color::BLACK), *c = T(3,Color::BLACK);
            Tile *d = T(1,Color::RED),  *e = T(2,Color::RED),  *f = T(3,Color::RED);
            mp.before.board = {{ a, b, c }, { d, e, f }};
            mp.after.board  = {{ a, b, c, d }, { e, f }};   // 拆散了第二組
            break;
        }
        case RT_RUN_SPLIT: {
            std::vector<Tile*> run;
            for (int i = 1; i <= 8; ++i) run.push_back(T(i, Color::BLACK));
            mp.before.board = { run };
            mp.after.board  = { { run[0],run[1],run[2],run[3] },
                                { run[4],run[5],run[6],run[7] } };
            break;
        }
    }
    return mp;
}

/* 走完一關：出牌達標 → 考 recap → 進下一關。

   不能直接呼叫 advance()——現在它要求兩個條件都達成。
   這個限制正是整合的目的：**出牌條件與 recap 是兩道獨立的關卡。**   */
static bool clearLevel(Session& s, unsigned seed) {
    int tech = s.currentSpec().technique;
    MovePair mp = moveForTechnique(tech);

    MoveMetrics mm;
    mm.tiles_played = 2;
    mm.touched_board = (tech == RT_BOARD_RESHUFFLE || tech == RT_RUN_SPLIT);

    for (int i = 0; i < 8 && !s.advanceStatus().moves_done; ++i)
        s.play(mp.before, mp.after, mm);

    auto recap = s.startRecap(seed);
    if (!recap) return false;
    for (int i = 0; i < recap->questionCount(); ++i)
        recap->answer(i, recap->question(i).correctIndex());
    s.submitRecap(recap->finish());
    return s.advance();
}

// ── 印出提示的變化 ───────────────────────────────────────
static void showHints(Session& s, const RummiState& st, int maxStuck) {
    for (int k = 0; k <= maxStuck; ++k) {
        Advice a = s.tick(st, k);
        printf("      卡 %2d 回合  ", k);
        if (!a.speak) printf("（沉默）\n");
        else printf("[%s] %s%s\n", tierName(a.tier), a.text.c_str(),
                    a.from_safety_net ? "   ← 保底" : "");
    }
}

int main() {
    RummikubDomain domain;

    printf("═══════════════════════════════════════════════════════════\n");
    printf(" 完整的教練系統 · 四個模組接起來\n");
    printf("═══════════════════════════════════════════════════════════\n\n");

    // 一個簡單的局面：桌面紅 4-5-6，手上紅 3 和紅 7
    RummiState st;
    st.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    st.hand  = { T(3,Color::RED), T(7,Color::RED) };

    // ═══ ① 模式影響提示 ═══
    printf("【① 模式決定音量】同一個局面、同一關，五種模式\n\n");
    for (CoachMode m : CoachModes::all()) {
        Session s(domain, m);
        printf("  《%s》\n", CoachModes::get(m).name.c_str());
        showHints(s, st, 4);
        printf("\n");
    }

    // ═══ ② 自訂挑戰 ═══
    printf("═══════════════════════════════════════════════════════════\n");
    printf("【② 自訂挑戰擋下違規的一手】\n\n");

    Session s(domain, CoachMode::BEGINNER);

    // 走完前五關才寫得出用到 joker 的腳本。
    // 注意這裡不能直接 advance()——每一關都要出牌達標 + 通過 recap。
    printf("  走完前五關（每一關都要用出那一關教的招數 + 通過 recap）：\n");
    for (int i = 0; i < 5; ++i) {
        int lv = s.currentLevel();
        std::string tech = domain.techniqueName(s.currentSpec().technique);
        bool ok = clearLevel(s, 1000u + i);
        printf("    第 %d 關（%s） → %s\n", lv, tech.c_str(),
               ok ? "過了" : "失敗");
    }
    printf("    現在在第 %d 關\n\n", s.currentLevel());

    const char* script = R"(
battle "純手工大清倉" {
    require tilesPlayed >= 3;
    forbid  joker;
    limit   time = 30;
}
)";
    printf("  腳本：%s\n", script);

    PlayerState ps = s.playerState();
    ps.mastery.assign(6, 3);              // 假設全部練滿，才能用 forbid
    std::vector<ParseError> errs;
    auto b = BattleParser::parse(script, ps, errs);
    if (!b) {
        for (auto& e : errs) printf("    第 %d 行 %s\n", e.line, e.message.c_str());
        cleanup();
        return 1;
    }
    s.setBattle(*b);
    printf("  已套用挑戰：「%s」\n\n", s.battleName().c_str());

    struct Case { const char* label; MoveMetrics m; };
    auto mk = [](int tiles, bool joker, int time) {
        MoveMetrics m;
        m.tiles_played = tiles;
        m.used_joker = joker;
        m.time_spent = time;
        return m;
    };

    Case cases[] = {
        { "出 4 張，沒用 Joker，花 10 秒", mk(4, false, 10) },
        { "出 2 張                     ", mk(2, false, 10) },
        { "出 4 張但用了 Joker          ", mk(4, true,  10) },
        { "出 4 張但花了 50 秒          ", mk(4, false, 50) },
    };
    for (const auto& c : cases) {
        BattleVerdict v = s.validateMove(c.m);
        printf("    %s  →  %s\n", c.label, v.allowed ? "✓ 可以出" : "✗ 擋下");
        for (const auto& x : v.violations) printf("        %s\n", x.c_str());
    }

    // ═══ ③ 過關與 Recap ═══
    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("【③ 出牌條件與 Recap 是兩道關卡】\n\n");

    Session s2(domain, CoachMode::BEGINNER);
    RummiState before, after;
    before.board = {{ T(4,Color::BLUE), T(5,Color::BLUE), T(6,Color::BLUE) }};
    Tile* b3 = T(3, Color::BLUE);
    before.hand = { b3 };
    after.board = {{ b3, before.board[0][0], before.board[0][1], before.board[0][2] }};
    after.hand = {};

    auto printStatus = [&](const char* tag) {
        AdvanceStatus a = s2.advanceStatus();
        printf("    %s\n", tag);
        printf("      出牌次數 %d/%d   自主 %d/%d   →  %s\n",
               a.uses, a.uses_needed, a.unassisted, a.unassisted_needed,
               a.moves_done ? "達成" : "未達成");
        printf("      Recap    %s\n", a.recap_done ? "已通過" : "尚未通過");
        printf("      可以進下一關？ %s\n\n", a.can_advance ? "是" : "否");
    };

    printStatus("一開始");

    MoveMetrics mm;
    mm.tiles_played = 1;
    for (int i = 0; i < 3; ++i) s2.play(before, after, mm);
    printStatus("出牌三次之後");

    printf("      → 出牌條件達成了，但還不能進下一關\n\n");

    auto recap = s2.startRecap(20260818u);
    if (recap) {
        printf("    取得 Recap，共 %d 題\n", recap->questionCount());
        printf("    第一題：%s\n", recap->question(0).prompt.c_str());
        for (int i = 0; i < recap->questionCount(); ++i)
            printf("      選項 %d 是正解\n", recap->question(i).correctIndex() + 1);

        // 全部答對
        for (int i = 0; i < recap->questionCount(); ++i)
            recap->answer(i, recap->question(i).correctIndex());
        RecapResult r = recap->finish();
        printf("\n    成績 %d/%d，%s\n", r.correct_first_try, r.total,
               r.passed ? "通過" : "未通過");
        s2.submitRecap(r);
    }
    printStatus("Recap 通過之後");

    bool ok = s2.advance();
    printf("    advance() → %s，現在在第 %d 關\n",
           ok ? "成功" : "失敗", s2.currentLevel());
    printf("    新的一關要重新過 Recap：%s\n",
           s2.advanceStatus().recap_done ? "已通過" : "尚未通過");

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf(" 四個模組各自能單獨測試，接線只在這一層。\n");
    printf(" CoachEngine 完全不知道模式、挑戰、Recap 的存在。\n");

    cleanup();
    return 0;
}
