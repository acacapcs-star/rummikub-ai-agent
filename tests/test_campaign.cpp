/* -------------------------------------------------------
   test_campaign.cpp —— src/ 底下舊版模組的測試

   coach_campaign 與 technique_detector 是抽象化之前的版本。
   它們沒有被 coach/ 那套取代——**learner_simulation 還在用它們**，
   而那個實驗的結論（L6 新手卡 16 回合、保底機制）是建立在這份程式碼上的。

   所以它們需要測試：如果這裡的行為改了而沒人發現，
   那份實驗報告就會變成在描述一個不存在的系統。

   編譯：
     g++ -std=c++17 -I ../src test_campaign.cpp \
         ../src/coach_campaign.cpp ../src/technique_detector.cpp \
         ../src/validator.cpp ../src/tile.cpp -o t
------------------------------------------------------- */

#include "coach_campaign.h"
#include "technique_detector.h"
#include "tile.h"
#include "validator.h"
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

// ── 造牌 ─────────────────────────────────────────────────
static std::vector<Tile*> g_pool;
static int g_id = 0;
static Tile* T(int n, Color c) { auto* t = new Tile(g_id++, n, c); g_pool.push_back(t); return t; }
static Tile* J()               { auto* t = new Tile(g_id++);       g_pool.push_back(t); return t; }
static void cleanup() { for (auto* t : g_pool) delete t; g_pool.clear(); }

static bool hasTech(const std::vector<Technique>& v, Technique t) {
    for (Technique x : v) if (x == t) return true;
    return false;
}

// ══════════════════════════════════════════════════════════
//  關卡設定
// ══════════════════════════════════════════════════════════
static void test_levels() {
    std::cout << "\n關卡設定\n";

    checkEq(CoachCampaign::totalLevels(), 6, "六個關卡");

    // 引導強度必須單調遞減
    bool decreasing = true;
    for (int lv = 2; lv <= 6; ++lv)
        if (CoachCampaign::levelConfig(lv).guidance_percent >=
            CoachCampaign::levelConfig(lv-1).guidance_percent)
            decreasing = false;
    check(decreasing, "★ 引導強度逐關遞減");
    checkEq(CoachCampaign::levelConfig(1).guidance_percent, 100, "第一關 100%");
    checkEq(CoachCampaign::levelConfig(6).guidance_percent, 40,  "第六關 40%");

    // 自主次數的要求逐關收緊
    bool stricter = true;
    for (int lv = 2; lv <= 6; ++lv)
        if (CoachCampaign::levelConfig(lv).required_unassisted <
            CoachCampaign::levelConfig(lv-1).required_unassisted)
            stricter = false;
    check(stricter, "自主次數的要求不遞減");

    const auto& L6 = CoachCampaign::levelConfig(6);
    checkEq(L6.required_unassisted, L6.required_uses,
            "★ 最後一關要求全部自主——那一關教的其實是「自己找」");
    check(L6.max_tier == HintTier::GENTLE_NUDGE, "最後一關上限只到輕推");
    check(L6.safety_net_tier != HintTier::REVEAL_MOVE,
          "★ 最後一關的保底也不給答案——核心約束不能被例外破壞");

    // 每一招都有對應的關卡
    for (int t = 0; t < static_cast<int>(Technique::TECHNIQUE_COUNT); ++t) {
        bool found = false;
        for (int lv = 1; lv <= 6; ++lv)
            if (static_cast<int>(CoachCampaign::levelConfig(lv).technique) == t)
                found = true;
        check(found, "  技巧「" +
              CoachCampaign::techniqueName(static_cast<Technique>(t)) +
              "」有對應的關卡");
    }
}

static void test_level_bounds() {
    std::cout << "\n關卡邊界\n";

    checkEq(CoachCampaign::levelConfig(0).level, 1,  "★ level 0 被夾到第 1 關");
    checkEq(CoachCampaign::levelConfig(-5).level, 1, "  負數也被夾住");
    checkEq(CoachCampaign::levelConfig(99).level, 6, "★ level 99 被夾到最後一關");
    check(true, "  夾住而不是崩潰——遊戲進行中崩潰對玩家更糟");
}

// ══════════════════════════════════════════════════════════
//  提示層級的決策
// ══════════════════════════════════════════════════════════
static void test_hint_tiers() {
    std::cout << "\n提示層級 · 遞進\n";

    CoachCampaign c;
    HintTier tier;

    check(c.shouldGiveHint(0, tier) && tier == HintTier::GENTLE_NUDGE,
          "L1 卡 0 回合 → 輕推");
    check(c.shouldGiveHint(1, tier) && tier == HintTier::POINT_TO_AREA,
          "L1 卡 1 回合 → 指方向");
    check(c.shouldGiveHint(2, tier) && tier == HintTier::REVEAL_MOVE,
          "L1 卡 2 回合 → 講答案");
    check(c.shouldGiveHint(99, tier) && tier == HintTier::REVEAL_MOVE,
          "卡再久也不會超過講答案");
}

static void test_silence() {
    std::cout << "\n提示層級 · 沉默\n";

    CoachCampaign c;
    c.advance();                 // 進到 L2
    HintTier tier;

    check(!c.shouldGiveHint(0, tier), "★ L2 卡 0 回合保持沉默");
    check( c.shouldGiveHint(1, tier), "  卡 1 回合才開口");
}

static void test_max_tier_cap() {
    std::cout << "\n提示層級 · 上限\n";

    CoachCampaign c;
    for (int i = 0; i < 3; ++i) c.advance();    // 進到 L4，上限是指方向

    HintTier tier;
    bool everReveal = false;
    for (int k = 0; k <= 9; ++k)                // 保底在第 10 回合，先看之前
        if (c.shouldGiveHint(k, tier) && tier == HintTier::REVEAL_MOVE)
            everReveal = true;
    check(!everReveal, "★ L4 在保底之前不會給答案——上限是指方向");
}

static void test_safety_net() {
    std::cout << "\n保底機制\n";

    CoachCampaign c;
    for (int i = 0; i < 5; ++i) c.advance();    // 進到 L6

    HintTier tier;
    bool fromNet = false;

    check(c.shouldGiveHint(11, tier, fromNet) && !fromNet,
          "L6 卡 11 回合：尚未觸發保底");
    check(tier == HintTier::GENTLE_NUDGE, "  仍是輕推");

    check(c.shouldGiveHint(12, tier, fromNet) && fromNet,
          "★ 卡 12 回合：保底觸發");
    check(tier == HintTier::POINT_TO_AREA,
          "★ L6 的保底只給指方向——那一關永遠不講答案");

    // L1 不需要保底，因為它本來就會給答案
    CoachCampaign c1;
    check(c1.shouldGiveHint(20, tier, fromNet) && !fromNet,
          "L1 不啟用保底（它本來就會給答案）");
}

// ══════════════════════════════════════════════════════════
//  掌握度
// ══════════════════════════════════════════════════════════
static void test_mastery() {
    std::cout << "\n掌握度 · 只升不降\n";

    CoachCampaign c;
    const Technique T0 = Technique::ATTACH_RUN;

    check(c.progressOf(T0).mastery == Mastery::LOCKED, "初始為未解鎖");

    c.recordUse({ T0, true, false });      // 看了答案
    check(c.progressOf(T0).mastery == Mastery::COPIED, "看答案做出來 → 一星");

    c.recordUse({ T0, false, true });      // 只給方向
    check(c.progressOf(T0).mastery == Mastery::PROMPTED, "只給方向 → 升到二星");

    c.recordUse({ T0, false, false });     // 完全自主
    check(c.progressOf(T0).mastery == Mastery::DISCOVERED, "完全自主 → 三星");

    c.recordUse({ T0, true, false });      // 又看了答案
    check(c.progressOf(T0).mastery == Mastery::DISCOVERED,
          "★ 再看一次答案也不會降級——學會了就是學會了");
}

static void test_use_counting() {
    std::cout << "\n次數統計\n";

    CoachCampaign c;
    const Technique T0 = Technique::ATTACH_RUN;

    c.recordUse({ T0, true,  false });     // 看答案
    c.recordUse({ T0, false, false });     // 自主
    c.recordUse({ T0, false, true  });     // 只看方向，也算自主

    checkEq(c.progressOf(T0).total_uses, 3, "總次數 3");
    checkEq(c.progressOf(T0).unassisted_uses, 2,
            "★ 自主次數 2——只有「看過答案」那次不算");
}

static void test_advance_conditions() {
    std::cout << "\n過關條件\n";

    CoachCampaign c;
    const Technique T0 = Technique::ATTACH_RUN;

    check(!c.canAdvance(), "一開始不能過關");

    // 三次全部看答案：次數夠，自主次數不足
    for (int i = 0; i < 3; ++i) c.recordUse({ T0, true, false });
    checkEq(c.progressOf(T0).total_uses, 3, "用了三次");
    checkEq(c.progressOf(T0).unassisted_uses, 0, "但沒有一次自主");
    check(!c.canAdvance(), "★ 不能過關——熟練不等於學會");

    c.recordUse({ T0, false, false });
    check(c.canAdvance(), "補一次自主之後才能過關");

    checkEq(c.currentLevel(), 1, "還在第 1 關");
    check(c.advance(), "advance 成功");
    checkEq(c.currentLevel(), 2, "進到第 2 關");
}

static void test_advance_bounds() {
    std::cout << "\n關卡上限\n";

    CoachCampaign c;
    for (int i = 0; i < 5; ++i) check(c.advance(), "  可以往前推進");
    checkEq(c.currentLevel(), 6, "到達第 6 關");
    check(!c.advance(), "★ 已是最後一關，advance 回傳 false");
    checkEq(c.currentLevel(), 6, "關卡數不會超出範圍");
}

// ══════════════════════════════════════════════════════════
//  Recap MCQ
// ══════════════════════════════════════════════════════════
static void test_recap() {
    std::cout << "\nRecap 題庫\n";

    for (int lv = 1; lv <= 6; ++lv) {
        auto qs = CoachCampaign::recapFor(lv);
        checkEq(static_cast<int>(qs.size()), 5,
                "Level " + std::to_string(lv) + " 有五題");

        bool ok = true;
        for (const auto& q : qs) {
            if (q.options.size() != 4) ok = false;
            int nCorrect = 0;
            for (const auto& o : q.options) if (o.correct) ++nCorrect;
            if (nCorrect != 1) ok = false;
            if (q.prompt.empty() || q.hint.empty() || q.explanation.empty())
                ok = false;
        }
        check(ok, "  四選一、恰好一個正解、題目提示解答都有");
    }
}

static void test_recap_judging() {
    std::cout << "\nRecap 判分\n";

    auto qs = CoachCampaign::recapFor(1);
    const auto& q = qs[0];

    int correct = -1;
    for (std::size_t i = 0; i < q.options.size(); ++i)
        if (q.options[i].correct) correct = static_cast<int>(i);
    check(correct >= 0, "找得到正解");

    check(CoachCampaign::judge(q, correct, 1) ==
          CoachCampaign::AnswerResult::CORRECT, "答對 → CORRECT");

    int wrong = (correct + 1) % 4;
    check(CoachCampaign::judge(q, wrong, 1) ==
          CoachCampaign::AnswerResult::WRONG_FIRST_TRY,
          "★ 第一次答錯 → 給提示，可以再試");
    check(CoachCampaign::judge(q, wrong, 2) ==
          CoachCampaign::AnswerResult::WRONG_SHOW_ANSWER,
          "★ 第二次答錯 → 給解答，不讓玩家用猜的過關");

    check(CoachCampaign::judge(q, -1, 1) !=
          CoachCampaign::AnswerResult::CORRECT, "越界的選項不算對");
    check(CoachCampaign::judge(q, 99, 1) !=
          CoachCampaign::AnswerResult::CORRECT, "  超出範圍也一樣");
}

// ══════════════════════════════════════════════════════════
//  顯示用函式
// ══════════════════════════════════════════════════════════
static void test_display() {
    std::cout << "\n顯示\n";

    for (int t = 0; t < static_cast<int>(Technique::TECHNIQUE_COUNT); ++t) {
        std::string n = CoachCampaign::techniqueName(static_cast<Technique>(t));
        check(!n.empty() && n != "未知技巧", "  技巧 " + std::to_string(t) + " 有名稱");
    }

    check(CoachCampaign::masteryStars(Mastery::LOCKED).empty(), "未解鎖沒有星星");
    checkEq(static_cast<int>(CoachCampaign::masteryStars(Mastery::COPIED).size()), 1, "一星");
    checkEq(static_cast<int>(CoachCampaign::masteryStars(Mastery::PROMPTED).size()), 2, "二星");
    checkEq(static_cast<int>(CoachCampaign::masteryStars(Mastery::DISCOVERED).size()), 3, "三星");
}

// ══════════════════════════════════════════════════════════
//  TechniqueDetector
// ══════════════════════════════════════════════════════════
static void test_detector_attach() {
    std::cout << "\n偵測器 · 接龍\n";

    Tile *r3 = T(3,Color::RED), *r4 = T(4,Color::RED),
         *r5 = T(5,Color::RED), *r6 = T(6,Color::RED);

    MoveSnapshot s;
    s.board_before = {{ r4, r5, r6 }};
    s.board_after  = {{ r3, r4, r5, r6 }};
    s.hand_before  = { r3 };
    s.hand_after   = {};

    auto t = TechniqueDetector::detect(s);
    check(hasTech(t, Technique::ATTACH_RUN), "認出接龍頭尾");
    check(!hasTech(t, Technique::RUN_SPLIT), "不會誤判成長龍切斷");
    checkEq(TechniqueDetector::tilesPlayedCount(s), 1, "出了 1 張");
}

static void test_detector_group() {
    std::cout << "\n偵測器 · 補第四色\n";

    Tile *a = T(7,Color::RED),  *b = T(7,Color::BLUE),
         *c = T(7,Color::BLACK), *y = T(7,Color::YELLOW);

    MoveSnapshot s;
    s.board_before = {{ a, b, c }};
    s.board_after  = {{ a, b, c, y }};
    s.hand_before  = { y };
    s.hand_after   = {};

    check(hasTech(TechniqueDetector::detect(s), Technique::COMPLETE_GROUP),
          "認出補第四色");
}

static void test_detector_joker() {
    std::cout << "\n偵測器 · Joker 的位置\n";

    // 中間 → 算補缺口
    {
        Tile *r4 = T(4,Color::RED), *r5 = T(5,Color::RED), *r7 = T(7,Color::RED);
        Tile* j = J();
        MoveSnapshot s;
        s.board_before = {{ r4, r5 }};
        s.board_after  = {{ r4, r5, j, r7 }};
        s.hand_before  = { j, r7 };
        s.hand_after   = {};
        check(hasTech(TechniqueDetector::detect(s), Technique::JOKER_FILL),
              "Joker 在中間 → 算補缺口");
    }
    // 尾端 → 不算
    {
        Tile *r4 = T(4,Color::BLUE), *r5 = T(5,Color::BLUE), *r6 = T(6,Color::BLUE);
        Tile* j = J();
        MoveSnapshot s;
        s.board_before = {{ r4, r5, r6 }};
        s.board_after  = {{ r4, r5, r6, j }};
        s.hand_before  = { j };
        s.hand_after   = {};
        check(!hasTech(TechniqueDetector::detect(s), Technique::JOKER_FILL),
              "★ Joker 接在尾端不算——那只是延長，普通牌也做得到");
    }
}

static void test_detector_split() {
    std::cout << "\n偵測器 · 切斷 vs 接長\n";

    // 真的切斷
    {
        std::vector<Tile*> run;
        for (int i = 1; i <= 8; ++i) run.push_back(T(i, Color::BLACK));
        MoveSnapshot s;
        s.board_before = { run };
        s.board_after  = { { run[0],run[1],run[2],run[3] },
                           { run[4],run[5],run[6],run[7] } };
        check(hasTech(TechniqueDetector::detect(s), Technique::RUN_SPLIT),
              "散成兩條合法 Run → 認出切斷");
    }
    // 只是接長
    {
        std::vector<Tile*> run;
        for (int i = 1; i <= 8; ++i) run.push_back(T(i, Color::YELLOW));
        Tile* nine = T(9, Color::YELLOW);
        std::vector<Tile*> longer = run;
        longer.push_back(nine);

        MoveSnapshot s;
        s.board_before = { run };
        s.board_after  = { longer };
        s.hand_before  = { nine };
        s.hand_after   = {};

        auto t = TechniqueDetector::detect(s);
        check(!hasTech(t, Technique::RUN_SPLIT),
              "★ 只是接長不算切斷——這是最容易誤判的一組");
        check(hasTech(t, Technique::ATTACH_RUN), "  應該被認成接龍");
    }
}

static void test_detector_nothing() {
    std::cout << "\n偵測器 · 寧可漏判\n";

    Tile* r3 = T(3, Color::RED);
    MoveSnapshot s;
    s.hand_before = {};
    s.hand_after  = { r3 };        // 只是抽了一張牌

    check(TechniqueDetector::detect(s).empty(),
          "★ 只抽牌時不回報任何技巧——誤判會讓玩家在沒學會時被判過關");
    checkEq(TechniqueDetector::tilesPlayedCount(s), 0, "出牌數 0");
}

static void test_detector_metrics() {
    std::cout << "\n偵測器 · 戰果量測\n";

    Tile *a = T(1,Color::RED), *b = T(2,Color::RED), *c = T(3,Color::RED);
    Tile *d = T(4,Color::RED), *e = T(5,Color::RED);

    MoveSnapshot s;
    s.board_before = {{ a, b, c }};
    s.board_after  = {{ a, b, c, d, e }};
    s.hand_before  = { d, e };
    s.hand_after   = {};

    checkEq(TechniqueDetector::tilesPlayedCount(s), 2, "出了 2 張");
    check(!TechniqueDetector::touchedExistingTiles(s),
          "只是接在後面，沒有動到桌面既有的組合");

    // 拆散一組 → 算動到桌面
    MoveSnapshot s2;
    s2.board_before = {{ a, b, c }, { d, e }};
    s2.board_after  = {{ a, b, c, d }, { e }};
    check(TechniqueDetector::touchedExistingTiles(s2),
          "★ 有組合被拆散 → 算動到桌面");
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "CoachCampaign 與 TechniqueDetector · 單元測試\n";
    std::cout << "════════════════════════════════════════";

    test_levels();
    test_level_bounds();
    test_hint_tiers();
    test_silence();
    test_max_tier_cap();
    test_safety_net();
    test_mastery();
    test_use_counting();
    test_advance_conditions();
    test_advance_bounds();
    test_recap();
    test_recap_judging();
    test_display();
    test_detector_attach();
    test_detector_group();
    test_detector_joker();
    test_detector_split();
    test_detector_nothing();
    test_detector_metrics();

    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    cleanup();
    return g_fail == 0 ? 0 : 1;
}
