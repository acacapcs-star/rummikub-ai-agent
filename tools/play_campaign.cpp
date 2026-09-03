/* =========================================================================
   play_campaign.cpp —— 可以真的玩的六關

   這支程式跟現有的三個東西是不同的層次：

     demo_rummikub.cpp      展示：把六關的設定印出來給人看
     learner_simulation.cpp 模擬：AI 學習者跑 2000 次，驗證門檻時序
     play_campaign.cpp      **玩**：人坐下來，一步一步走完六關

   前兩個回答的是「這個設計長什麼樣」和「這個設計合不合理」。
   這一個回答的是「一個人實際用起來是什麼感覺」——那是前兩者都答不了的。

   為什麼需要這一支：
     模擬跑出 L6 完全新手平均卡 12 回合，那是一個數字。
     但「卡 12 回合的時候，坐在螢幕前面是什麼感覺」——
     那件事只有真的坐下來玩才知道。

   跟主專案零耦合：不進 CMakeLists，自己一行就能編。

   編譯：
     g++ -std=c++17 -I src -I coach \
         tools/play_campaign.cpp src/tile.cpp src/validator.cpp \
         -o play_campaign && ./play_campaign
   ========================================================================= */

#include "../coach/coach_engine.h"
#include "../coach/domains/rummikub_domain.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <iomanip>
#include <cctype>

// ── 造牌：所有 Tile 存在單一 pool ────────────────────────
namespace {

std::vector<Tile*> g_pool;
int g_id = 0;

Tile* T(int n, Color c) {
    Tile* t = new Tile(g_id++, n, c);
    g_pool.push_back(t);
    return t;
}
Tile* J() {
    Tile* t = new Tile(g_id++);
    g_pool.push_back(t);
    return t;
}
void cleanup() {
    for (Tile* t : g_pool) delete t;
    g_pool.clear();
}

const char* colourName(Color c) {
    switch (c) {
        case Color::RED:    return "紅";
        case Color::YELLOW: return "黃";
        case Color::BLUE:   return "藍";
        case Color::BLACK:  return "黑";
        default:            return "?";
    }
}

std::string tileStr(Tile* t) {
    if (!t) return "??";
    if (t->isJoker()) return "🃏";
    std::ostringstream o;
    o << colourName(t->getColor()) << t->getNumber();
    return o.str();
}

void printState(const RummiState& s) {
    std::cout << "\n  桌面";
    if (s.board.empty()) {
        std::cout << "  （空）";
    } else {
        for (size_t i = 0; i < s.board.size(); ++i) {
            std::cout << "\n    [" << (i + 1) << "] ";
            for (Tile* t : s.board[i]) std::cout << tileStr(t) << " ";
        }
    }
    std::cout << "\n\n  手牌  ";
    for (size_t i = 0; i < s.hand.size(); ++i)
        std::cout << "(" << (i + 1) << ")" << tileStr(s.hand[i]) << " ";
    if (!s.initial_meld_done) std::cout << "\n        （尚未破冰，第一次出牌要自己湊滿 30 分）";
    std::cout << "\n";
}

const char* tierLabel(HintTier t) {
    switch (t) {
        case HintTier::GENTLE_NUDGE:  return "輕推";
        case HintTier::POINT_TO_AREA: return "指方向";
        case HintTier::REVEAL_MOVE:   return "講答案";
    }
    return "?";
}

// ── 每一關的初始局面 ─────────────────────────────────────
//
// 刻意設計成「有解但不明顯」——太明顯就沒有卡關可言，
// 太難則會一直觸發保底，兩種都測不出引導的節奏。
//
// ── 每一關的局面 ─────────────────────────────────────────
//
// 同一招要練三次，但三次不能是同一題——不然使用者只是背答案，
// 而 required_unassisted 想量的「自己找到」就失去意義。
//
// 所以每一關準備三個不同的局面：同一個技巧，不同的牌、不同的位置。
// variant 由已經用出幾次決定，練第幾次就出第幾題。
//
RummiState makeLevelState(int level, int variant) {
    RummiState s;
    int v = variant % 3;

    switch (level) {
        case 1:  // 接龍頭尾
            if (v == 0) {
                s.board = {{T(4, Color::RED), T(5, Color::RED), T(6, Color::RED)}};
                s.hand  = {T(3, Color::RED), T(9, Color::YELLOW), T(11, Color::BLUE)};
            } else if (v == 1) {
                // 這次要接在尾巴，不是頭
                s.board = {{T(8, Color::BLUE), T(9, Color::BLUE), T(10, Color::BLUE)}};
                s.hand  = {T(2, Color::BLACK), T(11, Color::BLUE), T(6, Color::YELLOW)};
            } else {
                // 桌面有兩組，要挑對的那一組
                s.board = {{T(3, Color::YELLOW), T(4, Color::YELLOW), T(5, Color::YELLOW)},
                           {T(10, Color::BLACK), T(11, Color::BLACK), T(12, Color::BLACK)}};
                s.hand  = {T(13, Color::BLACK), T(1, Color::RED), T(7, Color::BLUE)};
            }
            break;

        case 2:  // 補第四色
            if (v == 0) {
                s.board = {{T(7, Color::RED), T(7, Color::YELLOW), T(7, Color::BLUE)}};
                s.hand  = {T(7, Color::BLACK), T(2, Color::RED), T(12, Color::BLUE)};
            } else if (v == 1) {
                s.board = {{T(11, Color::BLACK), T(11, Color::BLUE), T(11, Color::YELLOW)}};
                s.hand  = {T(5, Color::RED), T(11, Color::RED), T(3, Color::BLUE)};
            } else {
                // 手上有兩張同數字，只有一張是缺的那個顏色
                s.board = {{T(4, Color::RED), T(4, Color::BLACK), T(4, Color::BLUE)}};
                s.hand  = {T(4, Color::YELLOW), T(9, Color::RED), T(6, Color::BLACK)};
            }
            break;

        case 3:  // Joker 補缺口
            if (v == 0) {
                s.board = {};
                s.hand  = {T(5, Color::RED), T(7, Color::RED), J(),
                           T(1, Color::BLACK), T(13, Color::YELLOW)};
            } else if (v == 1) {
                s.board = {};
                s.hand  = {T(9, Color::BLUE), T(11, Color::BLUE), J(),
                           T(2, Color::YELLOW), T(6, Color::RED)};
            } else {
                s.board = {};
                s.hand  = {T(2, Color::BLACK), T(4, Color::BLACK), J(),
                           T(8, Color::RED), T(10, Color::YELLOW)};
            }
            break;

        case 4:  // 破冰湊 30 分
            s.initial_meld_done = false;
            s.board = {};
            if (v == 0) {
                s.hand = {T(11, Color::RED), T(12, Color::RED), T(13, Color::RED),
                          T(2, Color::BLUE), T(4, Color::YELLOW)};
            } else if (v == 1) {
                s.hand = {T(10, Color::BLUE), T(11, Color::BLUE), T(12, Color::BLUE),
                          T(3, Color::RED), T(1, Color::BLACK)};
            } else {
                // 剛好 30 分，一分都不能少
                s.hand = {T(9, Color::BLACK), T(10, Color::BLACK), T(11, Color::BLACK),
                          T(5, Color::YELLOW), T(2, Color::RED)};
            }
            break;

        case 5:  // 大風吹重組
            // 這一關最難設計，而且第一版設計錯了。
            //
            // 錯誤一：手牌裡有能直接接上桌面的牌 → solve() 的優先序讓
            //         接龍先被找到，重組永遠輪不到。
            // 錯誤二：為了避開錯誤一，把手牌砍到只剩一張 →
            //         沒有選擇就沒有判斷，打 1 就過，引導失去意義。
            //
            // 正解是兩個條件同時滿足：
            //   · 手上要有好幾張，才需要「挑」
            //   · 但只有一張能構成重組，其餘都是死牌（接不上任何東西）
            if (v == 0) {
                s.board = {{T(5, Color::BLACK), T(5, Color::RED), T(5, Color::BLUE)},
                           {T(7, Color::BLACK), T(7, Color::RED), T(7, Color::BLUE)}};
                s.hand  = {T(12, Color::YELLOW), T(6, Color::BLACK), T(2, Color::RED)};
            } else if (v == 1) {
                s.board = {{T(9, Color::RED), T(9, Color::YELLOW), T(9, Color::BLUE)},
                           {T(11, Color::RED), T(11, Color::YELLOW), T(11, Color::BLUE)}};
                s.hand  = {T(3, Color::BLACK), T(13, Color::BLUE), T(10, Color::RED)};
            } else {
                s.board = {{T(2, Color::BLUE), T(2, Color::BLACK), T(2, Color::YELLOW)},
                           {T(4, Color::BLUE), T(4, Color::BLACK), T(4, Color::YELLOW)}};
                s.hand  = {T(3, Color::BLUE), T(8, Color::RED), T(11, Color::YELLOW)};
            }
            break;

        default:  // 長龍切斷
            if (v == 0) {
                s.board = {{T(1, Color::BLACK), T(2, Color::BLACK), T(3, Color::BLACK),
                            T(4, Color::BLACK), T(5, Color::BLACK), T(6, Color::BLACK),
                            T(7, Color::BLACK), T(8, Color::BLACK)}};
                s.hand  = {T(4, Color::BLACK), T(5, Color::BLACK)};
            } else if (v == 1) {
                s.board = {{T(3, Color::RED), T(4, Color::RED), T(5, Color::RED),
                            T(6, Color::RED), T(7, Color::RED), T(8, Color::RED),
                            T(9, Color::RED)}};
                s.hand  = {T(6, Color::RED), T(7, Color::RED)};
            } else {
                s.board = {{T(5, Color::BLUE), T(6, Color::BLUE), T(7, Color::BLUE),
                            T(8, Color::BLUE), T(9, Color::BLUE), T(10, Color::BLUE),
                            T(11, Color::BLUE), T(12, Color::BLUE)}};
                s.hand  = {T(8, Color::BLUE), T(9, Color::BLUE)};
            }
            break;
    }
    return s;
}


// ── 把一個動作套用到狀態上 ───────────────────────────────
//
// 領域刻意不提供 apply()：它的職責是「找解、翻譯、辨識」，
// 不包含改變世界。誰改變世界是呼叫端的事。
//
// 這裡的實作是簡化版——真正的規則驗證由 validator 負責，
// 這支程式只需要讓狀態往前走一步，好讓 classify() 有東西可以比對。
//
RummiState applyMove(const RummiState& s, const RummiMove& m) {
    RummiState next = s;

    // 從手牌移除打出去的牌
    for (Tile* t : m.tiles) {
        auto it = std::find(next.hand.begin(), next.hand.end(), t);
        if (it != next.hand.end()) next.hand.erase(it);
    }

    switch (m.kind) {
        case RummiMove::ATTACH_RUN:
        case RummiMove::COMPLETE_GROUP:
            if (m.target_set >= 0 &&
                m.target_set < static_cast<int>(next.board.size())) {
                auto& set = next.board[m.target_set];
                if (m.at_head)
                    set.insert(set.begin(), m.tiles.begin(), m.tiles.end());
                else
                    set.insert(set.end(), m.tiles.begin(), m.tiles.end());
            }
            break;

        case RummiMove::INITIAL_MELD:
            next.board.push_back(m.tiles);
            next.initial_meld_done = true;
            break;

        case RummiMove::JOKER_FILL:
            next.board.push_back(m.tiles);
            break;

        case RummiMove::BOARD_RESHUFFLE: {
            // 大風吹要真的拆桌面，不能只是另開一組。
            //
            // detectReshuffle 的判準是「原本某一組不再完整存在」——
            // 如果只是把牌加到旁邊，桌面兩組都還在，偵測不到，
            // observe() 就不會累加進度。第一版就是踩到這個。
            //
            // 這裡的實作：把手牌那張的同色鄰居從各組抽出來，
            // 跟它拼成新的一組，剩下的牌留在原位。
            if (m.tiles.empty()) break;
            Tile* played = m.tiles.front();
            if (played->isJoker()) { next.board.push_back(m.tiles); break; }

            std::vector<Tile*> rebuilt = {played};
            std::vector<std::vector<Tile*>> remain;

            for (auto& set : next.board) {
                std::vector<Tile*> keep;
                for (Tile* t : set) {
                    bool sameColour = !t->isJoker() &&
                                      t->getColor() == played->getColor();
                    bool adjacent = sameColour &&
                        std::abs(t->getNumber() - played->getNumber()) <= 2;
                    if (adjacent) rebuilt.push_back(t);
                    else          keep.push_back(t);
                }
                if (!keep.empty()) remain.push_back(keep);
            }

            std::sort(rebuilt.begin(), rebuilt.end(),
                      [](Tile* x, Tile* y) { return x->getNumber() < y->getNumber(); });
            remain.push_back(rebuilt);
            next.board = remain;
            break;
        }

        case RummiMove::RUN_SPLIT: {
            // 長龍切斷：把六張以上的順子切成兩段，讓打出的牌接上其中一段。
            //
            // detectRunSplit 是六個偵測器裡最嚴格的：切完之後
            // **每一段都必須是合法的 Run**，而且原本那組的牌要散在兩組以上。
            // 所以不能隨便從中間切——要切在打出的牌接得上的位置。
            //
            // 例：桌上黑1-8，手上黑4。
            //     切成 [黑1 黑2 黑3 黑4] 和 [黑5 黑6 黑7 黑8]，
            //     打出的黑4 接在前半段尾巴 → 兩段都還是合法順子。
            if (m.tiles.empty()) break;
            Tile* played = m.tiles.front();

            int target = -1;
            for (size_t k = 0; k < next.board.size(); ++k)
                if (next.board[k].size() >= 6 &&
                    Validator::isValidRun(next.board[k])) { target = static_cast<int>(k); break; }
            if (target < 0) { next.board.push_back(m.tiles); break; }

            std::vector<Tile*> run = next.board[target];

            // 找出打出的牌該插在哪裡：它的數字在這條 Run 的哪個位置
            size_t cut = run.size() / 2;
            if (!played->isJoker()) {
                for (size_t k = 0; k < run.size(); ++k) {
                    if (run[k]->isJoker()) continue;
                    // cut = k 而不是 k+1：前半段要「不含」這個數字，
                    // 打出的牌才接得上去而不重複。
                    // 桌上黑1-8、手上黑4 → 切成 [黑1 黑2 黑3] 和 [黑4..黑8]，
                    // 黑4 接在前段尾巴變成 [黑1-4]，兩段都是合法順子。
                    if (run[k]->getNumber() == played->getNumber()) { cut = k; break; }
                }
            }
            if (cut < 3) cut = 3;
            if (cut > run.size() - 3) cut = run.size() - 3;

            std::vector<Tile*> head(run.begin(), run.begin() + cut);
            std::vector<Tile*> tail(run.begin() + cut, run.end());
            head.push_back(played);      // 接在前半段的尾巴

            next.board.erase(next.board.begin() + target);
            next.board.push_back(head);
            next.board.push_back(tail);
            break;
        }
    }
    return next;
}

}  // namespace

// ═════════════════════════════════════════════════════════
//  主流程
// ═════════════════════════════════════════════════════════
int main() {
    RummikubDomain domain;
    CoachEngine<RummiState, RummiMove> engine(domain);

    std::cout << "══════════════════════════════════════════════════\n";
    std::cout << " 拉密認知教練 · 六關\n";
    std::cout << "══════════════════════════════════════════════════\n\n";
    std::cout << "  每一關教一招。引導會隨著關卡遞減——\n";
    std::cout << "  第一關卡住馬上給答案，第六關只會輕推，\n";
    std::cout << "  而且要連續三次自己找到才算過。\n\n";
    std::cout << "  怎麼出牌：\n";
    std::cout << "    直接打手牌的編號，例如  1\n";
    std::cout << "    要出好幾張就用空白隔開，例如  1 3\n\n";
    std::cout << "  其他指令：\n";
    std::cout << "    p  我想不到，跳過這一回合（會累積卡關）\n";
    std::cout << "    h  現在就給我提示\n";
    std::cout << "    n  這一關先跳過\n";
    std::cout << "    q  離開\n\n";

    int stuck = 0;
    RummiState state = makeLevelState(engine.currentLevel(), 0);

    while (true) {
        const LevelSpec& spec = engine.currentSpec();
        const auto& prog = engine.progressOf(spec.technique);

        std::cout << "\n──────────────────────────────────────────────────\n";
        std::cout << "第 " << spec.level << " 關 · " << spec.name
                  << "（引導 " << spec.guidance_percent << "%）\n";
        std::cout << "進度  用出 " << prog.total_uses << "/" << spec.required_uses
                  << "　自主 " << prog.unassisted_uses << "/" << spec.required_unassisted;
        std::string st = CoachEngine<RummiState, RummiMove>::stars(prog.mastery);
        if (!st.empty()) std::cout << "　" << st;
        std::cout << "\n卡關  " << stuck << " 回合\n";

        printState(state);

        // ── 引擎決定要不要開口 ──
        Advice advice = engine.tick(state, stuck);
        if (advice.speak) {
            std::cout << "\n  Coach（" << tierLabel(advice.tier);
            if (advice.from_safety_net) std::cout << " · 保底";
            std::cout << "）：" << advice.text << "\n";
        } else {
            std::cout << "\n  Coach：（沉默）\n";
        }

        std::cout << "\n> ";
        std::string cmd;
        if (!std::getline(std::cin, cmd)) break;
        // 使用者可能會打成 " 1" 或 "1 "——那不該算成不認得的指令
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), notSpace));
        cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), notSpace).base(), cmd.end());

        if (cmd.empty()) cmd = "p";

        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(cmd[0])));

        if (c == 'q') {
            std::cout << "\n離開。\n";
            break;
        }

        if (c == 'n') {
            if (!engine.advance()) {
                std::cout << "\n已經是最後一關了。\n";
            } else {
                stuck = 0;
                state = makeLevelState(engine.currentLevel(), 0);
            }
            continue;
        }

        if (c == 'h') {
            // 強制要提示：直接問領域拿最深的一層
            auto mv = domain.solve(state);
            if (mv) {
                std::cout << "\n  Coach（你主動要的）："
                          << domain.hint(HintTier::REVEAL_MOVE, *mv, state) << "\n";
            } else {
                std::cout << "\n  Coach：現在真的沒有能做的動作。\n";
            }
            ++stuck;
            continue;
        }

        if (c == 'p') {
            ++stuck;
            std::cout << "\n  （跳過，卡關 +1）\n";
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            // ── 使用者自己出牌 ──
            //
            // 這裡是這支程式跟 demo 最大的差別：使用者選的牌不一定對，
            // 而「選錯了會怎樣」正是引導設計要處理的事。
            std::vector<int> picked;
            std::istringstream is(cmd);
            int idx;
            while (is >> idx) picked.push_back(idx);

            // 檢查編號合法
            bool bad = false;
            for (int i : picked)
                if (i < 1 || i > static_cast<int>(state.hand.size())) bad = true;
            if (bad || picked.empty()) {
                std::cout << "\n  手牌只有 1 到 " << state.hand.size() << " 號。\n";
                continue;
            }

            // 把選到的牌抓出來
            std::vector<Tile*> chosen;
            for (int i : picked) chosen.push_back(state.hand[i - 1]);

            auto mv = domain.solve(state);
            if (!mv) {
                std::cout << "\n  Coach：現在真的沒有能做的動作。\n";
                ++stuck;
                continue;
            }

            // 選的牌跟正解一不一樣？
            std::vector<Tile*> want = mv->tiles;
            std::sort(chosen.begin(), chosen.end());
            std::sort(want.begin(), want.end());

            if (chosen != want) {
                std::cout << "\n  這一手接不上。";
                // 只在錯的時候給一點方向，不直接講答案——
                // 講答案的時機由關卡設定決定，不該因為出錯就破例。
                //
                // 但「張數不對」是介面問題不是理解問題，可以直接說。
                // 使用者看到手上兩張，很自然會想兩張都出。
                if (chosen.size() != want.size())
                    std::cout << "這一手要出 " << want.size() << " 張。";
                else
                    std::cout << "再看看手上還有什麼。";
                std::cout << "\n";
                ++stuck;
                continue;
            }

            RummiState before = state;
            state = applyMove(state, *mv);

            // 這一步是重點：引擎從「動作前後的狀態」反推用了哪一招，
            // 而不是由呼叫端直接告訴它。領域負責認，引擎負責記。
            engine.observe(before, state);

            std::cout << "\n  做了：" << domain.hint(HintTier::REVEAL_MOVE, *mv, before) << "\n";
            stuck = 0;

            if (engine.canAdvance()) {
                std::cout << "\n  ★ 第 " << spec.level << " 關過了。\n";
                if (!engine.advance()) {
                    std::cout << "\n══════════════════════════════════════════════════\n";
                    std::cout << " 六關全部走完\n";
                    std::cout << "══════════════════════════════════════════════════\n";
                    break;
                }
                state = makeLevelState(engine.currentLevel(), 0);
            } else {
                // 同一關再來一次——換下一個局面，不是同一題重來
                const auto& p2 = engine.progressOf(spec.technique);
                state = makeLevelState(spec.level, p2.total_uses);
            }
            continue;
        }

        std::cout << "\n  不認得這個指令。\n";
    }

    // ── 結算 ──
    std::cout << "\n──────────────────────────────────────────────────\n";
    std::cout << "掌握度\n\n";
    for (const auto& L : domain.levels()) {
        const auto& p = engine.progressOf(L.technique);
        std::cout << "  " << std::left << std::setw(16) << L.name
                  << "用出 " << p.total_uses
                  << "　自主 " << p.unassisted_uses << "　"
                  << CoachEngine<RummiState, RummiMove>::stars(p.mastery) << "\n";
    }
    std::cout << "\n  * 照抄　** 提示後做出　*** 自己找到\n";

    cleanup();
    return 0;
}
