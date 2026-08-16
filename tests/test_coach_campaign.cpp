// =========================================================================
// test_coach_campaign.cpp —— CoachCampaign 的單檔測試
//
// 編譯（在 repo 根目錄）：
//   g++ -std=c++17 -Wall -Wextra -I src src/coach_campaign.cpp
//       tests/test_coach_campaign.cpp -o build/test_coach_campaign
//   ./build/test_coach_campaign
//
// 不依賴任何測試框架，跟 tests/test_validator.cpp 同一個風格：
// 一個 CHECK 巨集、一個計數器、失敗時印出檔案行號與實際值。
//
// 這份測試守的是什麼：
//   CoachCampaign 沒有輸出對錯，它輸出的是「要不要說話、說多少」。
//   這種東西壞掉不會 crash、不會噴錯，只會安靜地變得比較囉嗦或比較沉默，
//   而那正好就是整個教練型 AI 的主張本身。所以這裡把設計承諾寫成斷言：
//     1. 六關的遞減曲線逐格固定（42 格 golden table）
//     2. 後段關卡永遠不會吐出答案
//     3. 掌握度只升不降
//     4. 過關要真的靠自己做到規定次數
//   以後改參數時，破壞承諾的那一格會自己叫出來。
// =========================================================================

#include "coach_campaign.h"

#include <cstdio>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────
// 極簡斷言
// ─────────────────────────────────────────────────────────
static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failed;                                                     \
            std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
        }                                                                   \
    } while (0)

#define CHECK_EQ_INT(actual, expected, msg)                                 \
    do {                                                                    \
        ++g_checks;                                                         \
        int a_ = (int)(actual), e_ = (int)(expected);                       \
        if (a_ != e_) {                                                     \
            ++g_failed;                                                     \
            std::printf("  [FAIL] %s:%d  %s (實際 %d，預期 %d)\n",          \
                        __FILE__, __LINE__, (msg), a_, e_);                 \
        }                                                                   \
    } while (0)

static void section(const char* name) {
    std::printf("\n== %s ==\n", name);
}

// ─────────────────────────────────────────────────────────
// 工具：把 shouldGiveHint 的結果壓成一個字元，方便做逐格比對
//   '.' 沉默   'N' GENTLE_NUDGE   'P' POINT_TO_AREA   'R' REVEAL_MOVE
// ─────────────────────────────────────────────────────────
static char tierChar(const CoachCampaign& c, int stuck) {
    HintTier t;
    if (!c.shouldGiveHint(stuck, t)) return '.';
    switch (t) {
        case HintTier::GENTLE_NUDGE:  return 'N';
        case HintTier::POINT_TO_AREA: return 'P';
        case HintTier::REVEAL_MOVE:   return 'R';
    }
    return '?';
}

// 深度排名，用來比較「有沒有超過 max_tier」與「有沒有倒退」。
// 沉默視為 0，三層依序 1/2/3。
static int tierRank(char c) {
    switch (c) {
        case '.': return 0;
        case 'N': return 1;
        case 'P': return 2;
        case 'R': return 3;
    }
    return -1;
}

static int rankOf(HintTier t) {
    switch (t) {
        case HintTier::GENTLE_NUDGE:  return 1;
        case HintTier::POINT_TO_AREA: return 2;
        case HintTier::REVEAL_MOVE:   return 3;
    }
    return -1;
}

// 把 campaign 推進到指定關卡（advance 只能一關一關走）
static CoachCampaign atLevel(int level) {
    CoachCampaign c;
    while (c.currentLevel() < level && c.advance()) {}
    return c;
}

// 建構一次使用記錄
static TechniqueUse useOf(Technique t, bool saw_reveal, bool saw_point) {
    TechniqueUse u;
    u.technique = t;
    u.saw_reveal = saw_reveal;
    u.saw_point = saw_point;
    return u;
}

// ─────────────────────────────────────────────────────────
// 1. 設定表自身的一致性
//
// 這一組測的不是程式邏輯，是那張表有沒有寫出「自相矛盾的設定」。
// 例如某關寫了 reveal_after_turns = 3 但 max_tier 只到 POINT——
// 那個 3 永遠不會生效，讀表的人卻會以為第 3 回合會給答案。
// 這種死設定編譯器抓不到，只能靠測試。
// ─────────────────────────────────────────────────────────
static void testLevelTableConsistency() {
    section("設定表一致性");

    CHECK_EQ_INT(CoachCampaign::totalLevels(), 6, "應該有六個關卡");

    int prev_guidance = 101;
    for (int lv = 1; lv <= CoachCampaign::totalLevels(); ++lv) {
        const LevelConfig& cfg = CoachCampaign::levelConfig(lv);
        std::string tag = "L" + std::to_string(lv);

        CHECK_EQ_INT(cfg.level, lv, (tag + " 的 level 欄位要跟索引一致").c_str());
        CHECK(!cfg.name.empty(), (tag + " 要有名稱").c_str());
        CHECK(!cfg.description.empty(), (tag + " 要有教學文字").c_str());

        // 引導強度必須嚴格遞減——這是整個關卡設計的主張
        CHECK(cfg.guidance_percent < prev_guidance,
              (tag + " 的引導強度要比前一關低").c_str());
        prev_guidance = cfg.guidance_percent;

        // 過關條件要合理
        CHECK(cfg.required_uses > 0, (tag + " 過關次數要大於 0").c_str());
        CHECK(cfg.required_unassisted >= 0 &&
              cfg.required_unassisted <= cfg.required_uses,
              (tag + " 自主次數不能超過總次數").c_str());

        // 死設定檢查：門檻設了卻被 max_tier 擋掉
        if (cfg.reveal_after_turns >= 0) {
            CHECK(cfg.max_tier == HintTier::REVEAL_MOVE,
                  (tag + " 設了 reveal 門檻，max_tier 就必須是 REVEAL_MOVE").c_str());
        }
        if (cfg.point_after_turns >= 0) {
            CHECK(cfg.max_tier != HintTier::GENTLE_NUDGE,
                  (tag + " 設了 point 門檻，max_tier 不能只到 NUDGE").c_str());
        }

        // 門檻本身要遞增，否則深層提示會比淺層先出現
        if (cfg.point_after_turns >= 0) {
            CHECK(cfg.nudge_after_turns <= cfg.point_after_turns,
                  (tag + " nudge 門檻要早於或等於 point").c_str());
        }
        if (cfg.reveal_after_turns >= 0) {
            CHECK(cfg.point_after_turns <= cfg.reveal_after_turns,
                  (tag + " point 門檻要早於或等於 reveal").c_str());
        }
        CHECK(cfg.nudge_after_turns >= 0,
              (tag + " nudge 門檻不該是永不觸發——那樣這關會完全不說話").c_str());
    }

    // 越界索引要夾住，不能讀到表外
    CHECK_EQ_INT(CoachCampaign::levelConfig(0).level, 1, "level 0 要夾到第一關");
    CHECK_EQ_INT(CoachCampaign::levelConfig(-5).level, 1, "負數要夾到第一關");
    CHECK_EQ_INT(CoachCampaign::levelConfig(99).level, 6, "超出範圍要夾到最後一關");
}

// ─────────────────────────────────────────────────────────
// 2. 遞減曲線 golden table（42 格）
//
// 這張表就是文件裡那張圖，逐格寫死：
//   L1: 輕指答答答答答
//   L2: ·輕指指答答答
//   L3: ·輕輕指指答答
//   L4: ··輕輕指指指
//   L5: ··輕輕輕指指
//   L6: ···輕輕輕輕
//
// 為什麼要寫死而不是重算一次公式：
// 重算公式的測試只會證明「程式跟程式一致」，改了參數照樣過。
// 寫死的表證明的是「程式跟當初的設計決定一致」——改參數就會斷，
// 而斷掉正是我要的，那代表有人動到了引導曲線，需要自己確認是不是故意的。
// ─────────────────────────────────────────────────────────
static void testGuidanceCurveGoldenTable() {
    section("遞減曲線 golden table");

    // stuck_turns = 0..6
    static const char* kExpected[6] = {
        "NPRRRRR",   // L1  輕指答答答答答
        ".NPPRRR",   // L2  ·輕指指答答答
        ".NNPPRR",   // L3  ·輕輕指指答答
        "..NNPPP",   // L4  ··輕輕指指指
        "..NNNPP",   // L5  ··輕輕輕指指
        "...NNNN",   // L6  ···輕輕輕輕
    };

    for (int lv = 1; lv <= 6; ++lv) {
        CoachCampaign c = atLevel(lv);
        CHECK_EQ_INT(c.currentLevel(), lv, "atLevel 應該推到指定關卡");

        std::string actual;
        for (int stuck = 0; stuck <= 6; ++stuck)
            actual += tierChar(c, stuck);

        const std::string expected = kExpected[lv - 1];
        if (actual != expected) {
            ++g_checks;
            ++g_failed;
            std::printf("  [FAIL] L%d 曲線不符  實際 %s  預期 %s\n",
                        lv, actual.c_str(), expected.c_str());
        } else {
            ++g_checks;
        }
    }
}

// ─────────────────────────────────────────────────────────
// 3. max_tier 是硬上限
//
// 「Level 6 只給輕推」不是一句文案，是承諾。
// 玩家卡到第 200 回合，系統還是不能講答案——因為那一關要教的
// 本來就不是那一招，是「自己找」這件事。
// 所以這裡不挑幾個點測，而是把每一關從 0 掃到 200 全部檢查。
// ─────────────────────────────────────────────────────────
static void testMaxTierIsHardCeiling() {
    section("max_tier 硬上限");

    for (int lv = 1; lv <= 6; ++lv) {
        CoachCampaign c = atLevel(lv);
        const LevelConfig& cfg = CoachCampaign::levelConfig(lv);
        int ceiling = rankOf(cfg.max_tier);

        bool ok = true;
        int worst_stuck = -1;
        for (int stuck = 0; stuck <= 200; ++stuck) {
            HintTier t;
            if (!c.shouldGiveHint(stuck, t)) continue;
            if (rankOf(t) > ceiling) { ok = false; worst_stuck = stuck; break; }
        }
        CHECK(ok, ("L" + std::to_string(lv) +
                   " 在某個卡關回合超過了 max_tier（stuck=" +
                   std::to_string(worst_stuck) + "）").c_str());
    }

    // 逐條點名，讓失敗訊息一眼看得出是哪個承諾破了
    for (int stuck = 0; stuck <= 200; ++stuck) {
        CHECK(tierChar(atLevel(6), stuck) != 'R', "L6 永遠不能給答案");
        CHECK(tierChar(atLevel(6), stuck) != 'P', "L6 連指方向都不該給");
        CHECK(tierChar(atLevel(5), stuck) != 'R', "L5 永遠不能給答案");
        CHECK(tierChar(atLevel(4), stuck) != 'R', "L4 永遠不能給答案");
    }
}

// ─────────────────────────────────────────────────────────
// 4. 單調性與負值
//
// 卡得越久，提示只能越深或持平，不能突然變淺——
// 玩家會覺得系統忘記他還卡著。
// 另外卡關回合數為負是不該發生的狀態，但真的傳進來時要沉默，不能誤觸門檻。
// ─────────────────────────────────────────────────────────
static void testMonotonicAndNegative() {
    section("單調性與負值");

    for (int lv = 1; lv <= 6; ++lv) {
        CoachCampaign c = atLevel(lv);
        int prev = 0;
        bool ok = true;
        for (int stuck = 0; stuck <= 100; ++stuck) {
            int cur = tierRank(tierChar(c, stuck));
            if (cur < prev) { ok = false; break; }
            prev = cur;
        }
        CHECK(ok, ("L" + std::to_string(lv) + " 的提示深度不該隨卡關變淺").c_str());
    }

    for (int lv = 1; lv <= 6; ++lv) {
        CoachCampaign c = atLevel(lv);
        CHECK(tierChar(c, -1) == '.',
              ("L" + std::to_string(lv) + " 卡關回合為負時應保持沉默").c_str());
        CHECK(tierChar(c, -999) == '.',
              ("L" + std::to_string(lv) + " 卡關回合為大負數時應保持沉默").c_str());
    }
}

// ─────────────────────────────────────────────────────────
// 5. 掌握度只升不降
//
// 「同一招可以重複升級，但不會退回去」是這個系統對玩家的承諾：
// 某天靠答案做出來，之後某天沒提示又用了一次，那是升級的瞬間；
// 反過來，已經自行發掘過的招，之後偶爾看一次答案不該被降級——
// 學會了就是學會了。
// 三種來源 × 三種既有狀態，全部走一遍。
// ─────────────────────────────────────────────────────────
static void testMasteryOnlyUpgrades() {
    section("掌握度只升不降");

    const Technique T = Technique::ATTACH_RUN;

    {   // 初始狀態
        CoachCampaign c;
        CHECK(c.progressOf(T).mastery == Mastery::LOCKED, "沒用過應該是 LOCKED");
        CHECK_EQ_INT(c.progressOf(T).total_uses, 0, "初始使用次數應為 0");
        CHECK_EQ_INT(c.progressOf(T).unassisted_uses, 0, "初始自主次數應為 0");
    }

    {   // 三種來源各自對應的星等
        CoachCampaign c1; c1.recordUse(useOf(T, true, false));
        CHECK(c1.progressOf(T).mastery == Mastery::COPIED, "看了答案應為 COPIED");

        CoachCampaign c2; c2.recordUse(useOf(T, false, true));
        CHECK(c2.progressOf(T).mastery == Mastery::PROMPTED, "只給方向應為 PROMPTED");

        CoachCampaign c3; c3.recordUse(useOf(T, false, false));
        CHECK(c3.progressOf(T).mastery == Mastery::DISCOVERED, "完全沒提示應為 DISCOVERED");

        // reveal 優先於 point：兩個都看過就是照做
        CoachCampaign c4; c4.recordUse(useOf(T, true, true));
        CHECK(c4.progressOf(T).mastery == Mastery::COPIED,
              "同時看過 reveal 與 point 應判為 COPIED");
    }

    {   // 升級路徑
        CoachCampaign c;
        c.recordUse(useOf(T, true, false));    // COPIED
        c.recordUse(useOf(T, false, true));    // → PROMPTED
        CHECK(c.progressOf(T).mastery == Mastery::PROMPTED, "COPIED 應可升到 PROMPTED");
        c.recordUse(useOf(T, false, false));   // → DISCOVERED
        CHECK(c.progressOf(T).mastery == Mastery::DISCOVERED, "PROMPTED 應可升到 DISCOVERED");
    }

    {   // 不可降級
        CoachCampaign c;
        c.recordUse(useOf(T, false, false));   // DISCOVERED
        c.recordUse(useOf(T, true, false));    // 又看了答案
        CHECK(c.progressOf(T).mastery == Mastery::DISCOVERED, "DISCOVERED 不該被降回 COPIED");
        c.recordUse(useOf(T, false, true));
        CHECK(c.progressOf(T).mastery == Mastery::DISCOVERED, "DISCOVERED 不該被降回 PROMPTED");

        CoachCampaign c2;
        c2.recordUse(useOf(T, false, true));   // PROMPTED
        c2.recordUse(useOf(T, true, false));
        CHECK(c2.progressOf(T).mastery == Mastery::PROMPTED, "PROMPTED 不該被降回 COPIED");
    }

    {   // 星數顯示
        CHECK(CoachCampaign::masteryStars(Mastery::LOCKED).empty(), "LOCKED 不該有星");
        CHECK_EQ_INT(CoachCampaign::masteryStars(Mastery::COPIED).size(), 1, "COPIED 一顆星");
        CHECK_EQ_INT(CoachCampaign::masteryStars(Mastery::PROMPTED).size(), 2, "PROMPTED 兩顆星");
        CHECK_EQ_INT(CoachCampaign::masteryStars(Mastery::DISCOVERED).size(), 3, "DISCOVERED 三顆星");
    }
}

// ─────────────────────────────────────────────────────────
// 6. 計數器
// ─────────────────────────────────────────────────────────
static void testCounters() {
    section("使用次數計數");

    const Technique T = Technique::COMPLETE_GROUP;

    CoachCampaign c;
    c.recordUse(useOf(T, true,  false));   // 看答案
    c.recordUse(useOf(T, true,  false));   // 看答案
    c.recordUse(useOf(T, false, true));    // 自主
    c.recordUse(useOf(T, false, false));   // 自主

    CHECK_EQ_INT(c.progressOf(T).total_uses, 4, "總次數應為 4");
    CHECK_EQ_INT(c.progressOf(T).unassisted_uses, 2, "自主次數應為 2（只有沒看 reveal 的算）");

    // 各招進度彼此獨立
    CHECK_EQ_INT(c.progressOf(Technique::ATTACH_RUN).total_uses, 0,
                 "記錄某一招不該影響其他招");

    // 越界的 technique 值：不該 crash，也不該污染任何進度
    TechniqueUse bad;
    bad.technique = Technique::TECHNIQUE_COUNT;
    c.recordUse(bad);
    CHECK_EQ_INT(c.progressOf(T).total_uses, 4, "越界記錄不該改動既有進度");
    for (int i = 0; i < (int)Technique::TECHNIQUE_COUNT; ++i) {
        Technique t = (Technique)i;
        if (t == T) continue;
        CHECK_EQ_INT(c.progressOf(t).total_uses, 0, "越界記錄不該寫進任何一招");
    }
}

// ─────────────────────────────────────────────────────────
// 7. 過關條件的邊界
//
// 這裡刻意逐關測 N-1 不過、N 過，因為過關條件是整個系統唯一
// 「會對玩家產生後果」的判斷——判太鬆等於發沒讀過書的畢業證書。
//
// 最關鍵的一條在最後：canAdvance 只能看本關的技巧。
// 玩家在第一關狂用長龍切斷是好事，但那不代表他學會了接龍頭尾。
// ─────────────────────────────────────────────────────────
static void testAdvanceThresholds() {
    section("過關條件邊界");

    for (int lv = 1; lv <= 6; ++lv) {
        const LevelConfig& cfg = CoachCampaign::levelConfig(lv);
        std::string tag = "L" + std::to_string(lv);

        // (a) 次數差一次：不過
        {
            CoachCampaign c = atLevel(lv);
            for (int i = 0; i < cfg.required_uses - 1; ++i)
                c.recordUse(useOf(cfg.technique, false, false));
            CHECK(!c.canAdvance(), (tag + " 次數差一次不該過關").c_str());
        }

        // (b) 次數剛好且全自主：過
        {
            CoachCampaign c = atLevel(lv);
            for (int i = 0; i < cfg.required_uses; ++i)
                c.recordUse(useOf(cfg.technique, false, false));
            CHECK(c.canAdvance(), (tag + " 次數達標且全自主應該過關").c_str());
        }

        // (c) 次數夠但自主次數差一次：不過
        if (cfg.required_unassisted > 0) {
            CoachCampaign c = atLevel(lv);
            int unassisted = cfg.required_unassisted - 1;
            for (int i = 0; i < unassisted; ++i)
                c.recordUse(useOf(cfg.technique, false, false));
            for (int i = unassisted; i < cfg.required_uses; ++i)
                c.recordUse(useOf(cfg.technique, true, false));   // 看答案
            CHECK_EQ_INT(c.progressOf(cfg.technique).total_uses, cfg.required_uses,
                         (tag + " 前置條件：總次數應已達標").c_str());
            CHECK(!c.canAdvance(),
                  (tag + " 自主次數差一次不該過關（次數夠不等於學會）").c_str());
        }

        // (d) 全部靠看答案做滿次數：只要這關要求自主，就不該過
        if (cfg.required_unassisted > 0) {
            CoachCampaign c = atLevel(lv);
            for (int i = 0; i < cfg.required_uses * 3; ++i)
                c.recordUse(useOf(cfg.technique, true, false));
            CHECK(!c.canAdvance(),
                  (tag + " 全靠看答案不該過關，做再多次也一樣").c_str());
        }

        // (e) 練別的招不能讓這一關過
        {
            CoachCampaign c = atLevel(lv);
            for (int i = 0; i < (int)Technique::TECHNIQUE_COUNT; ++i) {
                Technique other = (Technique)i;
                if (other == cfg.technique) continue;
                for (int k = 0; k < 10; ++k)
                    c.recordUse(useOf(other, false, false));
            }
            CHECK(!c.canAdvance(),
                  (tag + " 用其他招數不該讓本關過關").c_str());
        }
    }

    // L6 要求三次全自主——特別點名，因為這是整條曲線的終點
    {
        const LevelConfig& cfg = CoachCampaign::levelConfig(6);
        CHECK_EQ_INT(cfg.required_unassisted, cfg.required_uses,
                     "L6 應該要求每一次都自主完成");
    }
}

// ─────────────────────────────────────────────────────────
// 8. advance() 的行為
// ─────────────────────────────────────────────────────────
static void testAdvanceMechanics() {
    section("關卡推進");

    CoachCampaign c;
    CHECK_EQ_INT(c.currentLevel(), 1, "初始應在第一關");
    CHECK_EQ_INT(c.currentConfig().level, 1, "currentConfig 應對應 currentLevel");

    for (int lv = 2; lv <= 6; ++lv) {
        CHECK(c.advance(), "尚未到最後一關時 advance 應回傳 true");
        CHECK_EQ_INT(c.currentLevel(), lv, "關卡應遞增");
        CHECK_EQ_INT(c.currentConfig().level, lv, "currentConfig 應跟著換");
    }

    CHECK(!c.advance(), "最後一關再 advance 應回傳 false");
    CHECK_EQ_INT(c.currentLevel(), 6, "失敗的 advance 不該改變關卡");
    CHECK(!c.advance(), "重複呼叫仍應回傳 false 且不越界");
    CHECK_EQ_INT(c.currentLevel(), 6, "重複呼叫後關卡仍應為 6");

    // 過關不清空進度：玩家先前累積的星等要留著
    {
        CoachCampaign p;
        p.recordUse(useOf(Technique::ATTACH_RUN, false, false));
        p.advance();
        CHECK(p.progressOf(Technique::ATTACH_RUN).mastery == Mastery::DISCOVERED,
              "進到下一關不該清掉前一關的掌握度");
        CHECK_EQ_INT(p.progressOf(Technique::ATTACH_RUN).total_uses, 1,
                     "進到下一關不該清掉使用次數");
    }
}

// ─────────────────────────────────────────────────────────
// 9. MCQ 判定
// ─────────────────────────────────────────────────────────
static void testMcqJudge() {
    section("MCQ 判定");

    McqQuestion q;
    q.prompt = "測試題";
    q.options = {{"錯", false}, {"對", true}, {"也錯", false}};
    q.hint = "提示";
    q.explanation = "解答";

    using AR = CoachCampaign::AnswerResult;

    CHECK(CoachCampaign::judge(q, 1, 1) == AR::CORRECT, "選到正解應為 CORRECT");
    CHECK(CoachCampaign::judge(q, 1, 2) == AR::CORRECT, "第二次才選對仍是 CORRECT");
    CHECK(CoachCampaign::judge(q, 1, 9) == AR::CORRECT, "第幾次選對都算 CORRECT");

    CHECK(CoachCampaign::judge(q, 0, 1) == AR::WRONG_FIRST_TRY,
          "答錯第一次只該給提示");
    CHECK(CoachCampaign::judge(q, 0, 2) == AR::WRONG_SHOW_ANSWER,
          "答錯第二次才該給解答");
    CHECK(CoachCampaign::judge(q, 2, 3) == AR::WRONG_SHOW_ANSWER,
          "第三次以後也是給解答");

    // 越界索引一律視為答錯，不能因為讀到界外而誤判成正確
    CHECK(CoachCampaign::judge(q, -1, 1) == AR::WRONG_FIRST_TRY, "負數索引應視為答錯");
    CHECK(CoachCampaign::judge(q, 3, 1) == AR::WRONG_FIRST_TRY, "超出選項數應視為答錯");
    CHECK(CoachCampaign::judge(q, 99, 2) == AR::WRONG_SHOW_ANSWER, "大越界索引應視為答錯");

    // 沒有選項的題目不該 crash
    McqQuestion empty;
    CHECK(CoachCampaign::judge(empty, 0, 1) == AR::WRONG_FIRST_TRY,
          "空題目不該當成答對");
}

// ─────────────────────────────────────────────────────────
// 10. Recap 題庫內容
//
// 這一組不是測邏輯，是測「內容有沒有寫壞」——
// 忘了標正解、兩個選項都標對、提示欄複製貼上成解答，
// 這些都不會編譯失敗，但會直接毀掉那一關的教學。
//
// 特別是 hint != explanation：答錯第一次只給提示、第二次才給解答，
// 兩欄一樣的話等於第一次就把答案講完了，整個兩階段設計就沒了。
// ─────────────────────────────────────────────────────────
static void testRecapContent() {
    section("Recap 題庫");

    for (int lv = 1; lv <= 6; ++lv) {
        std::vector<McqQuestion> qs = CoachCampaign::recapFor(lv);
        std::string tag = "L" + std::to_string(lv);

        CHECK_EQ_INT(qs.size(), 5, (tag + " 應有 5 題").c_str());

        for (std::size_t i = 0; i < qs.size(); ++i) {
            const McqQuestion& q = qs[i];
            std::string qt = tag + " 第 " + std::to_string(i + 1) + " 題";

            CHECK(!q.prompt.empty(), (qt + " 題目不該是空的").c_str());
            CHECK(q.options.size() >= 2, (qt + " 至少要有兩個選項").c_str());
            CHECK(!q.hint.empty(), (qt + " 要有答錯提示").c_str());
            CHECK(!q.explanation.empty(), (qt + " 要有解答說明").c_str());
            CHECK(q.hint != q.explanation,
                  (qt + " 提示不能等於解答，否則第一次就洩底").c_str());

            int correct = 0;
            for (const McqOption& o : q.options) {
                CHECK(!o.text.empty(), (qt + " 有空白選項").c_str());
                if (o.correct) ++correct;
            }
            CHECK_EQ_INT(correct, 1, (qt + " 應該恰好有一個正解").c_str());
        }
    }
}

// ─────────────────────────────────────────────────────────
// 11. 顯示字串
// ─────────────────────────────────────────────────────────
static void testDisplayStrings() {
    section("顯示字串");

    for (int i = 0; i < (int)Technique::TECHNIQUE_COUNT; ++i) {
        Technique t = (Technique)i;
        std::string name = CoachCampaign::techniqueName(t);
        CHECK(!name.empty(), "每一招都要有名稱");
        CHECK(name != "未知技巧", "六招都不該落到預設分支");
    }
    CHECK(CoachCampaign::techniqueName(Technique::TECHNIQUE_COUNT) == "未知技巧",
          "哨兵值應回傳未知技巧");

    // 關卡名稱與技巧名稱要對得起來，避免表跟 switch 各講各的
    for (int lv = 1; lv <= 6; ++lv) {
        const LevelConfig& cfg = CoachCampaign::levelConfig(lv);
        CHECK(cfg.name == CoachCampaign::techniqueName(cfg.technique),
              ("L" + std::to_string(lv) + " 的關卡名稱應與技巧名稱一致").c_str());
    }
}

// ─────────────────────────────────────────────────────────
int main() {
    std::printf("CoachCampaign 測試\n");

    testLevelTableConsistency();
    testGuidanceCurveGoldenTable();
    testMaxTierIsHardCeiling();
    testMonotonicAndNegative();
    testMasteryOnlyUpgrades();
    testCounters();
    testAdvanceThresholds();
    testAdvanceMechanics();
    testMcqJudge();
    testRecapContent();
    testDisplayStrings();

    std::printf("\n─────────────────────────────\n");
    std::printf("%d 項檢查，%d 項失敗\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
