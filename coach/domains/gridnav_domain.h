#pragma once
#include "../coach_engine.h"
#include <algorithm>
#include <queue>
#include <sstream>

/* =========================================================================
   gridnav_domain.h —— 第二個領域：機器人格點導航

   這個檔案存在的唯一目的，是驗證 coach_engine 真的跟領域無關。

   它跟拉密沒有任何共同點：
     - 狀態是「機器人在哪、地圖長怎樣」，不是牌
     - 動作是「往哪個方向走一步」，不是出牌
     - 技巧是「繞障礙」「走對角」，不是「接龍」「補第四色」

   但引擎完全不用改一行。
   ========================================================================= */

// ── 狀態：機器人的位置與地圖 ─────────────────────────────
struct GridState {
    static const int W = 8, H = 6;
    int rx = 0, ry = 0;                 // 機器人位置
    int gx = 7, gy = 5;                 // 目標位置
    bool wall[H][W] = {};               // 障礙

    bool blocked(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) return true;
        return wall[y][x];
    }
    bool atGoal() const { return rx == gx && ry == gy; }
};

// ── 動作：往一個方向走一步 ───────────────────────────────
struct GridMove {
    int dx = 0, dy = 0;
    std::string dirName() const {
        if (dx == 0 && dy == -1) return "上";
        if (dx == 0 && dy ==  1) return "下";
        if (dx == -1 && dy == 0) return "左";
        if (dx ==  1 && dy == 0) return "右";
        if (dx == -1 && dy == -1) return "左上";
        if (dx ==  1 && dy == -1) return "右上";
        if (dx == -1 && dy ==  1) return "左下";
        return "右下";
    }
};

// ── 技巧編號 ─────────────────────────────────────────────
enum GridTechnique {
    GT_STRAIGHT = 0,   // 直線接近
    GT_DIAGONAL,       // 走對角線
    GT_AVOID,          // 繞開障礙
    GT_BACKTRACK,      // 暫時遠離目標以脫困
    GT_COUNT
};

class GridNavDomain : public CoachDomain<GridState, GridMove> {
public:
    GridNavDomain() { buildLevels(); }

    // ── ① 找解：BFS 找最短路的第一步 ──────────────────────
    std::optional<GridMove> solve(const GridState& s) const override {
        if (s.atGoal()) return std::nullopt;

        static const int DX[8] = {0,0,-1,1,-1,1,-1,1};
        static const int DY[8] = {-1,1,0,0,-1,-1,1,1};

        int dist[GridState::H][GridState::W];
        int firstDir[GridState::H][GridState::W];
        for (int y = 0; y < GridState::H; ++y)
            for (int x = 0; x < GridState::W; ++x) { dist[y][x] = -1; firstDir[y][x] = -1; }

        std::queue<std::pair<int,int>> q;
        q.push({s.rx, s.ry});
        dist[s.ry][s.rx] = 0;

        while (!q.empty()) {
            auto [cx, cy] = q.front(); q.pop();
            for (int d = 0; d < 8; ++d) {
                int nx = cx + DX[d], ny = cy + DY[d];
                if (s.blocked(nx, ny) || dist[ny][nx] >= 0) continue;
                dist[ny][nx] = dist[cy][cx] + 1;
                firstDir[ny][nx] = (dist[cy][cx] == 0) ? d : firstDir[cy][cx];
                if (nx == s.gx && ny == s.gy) {
                    return GridMove{DX[firstDir[ny][nx]], DY[firstDir[ny][nx]]};
                }
                q.push({nx, ny});
            }
        }
        return std::nullopt;   // 走不到 —— 引擎會誠實說「沒有能做的動作」
    }

    // ── ② 三種深淺的說法 ─────────────────────────────────
    std::string hint(HintTier tier, const GridMove& m,
                     const GridState& s) const override {
        switch (tier) {
            case HintTier::GENTLE_NUDGE:
                return "有路可以走，再看一次周圍。";
            case HintTier::POINT_TO_AREA: {
                // 只給大方向，不給精確方位
                std::string vertical   = (m.dy < 0) ? "上" : (m.dy > 0 ? "下" : "");
                std::string horizontal = (m.dx < 0) ? "左" : (m.dx > 0 ? "右" : "");
                return "往" + vertical + horizontal + "那一側找找看。";
            }
            case HintTier::REVEAL_MOVE:
                return "往「" + m.dirName() + "」走一步。";
        }
        return "";
    }

    // ── ③ 反推用了哪些技巧 ───────────────────────────────
    std::vector<int> classify(const GridState& before,
                              const GridState& after) const override {
        std::vector<int> found;
        int dx = after.rx - before.rx, dy = after.ry - before.ry;
        if (dx == 0 && dy == 0) return found;

        // 走對角線
        if (dx != 0 && dy != 0) found.push_back(GT_DIAGONAL);
        else                    found.push_back(GT_STRAIGHT);

        // 這一步旁邊有障礙 → 算繞障礙
        if (before.blocked(before.rx + dx, before.ry) ||
            before.blocked(before.rx, before.ry + dy))
            found.push_back(GT_AVOID);

        // 離目標變遠 → 脫困性後退
        auto d = [](int x1,int y1,int x2,int y2){
            return std::max(std::abs(x1-x2), std::abs(y1-y2)); };
        if (d(after.rx, after.ry, after.gx, after.gy) >
            d(before.rx, before.ry, before.gx, before.gy))
            found.push_back(GT_BACKTRACK);

        return found;
    }

    int techniqueCount() const override { return GT_COUNT; }

    std::string techniqueName(int t) const override {
        switch (t) {
            case GT_STRAIGHT:  return "直線接近";
            case GT_DIAGONAL:  return "走對角線";
            case GT_AVOID:     return "繞開障礙";
            case GT_BACKTRACK: return "後退脫困";
            default:           return "未知";
        }
    }

    const std::vector<LevelSpec>& levels() const override { return levels_; }

private:
    std::vector<LevelSpec> levels_;

    void buildLevels() {
        // 四個關卡，引導從 100% 遞減到 40%——
        // 曲線的形狀跟拉密一樣，因為那是引擎的事，不是領域的事。
        levels_ = {
            { 1, GT_STRAIGHT,  "直線接近", 100,
              0, 1, 2, HintTier::REVEAL_MOVE,   3, 1,  -1, HintTier::REVEAL_MOVE },
            { 2, GT_DIAGONAL,  "走對角線",  80,
              1, 2, 4, HintTier::REVEAL_MOVE,   3, 1,  -1, HintTier::REVEAL_MOVE },
            { 3, GT_AVOID,     "繞開障礙",  60,
              2, 4, -1, HintTier::POINT_TO_AREA, 3, 2, 10, HintTier::REVEAL_MOVE },
            { 4, GT_BACKTRACK, "後退脫困",  40,
              3, -1, -1, HintTier::GENTLE_NUDGE, 3, 3, 12, HintTier::POINT_TO_AREA },
        };
    }
};
