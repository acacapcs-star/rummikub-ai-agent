#include "../coach_engine.h"
#include "rummikub_domain.h"
#include "gridnav_domain.h"
#include <iostream>
#include <vector>

/* =========================================================================
   真實的拉密領域接進抽象引擎。

   對照 demo.cpp 裡那個 30 行的玩具版本——這一個有完整的六招、
   破冰計分、Joker 推算、大風吹與長龍切斷偵測。

   **引擎一行都沒有改。**
   ========================================================================= */

// ── 造牌工具：所有 Tile 存在單一 pool，其餘都是指標 ─────
static std::vector<Tile*> g_pool;
static int g_id = 0;

static Tile* T(int n, Color c) {
    Tile* t = new Tile(g_id++, n, c);
    g_pool.push_back(t);
    return t;
}
static Tile* J() {
    Tile* t = new Tile(g_id++);
    g_pool.push_back(t);
    return t;
}
static void cleanup() { for (Tile* t : g_pool) delete t; g_pool.clear(); }

// ── 印出三層提示 ─────────────────────────────────────────
static void showAllTiers(RummikubDomain& dom, const RummiState& s,
                         const char* title) {
    std::cout << "  " << title << "\n";
    auto move = dom.solve(s);
    if (!move) {
        std::cout << "     （找不到解 → 引擎會誠實說「現在真的沒有能做的動作」）\n\n";
        return;
    }
    std::cout << "     輕推   " << dom.hint(HintTier::GENTLE_NUDGE,  *move, s) << "\n";
    std::cout << "     指方向 " << dom.hint(HintTier::POINT_TO_AREA, *move, s) << "\n";
    std::cout << "     講答案 " << dom.hint(HintTier::REVEAL_MOVE,   *move, s) << "\n\n";
}

int main() {
    RummikubDomain dom;

    std::cout << "══════════════════════════════════════════════════════\n";
    std::cout << " 真實拉密領域 · 六招的三層提示\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    // ── 一、接龍頭尾 ──────────────────────────────────────
    {
        RummiState s;
        s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
        s.hand  = { T(3,Color::RED), T(9,Color::BLUE) };
        showAllTiers(dom, s, "① 桌面 紅4-5-6，手上有 紅3");
    }

    // ── 二、補第四色 ──────────────────────────────────────
    {
        RummiState s;
        s.board = {{ T(7,Color::RED), T(7,Color::BLUE), T(7,Color::BLACK) }};
        s.hand  = { T(7,Color::YELLOW), T(2,Color::RED) };
        showAllTiers(dom, s, "② 桌面 7 的三色組，手上有 黃7");
    }

    // ── 三、Joker 補缺口 ──────────────────────────────────
    {
        RummiState s;
        s.board = {{ T(1,Color::BLACK), T(2,Color::BLACK), T(3,Color::BLACK) }};
        s.hand  = { T(5,Color::RED), T(7,Color::RED), J() };
        showAllTiers(dom, s, "③ 手上有 紅5、紅7 和一張 Joker");
    }

    // ── 四、破冰 ──────────────────────────────────────────
    {
        RummiState s;
        s.initial_meld_done = false;
        s.hand = { T(11,Color::RED), T(12,Color::RED), T(13,Color::RED),
                   T(2,Color::BLUE) };
        showAllTiers(dom, s, "④ 尚未破冰，手上有 紅11-12-13（36 分）");
    }

    // ── 五、長龍切斷 ──────────────────────────────────────
    {
        RummiState s;
        std::vector<Tile*> longRun;
        for (int i = 1; i <= 8; ++i) longRun.push_back(T(i, Color::BLACK));
        s.board = { longRun };
        s.hand  = { T(4,Color::BLACK), T(5,Color::BLACK) };
        showAllTiers(dom, s, "⑤ 桌面 黑1到黑8（八張），手上有 黑4、黑5");
    }

    // ── 六、真的沒有解 ────────────────────────────────────
    {
        RummiState s;
        s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
        s.hand  = { T(9,Color::BLUE), T(11,Color::YELLOW) };
        showAllTiers(dom, s, "⑥ 手上的牌完全接不上");
    }

    // ═══════════════════════════════════════════════════════
    //  引擎跑真實領域：遞減曲線
    // ═══════════════════════════════════════════════════════
    std::cout << "══════════════════════════════════════════════════════\n";
    std::cout << " 引擎的遞減曲線（真實拉密領域）\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    RummiState s;
    s.board = {{ T(4,Color::RED), T(5,Color::RED), T(6,Color::RED) }};
    s.hand  = { T(3,Color::RED), T(7,Color::RED) };

    for (int lv = 1; lv <= 6; ++lv) {
        RummikubDomain d2;
        CoachEngine<RummiState, RummiMove> eng(d2);
        while (eng.currentLevel() < lv) eng.advance();
        const auto& spec = eng.currentSpec();

        std::cout << "  L" << lv << " " << spec.name
                  << "（引導 " << spec.guidance_percent << "%）  ";
        for (int stuck = 0; stuck <= 13; ++stuck) {
            Advice a = eng.tick(s, stuck);
            if (!a.speak)                                    std::cout << "·";
            else if (a.from_safety_net)                       std::cout << "◆";
            else if (a.tier == HintTier::GENTLE_NUDGE)        std::cout << "輕";
            else if (a.tier == HintTier::POINT_TO_AREA)       std::cout << "指";
            else                                              std::cout << "答";
        }
        std::cout << "\n";
    }
    std::cout << "\n  （· 沉默   輕 輕推   指 指方向   答 講答案   ◆ 保底）\n";
    std::cout << "  卡關回合   0  1  2  3  4  5  6  7  8  9 10 11 12 13\n\n";

    // ═══════════════════════════════════════════════════════
    //  同一個引擎，兩個領域
    // ═══════════════════════════════════════════════════════
    std::cout << "══════════════════════════════════════════════════════\n";
    std::cout << " 同一個引擎的第 1 關，兩個領域\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    {
        RummikubDomain d3;
        CoachEngine<RummiState, RummiMove> e1(d3);
        std::cout << "  【拉密】\n";
        for (int k = 0; k <= 2; ++k) {
            Advice a = e1.tick(s, k);
            std::cout << "    卡 " << k << " → " << (a.speak ? a.text : "（沉默）") << "\n";
        }
    }
    {
        GridNavDomain d4;
        CoachEngine<GridState, GridMove> e2(d4);
        GridState gs;
        gs.rx = 0; gs.ry = 0; gs.gx = 7; gs.gy = 5;
        std::cout << "\n  【機器人導航】\n";
        for (int k = 0; k <= 2; ++k) {
            Advice a = e2.tick(gs, k);
            std::cout << "    卡 " << k << " → " << (a.speak ? a.text : "（沉默）") << "\n";
        }
    }

    std::cout << "\n══════════════════════════════════════════════════════\n";
    std::cout << " 引擎不知道什麼是牌，也不知道什麼是機器人。\n";
    std::cout << " 它只決定：要不要開口、開口到哪一層。\n";

    cleanup();
    return 0;
}
