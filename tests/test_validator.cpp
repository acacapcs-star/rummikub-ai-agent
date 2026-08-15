/* -------------------------------------------------------
   validator 的單元測試。

   測的是 isValidRun / isValidGroup 這兩個函式——它們是整個遊戲引擎的地基，
   Board::applyProposedSets() 的七道檢查裡有兩道直接呼叫它們。
   一旦這裡判錯，AI 就會提交不合法的盤面而被引擎打回，
   或者更糟：把合法的組合誤判成不合法，白白放棄可以出的牌。

   測試不依賴任何框架，用最小的斷言巨集，單檔可編譯：

       g++ -std=c++17 -I src tests/test_validator.cpp src/validator.cpp src/tile.cpp -o test_validator
       ./test_validator
------------------------------------------------------- */

#include <iostream>
#include <string>
#include <vector>

#include "tile.h"
#include "validator.h"

// ── 極簡測試框架 ─────────────────────────────────────────
static int g_passed = 0;
static int g_failed = 0;

static void check(bool condition, const std::string& name) {
    if (condition) {
        ++g_passed;
        std::cout << "  ok   " << name << "\n";
    } else {
        ++g_failed;
        std::cout << "  FAIL " << name << "\n";
    }
}

// ── 造牌小工具 ───────────────────────────────────────────
// 所有 Tile 存在這個 pool 裡，測試期間不會被搬動，指標保持有效。
static std::vector<Tile*> g_pool;
static int g_next_id = 0;

static Tile* T(int number, Color color) {
    Tile* t = new Tile(g_next_id++, number, color);
    g_pool.push_back(t);
    return t;
}

static Tile* J() {
    Tile* t = new Tile(g_next_id++);
    g_pool.push_back(t);
    return t;
}

static void cleanup() {
    for (Tile* t : g_pool) delete t;
    g_pool.clear();
}

// ══════════════════════════════════════════════════════════
//  Run：3 張以上、同色、數字連續，Joker 可代入缺號
// ══════════════════════════════════════════════════════════
static void test_run_basics() {
    std::cout << "\nRun · 基本規則\n";

    check(Validator::isValidRun({T(3, Color::RED), T(4, Color::RED), T(5, Color::RED)}),
          "三張同色連號成立");

    check(Validator::isValidRun({T(1, Color::BLUE), T(2, Color::BLUE),
                                 T(3, Color::BLUE), T(4, Color::BLUE),
                                 T(5, Color::BLUE)}),
          "五張同色連號成立");

    check(!Validator::isValidRun({T(3, Color::RED), T(4, Color::RED)}),
          "只有兩張不成立");

    check(!Validator::isValidRun({T(3, Color::RED)}),
          "只有一張不成立");

    check(!Validator::isValidRun({}),
          "空集合不成立");
}

static void test_run_color() {
    std::cout << "\nRun · 顏色\n";

    check(!Validator::isValidRun({T(3, Color::RED), T(4, Color::BLUE), T(5, Color::RED)}),
          "中間換色不成立");

    check(!Validator::isValidRun({T(3, Color::RED), T(4, Color::RED), T(5, Color::BLACK)}),
          "尾端換色不成立");

    check(Validator::isValidRun({T(7, Color::YELLOW), T(8, Color::YELLOW),
                                 T(9, Color::YELLOW)}),
          "黃色也適用（四色都要能通過）");
}

static void test_run_consecutive() {
    std::cout << "\nRun · 連續性\n";

    check(!Validator::isValidRun({T(3, Color::RED), T(5, Color::RED), T(6, Color::RED)}),
          "中間跳號不成立");

    check(!Validator::isValidRun({T(3, Color::RED), T(3, Color::RED), T(4, Color::RED)}),
          "重複數字不成立");

    check(!Validator::isValidRun({T(5, Color::RED), T(4, Color::RED), T(3, Color::RED)}),
          "遞減順序不成立（順序有意義）");
}

static void test_run_joker() {
    std::cout << "\nRun · Joker 代入\n";

    check(Validator::isValidRun({T(3, Color::RED), J(), T(5, Color::RED)}),
          "Joker 補中間缺號");

    check(Validator::isValidRun({J(), T(4, Color::RED), T(5, Color::RED)}),
          "Joker 在開頭");

    check(Validator::isValidRun({T(3, Color::RED), T(4, Color::RED), J()}),
          "Joker 在結尾");

    check(Validator::isValidRun({T(3, Color::RED), J(), J(), T(6, Color::RED)}),
          "兩張 Joker 連續補號");

    check(!Validator::isValidRun({J(), J(), J()}),
          "全部都是 Joker 不成立——沒有任何已知數字可以推算");

    check(!Validator::isValidRun({T(3, Color::RED), J(), T(6, Color::RED)}),
          "一張 Joker 補不了兩格缺口");
}

static void test_run_boundary() {
    std::cout << "\nRun · 數字邊界（1–13）\n";

    check(Validator::isValidRun({T(1, Color::RED), T(2, Color::RED), T(3, Color::RED)}),
          "從 1 開始成立");

    check(Validator::isValidRun({T(11, Color::RED), T(12, Color::RED), T(13, Color::RED)}),
          "到 13 結束成立");

    check(!Validator::isValidRun({J(), T(1, Color::RED), T(2, Color::RED)}),
          "Joker 推算出 0 不成立——不能低於 1");

    check(!Validator::isValidRun({T(12, Color::RED), T(13, Color::RED), J()}),
          "Joker 推算出 14 不成立——不能超過 13");

    check(!Validator::isValidRun({T(13, Color::RED), T(1, Color::RED), T(2, Color::RED)}),
          "13 之後不會繞回 1");
}

// ══════════════════════════════════════════════════════════
//  Group：3–4 張、同數字、顏色互異，Joker 可補缺色
// ══════════════════════════════════════════════════════════
static void test_group_basics() {
    std::cout << "\nGroup · 基本規則\n";

    check(Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE), T(5, Color::BLACK)}),
          "三張同數異色成立");

    check(Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE),
                                   T(5, Color::BLACK), T(5, Color::YELLOW)}),
          "四張同數異色成立（四色滿）");

    check(!Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE)}),
          "只有兩張不成立");

    check(!Validator::isValidGroup({}),
          "空集合不成立");
}

static void test_group_size_limit() {
    std::cout << "\nGroup · 張數上限\n";

    // Rummikub 只有四色，所以 Group 最多四張——第五張必然重複顏色。
    check(!Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE),
                                    T(5, Color::BLACK), T(5, Color::YELLOW),
                                    T(5, Color::RED)}),
          "五張不成立——只有四種顏色");
}

static void test_group_distinct_colors() {
    std::cout << "\nGroup · 顏色互異\n";

    check(!Validator::isValidGroup({T(5, Color::RED), T(5, Color::RED), T(5, Color::BLUE)}),
          "顏色重複不成立");

    check(!Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE),
                                    T(5, Color::BLACK), T(5, Color::BLACK)}),
          "四張中有兩張同色不成立");
}

static void test_group_same_number() {
    std::cout << "\nGroup · 數字相同\n";

    check(!Validator::isValidGroup({T(5, Color::RED), T(6, Color::BLUE), T(5, Color::BLACK)}),
          "數字不同不成立");

    check(!Validator::isValidGroup({T(1, Color::RED), T(2, Color::BLUE), T(3, Color::BLACK)}),
          "三個都不同不成立");
}

static void test_group_joker() {
    std::cout << "\nGroup · Joker 補色\n";

    check(Validator::isValidGroup({T(5, Color::RED), T(5, Color::BLUE), J()}),
          "Joker 補第三色");

    check(Validator::isValidGroup({J(), T(5, Color::BLUE), T(5, Color::BLACK)}),
          "Joker 在開頭");

    check(Validator::isValidGroup({T(5, Color::RED), J(), J()}),
          "兩張 Joker 補兩色");

    check(!Validator::isValidGroup({J(), J(), J()}),
          "全部都是 Joker 不成立——沒有已知數字");

    check(!Validator::isValidGroup({T(5, Color::RED), T(5, Color::RED), J()}),
          "Joker 不能掩蓋顏色重複");
}

// ══════════════════════════════════════════════════════════
//  兩者互斥性：Run 與 Group 是不同的合法型態
// ══════════════════════════════════════════════════════════
static void test_run_group_are_distinct() {
    std::cout << "\nRun 與 Group 互不混淆\n";

    std::vector<Tile*> run = {T(3, Color::RED), T(4, Color::RED), T(5, Color::RED)};
    check(Validator::isValidRun(run) && !Validator::isValidGroup(run),
          "合法 Run 不會被判成 Group");

    std::vector<Tile*> group = {T(5, Color::RED), T(5, Color::BLUE), T(5, Color::BLACK)};
    check(Validator::isValidGroup(group) && !Validator::isValidRun(group),
          "合法 Group 不會被判成 Run");

    check(Validator::isValidSet(run) && Validator::isValidSet(group),
          "isValidSet 對兩者都成立");

    std::vector<Tile*> garbage = {T(3, Color::RED), T(7, Color::BLUE), T(11, Color::BLACK)};
    check(!Validator::isValidSet(garbage),
          "既非 Run 也非 Group 時 isValidSet 不成立");
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "validator 單元測試\n";
    std::cout << "==================\n";

    test_run_basics();
    test_run_color();
    test_run_consecutive();
    test_run_joker();
    test_run_boundary();

    test_group_basics();
    test_group_size_limit();
    test_group_distinct_colors();
    test_group_same_number();
    test_group_joker();

    test_run_group_are_distinct();

    std::cout << "\n==================\n";
    std::cout << "通過 " << g_passed << " 項，失敗 " << g_failed << " 項\n";

    cleanup();
    return g_failed == 0 ? 0 : 1;
}
