#include "coach_engine.h"
#include "domains/gridnav_domain.h"
#include <iostream>
#include <map>

/* =========================================================================
   兩個領域，同一個引擎。

   第一個是拉密的極簡版（只保留湊組合這件事），
   第二個是機器人格點導航。

   兩者的 State、Move、技巧、提示文字完全不同，
   但 CoachEngine 一行都沒改。
   ========================================================================= */

// ══════════════════════════════════════════════════════════
//  領域一：拉密（極簡版）
// ══════════════════════════════════════════════════════════
struct CardState {
    std::vector<int> hand;      // 手上的牌（數字，同色）
    std::vector<int> board;     // 桌面已有的一條 Run
};

struct CardMove {
    int tile = 0;               // 要出的那張牌
    bool atHead = false;        // 接在頭還是尾
};

enum CardTechnique { CT_ATTACH_HEAD = 0, CT_ATTACH_TAIL, CT_COUNT };

class RummikubDomain : public CoachDomain<CardState, CardMove> {
public:
    RummikubDomain() {
        levels_ = {
            { 1, CT_ATTACH_TAIL, "接在尾巴", 100,
              0, 1, 2, HintTier::REVEAL_MOVE,   2, 1,  -1, HintTier::REVEAL_MOVE },
            { 2, CT_ATTACH_HEAD, "接在頭部",  40,
              2, -1, -1, HintTier::GENTLE_NUDGE, 2, 2, 8, HintTier::POINT_TO_AREA },
        };
    }

    std::optional<CardMove> solve(const CardState& s) const override {
        if (s.board.empty()) return std::nullopt;
        int head = s.board.front(), tail = s.board.back();
        for (int t : s.hand) {
            if (t == tail + 1) return CardMove{t, false};
            if (t == head - 1) return CardMove{t, true};
        }
        return std::nullopt;
    }

    std::string hint(HintTier tier, const CardMove& m,
                     const CardState& s) const override {
        switch (tier) {
            case HintTier::GENTLE_NUDGE:
                return "手上有一張接得上去。";
            case HintTier::POINT_TO_AREA:
                return m.atHead ? "看看那條龍的前面。" : "看看那條龍的後面。";
            case HintTier::REVEAL_MOVE:
                return "把 " + std::to_string(m.tile) + " 接在" +
                       (m.atHead ? "頭" : "尾") + "。";
        }
        return "";
    }

    std::vector<int> classify(const CardState& before,
                              const CardState& after) const override {
        std::vector<int> found;
        if (after.board.size() <= before.board.size()) return found;
        if (after.board.front() < before.board.front()) found.push_back(CT_ATTACH_HEAD);
        if (after.board.back()  > before.board.back())  found.push_back(CT_ATTACH_TAIL);
        return found;
    }

    int techniqueCount() const override { return CT_COUNT; }
    std::string techniqueName(int t) const override {
        return t == CT_ATTACH_HEAD ? "接在頭部" : "接在尾巴";
    }
    const std::vector<LevelSpec>& levels() const override { return levels_; }

private:
    std::vector<LevelSpec> levels_;
};

// ══════════════════════════════════════════════════════════
//  共用的展示函式 —— 注意它是 template，不知道是哪個領域
// ══════════════════════════════════════════════════════════
template <typename State, typename Move>
void showTiers(CoachEngine<State, Move>& eng, const State& s,
               const char* label, int max_stuck) {
    const auto& spec = eng.currentSpec();
    std::cout << "  " << label << "（Level " << spec.level << " · "
              << spec.name << " · 引導 " << spec.guidance_percent << "%）\n";
    for (int stuck = 0; stuck <= max_stuck; ++stuck) {
        Advice a = eng.tick(s, stuck);
        std::cout << "    卡 " << stuck << " 回合 → ";
        if (!a.speak) {
            std::cout << "（沉默）\n";
        } else {
            const char* t = a.tier == HintTier::GENTLE_NUDGE  ? "輕推  " :
                            a.tier == HintTier::POINT_TO_AREA ? "指方向" : "講答案";
            std::cout << "[" << t << "] " << a.text
                      << (a.from_safety_net ? "   ← 保底觸發" : "") << "\n";
        }
    }
    std::cout << "\n";
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "══════════════════════════════════════════════════\n";
    std::cout << " 同一個 CoachEngine，兩個完全不同的領域\n";
    std::cout << "══════════════════════════════════════════════════\n\n";

    // ── 領域一：拉密 ──────────────────────────────────────
    std::cout << "【領域一】拉密\n";
    std::cout << "  State = 手牌 + 桌面    Move = 出哪張牌接哪一端\n\n";

    RummikubDomain rummi;
    CoachEngine<CardState, CardMove> engA(rummi);

    CardState cs;
    cs.board = {4, 5, 6};
    cs.hand  = {3, 7, 9};

    showTiers(engA, cs, "第一關", 3);
    engA.advance();
    showTiers(engA, cs, "第二關", 9);

    // ── 領域二：機器人導航 ────────────────────────────────
    std::cout << "【領域二】機器人格點導航\n";
    std::cout << "  State = 位置 + 地圖    Move = 往哪個方向走一步\n\n";

    GridNavDomain grid;
    CoachEngine<GridState, GridMove> engB(grid);

    GridState gs;
    gs.rx = 0; gs.ry = 0; gs.gx = 7; gs.gy = 5;
    gs.wall[1][1] = gs.wall[2][1] = gs.wall[3][1] = true;

    showTiers(engB, gs, "第一關", 3);
    engB.advance(); engB.advance(); engB.advance();
    showTiers(engB, gs, "第四關", 13);

    // ── 掌握度：兩個領域用同一套規則 ──────────────────────
    std::cout << "【掌握度升級】規則寫在引擎裡，兩個領域共用\n\n";

    GridNavDomain g2;
    CoachEngine<GridState, GridMove> engC(g2);
    GridState a = gs, b = gs;

    auto step = [&](int dx, int dy, const char* how, int stuck) {
        b = a; b.rx += dx; b.ry += dy;
        engC.tick(a, stuck);           // 先讓引擎決定給不給提示
        engC.observe(a, b);            // 再回報使用者做了什麼
        a = b;
        std::cout << "    " << how << " → 掌握度 "
                  << CoachEngine<GridState,GridMove>::stars(
                         engC.progressOf(GT_STRAIGHT).mastery)
                  << "\n";
    };

    step(1, 0, "卡 2 回合後才走（看過答案）", 2);
    step(1, 0, "卡 1 回合後走（只看到輕推）", 1);
    step(1, 0, "立刻就走（完全沒提示）    ", 0);

    std::cout << "\n══════════════════════════════════════════════════\n";
    std::cout << " 引擎不知道什麼是牌，也不知道什麼是機器人。\n"
              << " 它只知道：卡了幾回合、這一關給到哪一層、要不要開口。\n";
    return 0;
}
