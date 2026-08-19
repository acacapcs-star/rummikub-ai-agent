/* -------------------------------------------------------
   test_companions.cpp —— 三個角色的測試

   三個角色的分工：
     GUIDE   卡關時給提示          不出牌
     WITNESS 使出技能時給回饋      事後，不干預
     RECALL  玩家主動找歷史        只講規則，不指牌

   最重要的測試是**界線**——每個角色不做什麼，
   比它做什麼更能定義這個系統。
------------------------------------------------------- */
#include "companions.h"
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
static void test_六個形象() {
    std::cout << "\n六個形象（三角色 × 兩族群）\n";
    int count = 0;
    for (auto role : { Companion::GUIDE, Companion::WITNESS, Companion::RECALL })
        for (auto a : { Audience::KIDS, Audience::SENIORS }) {
            const auto& p = Companions::get(role, a);
            bool ok = !p.display_name.empty() && !p.appearance.empty()
                   && !p.tone.empty();
            check(ok, "  " + p.display_name + " 有名稱、形象與語氣");
            ++count;
        }
    checkEq(count, 6, "共六個形象");

    // 同一個角色在兩個族群要有不同的形象
    const auto& k = Companions::get(Companion::GUIDE, Audience::KIDS);
    const auto& s = Companions::get(Companion::GUIDE, Audience::SENIORS);
    check(k.display_name != s.display_name,
          "★ 同一個角色在兩個族群的形象不同");
}

// ══════════════════════════════════════════════════════════
static void test_對手風格() {
    std::cout << "\n對手風格偵測\n";
    OpponentReader r;

    check(r.style() == OpponentStyle::UNKNOWN,
          "★ 資料不足時回傳 UNKNOWN——猜錯的提醒比不提醒更糟");

    // 積極型：常動桌面、出很多張
    OpponentReader agg;
    for (int i = 0; i < 6; ++i) {
        OpponentTurn t;
        t.tiles_played = 4;
        t.touched_board = true;
        t.hand_size_after = 10 - i;
        agg.record(t);
    }
    check(agg.style() == OpponentStyle::AGGRESSIVE, "★ 常重組又出多張 → 積極");
    check(!OpponentReader::advice(OpponentStyle::AGGRESSIVE).empty(),
          "  積極型有對應的提醒");

    // 保守型：常抽牌
    OpponentReader con;
    for (int i = 0; i < 6; ++i) {
        OpponentTurn t;
        t.drew = (i % 3 != 0);
        t.tiles_played = t.drew ? 0 : 1;
        t.hand_size_after = 8;
        con.record(t);
    }
    check(con.style() == OpponentStyle::CONSERVATIVE ||
          con.style() == OpponentStyle::HOARDING, "常抽牌 → 保守或囤積");

    // 囤積型：抽牌而且手牌變多
    OpponentReader hoard;
    for (int i = 0; i < 6; ++i) {
        OpponentTurn t;
        t.drew = true;
        t.tiles_played = 0;
        t.hand_size_after = 8 + i;
        hoard.record(t);
    }
    check(hoard.style() == OpponentStyle::HOARDING,
          "★ 一直抽牌而且手牌越來越多 → 囤積");

    // 穩健型不給提醒
    check(OpponentReader::advice(OpponentStyle::STEADY).empty(),
          "★ 穩健的對手不需要特別提醒——沒事就別說話");
    check(OpponentReader::advice(OpponentStyle::UNKNOWN).empty(),
          "  還看不出來時也不說");

    // 滑動視窗
    OpponentReader win;
    for (int i = 0; i < 20; ++i) win.record(OpponentTurn{});
    check(win.observedTurns() <= OpponentReader::WINDOW,
          "★ 只看最近 8 回合——對手的策略會隨局勢改變");
}

// ══════════════════════════════════════════════════════════
static void test_guide() {
    std::cout << "\nGUIDE · 卡關時\n";

    GuideCompanion kids(Audience::KIDS);
    GuideCompanion seniors(Audience::SENIORS);

    check( kids.usesOpponentStyle(),    "★ 兒童版看對手風格");
    check(!seniors.usesOpponentStyle(), "★ 長者版不看——那個版本的重點不是競技");

    auto k = kids.speak(HintTier::GENTLE_NUDGE, "這裡有東西",
                        OpponentStyle::AGGRESSIVE);
    check(k.speak && !k.text.empty(), "兒童版會說話");
    check(!k.tactical.empty(), "  而且附上對手的戰術提醒");

    auto s = seniors.speak(HintTier::GENTLE_NUDGE, "我們一起看看",
                           OpponentStyle::AGGRESSIVE);
    check(s.tactical.empty(), "★ 長者版即使傳入對手風格也不提醒");

    auto deep = kids.speak(HintTier::REVEAL_MOVE, "把紅 3 接在這裡",
                           OpponentStyle::AGGRESSIVE);
    check(deep.tactical.empty(),
          "★ 已經在講答案了就不加戰術提醒——那只是多餘的資訊");
}

// ══════════════════════════════════════════════════════════
static void test_witness() {
    std::cout << "\nWITNESS · 使出技能時\n";

    WitnessCompanion kids(Audience::KIDS);
    WitnessCompanion seniors(Audience::SENIORS);

    auto k = kids.onTechnique(0, 5);
    check(k.speak && !k.text.empty(), "兒童版會回應");

    auto s = seniors.onTechnique(0, 5);
    check(s.speak && !s.text.empty(), "長者版也會回應");
    check(k.text != s.text, "★ 兩個族群的說法不同");

    // 長者版不該出現數字或比較
    bool hasDigit = false;
    for (char c : s.text) if (c >= '0' && c <= '9') hasDigit = true;
    check(!hasDigit,
          "★ 長者版的回饋不含數字——比較會製造壓力，而壓力損害認知表現");
}

// ══════════════════════════════════════════════════════════
static void test_recall_界線() {
    std::cout << "\nRECALL · 界線（這是整個角色的核心）\n";

    RecallCompanion r(Audience::KIDS);

    BoardFingerprint fp;
    fp.set_count = 3;
    fp.longest_run = 5;
    fp.hand_size = 8;
    fp.has_joker = true;
    fp.colors_on_board = 3;
    fp.playable_count = 2;

    r.remember(4, fp, 1);        // 記住「在這個局面用過大風吹」

    auto sug = r.suggest(fp);
    check(sug.found, "找到相似的歷史紀錄");
    check(sug.technique == 4, "  是大風吹重組");
    check(!sug.rule_text.empty(), "  有重述規則");

    /* 最重要的一項：規則裡不能出現任何具體的牌。
       規則  「把桌上的組合拆開重排，讓手上的牌接得上」
       答案  「把第 2 組拆開，紅 5 就接得上了」
       前者要玩家自己找，後者是代打。                      */
    const char* forbidden[] = { "紅", "藍", "黑", "黃", "第 1 組", "第 2 組" };
    bool clean = true;
    for (const char* f : forbidden)
        if (sug.rule_text.find(f) != std::string::npos) clean = false;
    check(clean, "★ 規則裡沒有任何具體的顏色或位置——只講方法，不講答案");

    checkEq(sug.times_used, 1, "報告使用次數");
    r.remember(4, fp, 1);
    check(r.suggest(fp).times_used == 2, "  用第二次之後次數增加");
}

static void test_recall_不硬湊() {
    std::cout << "\nRECALL · 沒有相似紀錄時\n";

    RecallCompanion empty(Audience::SENIORS);
    BoardFingerprint fp;
    fp.set_count = 3; fp.longest_run = 5; fp.hand_size = 8;

    auto s0 = empty.suggest(fp);
    check(!s0.found, "★ 沒有任何紀錄時不建議");
    check(!s0.opening.empty(), "  但仍然有話說，不會沉默");

    // 有紀錄但完全不像
    RecallCompanion r(Audience::KIDS);
    BoardFingerprint old;
    old.set_count = 1; old.longest_run = 3; old.hand_size = 14;
    old.has_joker = false; old.colors_on_board = 1; old.playable_count = 0;
    r.remember(0, old, 1);

    BoardFingerprint now;
    now.set_count = 6; now.longest_run = 10; now.hand_size = 2;
    now.has_joker = true; now.colors_on_board = 4; now.playable_count = 8;

    auto s1 = r.suggest(now, 0.55);
    check(!s1.found,
          "★ 局面完全不像時不硬湊——寧可說沒有，也不要找個不像的來充數");
    check(!s1.opening.empty(), "  一樣有話說");
}

static void test_相似度() {
    std::cout << "\n局面相似度\n";

    BoardFingerprint a;
    a.set_count = 3; a.longest_run = 5; a.hand_size = 8;
    a.has_joker = true; a.colors_on_board = 3; a.playable_count = 2;

    check(a.similarityTo(a) > 0.99, "★ 跟自己完全相同 → 相似度接近 1");

    BoardFingerprint b = a;
    b.hand_size = 9;
    double near = a.similarityTo(b);
    check(near > 0.9, "只差一張手牌 → 仍然很像");

    BoardFingerprint c;
    c.set_count = 6; c.longest_run = 10; c.hand_size = 1;
    c.has_joker = false; c.colors_on_board = 1; c.playable_count = 8;
    double far = a.similarityTo(c);
    check(far < near, "★ 差很多 → 相似度較低");
    check(far >= 0.0 && far <= 1.0, "相似度落在 0–1");
}

static void test_兩族群的措辭() {
    std::cout << "\n兩個族群的措辭\n";

    BoardFingerprint fp;
    fp.set_count = 3; fp.longest_run = 5; fp.hand_size = 8;

    RecallCompanion k(Audience::KIDS);
    RecallCompanion s(Audience::SENIORS);
    k.remember(0, fp, 1);
    s.remember(0, fp, 1);

    auto ks = k.suggest(fp);
    auto ss = s.suggest(fp);
    check(ks.found && ss.found, "兩邊都找得到");
    check(ks.opening != ss.opening, "★ 開場白不同");
    check(ks.rule_text == ss.rule_text, "  但規則的內容相同——規則不因人而異");
}

int main() {
    std::cout << "三個角色 · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_六個形象();
    test_對手風格();
    test_guide();
    test_witness();
    test_recall_界線();
    test_recall_不硬湊();
    test_相似度();
    test_兩族群的措辭();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
