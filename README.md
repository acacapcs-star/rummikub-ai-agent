# Rummikub AI Agent ＋ 認知教練引擎

[🇹🇼 中文](#中文) ｜ [🇬🇧 English](#english)

---

<a name="中文"></a>

## 🇹🇼 中文

> **關於這個 repository 的範圍**
>
> 本 repo 收錄的是**本人撰寫的原始碼與設計文件**（`src/`、`coach/`、`rl/`、`tests/`、`experiments/`、`docs/`）。完整建置另需課程提供的框架（`CMakeLists.txt`、`Dockerfile`、`server.py`、`visualizer/`）與助教提供的 baseline 物件檔（`prebuilt/ai_agent_baseline*.cpp.o`）；那些屬於課程材料，未包含在此，因此直接 clone 無法逕行編譯。
>
> 完整的實作說明、開發困難與測試數據見 **[期末報告 PDF](docs/report/2026_資訊之芽_拉密_期末報告_藍宥欣.pdf)**。
> Repo 導覽見 **[REPO_MAP.md](REPO_MAP.md)**。

---

## 這個專案其實有兩層

第一層是課程作業：拉密的規則引擎與 AI Agent。二階大作業 49.8/50，該屆最高分（二階結業 17 人）。

第二層是從第一層長出來的，現在比第一層還大——**一個領域無關的認知教練引擎**。

| | 目錄 | 檔數 | 行數 |
|---|---|---|---|
| 遊戲引擎與 AI Agent | `src/` | 29 | 4,400 |
| **認知教練引擎** | `coach/` | **29** | **7,316** |
| 強化學習實驗 | `rl/` | 5 | 1,356 |
| 測試 | `tests/` ＋ `coach/test_*` | 12 | 2,848 |
| 模擬實驗 | `experiments/` | 5 | 1,261 |

會分成兩層，是因為做完 AI Agent 之後我發現一件事：**讓 AI 打贏人很簡單，讓 AI 幫人學會怎麼打贏比較難。**

前者只要搜尋得夠深；後者要決定什麼時候該閉嘴。

---

## 核心主張

> **領域知道「答案是什麼」，引擎決定「要不要說、說多少」。**

這句話寫在 `coach/coach_engine.h` 的檔頭，是整個第二層的設計原則。

拉密的求解器知道怎麼湊出 30 分、怎麼接龍、怎麼用 Joker。但「這個人卡了三回合了，現在該提示嗎？提示到什麼程度？」——那件事跟拉密沒有關係。

把它抽出來之後，同一個引擎可以教下棋、教解題、教機器人路徑規劃，只要那個領域能回答三個問題：

```cpp
solve()      給我現在的狀態，找出一個解（找不到就回 nullopt）
hint()       把那個解翻譯成三種深淺的說法
classify()   從動作前後的狀態，反推使用者用了哪些技巧
```

引擎自己負責的是：依關卡與卡關回合數決定要不要開口、記錄掌握度、判斷過關、以及保底機制。

### 提示的三個深度

```cpp
enum class HintTier {
    GENTLE_NUDGE,    // 只說「這裡有東西」
    POINT_TO_AREA,   // 指出是哪一區
    REVEAL_MOVE      // 講出具體該做什麼
};
```

引擎找得到解，但**不會替使用者出牌**。這是刻意的：代打會讓人以為自己學會了。

### 抽象化是被驗證過的，不是宣稱的

`coach/domains/gridnav_domain.h` 是第二個領域：機器人在 8×6 的格點上避開障礙走到目標。

它跟拉密沒有任何共同點——狀態是位置與地圖不是牌，動作是移動不是出牌，技巧是「繞障礙」「走對角」不是「接龍」「補第四色」。

**但 `coach_engine.h` 一行都沒改。**

那個檔案存在的唯一目的就是證明這件事。如果抽象化做錯了，加第二個領域時引擎一定會需要動。

---

## 第一層：拉密引擎與 AI Agent

### 規則驗證

`src/validator.cpp` 處理三件事：Run（同色連號）、Group（同數異色）、以及 Joker 的替代判定。

Joker 是最麻煩的部分，因為它同時可以當任何牌，而一組牌裡可能有兩張 Joker。驗證時必須枚舉所有可能的替代方式，任何一種成立就算合法。

### 大風吹：全域重組

專案的核心演算法，設計文件在 [`docs/strategies/02_大風吹_windstorm.md`](docs/strategies/02_大風吹_windstorm.md)。

一般的出牌邏輯是「我手上有什麼能接上桌面」。大風吹反過來：**把整個桌面拆掉重組，看能不能多塞幾張手牌進去。**

對 baseline 千場測試勝率 82%。

### 一個沒有實作的介面

`solveRummikub` 保留了介面但沒有實作。原本規劃遞迴窮舉找最佳解，後來為了讓安全回滾鎖每一步可控，改成迭代貪心。

那個決定被明確記錄下來，而不是悄悄消失。**放棄的路徑跟走通的路徑一樣值得寫出來。**

### 個性化 Agent

`ai_agent_aggressive.h`、`ai_agent_conservative.h` 是同一份策略骨架的兩種參數化，用繼承與多型實作。設計取捨見 [`docs/strategies/05`](docs/strategies/05_個性化AI_personality_variants.md)。

---

## 第二層：認知教練引擎

### 六關遞減引導

關卡設定四個數字：卡幾回合才開口、開口給哪一層、過關要用出幾次、其中幾次必須是自主使用。

`coach/coach_modes.h` 在關卡之上疊了一層「音量」：

```
同一個引擎、同一套六關卡，差別只在 Coach 的音量。
```

四種模式把關卡的門檻乘上一個係數，並設一個上限。**即使 L1 的關卡設定寫著可以給答案，套上「挑戰極限」模式之後也不行**——那個模式的意思就是「不會有人來救你」。

### 掌握度只升不降

```cpp
if (static_cast<int>(cand) > static_cast<int>(p.mastery))
    p.mastery = cand;      // 只升不降
```

判定原則寫在註解裡：**寧可漏判不可誤判**。誤判會讓使用者在沒學會的情況下被判過關，那比多練幾次的代價高。

### Recap：做得出來不等於知道為什麼

`coach/recap/` 是關卡結束的五題選擇題。

過關條件是「用出三次、其中 N 次自主」——那量的是「做得出來」。Recap 問的是另一件事：**換一個情境，你還認得出這一招嗎？**

三種過關條件（全對／答對 N 題／只是給你看不影響）同時實作，由實驗決定用哪個。實驗紀錄在 [`docs/strategies/10`](docs/strategies/10_Recap過關條件實驗.md)。

### 兩種學習者

[`docs/strategies/11_認知訓練的兩個族群.md`](docs/strategies/11_認知訓練的兩個族群.md) 記錄了一個設計上的分歧：**同一套提示節奏，對兩種人的效果是相反的。**

會卡住但知道自己卡住的人，需要的是「再等一下」；卡住但不知道自己卡住的人，需要的是早一點被拍肩膀。

---

## 第三層：強化學習實驗

`rl/actor_critic.h` 是手刻的 Actor-Critic，沒有用 PyTorch。

檔頭寫了理由：

> 這個網路只有幾百個參數。手刻的話，每一步梯度都看得見——呼叫 `loss.backward()` 學不到那些東西。

`rl/ablation.cpp` 是消融實驗，`rl/mini_env.h` 是簡化環境。實驗設計見 [`docs/strategies/09_ActorCritic實驗.md`](docs/strategies/09_ActorCritic實驗.md)。

**這一層是為了學而做的，不是為了效能。** 拉密的 AI Agent 沒有用到強化學習——貪心加大風吹已經有 82% 勝率，導入 RL 只會讓行為變得不可解釋。

---

## 測試

12 個測試檔、2,848 行、88 個斷言。

| 檔案 | 測什麼 |
|---|---|
| `tests/test_validator.cpp` | Run / Group / Joker 替代 |
| `tests/test_engine_core.cpp` | 引擎核心流程 |
| `tests/test_cognitive_hint_engine.cpp` | 提示分層與觸發時機 |
| `tests/test_technique_detector.cpp` | 技巧反推 |
| `tests/test_campaign.cpp` · `test_coach_campaign.cpp` | 六關流程 |
| `coach/test_*.cpp`（6 個） | 引擎、模式、對象、循環、會話、夥伴 |
| `coach/domains/test_rummikub_domain.cpp` | 領域介面 |
| `coach/recap/test_recap.cpp` | 過關條件 |
| `rl/test_rl.cpp` | Actor-Critic |

```bash
./run_tests.sh
```

---

## 策略設計文件

`docs/strategies/` 收錄十二份設計說明，包含圖解與**想過但沒做出來的構想**：

| | |
|---|---|
| [01 一條龍](docs/strategies/01_一條龍_longest_run.md) | 最長 Run 掃描 |
| [02 大風吹](docs/strategies/02_大風吹_windstorm.md) | 全域重組演算法 |
| [03 調牌與心機戰術](docs/strategies/03_調牌與心機戰術_denial_tactics.md) | 讀牌、讀對手 |
| [04 掃描模式](docs/strategies/04_橫向縱向掃描模式_scan_modes.md) | 二維對照表與顏色分堆的取捨 |
| [05 個性化 AI](docs/strategies/05_個性化AI_personality_variants.md) | 繼承與多型的實驗 |
| [06 認知教練型 AI](docs/strategies/06_認知教練型AI_設計說明.md) · [EN](docs/strategies/06_cognitive_coach_en.md) | 六關遞減引導與技巧偵測 |
| [07 學習者模擬實驗](docs/strategies/07_學習者模擬實驗.md) | 模擬不同程度的使用者 |
| [08 抽象化](docs/strategies/08_抽象化.md) | 為什麼要把引擎跟領域分開 |
| [09 Actor-Critic 實驗](docs/strategies/09_ActorCritic實驗.md) | 手刻 RL 的過程 |
| [10 Recap 過關條件實驗](docs/strategies/10_Recap過關條件實驗.md) | 三種條件的比較 |
| [11 認知訓練的兩個族群](docs/strategies/11_認知訓練的兩個族群.md) | 同一套節奏，相反的效果 |
| [12 系統架構](docs/strategies/12_系統架構.md) | 整體結構 |

---

## 使用技術

```text
Language        C++17
Build           CMake
Environment     Docker
Core concepts   OOP · pointer identity · greedy strategy
                board reconstruction · game AI
                domain abstraction · policy gradient
```

---

## 編譯與執行

```bash
# 編譯
cmake -B build && cmake --build build

# 執行（預設 baseline0 vs AI_0）
./build/rummikub

# 視覺化（另開一個終端機）
python3 server.py
# 瀏覽器開啟 http://127.0.0.1:8080/visualizer/

# 教練模式示範
./build/coach_demo

# 測試
./run_tests.sh
```

---

## AI 協助揭露

本專案的演算法設計、策略決策與架構抽象由本人完成。AI 協助的範圍限於語法查詢、編譯錯誤排查、以及英文文件的翻譯校對。

`docs/strategies/` 底下的設計文件記錄了我實際走過的思路，包含放棄的方案與放棄的原因——那些是 AI 生不出來的東西，因為它們是失敗的紀錄。

我對每一行提交的程式碼負責，如需 code review 可隨時安排。

---

## 致謝

2026 資訊之芽 C++ 語法班。課程提供了框架與 baseline agent，讓這個專案有一個可以測試的對手。

---

<a name="english"></a>

## 🇬🇧 English

> **Scope of this repository**
>
> This repo contains **code and design documents written by me** (`src/`, `coach/`, `rl/`, `tests/`, `experiments/`, `docs/`). A full build additionally requires course-provided scaffolding (`CMakeLists.txt`, `Dockerfile`, `server.py`, `visualizer/`) and TA-provided baseline object files (`prebuilt/ai_agent_baseline*.cpp.o`). Those are course materials and are not included, so cloning this repo alone will not compile.
>
> Full implementation notes, difficulties encountered, and test data are in the **[final report (PDF, Chinese)](docs/report/2026_資訊之芽_拉密_期末報告_藍宥欣.pdf)**.
> Repo tour: **[REPO_MAP.md](REPO_MAP.md)**.

---

## This project has two layers

The first layer is the course assignment: a Rummikub rules engine and AI agent. It scored 49.8/50 on the second-stage project, the highest that term (17 students completed stage 2).

The second layer grew out of the first and is now larger than it — **a domain-agnostic cognitive coaching engine**.

| | Directory | Files | Lines |
|---|---|---|---|
| Game engine and AI agent | `src/` | 29 | 4,400 |
| **Cognitive coaching engine** | `coach/` | **29** | **7,316** |
| Reinforcement learning | `rl/` | 5 | 1,356 |
| Tests | `tests/` + `coach/test_*` | 12 | 2,848 |
| Simulation experiments | `experiments/` | 5 | 1,261 |

The split happened because finishing the AI agent made one thing obvious: **making an AI beat a person is easy; making an AI help a person learn to win is harder.**

The first only needs deeper search. The second needs to decide when to shut up.

---

## The core claim

> **The domain knows what the answer is. The engine decides whether to say it, and how much.**

That line is in the header comment of `coach/coach_engine.h` and is the design principle for the entire second layer.

The Rummikub solver knows how to reach 30 points, how to extend a run, how to place a joker. But "this person has been stuck for three turns — should I hint now, and how far?" has nothing to do with Rummikub.

Once that is factored out, the same engine can coach chess, puzzles, or robot path planning, provided the domain can answer three questions:

```cpp
solve()      given the current state, find a solution (nullopt if none)
hint()       translate that solution into three depths of telling
classify()   from before and after states, infer which techniques were used
```

The engine owns the rest: deciding whether to speak based on level and turns stuck, tracking mastery, judging completion, and the fallback mechanism.

### Three depths of hint

```cpp
enum class HintTier {
    GENTLE_NUDGE,    // there is something here
    POINT_TO_AREA,   // it is in this region
    REVEAL_MOVE      // here is the specific move
};
```

The engine can find the solution but **will not play the move for you**. That is deliberate: playing it for someone lets them believe they learned it.

### The abstraction is verified, not claimed

`coach/domains/gridnav_domain.h` is a second domain: a robot navigating an 8×6 grid around obstacles to a goal.

It has nothing in common with Rummikub — state is position and map rather than tiles, actions are moves rather than plays, techniques are "go around" and "cut the diagonal" rather than "extend a run" and "complete the fourth colour".

**And `coach_engine.h` did not change by a single line.**

That file exists for exactly this purpose. If the abstraction were wrong, adding a second domain would have forced the engine to change.

---

## Layer one: Rummikub engine and AI agent

### Rule validation

`src/validator.cpp` handles three things: runs (same colour, consecutive), groups (same number, different colours), and joker substitution.

Jokers are the hard part, because one can stand for any tile and a set may contain two of them. Validation enumerates every possible substitution; the set is legal if any one of them works.

### Windstorm: global reorganisation

The core algorithm of the project. Design document: [`docs/strategies/02`](docs/strategies/02_大風吹_windstorm.md) (Chinese).

Ordinary play logic asks "what in my hand can attach to the board". Windstorm inverts it: **take the whole board apart and rebuild it, checking whether more tiles from the hand can be fitted in.**

82% win rate over 1,000 games against baseline.

### An interface that was never implemented

`solveRummikub` keeps its interface but has no implementation. The original plan was recursive exhaustive search for the optimal solution; it became iterative greedy so that every step of the safe-rollback lock stayed controllable.

That decision is recorded explicitly rather than quietly disappearing. **A path abandoned is worth writing down as much as one taken.**

### Personality variants

`ai_agent_aggressive.h` and `ai_agent_conservative.h` are two parameterisations of the same strategy skeleton, implemented with inheritance and polymorphism. Trade-offs in [`docs/strategies/05`](docs/strategies/05_個性化AI_personality_variants.md).

---

## Layer two: the cognitive coaching engine

### Six levels of receding guidance

A level specifies four numbers: how many turns stuck before speaking, which tier to give, how many uses to pass, and how many of those must be unprompted.

`coach/coach_modes.h` adds a "volume" layer on top:

```
One engine, one set of six levels. The only difference is how loud the coach is.
```

Four modes scale the level thresholds by a coefficient and impose a ceiling. **Even if level 1 is configured to reveal the answer, the "challenge" mode overrides that** — the whole point of that mode is that nobody is coming to help.

### Mastery only rises

```cpp
if (static_cast<int>(cand) > static_cast<int>(p.mastery))
    p.mastery = cand;      // only rises, never falls
```

The judging principle is in the comment: **prefer a missed detection to a false one**. A false positive marks someone as having learned something they have not, which costs more than a few extra repetitions.

### Recap: doing it is not the same as knowing why

`coach/recap/` is a five-question multiple choice quiz at the end of a level.

The pass condition is "used three times, N of them unprompted" — that measures whether you can do it. Recap asks something else: **in a different situation, would you still recognise this move?**

Three pass conditions (all correct / N or more / no gate) are implemented simultaneously; which one to use is an experimental question. Notes in [`docs/strategies/10`](docs/strategies/10_Recap過關條件實驗.md).

### Two kinds of learner

[`docs/strategies/11`](docs/strategies/11_認知訓練的兩個族群.md) records a design tension: **the same hint timing has opposite effects on two groups of people.**

Someone who gets stuck and knows it needs the system to wait longer. Someone who gets stuck without noticing needs a tap on the shoulder sooner.

---

## Layer three: reinforcement learning

`rl/actor_critic.h` is a hand-written Actor-Critic. No PyTorch.

The reason is in the header:

> This network has a few hundred parameters. Writing it by hand makes every gradient step visible — calling `loss.backward()` teaches none of that.

`rl/ablation.cpp` is an ablation study; `rl/mini_env.h` is a simplified environment. Design notes in [`docs/strategies/09`](docs/strategies/09_ActorCritic實驗.md).

**This layer exists to learn from, not to ship.** The Rummikub agent does not use reinforcement learning — greedy plus windstorm already wins 82% of games, and adding RL would only make the behaviour unexplainable.

---

## Tests

12 test files, 2,848 lines, 88 assertions.

| File | Covers |
|---|---|
| `tests/test_validator.cpp` | Runs / groups / joker substitution |
| `tests/test_engine_core.cpp` | Engine core flow |
| `tests/test_cognitive_hint_engine.cpp` | Hint tiers and trigger timing |
| `tests/test_technique_detector.cpp` | Technique inference |
| `tests/test_campaign.cpp` · `test_coach_campaign.cpp` | Six-level flow |
| `coach/test_*.cpp` (6 files) | Engine, modes, audience, cycle, session, companions |
| `coach/domains/test_rummikub_domain.cpp` | Domain interface |
| `coach/recap/test_recap.cpp` | Pass conditions |
| `rl/test_rl.cpp` | Actor-Critic |

```bash
./run_tests.sh
```

---

## Design documents

`docs/strategies/` holds twelve design notes, with diagrams and **ideas that were considered and dropped**. Most are in Chinese; [06 has an English version](docs/strategies/06_cognitive_coach_en.md).

---

## Tech stack

```text
Language        C++17
Build           CMake
Environment     Docker
Core concepts   OOP · pointer identity · greedy strategy
                board reconstruction · game AI
                domain abstraction · policy gradient
```

---

## Build and run

```bash
# Build
cmake -B build && cmake --build build

# Run (default: baseline0 vs AI_0)
./build/rummikub

# Visualizer (separate terminal)
python3 server.py
# then open http://127.0.0.1:8080/visualizer/

# Coach demo
./build/coach_demo

# Tests
./run_tests.sh
```

---

## AI assistance disclosure

The algorithm design, strategic decisions and architectural abstraction in this project are my own. AI assistance was limited to syntax lookup, compiler error diagnosis, and proofreading the English documentation.

The design documents under `docs/strategies/` record the reasoning I actually went through, including approaches I abandoned and why. Those are the parts an AI could not have produced, because they are records of failure.

I stand behind every line committed here, and am available for code review at any time.

---

## Acknowledgments

2026 Sprout C++ Programming Class. The course provided the scaffolding and the baseline agent, which gave this project an opponent to test against.
