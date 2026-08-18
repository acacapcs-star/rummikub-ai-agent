#include "battle_parser.h"
#include <iostream>

/* =========================================================================
   demo_battle.cpp

   四組預設關卡本身就是用同一套語法寫的——
   玩家看得懂那些，就知道自訂的怎麼寫。
   ========================================================================= */

// ── 四組預設 ─────────────────────────────────────────────
static const char* PRESET_NO_JOKER = R"(
battle "無 Joker" {
    // 不能靠萬能牌，要真的找出組合
    forbid joker;
}
)";

static const char* PRESET_NO_DRAW = R"(
battle "不准抽牌" {
    // 每一手都要算準，不能靠抽牌拖時間
    require tilesPlayed >= 1;
}
)";

static const char* PRESET_RESHUFFLE_ONLY = R"(
battle "只計重組" {
    // 逼你看整個桌面，而不是只看兩端
    require touchedBoard;
    bonus   tilesPlayed >= 5 : 3;
}
)";

static const char* PRESET_TIMED = R"(
battle "限時" {
    // 從「想得出來」變成「反應得出來」
    limit time = 10;
}
)";

// ── 印出解析結果 ─────────────────────────────────────────
static void tryParse(const char* label, const std::string& src,
                     const PlayerState& player,
                     BattleMode mode = BattleMode::CUSTOM) {
    const char* tag = mode == BattleMode::PRESET ? "[預設]"
                    : mode == BattleMode::STRICT ? "[嚴格]" : "[自訂]";
    std::cout << "  ── " << tag << " " << label << " ──\n";
    std::vector<ParseError> errs;
    auto b = BattleParser::parse(src, player, errs, mode);

    if (!b) {
        for (const auto& e : errs) {
            if (e.is_lock) {
                std::cout << "    第 " << e.line << " 行  " << e.message << "\n";
                std::cout << "              " << e.lock_hint << "\n";
            } else {
                std::cout << "    第 " << e.line << " 行  " << e.message << "\n";
            }
        }
        std::cout << "\n";
        return;
    }

    std::cout << "    解析成功：「" << b->name << "」，共 "
              << b->clauses.size() << " 條規則\n";
    for (const Clause& c : b->clauses) {
        const char* k = c.kind == ClauseKind::REQUIRE ? "require"
                      : c.kind == ClauseKind::FORBID  ? "forbid "
                      : c.kind == ClauseKind::BONUS   ? "bonus  " : "limit  ";
        std::cout << "      " << k << "  "
                  << BattleFields::displayName(c.field);
        if (c.kind == ClauseKind::BONUS)
            std::cout << "  → +" << c.bonus_points << " 分";
        std::cout << "\n";
    }
    std::cout << "\n";
}

// ── 印出檢查結果 ─────────────────────────────────────────
static void tryCheck(const Battle& b, const MoveMetrics& m, const char* label) {
    BattleVerdict v = BattleChecker::check(b, m);
    std::cout << "    " << label << "  →  "
              << (v.allowed ? "✓ 可以出" : "✗ 不准出");
    if (v.bonus > 0) std::cout << "   額外 +" << v.bonus << " 分";
    std::cout << "\n";
    for (const auto& s : v.violations)
        std::cout << "         " << s << "\n";
}

int main() {
    std::cout << "══════════════════════════════════════════════════════\n";
    std::cout << " mini_battle · 玩家自訂的挑戰規則\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    // ═══ 一、預設關卡：不檢查解鎖 ═══
    std::cout << "【預設的四關 · 系統出的題目，任何人都能玩】\n\n";
    PlayerState novice;
    novice.level = 1;
    novice.mastery = { 1, 0, 0, 0, 0, 0 };   // 剛開始，只有第一招 ⭐

    tryParse("無 Joker",   PRESET_NO_JOKER,       novice, BattleMode::PRESET);
    tryParse("不准抽牌",   PRESET_NO_DRAW,        novice, BattleMode::PRESET);
    tryParse("只計重組",   PRESET_RESHUFFLE_ONLY, novice, BattleMode::PRESET);
    tryParse("限時",       PRESET_TIMED,          novice, BattleMode::PRESET);

    std::cout << "  新手在第 1 關就能玩全部四個——\n";
    std::cout << "  如果連玩都要先解鎖，那它們就變成後期內容了。\n\n";

    // ═══ 二、同一份腳本，自訂模式就被擋下 ═══
    std::cout << "【但自己寫的話，就要檢查解鎖】\n\n";
    tryParse("同樣的無 Joker", PRESET_NO_JOKER, novice, BattleMode::CUSTOM);

    // ═══ 三、掌握度分層 ═══
    std::cout << "【掌握度決定能寫哪種子句】\n\n";
    PlayerState learner;
    learner.level = 6;
    learner.mastery = { 3, 3, 1, 2, 2, 2 };   // Joker 只有 ⭐（看答案做出來的）

    tryParse("require joker（要求自己用）", R"(
battle "測試" {
    require joker;
}
)", learner);

    tryParse("forbid joker（禁止自己用）", R"(
battle "測試" {
    forbid joker;
}
)", learner);

    std::cout << "  掌握度升到 ⭐⭐ 之後：\n\n";
    PlayerState better = learner;
    better.mastery[2] = 2;
    tryParse("forbid joker", R"(
battle "測試" {
    forbid joker;
}
)", better);

    // ═══ 四、嚴格模式 ═══
    std::cout << "【嚴格模式 · 給想給自己更高門檻的玩家】\n\n";
    tryParse("同一份腳本，嚴格模式", R"(
battle "測試" {
    forbid joker;
}
)", better, BattleMode::STRICT);

    PlayerState expert2 = better;
    expert2.mastery[2] = 3;
    tryParse("掌握度升到 ⭐⭐⭐ 之後", R"(
battle "測試" {
    forbid joker;
}
)", expert2, BattleMode::STRICT);

    // ═══ 五、打錯字 ═══
    std::cout << "【打錯字時】\n\n";
    PlayerState mid;
    mid.level = 6;
    mid.mastery = { 3, 3, 3, 3, 3, 3 };
    tryParse("寫成 jokr", R"(
battle "測試" {
    forbid jokr;
}
)", mid);

    // ═══ 五、自訂挑戰的實際檢查 ═══
    std::cout << "══════════════════════════════════════════════════════\n";
    std::cout << " 自訂挑戰的實際運作\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    PlayerState expert;
    expert.level = 6;
    expert.mastery = { 3, 3, 3, 3, 3, 3 };

    const char* custom = R"(
battle "純手工大清倉" {
    // 一次出五張以上，不准用 Joker，而且要動到桌面
    require tilesPlayed >= 5;
    forbid  joker;
    require touchedBoard;
    bonus   tilesPlayed >= 8 : 5;
    limit   time = 30;
}
)";

    std::cout << "  腳本：\n";
    std::cout << custom << "\n";

    std::vector<ParseError> errs;
    auto battle = BattleParser::parse(custom, expert, errs);
    if (!battle) {
        for (const auto& e : errs)
            std::cout << "    第 " << e.line << " 行  " << e.message << "\n";
        return 1;
    }
    std::cout << "  解析成功，共 " << battle->clauses.size() << " 條規則\n\n";

    std::cout << "  ── 幾種出牌的判定 ──\n";

    MoveMetrics m1;
    m1.tiles_played = 6; m1.used_joker = false;
    m1.touched_board = true; m1.time_spent = 12;
    tryCheck(*battle, m1, "出 6 張，沒用 Joker，動了桌面，花 12 秒");

    MoveMetrics m2 = m1;
    m2.used_joker = true;
    tryCheck(*battle, m2, "同上但用了 Joker                    ");

    MoveMetrics m3 = m1;
    m3.tiles_played = 3;
    tryCheck(*battle, m3, "只出 3 張                           ");

    MoveMetrics m4 = m1;
    m4.tiles_played = 9;
    tryCheck(*battle, m4, "出 9 張                             ");

    MoveMetrics m5 = m1;
    m5.time_spent = 45;
    tryCheck(*battle, m5, "花了 45 秒                          ");

    MoveMetrics m6;
    m6.tiles_played = 7; m6.used_joker = true;
    m6.touched_board = false; m6.time_spent = 50;
    tryCheck(*battle, m6, "全部都違反                          ");

    std::cout << "\n══════════════════════════════════════════════════════\n";
    std::cout << " 限制是玩家自己設的，所以解法也是他自己找的。\n";
    return 0;
}
