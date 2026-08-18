#pragma once
#include "../coach_engine.h"
#include "../../src/tile.h"
#include "../../src/validator.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/* =========================================================================
   rummikub_domain.h —— 真實的拉密領域

   這個檔案把原本綁在 CognitiveHintEngine 與 TechniqueDetector 裡的
   拉密知識，包成 CoachDomain 的三個方法：

     solve()    找出一個可以出的組合
     hint()     把那個組合翻譯成三種深淺的說法
     classify() 從出牌前後的盤面反推用了哪一招

   **引擎完全沒有被修改。** 抽象層的驗證條件就是這一句。

   六招全部接進來，包括最複雜的大風吹與長龍切斷——
   如果連它們都塞得進同一個介面，那個抽象才算通過壓力測試。
   ========================================================================= */

// ── 狀態：出牌前後的完整盤面 ─────────────────────────────
struct RummiState {
    std::vector<std::vector<Tile*>> board;   // 桌面上的組合
    std::vector<Tile*> hand;                 // 手牌
    bool initial_meld_done = true;           // 是否已破冰
};

// ── 動作：一個可以出的組合 ───────────────────────────────
struct RummiMove {
    enum Kind { ATTACH_RUN, COMPLETE_GROUP, JOKER_FILL,
                INITIAL_MELD, BOARD_RESHUFFLE, RUN_SPLIT } kind = ATTACH_RUN;
    std::vector<Tile*> tiles;      // 要打出去的牌
    int target_set = -1;           // 要接到桌面第幾組（-1 = 新開一組）
    bool at_head = false;          // 接在頭還是尾
    int score = 0;                 // 破冰時的分數
};

// ── 六招的編號，跟 CoachEngine 的 technique 對應 ─────────
enum RummiTechnique {
    RT_ATTACH_RUN = 0,
    RT_COMPLETE_GROUP,
    RT_JOKER_FILL,
    RT_INITIAL_MELD,
    RT_BOARD_RESHUFFLE,
    RT_RUN_SPLIT,
    RT_COUNT
};

class RummikubDomain : public CoachDomain<RummiState, RummiMove> {
public:
    RummikubDomain() { buildLevels(); }

    // ═════════════════════════════════════════════════════
    //  ① solve —— 找出一個可以出的組合
    //
    //  依關卡順序嘗試：先找簡單的，找不到再試複雜的。
    //  這個順序也決定了「教練會先教什麼」。
    // ═════════════════════════════════════════════════════
    std::optional<RummiMove> solve(const RummiState& s) const override {
        // 破冰前只有一條路：湊滿 30 分
        if (!s.initial_meld_done) {
            if (auto m = findInitialMeld(s)) return m;
            return std::nullopt;
        }
        if (auto m = findAttachRun(s))      return m;
        if (auto m = findCompleteGroup(s))  return m;
        if (auto m = findJokerFill(s))      return m;
        if (auto m = findRunSplit(s))       return m;
        if (auto m = findReshuffle(s))      return m;
        return std::nullopt;   // 真的沒有 —— 引擎會誠實告訴使用者
    }

    // ═════════════════════════════════════════════════════
    //  ② hint —— 同一個解，三種深淺
    //
    //  輕推：只說有機會，不透露位置
    //  指方向：給範圍（哪一組、哪個顏色），不給具體是哪張牌
    //  講答案：完整說出要出哪張、接到哪裡
    // ═════════════════════════════════════════════════════
    std::string hint(HintTier tier, const RummiMove& m,
                     const RummiState& s) const override {
        switch (tier) {
            case HintTier::GENTLE_NUDGE:  return nudgeFor(m, s);
            case HintTier::POINT_TO_AREA: return pointFor(m, s);
            case HintTier::REVEAL_MOVE:   return revealFor(m, s);
        }
        return "";
    }

    // ═════════════════════════════════════════════════════
    //  ③ classify —— 從盤面變化反推用了哪一招
    //
    //  原則：寧可漏判，不可誤判。
    //  漏判只是少記一次進度，玩家再用一次就補回來；
    //  誤判會讓玩家在沒學會的情況下被判過關——那是系統在騙他。
    // ═════════════════════════════════════════════════════
    std::vector<int> classify(const RummiState& before,
                              const RummiState& after) const override {
        std::vector<int> found;

        if (!before.initial_meld_done && after.initial_meld_done)
            found.push_back(RT_INITIAL_MELD);

        if (detectAttachRun(before, after))     found.push_back(RT_ATTACH_RUN);
        if (detectCompleteGroup(before, after)) found.push_back(RT_COMPLETE_GROUP);
        if (detectJokerFill(before, after))     found.push_back(RT_JOKER_FILL);

        bool split = detectRunSplit(before, after);
        if (split) found.push_back(RT_RUN_SPLIT);

        // 長龍切斷本質上也動了桌面，但它是更明確的技巧，
        // 已經被認出來的話就不重複記為大風吹。
        if (!split && detectReshuffle(before, after))
            found.push_back(RT_BOARD_RESHUFFLE);

        return found;
    }

    int techniqueCount() const override { return RT_COUNT; }

    std::string techniqueName(int t) const override {
        switch (t) {
            case RT_ATTACH_RUN:      return "接龍頭尾";
            case RT_COMPLETE_GROUP:  return "補第四色";
            case RT_JOKER_FILL:      return "Joker 補缺口";
            case RT_INITIAL_MELD:    return "破冰湊 30 分";
            case RT_BOARD_RESHUFFLE: return "大風吹重組";
            case RT_RUN_SPLIT:       return "長龍切斷";
            default:                 return "未知技巧";
        }
    }

    const std::vector<LevelSpec>& levels() const override { return levels_; }

private:
    std::vector<LevelSpec> levels_;

    // ── 六個關卡：引導從 100% 遞減到 40% ─────────────────
    void buildLevels() {
        levels_ = {
            { 1, RT_ATTACH_RUN,      "接龍頭尾",     100,
              0, 1, 2,  HintTier::REVEAL_MOVE,    3, 1,  -1, HintTier::REVEAL_MOVE },
            { 2, RT_COMPLETE_GROUP,  "補第四色",      88,
              1, 2, 4,  HintTier::REVEAL_MOVE,    3, 1,  -1, HintTier::REVEAL_MOVE },
            { 3, RT_JOKER_FILL,      "Joker 補缺口",  76,
              1, 3, 5,  HintTier::REVEAL_MOVE,    3, 1,  -1, HintTier::REVEAL_MOVE },
            { 4, RT_INITIAL_MELD,    "破冰湊 30 分",  64,
              2, 4, -1, HintTier::POINT_TO_AREA,  3, 2,  10, HintTier::REVEAL_MOVE },
            { 5, RT_BOARD_RESHUFFLE, "大風吹重組",    52,
              2, 5, -1, HintTier::POINT_TO_AREA,  3, 2,  10, HintTier::REVEAL_MOVE },
            { 6, RT_RUN_SPLIT,       "長龍切斷",      40,
              3, -1, -1, HintTier::GENTLE_NUDGE,  3, 3,  12, HintTier::POINT_TO_AREA },
        };
    }

    // ═════════════════════════════════════════════════════
    //  找解：六個 finder
    // ═════════════════════════════════════════════════════

    // ── 接龍頭尾：手牌能接到某條既有 Run 的前或後 ─────────
    std::optional<RummiMove> findAttachRun(const RummiState& s) const {
        for (std::size_t i = 0; i < s.board.size(); ++i) {
            const auto& set = s.board[i];
            if (!Validator::isValidRun(set)) continue;

            int head = firstRealValue(set, true);
            int tail = firstRealValue(set, false);
            Color c;
            if (!runColor(set, c)) continue;

            for (Tile* t : s.hand) {
                if (t->isJoker() || t->getColor() != c) continue;
                if (t->getNumber() == tail + 1 && tail + 1 <= 13)
                    return RummiMove{RummiMove::ATTACH_RUN, {t},
                                     static_cast<int>(i), false, 0};
                if (t->getNumber() == head - 1 && head - 1 >= 1)
                    return RummiMove{RummiMove::ATTACH_RUN, {t},
                                     static_cast<int>(i), true, 0};
            }
        }
        return std::nullopt;
    }

    // ── 補第四色：桌面三張的 Group 缺一色 ────────────────
    std::optional<RummiMove> findCompleteGroup(const RummiState& s) const {
        for (std::size_t i = 0; i < s.board.size(); ++i) {
            const auto& set = s.board[i];
            if (set.size() != 3 || !Validator::isValidGroup(set)) continue;

            int num = -1;
            std::set<int> used;
            for (Tile* t : set) {
                if (t->isJoker()) continue;
                num = t->getNumber();
                used.insert(static_cast<int>(t->getColor()));
            }
            if (num < 0) continue;

            for (Tile* t : s.hand) {
                if (t->isJoker() || t->getNumber() != num) continue;
                if (used.count(static_cast<int>(t->getColor()))) continue;
                return RummiMove{RummiMove::COMPLETE_GROUP, {t},
                                 static_cast<int>(i), false, 0};
            }
        }
        return std::nullopt;
    }

    // ── Joker 補缺口：手上有 Joker，桌面某條 Run 中間缺一號 ──
    std::optional<RummiMove> findJokerFill(const RummiState& s) const {
        Tile* joker = nullptr;
        for (Tile* t : s.hand) if (t->isJoker()) { joker = t; break; }
        if (!joker) return std::nullopt;

        // 找兩張同色、數字差 2 的手牌 → Joker 填中間
        for (std::size_t a = 0; a < s.hand.size(); ++a) {
            for (std::size_t b = a + 1; b < s.hand.size(); ++b) {
                Tile* x = s.hand[a]; Tile* y = s.hand[b];
                if (x->isJoker() || y->isJoker()) continue;
                if (x->getColor() != y->getColor()) continue;
                if (std::abs(x->getNumber() - y->getNumber()) != 2) continue;

                std::vector<Tile*> tiles = (x->getNumber() < y->getNumber())
                                         ? std::vector<Tile*>{x, joker, y}
                                         : std::vector<Tile*>{y, joker, x};
                if (Validator::isValidRun(tiles))
                    return RummiMove{RummiMove::JOKER_FILL, tiles, -1, false, 0};
            }
        }
        return std::nullopt;
    }

    // ── 破冰：純手牌湊出 ≥30 分的合法組合 ────────────────
    std::optional<RummiMove> findInitialMeld(const RummiState& s) const {
        // 先試三張連號的 Run
        for (std::size_t a = 0; a < s.hand.size(); ++a)
            for (std::size_t b = 0; b < s.hand.size(); ++b)
                for (std::size_t c = 0; c < s.hand.size(); ++c) {
                    if (a == b || b == c || a == c) continue;
                    std::vector<Tile*> t = {s.hand[a], s.hand[b], s.hand[c]};
                    if (!Validator::isValidSet(t)) continue;
                    int sc = meldScore(t);
                    if (sc >= 30)
                        return RummiMove{RummiMove::INITIAL_MELD, t, -1, false, sc};
                }
        return std::nullopt;
    }

    // ── 長龍切斷：桌面有 ≥6 張的 Run，手牌能接到切點 ─────
    std::optional<RummiMove> findRunSplit(const RummiState& s) const {
        for (std::size_t i = 0; i < s.board.size(); ++i) {
            const auto& set = s.board[i];
            if (set.size() < 6 || !Validator::isValidRun(set)) continue;
            Color c;
            if (!runColor(set, c)) continue;

            // 切開之後會多出兩個新端點，看手牌接不接得上
            for (std::size_t cut = 3; cut + 3 <= set.size(); ++cut) {
                int leftTail  = valueAt(set, cut - 1);
                int rightHead = valueAt(set, cut);
                for (Tile* t : s.hand) {
                    if (t->isJoker() || t->getColor() != c) continue;
                    if (t->getNumber() == leftTail + 1 || t->getNumber() == rightHead - 1)
                        return RummiMove{RummiMove::RUN_SPLIT, {t},
                                         static_cast<int>(i),
                                         false, static_cast<int>(cut)};
                }
            }
        }
        return std::nullopt;
    }

    // ── 大風吹：桌面重組後能多出接點 ─────────────────────
    //  這裡只做「偵測有沒有機會」，不實際規劃重組路線——
    //  完整的重組是對戰型 AI 的 tryExtendBoard 在做的事，
    //  教練型只需要知道「有機會」就能給提示。
    std::optional<RummiMove> findReshuffle(const RummiState& s) const {
        if (s.board.size() < 2) return std::nullopt;

        // 把桌面所有牌依顏色分堆，看重拼之後有沒有新的接點
        for (Tile* t : s.hand) {
            if (t->isJoker()) continue;
            int adjacent = 0;
            for (const auto& set : s.board)
                for (Tile* b : set)
                    if (!b->isJoker() && b->getColor() == t->getColor() &&
                        std::abs(b->getNumber() - t->getNumber()) == 1)
                        ++adjacent;
            // 同色相鄰的牌散在不同組合裡 → 重組後可能接得上
            if (adjacent >= 2)
                return RummiMove{RummiMove::BOARD_RESHUFFLE, {t}, -1, false, 0};
        }
        return std::nullopt;
    }

    // ═════════════════════════════════════════════════════
    //  三層提示的文字
    // ═════════════════════════════════════════════════════
    std::string nudgeFor(const RummiMove& m, const RummiState&) const {
        switch (m.kind) {
            case RummiMove::ATTACH_RUN:      return "桌上有一條龍接得上你的牌。";
            case RummiMove::COMPLETE_GROUP:  return "有一組差一張就滿了。";
            case RummiMove::JOKER_FILL:      return "你的 Joker 現在派得上用場。";
            case RummiMove::INITIAL_MELD:    return "你手上湊得出 30 分。";
            case RummiMove::BOARD_RESHUFFLE: return "桌面重排一下會多出位置。";
            case RummiMove::RUN_SPLIT:       return "有一條龍長到可以切開。";
        }
        return "";
    }

    std::string pointFor(const RummiMove& m, const RummiState& s) const {
        switch (m.kind) {
            case RummiMove::ATTACH_RUN:
                return std::string("看看第 ") + std::to_string(m.target_set + 1) +
                       " 組的" + (m.at_head ? "前面" : "後面") + "。";
            case RummiMove::COMPLETE_GROUP:
                return "第 " + std::to_string(m.target_set + 1) + " 組缺一個顏色。";
            case RummiMove::JOKER_FILL:
            {
                Color jc;
                std::string cn = runColor(m.tiles, jc) ? colorName(jc) : "";
                return "看看你手上" + cn + "的那幾張。";
            }
            case RummiMove::INITIAL_MELD:
                return "從數字大的牌開始湊。";
            case RummiMove::BOARD_RESHUFFLE:
                return std::string("注意桌上") + colorName(m.tiles.front()->getColor()) +
                       "的那些牌。";
            case RummiMove::RUN_SPLIT:
                return "第 " + std::to_string(m.target_set + 1) +
                       " 組太長了，切開會多出接點。";
        }
        return "";
    }

    std::string revealFor(const RummiMove& m, const RummiState&) const {
        std::ostringstream os;
        switch (m.kind) {
            case RummiMove::ATTACH_RUN:
                os << "把 " << desc(m.tiles.front()) << " 接在第 "
                   << (m.target_set + 1) << " 組的" << (m.at_head ? "頭" : "尾") << "。";
                break;
            case RummiMove::COMPLETE_GROUP:
                os << "把 " << desc(m.tiles.front()) << " 補進第 "
                   << (m.target_set + 1) << " 組。";
                break;
            case RummiMove::JOKER_FILL:
                os << "用 Joker 湊出 ";
                for (Tile* t : m.tiles) os << desc(t) << " ";
                break;
            case RummiMove::INITIAL_MELD:
                os << "出 ";
                for (Tile* t : m.tiles) os << desc(t) << " ";
                os << "，共 " << m.score << " 分，可以破冰。";
                break;
            case RummiMove::BOARD_RESHUFFLE:
                os << "把桌上" << colorName(m.tiles.front()->getColor())
                   << "的牌拆開重排，就能接上 " << desc(m.tiles.front()) << "。";
                break;
            case RummiMove::RUN_SPLIT:
                os << "把第 " << (m.target_set + 1) << " 組從第 "
                   << m.score << " 張後面切開，然後接上 "
                   << desc(m.tiles.front()) << "。";
                break;
        }
        return os.str();
    }

    // ═════════════════════════════════════════════════════
    //  六個偵測器
    // ═════════════════════════════════════════════════════
    static std::vector<Tile*> playedTiles(const RummiState& b, const RummiState& a) {
        std::set<Tile*> after(a.hand.begin(), a.hand.end());
        std::vector<Tile*> played;
        for (Tile* t : b.hand)
            if (after.find(t) == after.end()) played.push_back(t);
        return played;
    }

    static bool isSupersetOf(const std::vector<Tile*>& big,
                             const std::vector<Tile*>& small) {
        std::set<Tile*> s(big.begin(), big.end());
        for (Tile* t : small) if (!s.count(t)) return false;
        return true;
    }

    static bool sameTiles(const std::vector<Tile*>& a, const std::vector<Tile*>& b) {
        if (a.size() != b.size()) return false;
        return std::set<Tile*>(a.begin(), a.end()) ==
               std::set<Tile*>(b.begin(), b.end());
    }

    bool detectAttachRun(const RummiState& b, const RummiState& a) const {
        auto played = playedTiles(b, a);
        if (played.empty()) return false;
        for (const auto& before_set : b.board) {
            if (!Validator::isValidRun(before_set)) continue;
            for (const auto& after_set : a.board) {
                if (after_set.size() <= before_set.size()) continue;
                if (!isSupersetOf(after_set, before_set)) continue;
                if (!Validator::isValidRun(after_set)) continue;
                for (Tile* t : after_set) {
                    bool wasThere = false;
                    for (Tile* x : before_set) if (x == t) { wasThere = true; break; }
                    if (wasThere) continue;
                    for (Tile* p : played) if (p == t) return true;
                }
            }
        }
        return false;
    }

    bool detectCompleteGroup(const RummiState& b, const RummiState& a) const {
        auto played = playedTiles(b, a);
        if (played.empty()) return false;
        for (const auto& before_set : b.board) {
            if (before_set.size() != 3 || !Validator::isValidGroup(before_set)) continue;
            for (const auto& after_set : a.board) {
                if (after_set.size() != 4) continue;
                if (!isSupersetOf(after_set, before_set)) continue;
                if (!Validator::isValidGroup(after_set)) continue;
                for (Tile* t : after_set) {
                    bool wasThere = false;
                    for (Tile* x : before_set) if (x == t) { wasThere = true; break; }
                    if (wasThere) continue;
                    for (Tile* p : played) if (p == t) return true;
                }
            }
        }
        return false;
    }

    // Joker 必須落在中間 —— 接在頭尾只是延長，任何普通牌也做得到，
    // 放中間才是真正「填補缺口」，那才是這一關要教的。
    bool detectJokerFill(const RummiState& b, const RummiState& a) const {
        auto played = playedTiles(b, a);
        bool hasJoker = false;
        for (Tile* t : played) if (t->isJoker()) { hasJoker = true; break; }
        if (!hasJoker) return false;

        for (const auto& set : a.board) {
            if (set.size() < 3 || !Validator::isValidRun(set)) continue;
            for (std::size_t i = 1; i + 1 < set.size(); ++i) {
                if (!set[i]->isJoker()) continue;
                for (Tile* p : played) if (p == set[i]) return true;
            }
        }
        return false;
    }

    bool detectReshuffle(const RummiState& b, const RummiState& a) const {
        if (b.board.empty()) return false;
        for (const auto& before_set : b.board) {
            bool intact = false;
            for (const auto& after_set : a.board)
                if (sameTiles(before_set, after_set) ||
                    isSupersetOf(after_set, before_set)) { intact = true; break; }
            if (!intact) return true;      // 有一組被拆散了
        }
        return false;
    }

    // 最嚴格的一個：必須找到「一分為二」的明確證據。
    // 只是「長 Run 消失」不算，因為那也可能是被併進更長的組合。
    bool detectRunSplit(const RummiState& b, const RummiState& a) const {
        for (const auto& before_set : b.board) {
            if (before_set.size() < 6 || !Validator::isValidRun(before_set)) continue;

            bool stillThere = false;
            for (const auto& s : a.board)
                if (sameTiles(s, before_set)) { stillThere = true; break; }
            if (stillThere) continue;

            std::vector<const std::vector<Tile*>*> carriers;
            for (const auto& after_set : a.board) {
                for (Tile* t : after_set) {
                    bool from = false;
                    for (Tile* x : before_set) if (x == t) { from = true; break; }
                    if (from) { carriers.push_back(&after_set); break; }
                }
            }
            if (carriers.size() < 2) continue;      // 散落在兩組以上才算切開

            bool allRuns = true;
            for (const auto* c : carriers)
                if (!Validator::isValidRun(*c)) { allRuns = false; break; }
            if (allRuns) return true;
        }
        return false;
    }

    // ═════════════════════════════════════════════════════
    //  小工具
    // ═════════════════════════════════════════════════════
    // 回傳這條 Run 的顏色。全是 Joker 時回傳 false（沒有可判定的顏色）
    static bool runColor(const std::vector<Tile*>& set, Color& out) {
        for (Tile* t : set)
            if (!t->isJoker()) { out = t->getColor(); return true; }
        return false;
    }

    // 取得 Run 中某個位置的實際數值（Joker 也算得出來）
    static int valueAt(const std::vector<Tile*>& set, std::size_t idx) {
        for (std::size_t i = 0; i < set.size(); ++i) {
            if (set[i]->isJoker()) continue;
            return set[i]->getNumber() + static_cast<int>(idx) - static_cast<int>(i);
        }
        return -1;
    }

    static int firstRealValue(const std::vector<Tile*>& set, bool head) {
        return valueAt(set, head ? 0 : set.size() - 1);
    }

    // 破冰計分直接用 Validator 現成的實作。
    // 它已經處理了「Joker 算它代表的值，不是固定 30」這件事——
    // 否則單靠一張 Joker 就能破冰，門檻形同虛設。
    static int meldScore(const std::vector<Tile*>& set) {
        return Validator::calculateInitialMeldScore(set);
    }

    static std::string colorName(Color c) {
        switch (c) {
            case Color::RED:    return "紅色";
            case Color::BLUE:   return "藍色";
            case Color::BLACK:  return "黑色";
            case Color::YELLOW: return "黃色";
        }
        return "";
    }

    static std::string desc(Tile* t) {
        if (t->isJoker()) return "Joker";
        return colorName(t->getColor()) + std::to_string(t->getNumber());
    }
};
