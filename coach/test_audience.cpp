/* -------------------------------------------------------
   test_audience.cpp —— 兩個族群模組的測試
------------------------------------------------------- */
#include "audience_profile.h"
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

// 跑一段表現序列，回傳最後的提示層級
static int runPattern(Audience a, const std::string& pattern, bool* guaranteed = nullptr) {
    AdaptiveDifficulty d(AudienceProfiles::get(a));
    for (char c : pattern) {
        if (c == 'x') d.onStuck();
        else          d.onSuccess();
    }
    if (guaranteed) *guaranteed = d.needsGuarantee();
    return static_cast<int>(d.tier());
}

static void test_兩個族群的設定() {
    std::cout << "\n族群設定\n";
    const auto& k = AudienceProfiles::get(Audience::KIDS);
    const auto& s = AudienceProfiles::get(Audience::SENIORS);

    check(!k.name.empty() && !s.name.empty(), "兩個版本都有名稱");
    check(!k.target_group.empty() && !s.target_group.empty(), "都標明了適用對象");

    checkEq(k.level_count, 6, "兒童版六關全開");
    checkEq(s.level_count, 4, "★ 長者版只開前四關——跳過最需要抑制控制的那兩關");

    check(!k.adaptive_difficulty, "兒童版難度由關卡決定");
    check( s.adaptive_difficulty, "★ 長者版依表現動態調整——認知功能會波動");

    check(k.allow_errors,  "兒童版允許犯錯——錯誤是學習的一部分");
    check(!s.allow_errors, "★ 長者版盡量避免——呼應無錯學習的原則");

    check( k.recap_gates_progress, "兒童版 Recap 要過才進關");
    check(!s.recap_gates_progress, "★ 長者版 Recap 不擋——測驗會製造壓力");

    check(s.session_minutes < k.session_minutes, "長者版單次時長較短");
    check(s.fatigue_reminder && !k.fatigue_reminder, "只有長者版主動提醒休息");
}

static void test_兒童版不受表現影響() {
    std::cout << "\n兒童版 · 固定難度\n";
    bool g = false;
    checkEq(runPattern(Audience::KIDS, "xxxxxxxxxx", &g), 0,
            "★ 連卡十回合，提示層級仍是輕推");
    check(!g, "  不會觸發成功保證");
    checkEq(runPattern(Audience::KIDS, "oooooooooo"), 0, "連續成功也不變");
    checkEq(runPattern(Audience::KIDS, "xoxoxoxoxo"), 0, "時好時壞也不變");
}

static void test_長者版動態調整() {
    std::cout << "\n長者版 · 動態難度\n";

    checkEq(runPattern(Audience::SENIORS, "x"),  0, "卡 1 回合 → 還是輕推");
    checkEq(runPattern(Audience::SENIORS, "xx"), 1, "★ 卡 2 回合 → 升到指方向");
    checkEq(runPattern(Audience::SENIORS, "xxxx"), 2, "★ 再卡 2 回合 → 升到講答案");
    checkEq(runPattern(Audience::SENIORS, "xxxxxxxx"), 2, "不會超過講答案");

    // 成功之後放鬆
    checkEq(runPattern(Audience::SENIORS, "xxooo"), 0,
            "★ 升到指方向後連續成功 3 次 → 降回輕推");
    checkEq(runPattern(Audience::SENIORS, "xxoo"), 1,
            "  只成功 2 次還不夠——撤除要比加強慢");
}

static void test_不對稱() {
    std::cout << "\n★ 加強要快，撤除要慢\n";
    const auto& s = AudienceProfiles::get(Audience::SENIORS);
    check(s.raise_after_stuck < s.lower_after_success,
          "加強的門檻（" + std::to_string(s.raise_after_stuck) +
          "）低於撤除的門檻（" + std::to_string(s.lower_after_success) + "）");
    std::cout << "         撤太快會造成挫折，而挫折的成本在這個族群高得多\n";
}

static void test_成功保證() {
    std::cout << "\n成功保證\n";
    bool g = false;

    runPattern(Audience::SENIORS, "xxx", &g);
    check(!g, "連續卡 3 回合：尚未觸發");

    runPattern(Audience::SENIORS, "xxxx", &g);
    check(g, "★ 連續卡 4 回合：觸發，直接給答案");

    // 中間成功一次就重置
    runPattern(Audience::SENIORS, "xxxoxxx", &g);
    check(!g, "★ 中間成功一次就重置——計數的是「連續」沒成功");

    runPattern(Audience::KIDS, "xxxxxxxxxx", &g);
    check(!g, "兒童版不啟用成功保證");
}

static void test_疲勞() {
    std::cout << "\n疲勞追蹤\n";
    FatigueTracker f(AudienceProfiles::get(Audience::SENIORS));

    f.addMinutes(20);
    check(!f.shouldSuggestBreak(), "20 分鐘：還不用休息");

    f.addMinutes(10);
    check(f.shouldSuggestBreak(), "★ 30 分鐘：建議休息");
    checkEq(f.overtimeMinutes(), 0, "剛好到門檻，超時 0 分鐘");

    f.addMinutes(15);
    checkEq(f.overtimeMinutes(), 15, "再玩 15 分鐘 → 超時 15 分鐘");

    f.reset();
    check(!f.shouldSuggestBreak(), "reset 之後重新計時");

    FatigueTracker k(AudienceProfiles::get(Audience::KIDS));
    k.addMinutes(60);
    check(!k.shouldSuggestBreak(), "兒童版不主動提醒");
}

static void test_認知對照() {
    std::cout << "\n認知功能對照\n";
    const auto& m = CognitiveMap::all();
    checkEq((int)m.size(), 6, "六招都有對應");

    bool complete = true;
    for (const auto& c : m)
        if (c.name.empty() || c.cognitive_domain.empty() || c.note.empty())
            complete = false;
    check(complete, "每一項都有名稱、認知功能與說明");

    // 長者版只開前四關，所以第五、六招（工作記憶、抑制控制）不在範圍內
    const auto& s = AudienceProfiles::get(Audience::SENIORS);
    const auto* t5 = CognitiveMap::forTechnique(4);
    const auto* t6 = CognitiveMap::forTechnique(5);
    check(t5 && t6, "找得到第五、六招");
    check(s.level_count <= 4,
          "★ 長者版不含工作記憶與抑制控制那兩關——那是最容易造成挫折的");

    check(CognitiveMap::forTechnique(99) == nullptr, "查詢不存在的技巧回傳 nullptr");
}

static void test_reset() {
    std::cout << "\n重置\n";
    AdaptiveDifficulty d(AudienceProfiles::get(Audience::SENIORS));
    for (int i = 0; i < 6; ++i) d.onStuck();
    check(static_cast<int>(d.tier()) > 0, "卡關後提示已加深");
    check(d.needsGuarantee(), "  也觸發了成功保證");

    d.reset();
    checkEq(static_cast<int>(d.tier()), 0, "reset 後回到輕推");
    check(!d.needsGuarantee(), "  成功保證也清除");
    checkEq(d.turnsWithoutSuccess(), 0, "  計數歸零");
}

int main() {
    std::cout << "兩個族群模組 · 單元測試\n";
    std::cout << "════════════════════════════════════════";
    test_兩個族群的設定();
    test_兒童版不受表現影響();
    test_長者版動態調整();
    test_不對稱();
    test_成功保證();
    test_疲勞();
    test_認知對照();
    test_reset();
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "通過 " << g_pass << " 項，失敗 " << g_fail << " 項\n";
    return g_fail == 0 ? 0 : 1;
}
