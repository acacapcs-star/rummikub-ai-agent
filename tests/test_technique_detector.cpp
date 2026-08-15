/* -------------------------------------------------------
   technique_detector 的單元測試。

   測的是「從盤面變化反推玩家用了哪一招」這件事。
   這個模組的檔頭寫著：寧可漏判，不可誤判——
   因為誤判會讓玩家在還沒學會的情況下被判定過關，
   等於系統發了一張沒讀過書的畢業證書。

   所以這份測試的重心不在「六個正例都認得出來」（那個你已經手動驗過），
   而在**誤判防線**：一整組「長得很像但其實不是」的盤面，
   每一個都必須回傳 NONE。那條原則現在是檔頭的一句話，
   這裡把它變成會叫的斷言。

   另外每一個測試盤面在使用前都會先用 Validator 檢查是不是合法盤面——
   如果測試資料本身就不合法，那測出來的結果沒有意義。

       g++ -std=c++17 -I src src/tile.cpp src/validator.cpp
           src/coach_campaign.cpp src/technique_detector.cpp
           tests/test_technique_detector.cpp -o test_technique_detector
       ./test_technique_detector
------------------------------------------------------- */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "tile.h"
#include "validator.h"
#include "coach_campaign.h"
#include "technique_detector.h"

// ── 極簡測試框架（跟 test_validator.cpp 同一套）─────────
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

// ── 判斷輔助 ─────────────────────────────────────────────
using Sets = std::vector<std::vector<Tile*>>;
using Hand = std::vector<Tile*>;

static bool has(const std::vector<Technique>& v, Technique t) {
    return std::find(v.begin(), v.end(), t) != v.end();
}

// 每一個測試盤面都必須是合法盤面。
// 這一條不是在測 detector，是在測「我寫的測試資料有沒有寫錯」——
// 用不合法的盤面去測誤判防線，測出來的通過是假的。
static bool legalSnapshot(const MoveSnapshot& s, const std::string& name) {
    bool before_ok = Validator::isValidBoard(s.board_before);
    bool after_ok  = Validator::isValidBoard(s.board_after);
    if (!before_ok) std::cout << "  FAIL 測試資料本身不合法（before）: " << name << "\n";
    if (!after_ok)  std::cout << "  FAIL 測試資料本身不合法（after）: "  << name << "\n";
    if (!before_ok || !after_ok) { ++g_failed; return false; }
    ++g_passed;
    std::cout << "  ok   測試盤面合法: " << name << "\n";
    return true;
}

static MoveSnapshot snap(Sets before, Sets after, Hand hb, Hand ha,
                         bool melded_before = true) {
    MoveSnapshot s;
    s.board_before = std::move(before);
    s.board_after  = std::move(after);
    s.hand_before  = std::move(hb);
    s.hand_after   = std::move(ha);
    s.initial_meld_done_before = melded_before;
    return s;
}

// ══════════════════════════════════════════════════════════
//  正例：六個情境的回歸測試
//
//  這一組是把你手動驗過的六個情境固定下來，之後改 detector
//  的時候有東西擋著。正例壞掉只是漏判（可接受），
//  真正的防線在下面那一組負例。
// ══════════════════════════════════════════════════════════
static void test_positive_attach_run() {
    std::cout << "\n正例 · 接龍頭尾\n";

    Tile* r3 = T(3, Color::RED);
    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* b7 = T(7, Color::BLUE);

    MoveSnapshot s = snap({{r4, r5, r6}}, {{r3, r4, r5, r6}}, {r3, b7}, {b7});
    if (!legalSnapshot(s, "接龍頭尾")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::ATTACH_RUN), "接在龍頭前面應判為 ATTACH_RUN");
    check(!has(got, Technique::BOARD_RESHUFFLE),
          "單純延長不算大風吹——原本那組還完整地在裡面");
    check(TechniqueDetector::tilesPlayedCount(s) == 1, "這一手出一張");
    check(!TechniqueDetector::touchedExistingTiles(s), "延長沒有動到桌面原有的牌");
}

static void test_positive_complete_group() {
    std::cout << "\n正例 · 補第四色\n";

    Tile* r5 = T(5, Color::RED);
    Tile* b5 = T(5, Color::BLUE);
    Tile* k5 = T(5, Color::BLACK);
    Tile* y5 = T(5, Color::YELLOW);

    MoveSnapshot s = snap({{r5, b5, k5}}, {{r5, b5, k5, y5}}, {y5}, {});
    if (!legalSnapshot(s, "補第四色")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::COMPLETE_GROUP), "三張變四張應判為 COMPLETE_GROUP");
    check(!has(got, Technique::ATTACH_RUN), "Group 補色不該同時算接龍");
}

static void test_positive_complete_group_with_joker() {
    std::cout << "\n正例 · 用 Joker 補第四色\n";

    Tile* r7 = T(7, Color::RED);
    Tile* b7 = T(7, Color::BLUE);
    Tile* k7 = T(7, Color::BLACK);
    Tile* jo = J();

    MoveSnapshot s = snap({{r7, b7, k7}}, {{r7, b7, k7, jo}}, {jo}, {});
    if (!legalSnapshot(s, "Joker 補第四色")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::COMPLETE_GROUP), "用 Joker 補滿 Group 也該判為 COMPLETE_GROUP");
    check(!has(got, Technique::JOKER_FILL),
          "Joker 補 Group 不是「補 Run 的缺口」，不該判為 JOKER_FILL");
}

static void test_positive_joker_fill() {
    std::cout << "\n正例 · Joker 補 Run 中間的缺口\n";

    Tile* b1 = T(1, Color::BLUE);
    Tile* b2 = T(2, Color::BLUE);
    Tile* b3 = T(3, Color::BLUE);
    Tile* r4 = T(4, Color::RED);
    Tile* jo = J();
    Tile* r6 = T(6, Color::RED);

    // 桌面原本有一組藍色（無關），玩家自己新開一組 紅4-Joker-紅6
    MoveSnapshot s = snap({{b1, b2, b3}},
                          {{b1, b2, b3}, {r4, jo, r6}},
                          {r4, jo, r6}, {});
    if (!legalSnapshot(s, "Joker 補缺口")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::JOKER_FILL), "Joker 落在 Run 中間應判為 JOKER_FILL");
    check(!has(got, Technique::ATTACH_RUN), "新開一組不是接龍");
    check(TechniqueDetector::tilesPlayedCount(s) == 3, "這一手出三張");
}

static void test_positive_initial_meld() {
    std::cout << "\n正例 · 破冰\n";

    Tile* y10 = T(10, Color::YELLOW);
    Tile* y11 = T(11, Color::YELLOW);
    Tile* y12 = T(12, Color::YELLOW);

    MoveSnapshot s = snap({}, {{y10, y11, y12}}, {y10, y11, y12}, {}, false);
    if (!legalSnapshot(s, "破冰")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::INITIAL_MELD), "破冰前出牌應判為 INITIAL_MELD");

    // 已破冰之後同樣的動作就不該再算破冰
    MoveSnapshot s2 = snap({}, {{y10, y11, y12}}, {y10, y11, y12}, {}, true);
    std::vector<Technique> got2 = TechniqueDetector::detect(s2);
    check(!has(got2, Technique::INITIAL_MELD), "已破冰後不該重複判為 INITIAL_MELD");
}

static void test_positive_run_split() {
    std::cout << "\n正例 · 長龍切斷\n";

    Tile* k1 = T(1, Color::BLACK);
    Tile* k2 = T(2, Color::BLACK);
    Tile* k3 = T(3, Color::BLACK);
    Tile* k4 = T(4, Color::BLACK);
    Tile* k5 = T(5, Color::BLACK);
    Tile* k6 = T(6, Color::BLACK);
    Tile* k7 = T(7, Color::BLACK);
    Tile* k8 = T(8, Color::BLACK);

    MoveSnapshot s = snap({{k1, k2, k3, k4, k5, k6, k7, k8}},
                          {{k1, k2, k3}, {k4, k5, k6, k7, k8}},
                          {}, {});
    if (!legalSnapshot(s, "長龍切斷")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(has(got, Technique::RUN_SPLIT), "八張切成兩條應判為 RUN_SPLIT");
    check(!has(got, Technique::BOARD_RESHUFFLE),
          "切斷已經是更明確的技巧，不該再重複記為大風吹");
}

static void test_positive_draw_only() {
    std::cout << "\n正例 · 只抽牌（什麼都沒做）\n";

    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* b9 = T(9, Color::BLUE);
    Tile* y2 = T(2, Color::YELLOW);

    // 抽了一張牌：手牌變多，桌面完全沒動
    MoveSnapshot s = snap({{r4, r5, r6}}, {{r4, r5, r6}}, {b9}, {b9, y2});
    if (!legalSnapshot(s, "只抽牌")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(got.empty(), "只抽牌不該判出任何技巧");
    check(TechniqueDetector::tilesPlayedCount(s) == 0, "只抽牌出牌數為 0");
    check(!TechniqueDetector::touchedExistingTiles(s), "只抽牌沒有動到桌面");
}

// ══════════════════════════════════════════════════════════
//  誤判防線：長得很像，但不是
//
//  這一組才是這份測試存在的理由。
//  「寧可漏判，不可誤判」目前只是檔頭的一句話——
//  下面每一條都是那句話的一個具體後果，
//  任何一條變紅，就代表有玩家會被誤判過關。
// ══════════════════════════════════════════════════════════
static void test_negative_new_set_is_not_attach() {
    std::cout << "\n誤判防線 · 新開一組 ≠ 接龍\n";

    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* b1 = T(1, Color::BLUE);
    Tile* b2 = T(2, Color::BLUE);
    Tile* b3 = T(3, Color::BLUE);

    MoveSnapshot s = snap({{r4, r5, r6}},
                          {{r4, r5, r6}, {b1, b2, b3}},
                          {b1, b2, b3}, {});
    if (!legalSnapshot(s, "新開一組")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(!has(got, Technique::ATTACH_RUN),
          "自己另外開一組不是接龍——沒有接到既有的牌");
    check(!has(got, Technique::COMPLETE_GROUP), "新開的 Run 不該算補第四色");
    check(!has(got, Technique::BOARD_RESHUFFLE), "沒動到桌面原牌不算大風吹");
    check(got.empty(), "新開一組（已破冰）不該判出任何官方招數");
}

static void test_negative_joker_at_tail_is_not_fill() {
    std::cout << "\n誤判防線 · Joker 接在尾端 ≠ 補缺口\n";

    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* jo = J();

    MoveSnapshot s = snap({{r4, r5, r6}}, {{r4, r5, r6, jo}}, {jo}, {});
    if (!legalSnapshot(s, "Joker 接尾端")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(!has(got, Technique::JOKER_FILL),
          "Joker 放尾端只是延長，任何一張普通牌也做得到，不是這關要教的");
    check(has(got, Technique::ATTACH_RUN), "但它確實是一次接龍，應該照實記錄");
}

static void test_negative_joker_at_head_is_not_fill() {
    std::cout << "\n誤判防線 · Joker 接在頭端 ≠ 補缺口\n";

    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* r7 = T(7, Color::RED);
    Tile* jo = J();

    MoveSnapshot s = snap({{r5, r6, r7}}, {{jo, r5, r6, r7}}, {jo}, {});
    if (!legalSnapshot(s, "Joker 接頭端")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(!has(got, Technique::JOKER_FILL), "Joker 放頭端同樣只是延長");
}

static void test_negative_short_run_split() {
    std::cout << "\n誤判防線 · 五張拆開 ≠ 長龍切斷\n";

    Tile* r1 = T(1, Color::RED);
    Tile* r2 = T(2, Color::RED);
    Tile* r3 = T(3, Color::RED);
    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);

    // 五張的 Run 拆成 1-2-3 與 4-5-6（手上補了一張紅 6）
    MoveSnapshot s = snap({{r1, r2, r3, r4, r5}},
                          {{r1, r2, r3}, {r4, r5, r6}},
                          {r6}, {});
    if (!legalSnapshot(s, "五張拆開")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(!has(got, Technique::RUN_SPLIT),
          "長龍切斷的門檻是六張以上，五張不算");
    check(has(got, Technique::BOARD_RESHUFFLE),
          "但它確實動了桌面，應該記為大風吹重組");
}

static void test_negative_merged_run_is_not_split() {
    std::cout << "\n誤判防線 · 長龍被併進更長的組合 ≠ 切斷\n";

    Tile* k1 = T(1, Color::BLACK);
    Tile* k2 = T(2, Color::BLACK);
    Tile* k3 = T(3, Color::BLACK);
    Tile* k4 = T(4, Color::BLACK);
    Tile* k5 = T(5, Color::BLACK);
    Tile* k6 = T(6, Color::BLACK);
    Tile* k7 = T(7, Color::BLACK);

    MoveSnapshot s = snap({{k1, k2, k3, k4, k5, k6}},
                          {{k1, k2, k3, k4, k5, k6, k7}},
                          {k7}, {});
    if (!legalSnapshot(s, "長龍被延長")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(!has(got, Technique::RUN_SPLIT),
          "長龍消失不代表被切斷，也可能只是被延長——這裡必須看到「一分為二」的證據");
    check(has(got, Technique::ATTACH_RUN), "這是一次接龍");
}

static void test_negative_no_change_at_all() {
    std::cout << "\n誤判防線 · 盤面完全沒變\n";

    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* b9 = T(9, Color::BLUE);

    MoveSnapshot s = snap({{r4, r5, r6}}, {{r4, r5, r6}}, {b9}, {b9});
    if (!legalSnapshot(s, "盤面沒變")) return;

    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(got.empty(), "什麼都沒發生時必須回傳空——這是最基本的一條");

    // 破冰前什麼都沒做，也不該算破冰
    MoveSnapshot s2 = snap({{r4, r5, r6}}, {{r4, r5, r6}}, {b9}, {b9}, false);
    std::vector<Technique> got2 = TechniqueDetector::detect(s2);
    check(!has(got2, Technique::INITIAL_MELD), "沒出牌不該算破冰");
}

static void test_negative_empty_board() {
    std::cout << "\n誤判防線 · 空盤面\n";

    Tile* r4 = T(4, Color::RED);

    MoveSnapshot s = snap({}, {}, {r4}, {r4});
    std::vector<Technique> got = TechniqueDetector::detect(s);
    check(got.empty(), "空盤面不該判出任何技巧，也不該當掉");
}

// ══════════════════════════════════════════════════════════
//  Private 自創招數用的「戰果」判斷
//
//  這幾個數字之後要拿來觸發命名視窗（3–4 張安靜記錄、
//  5–6 張詢問、≥7 張主動跳出），數錯就會在錯的時機打擾玩家。
// ══════════════════════════════════════════════════════════
static void test_outcome_counters() {
    std::cout << "\n戰果判斷 · 出牌張數與是否動到桌面\n";

    Tile* r1 = T(1, Color::RED);
    Tile* r2 = T(2, Color::RED);
    Tile* r3 = T(3, Color::RED);
    Tile* b1 = T(1, Color::BLUE);
    Tile* b2 = T(2, Color::BLUE);
    Tile* b3 = T(3, Color::BLUE);
    Tile* y1 = T(1, Color::YELLOW);
    Tile* k9 = T(9, Color::BLACK);

    // 一次出六張（兩組），沒動到桌面
    MoveSnapshot s = snap({}, {{r1, r2, r3}, {b1, b2, b3}},
                          {r1, r2, r3, b1, b2, b3, y1, k9}, {y1, k9});
    if (!legalSnapshot(s, "一手出六張")) return;

    check(TechniqueDetector::tilesPlayedCount(s) == 6, "出牌數應為 6");
    check(!TechniqueDetector::touchedExistingTiles(s),
          "桌面原本是空的，談不上動到既有的牌");

    // 手牌沒少但桌面重排：出牌數 0，卻動了桌面
    Tile* g1 = T(1, Color::BLACK);
    Tile* g2 = T(2, Color::BLACK);
    Tile* g3 = T(3, Color::BLACK);
    Tile* g4 = T(4, Color::BLACK);
    Tile* g5 = T(5, Color::BLACK);
    Tile* g6 = T(6, Color::BLACK);
    MoveSnapshot s2 = snap({{g1, g2, g3, g4, g5, g6}},
                           {{g1, g2, g3}, {g4, g5, g6}},
                           {k9}, {k9});
    if (!legalSnapshot(s2, "只重排桌面")) return;

    check(TechniqueDetector::tilesPlayedCount(s2) == 0, "純重排出牌數為 0");
    check(TechniqueDetector::touchedExistingTiles(s2), "純重排應判為動到桌面");
}

// ══════════════════════════════════════════════════════════
//  端到端：偵測 → 記錄 → 星等 → 過關
//
//  單獨測 detector 跟單獨測 campaign 都過，不代表串起來會對。
//  這一段走完整條路徑：從盤面變化一路走到「可以進下一關了嗎」。
// ══════════════════════════════════════════════════════════
static void test_end_to_end_level1() {
    std::cout << "\n端到端 · 用三次接龍過第一關\n";

    CoachCampaign campaign;
    check(campaign.currentLevel() == 1, "起點在第一關");

    // 三次接龍：第一次看了答案，後兩次沒看
    const bool saw_reveal[3] = {true, false, false};

    for (int i = 0; i < 3; ++i) {
        int base = 1 + i * 4;   // 每次用不同的牌，避免指標重複
        Tile* a = T(base,     Color::YELLOW);
        Tile* b = T(base + 1, Color::YELLOW);
        Tile* c = T(base + 2, Color::YELLOW);
        Tile* d = T(base + 3, Color::YELLOW);

        MoveSnapshot s = snap({{b, c, d}}, {{a, b, c, d}}, {a}, {});
        std::vector<Technique> got = TechniqueDetector::detect(s);
        check(has(got, Technique::ATTACH_RUN),
              "第 " + std::to_string(i + 1) + " 次應偵測到接龍");

        for (Technique t : got) {
            TechniqueUse use;
            use.technique = t;
            use.saw_reveal = saw_reveal[i];
            use.saw_point = false;
            campaign.recordUse(use);
        }

        // 前兩次不該過關：第一次次數不夠，第二次自主次數才 1 但總次數才 2
        if (i < 2) {
            check(!campaign.canAdvance(),
                  "只用了 " + std::to_string(i + 1) + " 次時不該過關");
        }
    }

    const TechniqueProgress& p = campaign.progressOf(Technique::ATTACH_RUN);
    check(p.total_uses == 3, "總共記錄三次");
    check(p.unassisted_uses == 2, "其中兩次沒看答案");
    check(p.mastery == Mastery::DISCOVERED,
          "最後一次完全沒提示，應該升到三星");
    check(campaign.canAdvance(), "三次且有自主，應該可以過關");
    check(campaign.advance() && campaign.currentLevel() == 2, "順利進入第二關");
}

// ── main ─────────────────────────────────────────────────
int main() {
    std::cout << "TechniqueDetector 測試\n";

    test_positive_attach_run();
    test_positive_complete_group();
    test_positive_complete_group_with_joker();
    test_positive_joker_fill();
    test_positive_initial_meld();
    test_positive_run_split();
    test_positive_draw_only();

    test_negative_new_set_is_not_attach();
    test_negative_joker_at_tail_is_not_fill();
    test_negative_joker_at_head_is_not_fill();
    test_negative_short_run_split();
    test_negative_merged_run_is_not_split();
    test_negative_no_change_at_all();
    test_negative_empty_board();

    test_outcome_counters();
    test_end_to_end_level1();

    std::cout << "\n─────────────────────────────\n";
    std::cout << g_passed << " 項通過，" << g_failed << " 項失敗\n";

    cleanup();
    return g_failed == 0 ? 0 : 1;
}
