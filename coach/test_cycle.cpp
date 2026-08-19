/* -------------------------------------------------------
   test_cycle.cpp —— 間隔重複與循環報告的測試
------------------------------------------------------- */
#include "spaced_cycle.h"
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

// ══════════════════════════════════════════════════════════
static void test_費氏循環() {
    std::cout << "\n費氏循環\n";

    // 頂點 13：1,2,3,5,8,13 然後回到 1
    int expect13[] = { 1,2,3,5,8,13, 1,2,3,5,8,13, 1,2 };
    bool ok = true;
    for (int i = 0; i < 14; ++i)
        if (FibonacciSchedule::intervalAt(i, 13) != expect13[i]) ok = false;
    check(ok, "★ 頂點 13：1,2,3,5,8,13 之後循環回 1");

    // 頂點 8：循環更密集
    int expect8[] = { 1,2,3,5,8, 1,2,3,5,8, 1,2 };
    ok = true;
    for (int i = 0; i < 12; ++i)
        if (FibonacciSchedule::intervalAt(i, 8) != expect8[i]) ok = false;
    check(ok, "★ 頂點 8：循環週期較短");

    checkEq(FibonacciSchedule::cycleLength(8),  5, "頂點 8 → 一輪 5 步");
    checkEq(FibonacciSchedule::cycleLength(13), 6, "頂點 13 → 一輪 6 步");
    checkEq(FibonacciSchedule::cycleLength(21), 7, "頂點 21 → 一輪 7 步");

    checkEq(FibonacciSchedule::cycleNumber(0, 13), 1,  "第 0 步在第 1 輪");
    checkEq(FibonacciSchedule::cycleNumber(5, 13), 1,  "第 5 步仍在第 1 輪");
    checkEq(FibonacciSchedule::cycleNumber(6, 13), 2,  "★ 第 6 步進入第 2 輪");

    check(FibonacciSchedule::isCycleEnd(5, 13),  "第 5 步是一輪的最後一步");
    check(!FibonacciSchedule::isCycleEnd(4, 13), "第 4 步還不是");
}

static void test_兩族群的頂點() {
    std::cout << "\n兩個族群的循環密度\n";
    SpacedCycle kids(AudienceProfiles::get(Audience::KIDS), 6);
    SpacedCycle seniors(AudienceProfiles::get(Audience::SENIORS), 4);

    checkEq(seniors.capDays(), 8,  "★ 長者版頂點 8——循環密集");
    checkEq(kids.capDays(),   21, "★ 兒童版頂點 21——循環寬鬆");
    check(seniors.cycleLength() < kids.cycleLength(),
          "  長者版一輪的步數較少，回頭練得更頻繁");

    seniors.setCapDays(13);
    checkEq(seniors.capDays(), 13, "可以客製化頂點");
}

static void test_排程推進() {
    std::cout << "\n排程 · 成功往前、失敗退一格\n";
    SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 4);

    checkEq(c.scheduleOf(0).step, 0, "起始在第 0 步");
    checkEq(c.scheduleOf(0).days_until_due, 1, "  第一次間隔 1 天");

    c.record(0, true, 2, 0, 1);
    checkEq(c.scheduleOf(0).step, 1, "成功 → 往前一步");
    checkEq(c.scheduleOf(0).days_until_due, 2, "  間隔變成 2 天");

    c.record(0, true, 2, 1, 2);
    checkEq(c.scheduleOf(0).days_until_due, 3, "再成功 → 3 天");

    c.record(0, false, 5, 2, 2);
    checkEq(c.scheduleOf(0).step, 1,
            "★ 失敗退一格——不是打回原點，一次失敗不代表全部忘光");
    checkEq(c.scheduleOf(0).days_until_due, 2, "  間隔退回 2 天");

    // 在第 0 步失敗不會變成負的
    SpacedCycle c2(AudienceProfiles::get(Audience::SENIORS), 4);
    c2.record(0, false, 5, 0, 0);
    checkEq(c2.scheduleOf(0).step, 0, "在起點失敗不會退到負數");
}

static void test_到期() {
    std::cout << "\n到期判定\n";
    SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 4);

    checkEq((int)c.dueTechniques().size(), 0, "一開始還沒到期");

    c.advanceDays(1);
    checkEq((int)c.dueTechniques().size(), 4, "★ 過一天後四個技巧都到期");

    c.record(0, true, 2, 0, 1);        // 練完第一個，間隔變 2 天
    checkEq((int)c.dueTechniques().size(), 3, "  練過的那個不再到期");

    c.advanceDays(2);
    checkEq((int)c.dueTechniques().size(), 4, "再過兩天又全部到期");
}

static void test_循環報告() {
    std::cout << "\n循環報告\n";
    SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 4);

    // 技巧 0 很強、技巧 2 很弱
    for (int i = 0; i < 10; ++i) {
        c.record(0, true,  1, 0, 3);      // 全對，很快
        c.record(1, i % 2 == 0, 3, 0, 1); // 一半
        c.record(2, false, 5, 0, 0);      // 全錯
        c.record(3, i % 4 != 0, 2, 0, 2); // 大多對
    }

    CycleReport r = c.finishCycle();
    checkEq(r.cycle_number, 1, "第 1 輪");
    checkEq(r.total_attempts, 40, "共 40 次練習");
    check(!r.hasPrevious(), "第一輪沒有上一輪可比");

    bool s0 = false, n2 = false;
    for (int t : r.strengths)  if (t == 0) s0 = true;
    for (int t : r.needs_work) if (t == 2) n2 = true;
    check(s0, "★ 技巧 0 被列為強項（自主率 100%）");
    check(n2, "★ 技巧 2 被列為待補強（自主率 0%）");

    check(r.next_weights.at(2) > r.next_weights.at(0),
          "★ 弱項的下一輪權重高於強項");
    check(r.next_weights.at(0) > 0.0,
          "★ 強項的權重降低但不歸零——已學會的技巧仍要持續使用");

    check(r.stability > 0, "有算出穩定度");
    std::cout << "         （穩定度 " << r.stability
              << "，技巧之間落差越大這個值越高）\n";
}

static void test_跨輪比較() {
    std::cout << "\n跨輪比較\n";
    SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 2);

    // 第一輪：一半成功
    for (int i = 0; i < 10; ++i) {
        c.record(0, i % 2 == 0, 3, 0, 1);
        c.record(1, i % 2 == 0, 3, 0, 1);
    }
    CycleReport r1 = c.finishCycle();

    // 第二輪：全部成功
    for (int i = 0; i < 10; ++i) {
        c.record(0, true, 2, 1, 2);
        c.record(1, true, 2, 1, 2);
    }
    CycleReport r2 = c.finishCycle();

    checkEq(r2.cycle_number, 2, "第 2 輪");
    check(r2.hasPrevious(), "★ 有上一輪可以比較");
    check(r2.improvement() > 0, "★ 自主率提升，improvement 為正");
    std::cout << "         （" << (int)(r1.overall_autonomy*100) << "% → "
              << (int)(r2.overall_autonomy*100) << "%）\n";
}

static void test_難度調節() {
    std::cout << "\n難度自動調節\n";

    // 通過率高 + 速度快 → 加難
    {
        SpacedCycle c(AudienceProfiles::get(Audience::KIDS), 2);
        for (int i = 0; i < 10; ++i) {
            c.record(0, true, 1, 0, 3);
            c.record(1, true, 2, 0, 3);
        }
        CycleReport r = c.finishCycle();
        check(r.next_difficulty_ratio > 0.6,
              "★ 通過率高且速度快 → 提高難題比例");
        std::cout << "         「" << r.difficulty_note << "」\n";
    }

    // 通過率高但慢 → 維持
    {
        SpacedCycle c(AudienceProfiles::get(Audience::KIDS), 2);
        for (int i = 0; i < 10; ++i) {
            c.record(0, true, 8, 0, 3);
            c.record(1, true, 9, 0, 3);
        }
        CycleReport r = c.finishCycle();
        check(r.next_difficulty_ratio <= 0.6,
              "★ 答得準但慢 → 不加難，先練熟練度");
        std::cout << "         「" << r.difficulty_note << "」\n";
    }

    // 通過率低 → 降難
    {
        SpacedCycle c(AudienceProfiles::get(Audience::KIDS), 2);
        for (int i = 0; i < 10; ++i) {
            c.record(0, false, 6, 0, 0);
            c.record(1, false, 6, 0, 0);
        }
        CycleReport r = c.finishCycle();
        check(r.next_difficulty_ratio < 0.4,
              "★ 通過率偏低 → 降低難題比例");
        std::cout << "         「" << r.difficulty_note << "」\n";
    }

    // 長者版有難度上限
    {
        SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 2);
        for (int i = 0; i < 10; ++i) {
            c.record(0, true, 1, 0, 3);
            c.record(1, true, 1, 0, 3);
        }
        CycleReport r = c.finishCycle();
        check(r.next_difficulty_ratio <= 0.6,
              "★ 長者版即使表現極佳，難度也有上限");
    }
}

static void test_學會的記錄() {
    std::cout << "\n升級的記錄\n";
    SpacedCycle c(AudienceProfiles::get(Audience::SENIORS), 3);

    c.record(0, true,  2, 0, 2);      // 掌握度 0 → 2
    c.record(1, false, 5, 1, 1);      // 沒變
    c.record(2, true,  2, 2, 3);      // 2 → 3

    CycleReport r = c.finishCycle();
    checkEq((int)r.learned.size(), 2, "★ 兩個技巧升級了");
    bool has0 = false, has2 = false, has1 = false;
    for (int t : r.learned) {
        if (t == 0) has0 = true;
        if (t == 1) has1 = true;
        if (t == 2) has2 = true;
    }
    check(has0 && has2, "  記錄的是升級的那兩個");
    check(!has1, "  掌握度沒變的不算");
}

int main() {
    std::cout << "間隔重複與循環報告 · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_費氏循環();
    test_兩族群的頂點();
    test_排程推進();
    test_到期();
    test_循環報告();
    test_跨輪比較();
    test_難度調節();
    test_學會的記錄();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
