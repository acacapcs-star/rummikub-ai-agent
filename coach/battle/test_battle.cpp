/* -------------------------------------------------------
   test_battle.cpp —— mini_battle 的單元測試

   兩塊：
     解析器  語法對不對、錯誤訊息準不準、解鎖擋不擋得住
     檢查器  規則判定正不正確

   解析器的測試比檢查器多，因為**錯誤訊息的品質就是這個功能的價值**。
   一個只會說「解析失敗」的腳本語言，玩家寫兩次就不想寫了。

   編譯：
     g++ -std=c++17 test_battle.cpp -o test_battle && ./test_battle
------------------------------------------------------- */

#include "battle_parser.h"
#include <iostream>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const std::string& name) {
    if (cond) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else      { ++g_fail; std::cout << "  FAIL " << name << "\n"; }
}

static void checkEq(int got, int want, const std::string& name) {
    if (got == want) { ++g_pass; std::cout << "  ok   " << name << "\n"; }
    else { ++g_fail; std::cout << "  FAIL " << name
                               << "   (得到 " << got << "，預期 " << want << ")\n"; }
}

// ── 測試用的玩家狀態 ─────────────────────────────────────
static PlayerState makePlayer(int level, std::vector<int> mastery) {
    PlayerState p;
    p.level = level;
    p.mastery = std::move(mastery);
    return p;
}

static PlayerState expert()  { return makePlayer(6, {3,3,3,3,3,3}); }
static PlayerState novice()  { return makePlayer(1, {0,0,0,0,0,0}); }

// 解析一段腳本，回傳是否成功
static bool parses(const std::string& src, const PlayerState& p,
                   BattleMode mode = BattleMode::CUSTOM,
                   Battle* out = nullptr,
                   std::vector<ParseError>* errs_out = nullptr) {
    std::vector<ParseError> errs;
    auto b = BattleParser::parse(src, p, errs, mode);
    if (errs_out) *errs_out = errs;
    if (b && out) *out = *b;
    return b.has_value();
}

// ══════════════════════════════════════════════════════════
//  語法
// ══════════════════════════════════════════════════════════
static void test_basic_syntax() {
    std::cout << "\n語法 · 基本形式\n";
    auto p = expert();

    check(parses(R"(battle "測試" { require tilesPlayed >= 3; })", p),
          "最小的合法腳本");

    Battle b;
    parses(R"(battle "我的挑戰" {
        require tilesPlayed >= 3;
        forbid  joker;
        bonus   touchedBoard : 2;
        limit   time = 10;
    })", p, BattleMode::CUSTOM, &b);
    checkEq(static_cast<int>(b.clauses.size()), 4, "四種子句都解析得出來");
    check(b.name == "我的挑戰", "名字正確");

    check(parses(R"(
        // 這是註解
        battle "測試" {
            require tilesPlayed >= 3;   // 行尾註解
        })", p),
        "註解會被忽略");

    check(parses("battle \"測試\" {\n\n\n  require tilesPlayed >= 3;\n\n}", p),
          "空行不影響");
}

static void test_syntax_errors() {
    std::cout << "\n語法 · 錯誤處理\n";
    auto p = expert();

    check(!parses(R"(battle 測試 { require tilesPlayed >= 3; })", p),
          "名字沒加引號 → 拒絕");
    check(!parses(R"(battle "測試" require tilesPlayed >= 3; })", p),
          "少了左大括號 → 拒絕");
    check(!parses(R"(battle "測試" { require tilesPlayed >= 3; )", p),
          "少了右大括號 → 拒絕");
    check(!parses(R"(battle "測試" { require tilesPlayed >= 3 })", p),
          "少了分號 → 拒絕");
    check(!parses(R"(battle "測試" { })", p),
          "沒有任何規則 → 拒絕（空的挑戰沒有意義）");
    check(!parses(R"(battle "沒關引號 { require tilesPlayed >= 3; })", p),
          "字串沒關 → 拒絕");
    check(!parses(R"(battle "測試" { allow tilesPlayed >= 3; })", p),
          "不存在的關鍵字 → 拒絕");
}

static void test_typo_suggestion() {
    std::cout << "\n語法 · 拼字建議\n";
    auto p = expert();
    std::vector<ParseError> errs;

    parses(R"(battle "測試" { forbid jokr; })", p, BattleMode::CUSTOM, nullptr, &errs);
    check(!errs.empty(), "打錯字會產生錯誤");
    check(!errs.empty() && errs[0].message.find("joker") != std::string::npos,
          "★ 'jokr' → 建議 'joker'");

    errs.clear();
    parses(R"(battle "測試" { require tilesPlayd >= 3; })", p,
           BattleMode::CUSTOM, nullptr, &errs);
    check(!errs.empty() && errs[0].message.find("tilesPlayed") != std::string::npos,
          "★ 'tilesPlayd' → 建議 'tilesPlayed'");

    errs.clear();
    parses(R"(battle "測試" { require zzzzzzzz >= 3; })", p,
           BattleMode::CUSTOM, nullptr, &errs);
    check(!errs.empty() && errs[0].message.find("你是指") == std::string::npos,
          "完全不像任何欄位時，不亂建議");
}

// ══════════════════════════════════════════════════════════
//  解鎖
// ══════════════════════════════════════════════════════════
static void test_level_lock() {
    std::cout << "\n解鎖 · 關卡\n";
    std::vector<ParseError> errs;

    check(parses(R"(battle "測試" { require tilesPlayed >= 3; })", novice()),
          "L1 欄位新手可用");

    errs.clear();
    check(!parses(R"(battle "測試" { require joker; })", novice(),
                  BattleMode::CUSTOM, nullptr, &errs),
          "L3 欄位新手不可用");
    check(!errs.empty() && errs[0].is_lock, "標記為 lock 錯誤（要顯示鎖頭圖示）");
    check(!errs.empty() && errs[0].lock_hint.find("第 3 關") != std::string::npos,
          "錯誤訊息說明需要通過第幾關");

    // 逐關檢查每個欄位的解鎖時機
    struct Case { const char* field; int level; };
    Case cases[] = {
        { "tilesPlayed",  1 }, { "handSize",     1 }, { "time",        1 },
        { "setsAffected", 2 }, { "joker",        3 }, { "meldScore",   4 },
        { "touchedBoard", 5 }, { "runSplit",     6 },
    };
    for (const auto& c : cases) {
        std::string src = std::string("battle \"t\" { require ") + c.field + " >= 1; }";
        bool below = parses(src, makePlayer(c.level - 1, {3,3,3,3,3,3}));
        bool at    = parses(src, makePlayer(c.level,     {3,3,3,3,3,3}));
        check(!below && at,
              std::string(c.field) + " 恰好在第 " + std::to_string(c.level) + " 關解鎖");
    }
}

static void test_mastery_lock() {
    std::cout << "\n解鎖 · 掌握度\n";

    // Joker 是第 3 招（index 2）
    auto star1 = makePlayer(6, {3,3,1,3,3,3});   // Joker ⭐
    auto star2 = makePlayer(6, {3,3,2,3,3,3});   // Joker ⭐⭐
    auto star3 = makePlayer(6, {3,3,3,3,3,3});   // Joker ⭐⭐⭐

    check(parses(R"(battle "t" { require joker; })", star1),
          "⭐ 可以 require（要求自己用）");
    check(!parses(R"(battle "t" { forbid joker; })", star1),
          "⭐ 不能 forbid");
    check(parses(R"(battle "t" { forbid joker; })", star2),
          "⭐⭐ 可以 forbid（一般模式）");

    check(!parses(R"(battle "t" { forbid joker; })", star2, BattleMode::STRICT),
          "★ 嚴格模式下 ⭐⭐ 仍不能 forbid");
    check(parses(R"(battle "t" { forbid joker; })", star3, BattleMode::STRICT),
          "★ 嚴格模式需要 ⭐⭐⭐");

    check(parses(R"(battle "t" { require tilesPlayed >= 3; })",
                 makePlayer(6, {0,0,0,0,0,0})),
          "不對應技巧的欄位不需要掌握度");
}

static void test_preset_mode() {
    std::cout << "\n解鎖 · 預設模式不檢查\n";

    const char* no_joker = R"(battle "無 Joker" { forbid joker; })";

    check(!parses(no_joker, novice(), BattleMode::CUSTOM),
          "自訂模式：新手被擋");
    check(parses(no_joker, novice(), BattleMode::PRESET),
          "★ 預設模式：新手玩得到");

    const char* hard = R"(battle "混合" {
        forbid  joker;
        require touchedBoard;
        require runSplit;
    })";
    check(parses(hard, novice(), BattleMode::PRESET),
          "★ 預設模式下，連 L6 的欄位新手都能玩");
    check(!parses(hard, novice(), BattleMode::CUSTOM),
          "同一份腳本，自訂模式被擋");
}

// ══════════════════════════════════════════════════════════
//  規則檢查
// ══════════════════════════════════════════════════════════
static MoveMetrics makeMove(int tiles, bool joker, bool board, int time) {
    MoveMetrics m;
    m.tiles_played  = tiles;
    m.used_joker    = joker;
    m.touched_board = board;
    m.time_spent    = time;
    return m;
}

static void test_require() {
    std::cout << "\n檢查 · require\n";
    Battle b;
    parses(R"(battle "t" { require tilesPlayed >= 5; })", expert(),
           BattleMode::CUSTOM, &b);

    check( BattleChecker::check(b, makeMove(5, false, false, 0)).allowed, "出 5 張 → 通過");
    check( BattleChecker::check(b, makeMove(9, false, false, 0)).allowed, "出 9 張 → 通過");
    check(!BattleChecker::check(b, makeMove(4, false, false, 0)).allowed, "出 4 張 → 擋下");

    auto v = BattleChecker::check(b, makeMove(4, false, false, 0));
    check(!v.violations.empty(), "違規時有說明");
    check(!v.violations.empty() &&
          v.violations[0].find("實際 4") != std::string::npos,
          "★ 說明裡包含實際數值——只說「違規」對玩家沒有幫助");
}

static void test_forbid() {
    std::cout << "\n檢查 · forbid\n";
    Battle b;
    parses(R"(battle "t" { forbid joker; })", expert(), BattleMode::CUSTOM, &b);

    check( BattleChecker::check(b, makeMove(3, false, false, 0)).allowed, "沒用 Joker → 通過");
    check(!BattleChecker::check(b, makeMove(3, true,  false, 0)).allowed, "用了 Joker → 擋下");
}

static void test_limit() {
    std::cout << "\n檢查 · limit\n";
    Battle b;
    parses(R"(battle "t" { limit time = 10; })", expert(), BattleMode::CUSTOM, &b);

    check( BattleChecker::check(b, makeMove(3, false, false,  5)).allowed, "5 秒 → 通過");
    check( BattleChecker::check(b, makeMove(3, false, false, 10)).allowed, "剛好 10 秒 → 通過");
    check(!BattleChecker::check(b, makeMove(3, false, false, 11)).allowed, "11 秒 → 擋下");
}

static void test_bonus() {
    std::cout << "\n檢查 · bonus\n";
    Battle b;
    parses(R"(battle "t" {
        require tilesPlayed >= 3;
        bonus   tilesPlayed >= 8 : 5;
        bonus   touchedBoard : 2;
    })", expert(), BattleMode::CUSTOM, &b);

    checkEq(BattleChecker::check(b, makeMove(5, false, false, 0)).bonus, 0,  "5 張不加分");
    checkEq(BattleChecker::check(b, makeMove(9, false, false, 0)).bonus, 5,  "9 張 +5");
    checkEq(BattleChecker::check(b, makeMove(9, false, true,  0)).bonus, 7,  "9 張且動桌面 +7");
    checkEq(BattleChecker::check(b, makeMove(3, false, true,  0)).bonus, 2,  "只有動桌面 +2");

    check(BattleChecker::check(b, makeMove(3, false, false, 0)).allowed,
          "★ bonus 不成立不影響能不能出");
}

static void test_multiple_violations() {
    std::cout << "\n檢查 · 多重違規\n";
    Battle b;
    parses(R"(battle "t" {
        require tilesPlayed >= 5;
        forbid  joker;
        require touchedBoard;
        limit   time = 30;
    })", expert(), BattleMode::CUSTOM, &b);

    auto v = BattleChecker::check(b, makeMove(2, true, false, 50));
    check(!v.allowed, "全部違反 → 擋下");
    checkEq(static_cast<int>(v.violations.size()), 4,
            "★ 四條都列出來——不要只報第一個，玩家會改了一個又撞到下一個");

    auto ok = BattleChecker::check(b, makeMove(6, false, true, 20));
    check(ok.allowed && ok.violations.empty(), "全部符合 → 通過且無違規訊息");
}

static void test_flag_shorthand() {
    std::cout << "\n語法 · 旗標欄位的簡寫\n";
    Battle b1, b2;
    check(parses(R"(battle "t" { require touchedBoard; })", expert(),
                 BattleMode::CUSTOM, &b1),
          "require touchedBoard; 可省略運算子");
    check(parses(R"(battle "t" { require touchedBoard == 1; })", expert(),
                 BattleMode::CUSTOM, &b2),
          "寫完整也可以");

    auto m = makeMove(3, false, true, 0);
    check(BattleChecker::check(b1, m).allowed == BattleChecker::check(b2, m).allowed,
          "兩種寫法行為相同");
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "mini_battle · 單元測試\n";
    std::cout << "════════════════════════════════════════";

    test_basic_syntax();
    test_syntax_errors();
    test_typo_suggestion();
    test_level_lock();
    test_mastery_lock();
    test_preset_mode();
    test_require();
    test_forbid();
    test_limit();
    test_bonus();
    test_multiple_violations();
    test_flag_shorthand();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
