# 延伸專案：個性化 AI —— 繼承與多型的實驗與取捨
 
> 個人延伸專案（非資訊之芽大作業二內容）｜對應 `AIAgent_Conservative` / `AIAgent_Aggressive`
 
## 這是什麼
 
在原本的混合戰術型 `AIAgent_0` 基礎上，設計了兩種不同個性的子類別，探索「同一套核心演算法，能不能透過調整決策參數長出不同風格」：
 
- **`AIAgent_Conservative`（保守型）**：更晚判定對手卡關、囤牌冷卻更久、更晚進入全力進攻模式
- **`AIAgent_Aggressive`（進攻型）**：更快判定對手卡關、幾乎不囤牌、更早進入全力進攻模式
## 架構設計：Virtual 鉤子，不是重寫演算法
 
`AIAgent_0` 加入三個 `protected virtual` 的參數鉤子：
 
```cpp
virtual int getEnemyDrawThreshold() const { return 8; } 
virtual int getHoardCooldownTurns() const { return 4; }
virtual int getAntiStalemateThreshold() const { return 20; }
```
 
原本寫死的數字（`enemy_draw_count >= 8`、`draw_cooldown = 4`、`draw_pile_size <= 20`）全部換成呼叫這幾個鉤子。兩個子類別只覆寫這三個回傳值，`playTurn()`、`tryExtendBoard()` 等核心演算法一行都不用重寫：
 
```cpp
class AIAgent_Conservative : public AIAgent_0 {
protected:
    int getEnemyDrawThreshold() const override { return 15; }
    int getHoardCooldownTurns() const override { return 8; }
    int getAntiStalemateThreshold() const override { return 15; }
};
 
class AIAgent_Aggressive : public AIAgent_0 {
protected:
    int getEnemyDrawThreshold() const override { return 3; }
    int getHoardCooldownTurns() const override { return 1; }
    int getAntiStalemateThreshold() const override { return 25; }
};
```
 
這是真正的多型：`main.cpp` 用 `Player*` 指標指向不同子類別的物件，呼叫的都是同一個繼承來的 `playTurn()`，但因為虛擬函式機制，實際執行時會自動呼叫各自覆寫過的鉤子，行為就長出差異。
 
## 實測結果
 
**保守型 vs 進攻型（300 場對打）**：保守型 148 / 300（49.3%）——非常接近五五波，沒有一方壓倒性勝出。
 
**分別對戰真實 baseline1（各 200 場）**：
 
| 版本 | 對 baseline1 勝率 |
|---|---|
| 原版混合戰術型 `AIAgent_0` | 64% |
| 保守型 | 52.5% |
| 進攻型 | 50.5% |
 
## 誠實的結論：個性化不是沒有代價的
 
兩個個性化版本對戰真實 baseline1 的勝率，都比原版低了 12～14 個百分點。原因並不難理解：原版的參數（門檻 8、冷卻 4）並不是隨便設定的，而是針對 baseline1 實測調校出來的甜蜜點（過程記錄在 `docs/strategies/03_調牌與心機戰術_denial_tactics.md` 裡：門檻原本設 3，實測後調高到 8 才穩定）。保守型跟進攻型都把這個參數往極端方向推，等於偏離了已經調校好的甜蜜點，勝率下降是合理的結果，不是實作有 bug。
 
**這個發現的價值，不在於「做出更會贏的 AI」，而在於誠實驗證了一件事：「風格多樣性」與「最佳勝率」之間存在真實的取捨。** 這個取捨本身，也呼應了未來想把這套架構延伸到認知訓練應用時的設計邏輯——如果目標從「打贏對手」換成「陪伴、引導」，本來就不會再以勝率作為唯一的優化目標，犧牲一點勝率換取更穩定、可預期的行為風格，會是合理甚至必要的取捨。
