#pragma once
#include <string>
#include <vector>
#include <optional>

/* =========================================================================
   mini_battle.h —— 玩家自訂的挑戰規則

   為什麼要這個：
     六個關卡教的是「怎麼做」，但真正的熟練是「在限制下還做得到」。
     一個玩家如果能在「不准用 Joker」的條件下打完一局，
     那比他在無限制下打十局更能證明他懂。

     而且限制由玩家自己設定——那跟 Private 招數是同一個主權：
     **系統不規定你該挑戰什麼，你自己出題給自己。**

   語法：

     battle "我的挑戰" {
         require tilesPlayed >= 3;
         forbid  joker;
         bonus   touchedBoard : 2;
         limit   time = 10;
     }

   四種子句：

     require  必須成立，否則不准出
     forbid   不准發生，否則不准出
     bonus    成立時額外加分（不影響能不能出）
     limit    數值上限，超過就算違規

   欄位有解鎖門檻——沒學過的招數，連拿來當條件都不行。
   這讓「過關」不只是拿獎章，而是真的多一個能用的東西。
   ========================================================================= */

// ── 可用的欄位 ───────────────────────────────────────────
enum class BattleField {
    TILES_PLAYED,    // 這一手出幾張
    HAND_SIZE,       // 手上剩幾張
    TIME_SPENT,      // 這一手花幾秒
    SETS_AFFECTED,   // 影響幾個組合
    USED_JOKER,      // 有沒有用 Joker（0/1）
    MELD_SCORE,      // 這一手的分數
    TOUCHED_BOARD,   // 有沒有動桌面（0/1）
    RUN_SPLIT,       // 有沒有切斷長龍（0/1）
    FIELD_COUNT
};

// ── 子句 ─────────────────────────────────────────────────
enum class ClauseKind { REQUIRE, FORBID, BONUS, LIMIT };
enum class CompareOp  { GE, LE, EQ, NE, GT, LT };

struct Clause {
    ClauseKind kind;
    BattleField field;
    CompareOp op = CompareOp::NE;
    int value = 0;          // forbid 不需要，bonus 用來當加分值
    int bonus_points = 0;   // 只有 BONUS 用
};

// ── 一組挑戰 ─────────────────────────────────────────────
struct Battle {
    std::string name;
    std::vector<Clause> clauses;
};

// ── 一手棋的量測值（給規則檢查用）───────────────────────
struct MoveMetrics {
    int  tiles_played   = 0;
    int  hand_size      = 0;
    int  time_spent     = 0;
    int  sets_affected  = 0;
    bool used_joker     = false;
    int  meld_score     = 0;
    bool touched_board  = false;
    bool run_split      = false;

    int get(BattleField f) const {
        switch (f) {
            case BattleField::TILES_PLAYED:   return tiles_played;
            case BattleField::HAND_SIZE:      return hand_size;
            case BattleField::TIME_SPENT:     return time_spent;
            case BattleField::SETS_AFFECTED:  return sets_affected;
            case BattleField::USED_JOKER:     return used_joker ? 1 : 0;
            case BattleField::MELD_SCORE:     return meld_score;
            case BattleField::TOUCHED_BOARD:  return touched_board ? 1 : 0;
            case BattleField::RUN_SPLIT:      return run_split ? 1 : 0;
            default:                          return 0;
        }
    }
};

// ── 檢查結果 ─────────────────────────────────────────────
struct BattleVerdict {
    bool allowed = true;            // 這一手能不能出
    int  bonus = 0;                 // 額外得分
    std::vector<std::string> violations;   // 違反了哪些規則（給玩家看）
};

// ── 欄位的解鎖與顯示 ─────────────────────────────────────
class BattleFields {
public:
    // 解鎖需要通過第幾關
    static int unlockLevel(BattleField f) {
        switch (f) {
            case BattleField::TILES_PLAYED:
            case BattleField::HAND_SIZE:
            case BattleField::TIME_SPENT:     return 1;
            case BattleField::SETS_AFFECTED:  return 2;
            case BattleField::USED_JOKER:     return 3;
            case BattleField::MELD_SCORE:     return 4;
            case BattleField::TOUCHED_BOARD:  return 5;
            case BattleField::RUN_SPLIT:      return 6;
            default:                          return 99;
        }
    }

    /* 掌握度決定「能用在哪種子句」。

       require 是「要求自己用這招」——會了就行。
       forbid  是「禁止自己用這招」——那更難，
               因為你得熟到能繞開它，用別的方式達成目的。

       所以：
         COPIED      ⭐   只能 require
         PROMPTED    ⭐⭐  可以 require / limit / bonus
         DISCOVERED  ⭐⭐⭐ 才能 forbid                             */
    static int masteryNeeded(ClauseKind k, bool strict = false) {
        switch (k) {
            case ClauseKind::REQUIRE: return 1;              // COPIED
            case ClauseKind::LIMIT:
            case ClauseKind::BONUS:   return 2;              // PROMPTED
            case ClauseKind::FORBID:  return strict ? 3 : 2; // 見下方說明
        }
        return 3;
    }

    // 這個欄位對應到哪一招（用來查掌握度）；-1 表示不對應任何技巧
    static int techniqueOf(BattleField f) {
        switch (f) {
            case BattleField::USED_JOKER:    return 2;   // RT_JOKER_FILL
            case BattleField::MELD_SCORE:    return 3;   // RT_INITIAL_MELD
            case BattleField::TOUCHED_BOARD: return 4;   // RT_BOARD_RESHUFFLE
            case BattleField::RUN_SPLIT:     return 5;   // RT_RUN_SPLIT
            default:                         return -1;  // 不需要掌握度
        }
    }

    static std::string name(BattleField f) {
        switch (f) {
            case BattleField::TILES_PLAYED:   return "tilesPlayed";
            case BattleField::HAND_SIZE:      return "handSize";
            case BattleField::TIME_SPENT:     return "time";
            case BattleField::SETS_AFFECTED:  return "setsAffected";
            case BattleField::USED_JOKER:     return "joker";
            case BattleField::MELD_SCORE:     return "meldScore";
            case BattleField::TOUCHED_BOARD:  return "touchedBoard";
            case BattleField::RUN_SPLIT:      return "runSplit";
            default:                          return "?";
        }
    }

    static std::string displayName(BattleField f) {
        switch (f) {
            case BattleField::TILES_PLAYED:   return "出牌張數";
            case BattleField::HAND_SIZE:      return "手牌數";
            case BattleField::TIME_SPENT:     return "花費秒數";
            case BattleField::SETS_AFFECTED:  return "影響組數";
            case BattleField::USED_JOKER:     return "使用 Joker";
            case BattleField::MELD_SCORE:     return "這手的分數";
            case BattleField::TOUCHED_BOARD:  return "動到桌面";
            case BattleField::RUN_SPLIT:      return "切斷長龍";
            default:                          return "?";
        }
    }

    static std::optional<BattleField> parse(const std::string& s) {
        for (int i = 0; i < (int)BattleField::FIELD_COUNT; ++i) {
            auto f = static_cast<BattleField>(i);
            if (name(f) == s) return f;
        }
        return std::nullopt;
    }

    // 是不是 0/1 的旗標欄位（forbid 只對這種有意義）
    static bool isFlag(BattleField f) {
        return f == BattleField::USED_JOKER ||
               f == BattleField::TOUCHED_BOARD ||
               f == BattleField::RUN_SPLIT;
    }
};

/* ── 三種嚴格程度 ────────────────────────────────────────

   PRESET   系統出的題目——不檢查解鎖，任何人都能玩。
            預設的四關是設計者精心挑過的，本來就該讓新手接觸；
            如果連玩都要先解鎖，那它們就變成後期內容了。

   CUSTOM   玩家自己出題——檢查關卡解鎖與掌握度。
            forbid 需要 ⭐⭐（PROMPTED）。

   STRICT   自訂的嚴格模式——forbid 需要 ⭐⭐⭐（DISCOVERED）。
            「禁止自己用某招」比「要求自己用」難，
            因為你得熟到能繞開它、用別的方式達成目的。
            這個模式留給想給自己更高門檻的玩家。
   ──────────────────────────────────────────────────────── */
enum class BattleMode { PRESET, CUSTOM, STRICT };

// ── 解析錯誤 ─────────────────────────────────────────────
struct ParseError {
    int line = 0;
    std::string message;
    bool is_lock = false;        // 是不是「尚未解鎖」造成的
    std::string lock_hint;       // 「需要通過第 3 關」
};
