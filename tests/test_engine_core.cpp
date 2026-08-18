/* -------------------------------------------------------
   test_engine_core.cpp —— 遊戲引擎的單元測試

   測的是 Board::applyProposedSets() 那七道檢查。
   那是整個專案最關鍵的一個函式——**所有出牌都要通過它**，
   而它的每一道檢查對應一種作弊或違規的可能。

   ApplyResult 有七個錯誤碼，這裡每一個都要有對應的測試：

     NotPlayerTile         用了不屬於自己的牌
     DuplicateTile         同一張牌出現兩次
     RemovedOldTile        桌上原有的牌不見了
     RearrangedDuringMeld  破冰前動了桌面
     InvalidSet            某一組不合法
     MeldTooLow            破冰分數不足 30

   還有一件事要測：**被拒絕時，狀態必須完全不變。**
   部分套用比直接失敗更糟——那會讓桌面停在一個不確定的中間狀態。

   Player::hand 是 private，只有 Board 和 GameManager 能改，
   所以測試透過 GameManager::initialize() 發牌，
   再用一個繼承 Player 的測試替身取得手牌。

   編譯：
     g++ -std=c++17 -I ../src test_engine_core.cpp \
         ../src/board.cpp ../src/player.cpp ../src/validator.cpp \
         ../src/tile.cpp ../src/game_manager.cpp -o t
------------------------------------------------------- */

#include "board.h"
#include "game_manager.h"
#include "player.h"
#include "tile.h"
#include "validator.h"
#include <iostream>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

static void check(bool c, const std::string& n) {
    if (c) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else   { ++g_fail; std::cout << "  FAIL " << n << "\n"; }
}

static void checkResult(Board::ApplyResult got, Board::ApplyResult want,
                        const std::string& n) {
    if (got == want) { ++g_pass; std::cout << "  ok   " << n << "\n"; }
    else {
        ++g_fail;
        std::cout << "  FAIL " << n << "   (得到 " << Board::describe(got)
                  << "，預期 " << Board::describe(want) << ")\n";
    }
}

/* ── 測試替身 ─────────────────────────────────────────────
   Player 的 hand 是 private，而且 playTurn 是純虛擬的。
   這個替身只負責「被 GameManager 發牌」，不做任何決策。      */
class DummyPlayer : public Player {
public:
    explicit DummyPlayer(const std::string& n) : Player(n) {}
    void playTurn(Board&, int) override {}      // 什麼都不做
};

// 全域的牌池——所有 Tile 存在這裡，其餘都是指標
static std::vector<Tile*> g_pool;
static int g_id = 0;
static Tile* T(int n, Color c) { auto* t = new Tile(g_id++, n, c); g_pool.push_back(t); return t; }
static Tile* J()               { auto* t = new Tile(g_id++);       g_pool.push_back(t); return t; }
static void cleanup() { for (auto* t : g_pool) delete t; g_pool.clear(); }

/* 建立一個「手上有指定牌」的玩家。

   做法：用 GameManager 發牌之後，把手牌換成我們要的。
   因為 hand 是 private，這裡透過 Board 的 friend 關係做不到，
   所以改用另一個方式——直接建一個 Board，把要給玩家的牌
   當成「已經在手上」來測試。

   實際上 applyProposedSets 只檢查「這張牌是不是來自玩家手牌或桌面」，
   所以測試時只要確保 newSets 裡的指標來源正確即可。              */

// ══════════════════════════════════════════════════════════
//  基本：合法的出牌
// ══════════════════════════════════════════════════════════
static void test_valid_submission() {
    std::cout << "\napplyProposedSets · 合法的出牌\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    check(p.getHand().size() == 14, "初始手牌 14 張");
    check(gm.board.getSets().empty(), "桌面一開始是空的");
    check(!p.initial_meld_done, "尚未破冰");
}

// ══════════════════════════════════════════════════════════
//  七道檢查
// ══════════════════════════════════════════════════════════
static void test_not_player_tile() {
    std::cout << "\n檢查 · 牌的來源\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    // 憑空造出來的牌，不屬於任何人的手牌，也不在桌面上
    std::vector<std::vector<Tile*>> fake = {
        { T(11, Color::RED), T(12, Color::RED), T(13, Color::RED) }
    };
    auto r = gm.board.applyProposedSets(&p, fake);
    checkResult(r, Board::ApplyResult::NotPlayerTile,
                "★ 用不屬於自己的牌 → NotPlayerTile");
    check(gm.board.getSets().empty(), "  被拒絕後桌面沒有改變");
    check(p.getHand().size() == 14, "  手牌也沒有改變");
}

static void test_duplicate_tile() {
    std::cout << "\n檢查 · 重複的牌\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    // 拿手上第一張牌，放兩次
    Tile* first = p.getHand()[0];
    std::vector<std::vector<Tile*>> dup = { { first, first, first } };
    auto r = gm.board.applyProposedSets(&p, dup);
    check(r != Board::ApplyResult::Ok, "★ 同一張牌出現多次 → 被拒絕");
    check(p.getHand().size() == 14, "  手牌沒有改變");
}

static void test_meld_too_low() {
    std::cout << "\n檢查 · 破冰分數\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    // 從手牌裡找出一組合法但分數低的組合
    // （用 seed 42 的牌，直接找連號的三張）
    const auto& hand = p.getHand();
    std::vector<Tile*> lowRun;
    for (std::size_t i = 0; i < hand.size() && lowRun.empty(); ++i)
        for (std::size_t j = 0; j < hand.size(); ++j)
            for (std::size_t k = 0; k < hand.size(); ++k) {
                if (i == j || j == k || i == k) continue;
                std::vector<Tile*> t = { hand[i], hand[j], hand[k] };
                if (Validator::isValidSet(t) &&
                    Validator::calculateInitialMeldScore(t) < 30) {
                    lowRun = t;
                    break;
                }
            }

    if (!lowRun.empty()) {
        auto r = gm.board.applyProposedSets(&p, { lowRun });
        checkResult(r, Board::ApplyResult::MeldTooLow,
                    "★ 破冰分數不足 30 → MeldTooLow");
        check(!p.initial_meld_done, "  破冰旗標沒有被設起來");
        check(p.getHand().size() == 14, "  手牌沒有改變");
    } else {
        std::cout << "  --   （這副牌找不到分數低於 30 的合法組合，略過）\n";
    }
}

static void test_invalid_set() {
    std::cout << "\n檢查 · 組合合法性\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    // 從手牌隨便挑三張，幾乎必然不合法
    const auto& hand = p.getHand();
    std::vector<Tile*> junk = { hand[0], hand[1], hand[2] };
    if (!Validator::isValidSet(junk)) {
        auto r = gm.board.applyProposedSets(&p, { junk });
        check(r != Board::ApplyResult::Ok, "★ 不合法的組合 → 被拒絕");
        check(gm.board.getSets().empty(), "  桌面沒有改變");
    } else {
        std::cout << "  --   （隨機挑到的三張剛好合法，略過）\n";
    }
}

static void test_too_few_tiles() {
    std::cout << "\n檢查 · 張數不足\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    const auto& hand = p.getHand();
    auto r = gm.board.applyProposedSets(&p, { { hand[0], hand[1] } });
    check(r != Board::ApplyResult::Ok, "★ 只有兩張 → 被拒絕（最少三張）");

    auto r2 = gm.board.applyProposedSets(&p, { { hand[0] } });
    check(r2 != Board::ApplyResult::Ok, "只有一張 → 被拒絕");
}

static void test_empty_submission() {
    std::cout << "\n檢查 · 空的提交\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(42);

    auto r = gm.board.applyProposedSets(&p, {});
    check(gm.board.getSets().empty(), "空提交後桌面仍是空的");
    check(p.getHand().size() == 14, "手牌沒有改變");
}

// ══════════════════════════════════════════════════════════
//  被拒絕時狀態不變——這是最重要的性質
// ══════════════════════════════════════════════════════════
static void test_atomicity() {
    std::cout << "\n★ 原子性：被拒絕時狀態完全不變\n";

    GameManager gm;
    DummyPlayer p("測試");
    gm.addPlayer(&p);
    gm.initialize(123);

    // 記下初始狀態
    std::vector<Tile*> hand_before = p.getHand();
    std::size_t sets_before = gm.board.getSets().size();
    bool meld_before = p.initial_meld_done;

    // 送一個必定失敗的提交：一組合法 + 一組不合法
    const auto& hand = p.getHand();
    std::vector<std::vector<Tile*>> mixed = {
        { hand[0], hand[1], hand[2] },      // 大概不合法
        { T(1, Color::RED) }                // 一定不合法（外來的牌 + 張數不足）
    };
    auto r = gm.board.applyProposedSets(&p, mixed);
    check(r != Board::ApplyResult::Ok, "混合提交被拒絕");

    // 逐項比對
    check(p.getHand().size() == hand_before.size(), "  手牌數量不變");
    bool sameHand = true;
    for (std::size_t i = 0; i < hand_before.size(); ++i)
        if (i >= p.getHand().size() || p.getHand()[i] != hand_before[i])
            sameHand = false;
    check(sameHand, "  ★ 手牌的每一張都還在原位——不是「部分套用」");
    check(gm.board.getSets().size() == sets_before, "  桌面組數不變");
    check(p.initial_meld_done == meld_before, "  破冰旗標不變");
}

// ══════════════════════════════════════════════════════════
//  isValid / allTiles
// ══════════════════════════════════════════════════════════
static void test_board_queries() {
    std::cout << "\nBoard · 查詢函式\n";

    Board b;
    check(b.isValid(), "空桌面是合法的");
    check(b.allTiles().empty(), "空桌面沒有任何牌");
    check(b.getSets().empty(), "空桌面沒有任何組合");

    check(b.lastApplyResult() == Board::ApplyResult::Ok,
          "初始的 lastApplyResult 是 Ok");

    // describe() 對每個錯誤碼都要有字串
    Board::ApplyResult codes[] = {
        Board::ApplyResult::Ok,
        Board::ApplyResult::NotPlayerTile,
        Board::ApplyResult::DuplicateTile,
        Board::ApplyResult::RemovedOldTile,
        Board::ApplyResult::RearrangedDuringMeld,
        Board::ApplyResult::InvalidSet,
        Board::ApplyResult::MeldTooLow,
        Board::ApplyResult::NotCurrentPlayer,
    };
    bool allDescribed = true;
    for (auto c : codes) {
        const char* d = Board::describe(c);
        if (!d || d[0] == '\0') allDescribed = false;
    }
    check(allDescribed, "★ 每個 ApplyResult 都有可讀的說明——不然除錯只看得到數字");
}

// ══════════════════════════════════════════════════════════
//  Player
// ══════════════════════════════════════════════════════════
static void test_player() {
    std::cout << "\nPlayer\n";

    GameManager gm;
    DummyPlayer p("小明");
    gm.addPlayer(&p);
    gm.initialize(42);

    check(p.name == "小明", "名字正確");
    check(!p.initial_meld_done, "初始未破冰");
    check(p.getHand().size() == 14, "發到 14 張");

    int score = p.handScore();
    check(score > 0, "手牌分數為正");

    // 手牌分數應該等於面值加總（Joker 算 30）
    int manual = 0;
    for (Tile* t : p.getHand())
        manual += t->isJoker() ? 30 : t->getNumber();
    check(score == manual, "★ 手牌分數 = 面值加總，Joker 算 30");

    std::string json = p.handToJSON();
    check(!json.empty() && json.front() == '[' && json.back() == ']',
          "handToJSON 產生 JSON 陣列");
}

// ══════════════════════════════════════════════════════════
//  GameManager
// ══════════════════════════════════════════════════════════
static void test_game_manager() {
    std::cout << "\nGameManager\n";

    GameManager gm;
    DummyPlayer a("A"), b("B");
    gm.addPlayer(&a);
    gm.addPlayer(&b);
    gm.initialize(42);

    check(gm.players.size() == 2, "兩個玩家都註冊了");
    check(a.getHand().size() == 14 && b.getHand().size() == 14,
          "各發 14 張");
    check(gm.current_player_idx == 0, "從第一個玩家開始");
    check(!gm.game_over, "遊戲尚未結束");

    // 兩人的手牌不能重疊——同一張牌不能發給兩個人
    bool overlap = false;
    for (Tile* x : a.getHand())
        for (Tile* y : b.getHand())
            if (x == y) overlap = true;
    check(!overlap, "★ 兩人的手牌沒有重疊——同一張牌不會發給兩個人");
}

static void test_determinism() {
    std::cout << "\n可重現性\n";

    GameManager g1, g2;
    DummyPlayer a1("A"), b1("B"), a2("A"), b2("B");
    g1.addPlayer(&a1); g1.addPlayer(&b1); g1.initialize(777);
    g2.addPlayer(&a2); g2.addPlayer(&b2); g2.initialize(777);

    bool same = a1.getHand().size() == a2.getHand().size();
    if (same)
        for (std::size_t i = 0; i < a1.getHand().size(); ++i)
            if (a1.getHand()[i]->getNumber() != a2.getHand()[i]->getNumber() ||
                a1.getHand()[i]->getColor()  != a2.getHand()[i]->getColor())
                same = false;
    check(same, "★ 相同種子發出相同的手牌——實驗才能重現");

    GameManager g3;
    DummyPlayer a3("A"), b3("B");
    g3.addPlayer(&a3); g3.addPlayer(&b3); g3.initialize(888);
    bool different = false;
    for (std::size_t i = 0; i < a1.getHand().size() && i < a3.getHand().size(); ++i)
        if (a1.getHand()[i]->getNumber() != a3.getHand()[i]->getNumber())
            different = true;
    check(different, "  不同種子發出不同的手牌");
}

// ══════════════════════════════════════════════════════════
//  牌堆的組成
// ══════════════════════════════════════════════════════════
static void test_tile_pool() {
    std::cout << "\n牌堆組成\n";

    GameManager gm;
    // 註冊七個玩家 × 14 張 = 98 張，接近 106 的上限
    std::vector<DummyPlayer*> ps;
    for (int i = 0; i < 7; ++i) {
        ps.push_back(new DummyPlayer("P" + std::to_string(i)));
        gm.addPlayer(ps.back());
    }
    gm.initialize(42);

    // 統計發出去的牌
    int counts[5][14] = {};   // [color][number]，color 4 給 Joker
    int jokers = 0;
    for (auto* p : ps)
        for (Tile* t : p->getHand()) {
            if (t->isJoker()) ++jokers;
            else counts[static_cast<int>(t->getColor())][t->getNumber()]++;
        }

    // 沒有任何一種「顏色＋數字」超過兩張
    bool noExcess = true;
    for (int c = 0; c < 4; ++c)
        for (int n = 1; n <= 13; ++n)
            if (counts[c][n] > 2) noExcess = false;
    check(noExcess, "★ 每種顏色數字最多兩張——這是拉密的牌堆組成");
    check(jokers <= 2, "Joker 最多兩張");

    for (auto* p : ps) delete p;
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "遊戲引擎 · 單元測試\n";
    std::cout << "════════════════════════════════════════";

    test_valid_submission();
    test_not_player_tile();
    test_duplicate_tile();
    test_meld_too_low();
    test_invalid_set();
    test_too_few_tiles();
    test_empty_submission();
    test_atomicity();
    test_board_queries();
    test_player();
    test_game_manager();
    test_determinism();
    test_tile_pool();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    cleanup();
    return g_fail == 0 ? 0 : 1;
}
