/* -------------------------------------------------------
   test_modes.cpp —— 五種模式的單元測試
------------------------------------------------------- */
#include "coach_modes.h"
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

static const LevelSpec L1 = {
    1, 0, "接龍頭尾", 100,
    0, 1, 2, HintTier::REVEAL_MOVE, 3, 1, -1, HintTier::REVEAL_MOVE
};
static const LevelSpec L6 = {
    6, 5, "長龍切斷", 40,
    3, -1, -1, HintTier::GENTLE_NUDGE, 3, 3, 12, HintTier::POINT_TO_AREA
};

// 在卡 stuck 回合時，這個設定會給哪一層（-1 = 沉默）
static int tierAt(const LevelSpec& c, int stuck) {
    if (c.safety_net_after_turns >= 0 && stuck >= c.safety_net_after_turns)
        return static_cast<int>(c.safety_net_tier);
    if (c.reveal_after_turns >= 0 && stuck >= c.reveal_after_turns &&
        c.max_tier == HintTier::REVEAL_MOVE) return 2;
    if (c.point_after_turns >= 0 && stuck >= c.point_after_turns &&
        c.max_tier != HintTier::GENTLE_NUDGE) return 1;
    if (c.nudge_after_turns >= 0 && stuck >= c.nudge_after_turns) return 0;
    return -1;
}

static void test_五種都存在() {
    std::cout << "\n模式清單\n";
    auto all = CoachModes::all();
    checkEq((int)all.size(), 5, "共五種模式");
    for (CoachMode m : all) {
        const auto& p = CoachModes::get(m);
        check(!p.name.empty() && !p.description.empty(),
              "  " + p.name + " 有名稱與說明");
    }
}

static void test_音量遞減() {
    std::cout << "\n音量單調遞減\n";
    // 用 L1 檢查：後面的模式在同一個卡關回合數，音量不該比前面的高
    for (int stuck = 0; stuck <= 15; ++stuck) {
        int prev = 99;
        bool mono = true;
        for (CoachMode m : CoachModes::all()) {
            int t = tierAt(CoachModes::apply(L1, m), stuck);
            if (t > prev) mono = false;
            prev = t;
        }
        if (stuck == 0 || stuck == 5 || stuck == 15)
            check(mono, "卡 " + std::to_string(stuck) + " 回合時，音量由前往後不遞增");
    }
}

static void test_音量上限() {
    std::cout << "\n音量上限\n";
    auto s = CoachModes::apply(L1, CoachMode::COMPETITIVE);
    bool everReveal = false, everPoint = false;
    for (int k = 0; k <= 40; ++k) {
        int t = tierAt(s, k);
        if (t == 2) everReveal = true;
        if (t == 1) everPoint = true;
    }
    check(!everReveal, "★ 較量組永遠不給答案——即使 L1 的關卡設定允許");
    check(!everPoint,  "★ 較量組也不指方向，上限就是輕推");

    auto st = CoachModes::apply(L1, CoachMode::STYLISH);
    bool stylishReveal = false;
    for (int k = 0; k <= 40; ++k) if (tierAt(st, k) == 2) stylishReveal = true;
    check(!stylishReveal, "炫酷組上限是指方向，不給答案");

    auto bg = CoachModes::apply(L1, CoachMode::BEGINNER);
    bool beginnerReveal = false;
    for (int k = 0; k <= 40; ++k) if (tierAt(bg, k) == 2) beginnerReveal = true;
    check(beginnerReveal, "新手練組會給答案");
}

static void test_高手過招完全安靜() {
    std::cout << "\n高手過招\n";
    for (const LevelSpec* base : { &L1, &L6 }) {
        auto s = CoachModes::apply(*base, CoachMode::SPARRING);
        bool silent = true;
        for (int k = 0; k <= 60; ++k) if (tierAt(s, k) >= 0) silent = false;
        check(silent, std::string("★ L") + std::to_string(base->level) +
                      " 在高手過招下，卡再久都完全不出聲");
    }
    check(!CoachModes::get(CoachMode::SPARRING).hint_during_play,
          "標記為遊戲中不提示");
    checkEq(CoachModes::get(CoachMode::SPARRING).postgame_detail, 2,
            "★ 回饋全部挪到賽後，所以賽後分析最詳細");
}

static void test_保底() {
    std::cout << "\n保底機制\n";
    auto ex = CoachModes::apply(L6, CoachMode::EXTREME);
    checkEq(ex.safety_net_after_turns, -1,
            "★ 挑戰極限組關掉保底——那個模式的意思就是沒有人會來救你");

    auto bg = CoachModes::apply(L6, CoachMode::BEGINNER);
    check(bg.safety_net_after_turns >= 0, "新手練組保留保底");

    // 保底的層級也受模式的上限限制
    auto comp = CoachModes::apply(L6, CoachMode::COMPETITIVE);
    check(comp.safety_net_after_turns < 0 ||
          comp.safety_net_tier == HintTier::GENTLE_NUDGE,
          "★ 較量組的保底也不能超過輕推——上限不能被例外機制突破");
}

static void test_門檻倍率() {
    std::cout << "\n門檻倍率\n";
    auto bg = CoachModes::apply(L1, CoachMode::BEGINNER);
    checkEq(bg.nudge_after_turns, L1.nudge_after_turns, "新手練組 ×1.0，門檻不變");
    checkEq(bg.point_after_turns, L1.point_after_turns, "  指方向門檻不變");

    auto comp = CoachModes::apply(L6, CoachMode::COMPETITIVE);
    check(comp.nudge_after_turns > L6.nudge_after_turns,
          "★ 較量組 ×2.0，比原本更晚開口");

    auto ex = CoachModes::apply(L6, CoachMode::EXTREME);
    check(ex.nudge_after_turns > comp.nudge_after_turns,
          "挑戰極限組又比較量組更晚");

    // -1 乘上倍率仍是 -1
    auto s = CoachModes::apply(L6, CoachMode::STYLISH);
    checkEq(s.point_after_turns, -1, "★ 原本就不啟用的門檻（-1），乘倍率後仍是 -1");
}

static void test_私人招數偵測() {
    std::cout << "\n自創招數的偵測\n";
    check(CoachModes::get(CoachMode::STYLISH).detect_private,
          "★ 炫酷組是自創招數的主場");
    checkEq(CoachModes::get(CoachMode::STYLISH).private_threshold, 3,
            "  門檻最低（出 3 張就記錄）");
    check(!CoachModes::get(CoachMode::BEGINNER).detect_private,
          "新手練組不偵測——那個階段先把基本功學會");
    check(CoachModes::get(CoachMode::EXTREME).private_threshold >
          CoachModes::get(CoachMode::STYLISH).private_threshold,
          "挑戰極限組門檻較高，少打擾");
}

static void test_跨模式比較() {
    std::cout << "\n兩個軸的交互作用\n";
    // L1 在最安靜的模式，應該比 L6 在最吵的模式還安靜
    auto l1_extreme  = CoachModes::apply(L1, CoachMode::EXTREME);
    auto l6_beginner = CoachModes::apply(L6, CoachMode::BEGINNER);

    int silent1 = 0, silent6 = 0;
    for (int k = 0; k <= 15; ++k) {
        if (tierAt(l1_extreme,  k) < 0) ++silent1;
        if (tierAt(l6_beginner, k) < 0) ++silent6;
    }
    check(silent1 >= silent6,
          "★ 第 1 關在挑戰極限組，不比第 6 關在新手練組吵");
    std::cout << "         （L1 挑戰極限沉默 " << silent1
              << " 回合，L6 新手練組沉默 " << silent6 << " 回合）\n";
}

int main() {
    std::cout << "五種模式 · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_五種都存在();
    test_音量遞減();
    test_音量上限();
    test_高手過招完全安靜();
    test_保底();
    test_門檻倍率();
    test_私人招數偵測();
    test_跨模式比較();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
