/* -------------------------------------------------------
   cognitive_hint_engine 的單元測試。

   為什麼這個模組的測試要另外寫得比較兇：
   對戰型 AI 講錯話的代價是自己輸一局；教練型 AI 講錯話的代價是
   新手照做，然後以為是自己錯了。所以這裡測的不是「提示好不好」，
   是三條誠實性不變式：

     H1  說有牌可以出的時候，必須真的有牌可以出。（不能無中生有）
     H2  說接不上的時候，範圍內必須真的接不上。（不能該說不說）
     H3  同一個盤面，三層提示必須對「有沒有牌可出」講同一件事。
         在輕推層說有機會、到了答案層卻說接不上，是最糟的失敗方式。

   H1 和 H2 都用 Validator 當獨立的參考答案交叉驗證，
   不是把引擎的邏輯再抄一遍——抄一遍只會證明程式跟自己一致。

   「範圍內」指的是引擎自己宣告的掃描範圍：
   非 Joker 的手牌，接到頭尾皆非 Joker 的 Run，或補滿三張的 Group。
   範圍外的漏判另外用具名測試記錄（見最後一節）。

       g++ -std=c++17 -I src src/tile.cpp src/validator.cpp
           src/cognitive_hint_engine.cpp
           tests/test_cognitive_hint_engine.cpp -o test_hint_engine
       ./test_hint_engine
------------------------------------------------------- */

#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "tile.h"
#include "validator.h"
#include "cognitive_hint_engine.h"

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

using Sets = std::vector<std::vector<Tile*>>;
using Hand = std::vector<Tile*>;

// 引擎在掃描不到動作時回傳的那句話。
// 寫成常數是刻意的：這句話是整個模組的誠實承諾，
// 改動它的人應該在這裡看到一堆測試跟著動。
static const std::string kNoMove =
    "目前看起來手牌跟桌面接不太上，可以考慮先抽一張牌試試看喔！";
static const std::string kNoMeld =
    "目前手牌裡還湊不出合法的組合（Run 或 Group），可以考慮先抽一張牌試試看喔！";

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static std::string colorWord(Color c) {
    switch (c) {
        case Color::RED:    return "紅";
        case Color::BLUE:   return "藍";
        case Color::YELLOW: return "黃";
        case Color::BLACK:  return "黑";
    }
    return "";
}

// ══════════════════════════════════════════════════════════
//  參考答案：完全用 Validator 判，不看引擎怎麼寫的
//
//  刻意只用「把牌接上去之後，Validator 說這組合法嗎」來判斷，
//  而不是重寫一次頭尾比對的算術。如果引擎的算術寫錯（例如把
//  head->getNumber() - 1 寫成 + 1），抄一遍的參考答案會跟著錯，
//  用 Validator 的則會抓到。
// ══════════════════════════════════════════════════════════
static bool refHasAttachMove(const Hand& hand, const Sets& board) {
    for (const auto& set : board) {
        if (set.size() < 3) continue;
        if (!Validator::isValidRun(set)) continue;
        if (set.front()->isJoker() || set.back()->isJoker()) continue;

        for (Tile* t : hand) {
            if (t->isJoker()) continue;

            std::vector<Tile*> pre;
            pre.push_back(t);
            pre.insert(pre.end(), set.begin(), set.end());
            if (Validator::isValidRun(pre)) return true;

            std::vector<Tile*> post(set);
            post.push_back(t);
            if (Validator::isValidRun(post)) return true;
        }
    }
    return false;
}

static bool refHasGroupMove(const Hand& hand, const Sets& board) {
    for (const auto& set : board) {
        if (set.size() != 3) continue;
        bool has_joker = false;
        for (Tile* t : set) if (t->isJoker()) has_joker = true;
        if (has_joker) continue;
        if (!Validator::isValidGroup(set)) continue;

        for (Tile* t : hand) {
            if (t->isJoker()) continue;
            std::vector<Tile*> bigger(set);
            bigger.push_back(t);
            if (Validator::isValidGroup(bigger)) return true;
        }
    }
    return false;
}

static bool refHasMove(const Hand& hand, const Sets& board) {
    return refHasAttachMove(hand, board) || refHasGroupMove(hand, board);
}

// ══════════════════════════════════════════════════════════
//  tierFromStuckTurns
// ══════════════════════════════════════════════════════════
static void test_tier_ladder() {
    std::cout << "\n卡關回合 → 提示層級\n";

    check(CognitiveHintEngine::tierFromStuckTurns(0) == HintTier::GENTLE_NUDGE,
          "第 0 回合只輕推");
    check(CognitiveHintEngine::tierFromStuckTurns(1) == HintTier::GENTLE_NUDGE,
          "第 1 回合仍是輕推");
    check(CognitiveHintEngine::tierFromStuckTurns(2) == HintTier::POINT_TO_AREA,
          "第 2 回合開始指方向");
    check(CognitiveHintEngine::tierFromStuckTurns(3) == HintTier::POINT_TO_AREA,
          "第 3 回合仍是指方向");
    check(CognitiveHintEngine::tierFromStuckTurns(4) == HintTier::REVEAL_MOVE,
          "第 4 回合才給答案");
    check(CognitiveHintEngine::tierFromStuckTurns(100) == HintTier::REVEAL_MOVE,
          "再久也只是給答案，沒有第四層");

    // 負值是不該發生的狀態，但真的傳進來時不能比第 0 回合更深
    check(CognitiveHintEngine::tierFromStuckTurns(-1) == HintTier::GENTLE_NUDGE,
          "負數回合不該觸發更深的提示");

    // 單調性：卡得越久，提示只能越深或持平
    int prev = 0;
    bool monotonic = true;
    for (int i = 0; i <= 50; ++i) {
        int cur = static_cast<int>(CognitiveHintEngine::tierFromStuckTurns(i));
        if (cur < prev) { monotonic = false; break; }
        prev = cur;
    }
    check(monotonic, "提示層級不該隨卡關時間變淺");
}

// ══════════════════════════════════════════════════════════
//  H1：說有牌可以出的時候，講的那張牌必須真的接得上
//
//  用「只有唯一一個合法動作」的盤面，這樣 REVEAL 的內容是可預測的，
//  可以直接比對它有沒有講出正確的那張牌與正確的方向。
// ══════════════════════════════════════════════════════════
static void test_reveal_names_a_legal_tile() {
    std::cout << "\nH1 · REVEAL 講的牌必須真的合法\n";

    {   // 唯一動作：紅 3 接在紅 4-5-6 的前面
        Tile* r4 = T(4, Color::RED);
        Tile* r5 = T(5, Color::RED);
        Tile* r6 = T(6, Color::RED);
        Tile* r3 = T(3, Color::RED);
        Tile* b9 = T(9, Color::BLUE);

        Sets board = {{r4, r5, r6}};
        Hand hand = {r3, b9};

        check(Validator::isValidBoard(board), "測試盤面合法（接龍頭端）");
        check(refHasMove(hand, board), "參考答案：確實有一個合法動作");

        std::string s = CognitiveHintEngine::generateHint(hand, board, HintTier::REVEAL_MOVE);
        check(s != kNoMove, "有動作時不該回傳「接不上」");
        check(contains(s, colorWord(Color::RED) + "色 3"), "應該講出紅色 3 這張牌");
        check(contains(s, "前面"), "應該講出接在前面");
        check(!contains(s, "後面"), "不該同時說接在後面");

        // 交叉驗證：把它講的那張牌真的接上去，Validator 要說合法
        std::vector<Tile*> result = {r3, r4, r5, r6};
        check(Validator::isValidRun(result), "照著提示接上去，Validator 判定合法");
    }

    {   // 唯一動作：藍 8 接在藍 5-6-7 的後面
        Tile* b5 = T(5, Color::BLUE);
        Tile* b6 = T(6, Color::BLUE);
        Tile* b7 = T(7, Color::BLUE);
        Tile* b8 = T(8, Color::BLUE);
        Tile* y2 = T(2, Color::YELLOW);

        Sets board = {{b5, b6, b7}};
        Hand hand = {b8, y2};

        std::string s = CognitiveHintEngine::generateHint(hand, board, HintTier::REVEAL_MOVE);
        check(contains(s, colorWord(Color::BLUE) + "色 8"), "應該講出藍色 8");
        check(contains(s, "後面"), "應該講出接在後面");

        std::vector<Tile*> result = {b5, b6, b7, b8};
        check(Validator::isValidRun(result), "照著提示接上去，Validator 判定合法");
    }

    {   // 唯一動作：補第四色
        Tile* r5 = T(5, Color::RED);
        Tile* b5 = T(5, Color::BLUE);
        Tile* k5 = T(5, Color::BLACK);
        Tile* y5 = T(5, Color::YELLOW);

        Sets board = {{r5, b5, k5}};
        Hand hand = {y5};

        check(refHasGroupMove(hand, board), "參考答案：確實可以補第四色");

        std::string s = CognitiveHintEngine::generateHint(hand, board, HintTier::REVEAL_MOVE);
        check(contains(s, colorWord(Color::YELLOW) + "色 5"), "應該講出黃色 5");

        std::vector<Tile*> result = {r5, b5, k5, y5};
        check(Validator::isValidGroup(result), "照著提示補上去，Validator 判定合法");
    }
}

// ══════════════════════════════════════════════════════════
//  H2：接不上的時候要誠實說接不上，不能硬掰
// ══════════════════════════════════════════════════════════
static void test_honest_when_no_move() {
    std::cout << "\nH2 · 沒有動作時必須誠實\n";

    Tile* r4 = T(4, Color::RED);
    Tile* r5 = T(5, Color::RED);
    Tile* r6 = T(6, Color::RED);
    Tile* y9 = T(9, Color::YELLOW);
    Tile* k2 = T(2, Color::BLACK);

    Sets board = {{r4, r5, r6}};
    Hand hand = {y9, k2};

    check(!refHasMove(hand, board), "參考答案：確實沒有可行動作");

    for (HintTier tier : {HintTier::GENTLE_NUDGE, HintTier::POINT_TO_AREA,
                          HintTier::REVEAL_MOVE}) {
        std::string s = CognitiveHintEngine::generateHint(hand, board, tier);
        check(s == kNoMove, "沒有動作時，每一層都要回傳同一句誠實的話");
        check(!contains(s, "可以出喔"), "不該暗示有牌可以出");
    }

    // 空桌面、空手牌都不該當掉
    check(CognitiveHintEngine::generateHint({}, {}, HintTier::REVEAL_MOVE) == kNoMove,
          "空手牌空桌面應回傳誠實訊息");
    check(CognitiveHintEngine::generateHint(hand, {}, HintTier::REVEAL_MOVE) == kNoMove,
          "桌面是空的時候沒有東西可以接");
}

// ══════════════════════════════════════════════════════════
//  隨機盤面property test：H1 / H2 / H3 一起掃
//
//  手挑的案例只能證明「我想得到的情況沒壞」。
//  這一段用隨機合法盤面掃幾千次，用 Validator 當參考答案，
//  找的是「我想不到的情況」。
// ══════════════════════════════════════════════════════════
namespace {

struct Deck {
    int count[14][4];   // count[number][color]
    int jokers;

    Deck() { reset(); }
    void reset() {
        for (int n = 1; n <= 13; ++n)
            for (int c = 0; c < 4; ++c)
                count[n][c] = 2;      // 標準拉密每種牌兩張
        jokers = 2;
    }
    bool take(int n, int c) {
        if (count[n][c] <= 0) return false;
        --count[n][c];
        return true;
    }
};

// 隨機生一個合法盤面（若干條 Run 與 Group）
Sets randomBoard(std::mt19937& rng, Deck& deck, int max_sets) {
    Sets board;
    std::uniform_int_distribution<int> d_sets(0, max_sets);
    int n_sets = d_sets(rng);

    for (int s = 0; s < n_sets; ++s) {
        bool make_run = (rng() % 2) == 0;
        if (make_run) {
            int color = rng() % 4;
            int len = 3 + rng() % 3;                 // 3~5
            int start = 1 + rng() % (13 - len + 1);
            bool ok = true;
            for (int k = 0; k < len; ++k)
                if (deck.count[start + k][color] <= 0) { ok = false; break; }
            if (!ok) continue;

            std::vector<Tile*> set;
            for (int k = 0; k < len; ++k) {
                deck.take(start + k, color);
                set.push_back(T(start + k, static_cast<Color>(color)));
            }
            board.push_back(set);
        } else {
            int number = 1 + rng() % 13;
            int len = 3 + rng() % 2;                 // 3~4
            std::vector<int> colors = {0, 1, 2, 3};
            for (int i = 3; i > 0; --i) std::swap(colors[i], colors[rng() % (i + 1)]);

            std::vector<int> chosen;
            for (int c : colors) {
                if ((int)chosen.size() == len) break;
                if (deck.count[number][c] > 0) chosen.push_back(c);
            }
            if ((int)chosen.size() < 3) continue;

            std::vector<Tile*> set;
            for (int c : chosen) {
                deck.take(number, c);
                set.push_back(T(number, static_cast<Color>(c)));
            }
            board.push_back(set);
        }
    }
    return board;
}

Hand randomHand(std::mt19937& rng, Deck& deck, int size) {
    Hand hand;
    for (int i = 0; i < size; ++i) {
        if (deck.jokers > 0 && (rng() % 12) == 0) {
            --deck.jokers;
            hand.push_back(J());
            continue;
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            int n = 1 + rng() % 13;
            int c = rng() % 4;
            if (deck.take(n, c)) {
                hand.push_back(T(n, static_cast<Color>(c)));
                break;
            }
        }
    }
    return hand;
}

}  // namespace

static void test_random_consistency() {
    std::cout << "\n隨機盤面 · 誠實性掃描\n";

    const int kRounds = 3000;
    std::mt19937 rng(20260815);

    int false_positive = 0;   // 沒有動作卻說有（最嚴重）
    int false_negative = 0;   // 有動作卻說沒有
    int tier_mismatch = 0;    // 三層講的話不一致
    int boards_invalid = 0;
    int with_move = 0;

    for (int round = 0; round < kRounds; ++round) {
        Deck deck;
        Sets board = randomBoard(rng, deck, 4);
        Hand hand = randomHand(rng, deck, 5 + rng() % 8);

        if (!Validator::isValidBoard(board)) { ++boards_invalid; continue; }

        bool ref = refHasMove(hand, board);
        if (ref) ++with_move;

        std::string nudge  = CognitiveHintEngine::generateHint(hand, board, HintTier::GENTLE_NUDGE);
        std::string point  = CognitiveHintEngine::generateHint(hand, board, HintTier::POINT_TO_AREA);
        std::string reveal = CognitiveHintEngine::generateHint(hand, board, HintTier::REVEAL_MOVE);

        bool says_nudge  = (nudge  != kNoMove);
        bool says_point  = (point  != kNoMove);
        bool says_reveal = (reveal != kNoMove);

        if (!(says_nudge == says_point && says_point == says_reveal)) ++tier_mismatch;
        if (!ref && says_reveal) ++false_positive;
        if (ref && !says_reveal) ++false_negative;
    }

    std::cout << "  （掃了 " << kRounds << " 個隨機盤面，其中 " << with_move
              << " 個確實有可行動作）\n";

    check(boards_invalid == 0, "產生的隨機盤面全部合法");
    check(with_move > kRounds / 20,
          "有動作的盤面要夠多，否則這個測試等於沒測到");
    check(false_positive == 0,
          "H1：沒有可行動作時，引擎絕不能說有（發生 " +
          std::to_string(false_positive) + " 次）");
    check(false_negative == 0,
          "H2：範圍內有可行動作時，引擎不該說接不上（發生 " +
          std::to_string(false_negative) + " 次）");
    check(tier_mismatch == 0,
          "H3：三層提示必須對「有沒有牌可出」講同一件事（發生 " +
          std::to_string(tier_mismatch) + " 次）");
}

// ══════════════════════════════════════════════════════════
//  破冰提示
//
//  破冰提示比一般提示更容易講錯話，因為它要報一個「分數」，
//  而分數是玩家會直接拿來做決定的東西：
//  說到 30 分他就出牌，說沒到他就抽牌。報錯就是害他。
// ══════════════════════════════════════════════════════════
static void test_meld_hint_score_honesty() {
    std::cout << "\n破冰提示 · 分數必須誠實\n";

    {   // 湊得到 30 分：紅 10-11-12 = 33
        Tile* r10 = T(10, Color::RED);
        Tile* r11 = T(11, Color::RED);
        Tile* r12 = T(12, Color::RED);

        Hand hand = {r10, r11, r12};
        check(Validator::isValidRun(hand), "前置：這三張確實是合法 Run");
        check(Validator::calculateInitialMeldScore(hand) == 33, "前置：確實是 33 分");

        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::REVEAL_MOVE);
        check(s != kNoMeld, "湊得出組合時不該說湊不出");
        check(contains(s, "33"), "應該報出正確的分數 33");
        check(!contains(s, "還不到 30 分"), "已達標不該說還不到 30 分");
    }

    {   // 湊不到 30 分：紅 1-2-3 = 6
        Tile* r1 = T(1, Color::RED);
        Tile* r2 = T(2, Color::RED);
        Tile* r3 = T(3, Color::RED);

        Hand hand = {r1, r2, r3};
        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::REVEAL_MOVE);
        check(contains(s, "還不到 30 分"), "沒達標必須明講還不到 30 分");
        check(!contains(s, "可以直接出牌破冰"), "沒達標不該叫玩家出牌");
    }

    {   // 用 Joker 補成 Group：紅 10 + 藍 10 + Joker = 30
        Tile* r10 = T(10, Color::RED);
        Tile* b10 = T(10, Color::BLUE);
        Tile* jo  = J();

        Hand hand = {r10, b10, jo};
        std::vector<Tile*> as_group = {r10, b10, jo};
        check(Validator::isValidGroup(as_group), "前置：這三張是合法 Group");
        check(Validator::calculateInitialMeldScore(as_group) == 30, "前置：確實是 30 分");

        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::POINT_TO_AREA);
        check(contains(s, "30"), "應該報出 30 分");
        check(contains(s, "Joker"), "有用到 Joker 就要講出來，玩家才知道代價");
    }

    {   // 真的什麼都湊不出來
        Tile* r1 = T(1, Color::RED);
        Tile* b5 = T(5, Color::BLUE);
        Tile* y9 = T(9, Color::YELLOW);

        Hand hand = {r1, b5, y9};
        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::REVEAL_MOVE);
        check(s == kNoMeld, "真的湊不出來時要誠實說湊不出來");
    }
}

// ══════════════════════════════════════════════════════════
//  已知範圍外：Joker 只填內部缺口，不往兩端延伸
//
//  這是 findBestMeldCandidate 檔頭自己寫明的簡化。
//  下面兩條測的是這個簡化在真實牌型下會不會產生「會害到玩家」的錯話。
//  第一條只是少報一組（可接受）；第二條會讓玩家在手上有 36 分的
//  破冰組合時被告知「湊不出來，去抽牌」——那不是漏判，是錯話。
// ══════════════════════════════════════════════════════════
static void test_known_gap_joker_extends_end() {
    std::cout << "\n已知範圍外 · Joker 往兩端延伸\n";

    {   // 低分版本：紅 1、紅 2 + Joker = 紅 1-2-3，只有 6 分
        Tile* r1 = T(1, Color::RED);
        Tile* r2 = T(2, Color::RED);
        Tile* jo = J();

        std::vector<Tile*> as_run = {r1, r2, jo};
        check(Validator::isValidRun(as_run), "前置：紅1-紅2-Joker 是合法 Run");

        Hand hand = {r1, r2, jo};
        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::REVEAL_MOVE);
        // 這一組本來就不到 30 分，少報它不影響玩家決策，只記錄不斷言成功。
        std::cout << "  info 低分情形引擎回應："
                  << (s == kNoMeld ? "（說湊不出來）" : "（有給組合）") << "\n";
    }

    {   // 高分版本：紅 11、紅 12 + Joker = 紅 11-12-13，36 分，足以破冰
        Tile* r11 = T(11, Color::RED);
        Tile* r12 = T(12, Color::RED);
        Tile* jo  = J();

        std::vector<Tile*> as_run = {r11, r12, jo};
        check(Validator::isValidRun(as_run), "前置：紅11-紅12-Joker 是合法 Run");
        check(Validator::calculateInitialMeldScore(as_run) == 36, "前置：確實是 36 分，超過 30");

        Hand hand = {r11, r12, jo};
        std::string s = CognitiveHintEngine::generateMeldHint(hand, HintTier::REVEAL_MOVE);

        check(s != kNoMeld,
              "手上有 36 分的破冰組合時，不該被告知湊不出組合、去抽牌");
    }
}

// ── main ─────────────────────────────────────────────────
int main() {
    std::cout << "CognitiveHintEngine 測試\n";

    test_tier_ladder();
    test_reveal_names_a_legal_tile();
    test_honest_when_no_move();
    test_random_consistency();
    test_meld_hint_score_honesty();
    test_known_gap_joker_extends_end();

    std::cout << "\n─────────────────────────────\n";
    std::cout << g_passed << " 項通過，" << g_failed << " 項失敗\n";

    cleanup();
    return g_failed == 0 ? 0 : 1;
}
