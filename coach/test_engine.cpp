/* -------------------------------------------------------
   test_engine.cpp —— 抽象引擎的單元測試

   這裡測的是「引擎的決策邏輯」，不是任何一個領域。
   所以用一個假的領域（FakeDomain）當替身——
   它的 solve/hint/classify 都回傳可預測的固定值，
   讓測試能專注在「什麼時候開口、給哪一層、掌握度怎麼升」。

   這是分層設計的直接好處：**引擎可以在沒有任何真實領域的情況下被測試。**

   編譯：
     g++ -std=c++17 test_engine.cpp -o test_engine && ./test_engine
------------------------------------------------------- */

#include "coach_engine.h"
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const std::string& name) {
    if (cond) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else      { ++g_fail; std::cout << "  FAIL " << name << "\n"; }
}

template <typename T>
static void checkEq(const T& got, const T& want, const std::string& name) {
    if (got == want) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else { ++g_fail; std::cout << "  FAIL " << name
                               << "   (得到 " << got << "，預期 " << want << ")\n"; }
}

// ══════════════════════════════════════════════════════════
//  假領域：所有回傳值都可預測
// ══════════════════════════════════════════════════════════
struct FakeState { bool hasSolution = true; int techniqueUsed = -1; };
struct FakeMove  { int id = 0; };

class FakeDomain : public CoachDomain<FakeState, FakeMove> {
public:
    FakeDomain() {
        levels_ = {
            // L1：一給到底，沒有保底
            { 1, 0, "L1", 100, 0, 1, 2, HintTier::REVEAL_MOVE,   3, 1, -1, HintTier::REVEAL_MOVE },
            // L2：上限只到指方向，卡 10 回合保底給答案
            { 2, 1, "L2",  60, 2, 4, -1, HintTier::POINT_TO_AREA, 3, 2, 10, HintTier::REVEAL_MOVE },
            // L3：上限只到輕推，卡 12 回合保底給指方向
            { 3, 2, "L3",  40, 3, -1, -1, HintTier::GENTLE_NUDGE, 3, 3, 12, HintTier::POINT_TO_AREA },
        };
    }
    std::optional<FakeMove> solve(const FakeState& s) const override {
        if (!s.hasSolution) return std::nullopt;
        return FakeMove{42};
    }
    std::string hint(HintTier t, const FakeMove&, const FakeState&) const override {
        return t == HintTier::GENTLE_NUDGE  ? "nudge"
             : t == HintTier::POINT_TO_AREA ? "point" : "reveal";
    }
    std::vector<int> classify(const FakeState&, const FakeState& after) const override {
        if (after.techniqueUsed < 0) return {};
        return { after.techniqueUsed };
    }
    int techniqueCount() const override { return 3; }
    std::string techniqueName(int t) const override { return "T" + std::to_string(t); }
    const std::vector<LevelSpec>& levels() const override { return levels_; }
private:
    std::vector<LevelSpec> levels_;
};

// ══════════════════════════════════════════════════════════
using Eng = CoachEngine<FakeState, FakeMove>;

static void test_no_solution() {
    std::cout << "\n找不到解時\n";
    FakeDomain d; Eng e(d);
    FakeState s; s.hasSolution = false;

    Advice a = e.tick(s, 0);
    check(a.speak, "沒有解時仍然開口——不能靜默讓使用者乾等");
    check(a.text.find("沒有") != std::string::npos,
          "誠實說沒有可做的動作，不硬掰一個提示");
}

static void test_tier_progression() {
    std::cout << "\nL1 · 提示層級依卡關回合遞進\n";
    FakeDomain d; Eng e(d);
    FakeState s;

    checkEq(e.tick(s, 0).text, std::string("nudge"),  "卡 0 → 輕推");
    checkEq(e.tick(s, 1).text, std::string("point"),  "卡 1 → 指方向");
    checkEq(e.tick(s, 2).text, std::string("reveal"), "卡 2 → 講答案");
    checkEq(e.tick(s, 9).text, std::string("reveal"), "卡 9 → 仍是講答案（不會再往上）");
}

static void test_silence() {
    std::cout << "\nL2 · 門檻未到時保持沉默\n";
    FakeDomain d; Eng e(d);
    e.advance();
    FakeState s;

    check(!e.tick(s, 0).speak, "卡 0 → 沉默");
    check(!e.tick(s, 1).speak, "卡 1 → 沉默");
    check( e.tick(s, 2).speak, "卡 2 → 開口");
}

static void test_max_tier_cap() {
    std::cout << "\n上限限制：不能超過該關卡的 max_tier\n";
    FakeDomain d; Eng e(d);
    e.advance();                     // L2：上限是指方向
    FakeState s;

    Advice a = e.tick(s, 8);         // 卡很久，但還沒到保底門檻
    checkEq(a.text, std::string("point"), "L2 卡 8 回合仍只給指方向，不會跳到答案");
    check(!a.from_safety_net, "這一次不是保底觸發的");
}

static void test_safety_net() {
    std::cout << "\n保底機制\n";
    FakeDomain d; Eng e(d);
    e.advance(); e.advance();        // L3：上限是輕推，保底 12 回合給指方向
    FakeState s;

    Advice before = e.tick(s, 11);
    checkEq(before.text, std::string("nudge"), "卡 11 → 還是輕推");
    check(!before.from_safety_net, "尚未觸發保底");

    Advice after = e.tick(s, 12);
    checkEq(after.text, std::string("point"), "卡 12 → 保底把音量調高一格");
    check(after.from_safety_net, "標記為保底觸發");
    check(after.text != "reveal",
          "L3 的保底只到指方向——那一關永遠不講答案的約束沒被破壞");
}

static void test_mastery_upgrade() {
    std::cout << "\n掌握度只升不降\n";
    FakeDomain d; Eng e(d);
    FakeState before, after;
    after.techniqueUsed = 0;

    e.tick(before, 2);               // 看過答案
    e.observe(before, after);
    check(e.progressOf(0).mastery == Mastery::COPIED, "看過答案 → 一星");

    e.tick(before, 1);               // 只看到指方向
    e.observe(before, after);
    check(e.progressOf(0).mastery == Mastery::PROMPTED, "只給方向 → 升到二星");

    e.tick(before, 0);               // 只看到輕推
    e.observe(before, after);
    check(e.progressOf(0).mastery == Mastery::DISCOVERED, "完全自主 → 升到三星");

    e.tick(before, 2);               // 又看了答案
    e.observe(before, after);
    check(e.progressOf(0).mastery == Mastery::DISCOVERED,
          "再看一次答案也不會降級——學會了就是學會了");
}

static void test_unassisted_counting() {
    std::cout << "\n自主次數的計算\n";
    FakeDomain d; Eng e(d);
    FakeState before, after;
    after.techniqueUsed = 0;

    e.tick(before, 2); e.observe(before, after);   // 看答案
    e.tick(before, 0); e.observe(before, after);   // 沒看
    e.tick(before, 0); e.observe(before, after);   // 沒看

    checkEq(e.progressOf(0).total_uses, 3, "總次數 3");
    checkEq(e.progressOf(0).unassisted_uses, 2, "自主次數 2（看答案那次不算）");
}

static void test_advance_requires_both() {
    std::cout << "\n過關要同時滿足次數與自主次數\n";
    FakeDomain d; Eng e(d);
    FakeState before, after;
    after.techniqueUsed = 0;

    // 三次全部都看答案 → 次數夠但自主次數 0
    for (int i = 0; i < 3; ++i) { e.tick(before, 2); e.observe(before, after); }
    checkEq(e.progressOf(0).total_uses, 3, "用了三次");
    checkEq(e.progressOf(0).unassisted_uses, 0, "但沒有一次是自主的");
    check(!e.canAdvance(), "不能過關——熟練不等於學會");

    e.tick(before, 0); e.observe(before, after);   // 補一次自主
    check(e.canAdvance(), "補上一次自主之後才能過關");
}

static void test_level_bounds() {
    std::cout << "\n關卡邊界\n";
    FakeDomain d; Eng e(d);
    checkEq(e.currentLevel(), 1, "起始在第 1 關");
    check(e.advance(), "可以進到第 2 關");
    check(e.advance(), "可以進到第 3 關");
    check(!e.advance(), "已是最後一關，回傳 false");
    checkEq(e.currentLevel(), 3, "關卡數不會超出範圍");
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "抽象教練引擎 · 單元測試\n";
    std::cout << "（用假領域測試，不依賴任何真實應用）\n";
    std::cout << "════════════════════════════════════════";

    test_no_solution();
    test_tier_progression();
    test_silence();
    test_max_tier_cap();
    test_safety_net();
    test_mastery_upgrade();
    test_unassisted_counting();
    test_advance_requires_both();
    test_level_bounds();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
