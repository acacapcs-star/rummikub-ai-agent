# Rummikub AI Agent 設計實作：從規則驗證到策略優化
## 專案簡介
本專案是 2026 資訊之芽 C++ 班大作業二的延伸實作，主題為拉密（Rummikub）遊戲系統與 AI Agent 設計。專案目標不只是讓程式能夠正確判斷牌組是否合法，也需要設計一支能夠自動分析局勢、出牌、利用桌面牌重組，並嘗試打敗 baseline agent 的 AI。
在這個專案中，我完成了遊戲規則驗證、Human Agent 檔案讀取，以及 AI Agent 的策略設計。AI Agent 的核心目標是：在一般對戰中保留彈性與爆發力，在殘局中降低手牌失分，並且盡量避免提交非法盤面。
這個專案讓我第一次完整體驗到「遊戲規則 → 程式建模 → AI 策略 → 壓力測試 → 策略修正」的開發流程。
---
## 使用技術

```text
Language: C++
Build System: CMake
Environment: Docker
Version Control: Git / GitHub
Core Concepts: OOP, pointer identity, greedy strategy, board reconstruction, game AI
---
 
## 專案結構
 
```
sprout2026-hw2/
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── server.py
├── src/
│   ├── main.cpp
│   ├── tile.h / tile.cpp
│   ├── validator.h / validator.cpp     ← 本次修改（遊戲引擎）
│   ├── board.h / board.cpp
│   ├── player.h / player.cpp
│   ├── game_manager.h / game_manager.cpp
│   ├── ai_agent_0.h / ai_agent_0.cpp   ← 本次修改（AI agent）
│   ├── ai_agent_baseline0.h
│   ├── ai_agent_baseline1.h
│   └── human_agent.h / human_agent.cpp ← 本次修改（human agent）
├── prebuilt/
├── grader/
└── visualizer/
    ├── index.html
    ├── app.js
    └── styles.css
```
 
---
