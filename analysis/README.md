# 行為指標分析

從拉密引擎的決策軌跡抽取行為指標，作為後續「從遊玩行為讀取認知相關指標」的第一步。

## 這一步在回答什麼

六個指標是憑直覺選的。第一個該問的問題不是「它們測到了什麼」，
而是「它們之中有幾個其實在測同一件事」。

## 方法

`TurnMetrics`（`src/turn_metrics.h`）在每一手 `playTurn` 進出時記錄一列：

| 欄位 | 意義 |
|---|---|
| `us` | 決策耗時（微秒） |
| `regroup` | 這手是否走了大風吹重組 |
| `tiles` | 這手打出幾張 |
| `melded` / `meld_attempts` | 破冰是否完成 / 嘗試次數 |
| `extend_calls` | `tryExtendBoard` 被呼叫幾次 |
| `failed_applies` | `applyProposedSets` 失敗次數 |
| `had_option` | 這手是否存在可行動作 |

100 場 AI 對局，共 11,306 手。

```bash
for i in $(seq 1 100); do ./test_build 0 b0 > /dev/null; done
python3 analysis/correlate_metrics.py runs100.csv
```

## 結果

**一組重複：`regroup × extend_calls = 0.979`**

兩者幾乎等價。88% 的手 `extend_calls` 是 0 或 1，最大值僅 3，
因此「有沒有重組」與「重組幾次」在多數情況下攜帶相同資訊。
保留 `extend_calls`——它保有層次，`regroup` 只是它的二值化版本。

**一個無變異的欄位：`failed_applies`（mean 0.00, sd 0.07）**

11,306 手中幾乎不曾非零。安全回滾鎖使得 `applyProposedSets` 極少失敗。
對 AI 而言這個欄位測不到東西。但人類玩家會嘗試不合法的組合，AI 不會——
**保留此欄位，但其效度需以人類資料驗證。**

**兩個獨立指標：`us` 與 `meld_attempts`**

`meld_attempts` 與其餘欄位的相關皆低於 0.11，測的是不同面向（首出延遲）。
`us` 與各欄位相關介於 0.30–0.43，無單一欄位可取代，是綜合性指標。

**一組偏高但未達門檻：`tiles × had_option = 0.70`**

邏輯上合理（有解才出得了牌）。後續可考慮合併為「機會利用率」。

## 結論

六個指標中，一個確定冗餘（`regroup`），一個在 AI 資料中無變異（`failed_applies`）。
實際可用的獨立維度是三到四個，不是六個。

## 這不是什麼

- 這些指標與任何認知功能的關聯**尚未驗證**。目前只證明它們在引擎中可被穩定記錄。
- 資料來自 AI 自我對局（`b0`/`b1` 在本機皆建立為 `AIAgent_0`），非人類玩家。
- `melded` 與 `meld_attempts` 目前為全域靜態變數，兩名玩家共用計數，
  尚未依玩家分離——若要作為個人指標，此處必須先修正。
- 無任何醫療或診斷性質之主張。
