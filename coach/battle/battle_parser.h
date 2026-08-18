#pragma once
#include "mini_battle.h"
#include <algorithm>
#include <cctype>
#include <sstream>

/* =========================================================================
   battle_parser.h —— 把腳本文字變成 Battle 結構

   解析分兩步：
     ① tokenize   把文字切成 token（關鍵字、識別字、數字、符號）
     ② parse      按語法組成 Battle

   為什麼不用正規表示式一次解決：
     因為錯誤訊息會很爛。玩家寫錯的時候，
     「第 3 行：'jokr' 不是有效的欄位，你是指 'joker' 嗎？」
     比「解析失敗」有用得多。

   解析時同時檢查解鎖狀態——一個沒學過的欄位，
   連寫進腳本都不行，而且錯誤訊息要說清楚需要通過第幾關。
   ========================================================================= */

// ── 玩家目前的能力（解析時要查）─────────────────────────
struct PlayerState {
    int level = 1;                       // 目前在第幾關
    std::vector<int> mastery;            // 每一招的掌握度 0~3

    int masteryOf(int technique) const {
        if (technique < 0) return 3;     // 不對應技巧的欄位，視為全解鎖
        if (technique >= (int)mastery.size()) return 0;
        return mastery[technique];
    }
};

// ── Token ────────────────────────────────────────────────
struct Token {
    enum Type { WORD, NUMBER, STRING, SYMBOL, END } type;
    std::string text;
    int value = 0;
    int line = 1;
};

class BattleParser {
public:
    // 解析成功回傳 Battle，失敗時 errors 非空
    static std::optional<Battle> parse(const std::string& src,
                                       const PlayerState& player,
                                       std::vector<ParseError>& errors,
                                       BattleMode mode = BattleMode::CUSTOM) {
        std::vector<Token> toks = tokenize(src, errors);
        if (!errors.empty()) return std::nullopt;

        std::size_t p = 0;
        Battle b;

        // battle "名字" {
        if (!expectWord(toks, p, "battle", errors)) return std::nullopt;
        if (p >= toks.size() || toks[p].type != Token::STRING) {
            errors.push_back({ line(toks, p), "battle 後面要接一個用引號括起來的名字" });
            return std::nullopt;
        }
        b.name = toks[p++].text;
        if (!expectSymbol(toks, p, "{", errors)) return std::nullopt;

        // 子句
        while (p < toks.size() && !(toks[p].type == Token::SYMBOL && toks[p].text == "}")) {
            if (!parseClause(toks, p, player, b, errors, mode)) {
                // 錯誤已記錄，跳到下一個分號繼續，才能一次回報多個錯誤
                while (p < toks.size() &&
                       !(toks[p].type == Token::SYMBOL && toks[p].text == ";")) ++p;
                if (p < toks.size()) ++p;
            }
        }

        if (!expectSymbol(toks, p, "}", errors)) return std::nullopt;
        if (!errors.empty()) return std::nullopt;

        if (b.clauses.empty()) {
            errors.push_back({ 1, "這個挑戰沒有任何規則——至少要寫一條" });
            return std::nullopt;
        }
        return b;
    }

private:
    static int line(const std::vector<Token>& t, std::size_t p) {
        return p < t.size() ? t[p].line : (t.empty() ? 1 : t.back().line);
    }

    // ── ① 切詞 ───────────────────────────────────────────
    static std::vector<Token> tokenize(const std::string& s,
                                       std::vector<ParseError>& errors) {
        std::vector<Token> out;
        int ln = 1;
        std::size_t i = 0;

        while (i < s.size()) {
            char c = s[i];

            if (c == '\n') { ++ln; ++i; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

            // 註解：// 到行尾
            if (c == '/' && i + 1 < s.size() && s[i+1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }

            // 字串
            if (c == '"') {
                std::string val;
                ++i;
                while (i < s.size() && s[i] != '"') {
                    if (s[i] == '\n') ++ln;
                    val += s[i++];
                }
                if (i >= s.size()) {
                    errors.push_back({ ln, "字串沒有結尾的引號" });
                    return out;
                }
                ++i;
                out.push_back({ Token::STRING, val, 0, ln });
                continue;
            }

            // 數字
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::string num;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    num += s[i++];
                out.push_back({ Token::NUMBER, num, std::stoi(num), ln });
                continue;
            }

            // 識別字
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string w;
                while (i < s.size() &&
                       (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
                    w += s[i++];
                out.push_back({ Token::WORD, w, 0, ln });
                continue;
            }

            // 兩字元的比較運算子
            if (i + 1 < s.size()) {
                std::string two = s.substr(i, 2);
                if (two == ">=" || two == "<=" || two == "==" || two == "!=") {
                    out.push_back({ Token::SYMBOL, two, 0, ln });
                    i += 2;
                    continue;
                }
            }

            // 單字元符號
            if (std::string("{};:=<>").find(c) != std::string::npos) {
                out.push_back({ Token::SYMBOL, std::string(1, c), 0, ln });
                ++i;
                continue;
            }

            errors.push_back({ ln, std::string("看不懂的符號 '") + c + "'" });
            return out;
        }

        out.push_back({ Token::END, "", 0, ln });
        return out;
    }

    static bool expectWord(const std::vector<Token>& t, std::size_t& p,
                           const std::string& w, std::vector<ParseError>& e) {
        if (p < t.size() && t[p].type == Token::WORD && t[p].text == w) { ++p; return true; }
        e.push_back({ line(t, p), "這裡應該是 '" + w + "'" });
        return false;
    }

    static bool expectSymbol(const std::vector<Token>& t, std::size_t& p,
                             const std::string& s, std::vector<ParseError>& e) {
        if (p < t.size() && t[p].type == Token::SYMBOL && t[p].text == s) { ++p; return true; }
        e.push_back({ line(t, p), "這裡應該是 '" + s + "'" });
        return false;
    }

    // ── 拼字建議：找最接近的欄位名 ───────────────────────
    // 玩家打錯字的時候，「你是指 joker 嗎」比「無效欄位」有用得多。
    static std::string suggest(const std::string& typo) {
        std::string best;
        int bestDist = 99;
        for (int i = 0; i < (int)BattleField::FIELD_COUNT; ++i) {
            std::string cand = BattleFields::name(static_cast<BattleField>(i));
            int d = editDistance(typo, cand);
            if (d < bestDist) { bestDist = d; best = cand; }
        }
        return bestDist <= 3 ? best : "";
    }

    static int editDistance(const std::string& a, const std::string& b) {
        std::vector<std::vector<int>> d(a.size()+1, std::vector<int>(b.size()+1));
        for (std::size_t i = 0; i <= a.size(); ++i) d[i][0] = (int)i;
        for (std::size_t j = 0; j <= b.size(); ++j) d[0][j] = (int)j;
        for (std::size_t i = 1; i <= a.size(); ++i)
            for (std::size_t j = 1; j <= b.size(); ++j)
                d[i][j] = std::min({ d[i-1][j] + 1, d[i][j-1] + 1,
                                     d[i-1][j-1] + (a[i-1] == b[j-1] ? 0 : 1) });
        return d[a.size()][b.size()];
    }

    // ── ② 解析一條子句 ───────────────────────────────────
    static bool parseClause(const std::vector<Token>& t, std::size_t& p,
                            const PlayerState& player, Battle& b,
                            std::vector<ParseError>& errors,
                            BattleMode mode) {
        if (p >= t.size() || t[p].type != Token::WORD) {
            errors.push_back({ line(t, p), "這裡應該是 require / forbid / bonus / limit" });
            return false;
        }

        std::string kw = t[p].text;
        ClauseKind kind;
        if      (kw == "require") kind = ClauseKind::REQUIRE;
        else if (kw == "forbid")  kind = ClauseKind::FORBID;
        else if (kw == "bonus")   kind = ClauseKind::BONUS;
        else if (kw == "limit")   kind = ClauseKind::LIMIT;
        else {
            errors.push_back({ t[p].line,
                "'" + kw + "' 不是有效的關鍵字，可用的是 require / forbid / bonus / limit" });
            return false;
        }
        int kwLine = t[p].line;
        ++p;

        // 欄位
        if (p >= t.size() || t[p].type != Token::WORD) {
            errors.push_back({ line(t, p), kw + " 後面要接一個欄位名稱" });
            return false;
        }
        std::string fname = t[p].text;
        auto fopt = BattleFields::parse(fname);
        if (!fopt) {
            std::string hint = suggest(fname);
            errors.push_back({ t[p].line,
                "'" + fname + "' 不是有效的欄位" +
                (hint.empty() ? "" : "，你是指 '" + hint + "' 嗎？") });
            return false;
        }
        BattleField field = *fopt;
        int fieldLine = t[p].line;
        ++p;

        // ── 解鎖檢查 ─────────────────────────────────────
        // PRESET 是系統出的題目，不檢查——那四關本來就該讓新手玩得到。
        int need = BattleFields::unlockLevel(field);
        if (mode != BattleMode::PRESET && player.level < need) {
            ParseError e;
            e.line = fieldLine;
            e.is_lock = true;
            e.message = "🔒 '" + fname + "' 尚未解鎖";
            e.lock_hint = "需要通過第 " + std::to_string(need) + " 關（" +
                          BattleFields::displayName(field) + "）";
            errors.push_back(e);
            return false;
        }

        // ── 掌握度檢查 ───────────────────────────────────
        // require 是「要求自己用」，forbid 是「禁止自己用」——後者更難，
        // 因為得熟到能繞開它、用別的方式達成目的。
        int tech = BattleFields::techniqueOf(field);
        int haveMastery = player.masteryOf(tech);
        int needMastery = BattleFields::masteryNeeded(
                              kind, mode == BattleMode::STRICT);
        if (mode != BattleMode::PRESET && tech >= 0 && haveMastery < needMastery) {
            static const char* STARS[] = { "🔒", "⭐", "⭐⭐", "⭐⭐⭐" };
            ParseError e;
            e.line = kwLine;
            e.is_lock = true;
            e.message = "🔒 '" + kw + " " + fname + "' 需要更高的掌握度";
            e.lock_hint = std::string("目前 ") + STARS[haveMastery] +
                          "，需要 " + STARS[needMastery] +
                          (kind == ClauseKind::FORBID
                             ? "（禁止自己用某招，要熟到能繞開它）" : "") +
                          (mode == BattleMode::STRICT
                             ? "　※ 嚴格模式" : "");
            errors.push_back(e);
            return false;
        }

        Clause c;
        c.kind = kind;
        c.field = field;

        // ── 依關鍵字解析後面的部分 ───────────────────────
        if (kind == ClauseKind::FORBID) {
            // forbid joker;  ← 旗標欄位不需要運算子
            if (BattleFields::isFlag(field)) {
                c.op = CompareOp::EQ;
                c.value = 0;                     // 必須等於 0（沒發生）
            } else {
                // forbid tilesPlayed >= 5;  ← 非旗標欄位要給條件
                if (!parseCompare(t, p, c, errors)) return false;
            }
        }
        else if (kind == ClauseKind::BONUS) {
            // bonus touchedBoard : 2;
            if (!BattleFields::isFlag(field)) {
                if (!parseCompare(t, p, c, errors)) return false;
            } else {
                c.op = CompareOp::EQ;
                c.value = 1;
            }
            if (!expectSymbol(t, p, ":", errors)) return false;
            if (p >= t.size() || t[p].type != Token::NUMBER) {
                errors.push_back({ line(t, p), "bonus 的 ':' 後面要接加分的數字" });
                return false;
            }
            c.bonus_points = t[p++].value;
        }
        else if (kind == ClauseKind::LIMIT) {
            // limit time = 10;
            if (!expectSymbol(t, p, "=", errors)) return false;
            if (p >= t.size() || t[p].type != Token::NUMBER) {
                errors.push_back({ line(t, p), "limit 的 '=' 後面要接一個數字" });
                return false;
            }
            c.op = CompareOp::LE;
            c.value = t[p++].value;
        }
        else {  // REQUIRE
            if (BattleFields::isFlag(field) &&
                (p >= t.size() || t[p].type == Token::SYMBOL) &&
                (p < t.size() && t[p].text == ";")) {
                // require touchedBoard;  ← 旗標欄位可省略運算子
                c.op = CompareOp::EQ;
                c.value = 1;
            } else {
                if (!parseCompare(t, p, c, errors)) return false;
            }
        }

        if (!expectSymbol(t, p, ";", errors)) return false;
        b.clauses.push_back(c);
        return true;
    }

    static bool parseCompare(const std::vector<Token>& t, std::size_t& p,
                             Clause& c, std::vector<ParseError>& errors) {
        if (p >= t.size() || t[p].type != Token::SYMBOL) {
            errors.push_back({ line(t, p), "這裡應該是比較運算子（>= <= == != > <）" });
            return false;
        }
        const std::string& op = t[p].text;
        if      (op == ">=") c.op = CompareOp::GE;
        else if (op == "<=") c.op = CompareOp::LE;
        else if (op == "==") c.op = CompareOp::EQ;
        else if (op == "!=") c.op = CompareOp::NE;
        else if (op == ">")  c.op = CompareOp::GT;
        else if (op == "<")  c.op = CompareOp::LT;
        else {
            errors.push_back({ t[p].line, "'" + op + "' 不是比較運算子" });
            return false;
        }
        ++p;

        if (p >= t.size() || t[p].type != Token::NUMBER) {
            errors.push_back({ line(t, p), "比較運算子後面要接一個數字" });
            return false;
        }
        c.value = t[p++].value;
        return true;
    }
};

/* =========================================================================
   規則檢查
   ========================================================================= */
class BattleChecker {
public:
    static BattleVerdict check(const Battle& b, const MoveMetrics& m) {
        BattleVerdict v;
        for (const Clause& c : b.clauses) {
            bool holds = compare(m.get(c.field), c.op, c.value);
            switch (c.kind) {
                case ClauseKind::REQUIRE:
                    if (!holds) {
                        v.allowed = false;
                        v.violations.push_back(
                            "違反 require：" + BattleFields::displayName(c.field) +
                            " 必須 " + opName(c.op) + " " + std::to_string(c.value) +
                            "（實際 " + std::to_string(m.get(c.field)) + "）");
                    }
                    break;
                case ClauseKind::FORBID:
                    // forbid 的 clause 存的是「允許的狀態」，不成立就代表違規
                    if (!holds) {
                        v.allowed = false;
                        v.violations.push_back(
                            "違反 forbid：這一手用到了 " +
                            BattleFields::displayName(c.field));
                    }
                    break;
                case ClauseKind::LIMIT:
                    if (!holds) {
                        v.allowed = false;
                        v.violations.push_back(
                            "超過 limit：" + BattleFields::displayName(c.field) +
                            " 上限 " + std::to_string(c.value) +
                            "（實際 " + std::to_string(m.get(c.field)) + "）");
                    }
                    break;
                case ClauseKind::BONUS:
                    if (holds) v.bonus += c.bonus_points;
                    break;
            }
        }
        return v;
    }

private:
    static bool compare(int lhs, CompareOp op, int rhs) {
        switch (op) {
            case CompareOp::GE: return lhs >= rhs;
            case CompareOp::LE: return lhs <= rhs;
            case CompareOp::EQ: return lhs == rhs;
            case CompareOp::NE: return lhs != rhs;
            case CompareOp::GT: return lhs >  rhs;
            case CompareOp::LT: return lhs <  rhs;
        }
        return false;
    }

    static std::string opName(CompareOp op) {
        switch (op) {
            case CompareOp::GE: return "≥";
            case CompareOp::LE: return "≤";
            case CompareOp::EQ: return "=";
            case CompareOp::NE: return "≠";
            case CompareOp::GT: return ">";
            case CompareOp::LT: return "<";
        }
        return "?";
    }
};
