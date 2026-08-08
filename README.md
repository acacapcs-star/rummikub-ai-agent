# TankMan RL Agent
> 以強化學習（PPO）訓練自主坦克決策系統  
> 榮獲 2025 FunAI 營隊坦克大作戰冠軍（39 組參賽）

---

## 專案簡介

本專案為 [TankMan](https://github.com/Jesse-Jumbo/TankMan) 遊戲平台的 AI 模組，核心問題是：

> **如何讓 AI 在動態環境中，同時學會「移動追敵」與「瞄準射擊」兩種技能？**

我將這個問題拆解為兩個獨立的強化學習子任務，各自設計 Gymnasium 環境與獎勵函數，再整合為完整的對戰 Agent。

---

## 系統架構

```
TankMan RL Agent
├── tankman/
│   ├── base_env.py          # 基礎 Gymnasium 環境（抽象類別）
│   ├── chase_env.py         # 追擊模組：學習移動朝向目標
│   ├── aim_env.py           # 瞄準模組：學習轉砲管＋判斷射擊時機
│   └── resupply_env_v0.py   # 補給模組：學習移動至補給站
└── TankMan_student/
    └── ml/
        ├── train.py         # PPO 訓練腳本
        ├── ml_play_model.py # 載入模型進行對戰
        └── QT.py            # 規則式基準 Agent
```

---

## 核心設計：三個子模組

### 1. 瞄準模組（`aim_env.py`）

**任務**：讓坦克砲管轉向目標點，並在對準時射擊。

**觀察空間（Observation Space）**：
```
[gun_angle_index, angle_to_target_index]  # shape: (2,), dtype: float32
```
- 將 360° 離散化為 8 個方向（每格 45°），讓 RL 更容易學習角度關係

**動作空間（Action Space）**：
```
0: NONE
1: AIM_LEFT   # 砲管左轉
2: AIM_RIGHT  # 砲管右轉
3: SHOOT      # 射擊
```

**獎勵函數設計**：

| 情境 | 動作 | 獎勵 |
|------|------|------|
| 砲管偏右於目標 | AIM_LEFT（正確） | +1.0 |
| 砲管偏右於目標 | AIM_RIGHT（錯誤） | -1.0 |
| 砲管偏左於目標 | AIM_RIGHT（正確） | +1.0 |
| 砲管偏左於目標 | AIM_LEFT（錯誤） | -1.0 |
| 砲管完全對準目標 | SHOOT | +1.0 |
| 砲管未對準 | SHOOT（亂射） | -1.0 |
| 對準時不射擊 | 任何非SHOOT | -0.5 |
| 砲管與目標相差 180° | 轉任意方向 | -0.5 |

---

### 2. 追擊模組（`chase_env.py`）

**任務**：讓坦克車身轉向並前進至目標位置。

**觀察空間**：
```
[tank_angle_index, angle_to_target_index]  # shape: (2,), dtype: float32
```

**動作空間**：
```
0: NONE
1: FORWARD    # 前進
2: BACKWARD   # 後退
3: TURN_LEFT  # 左轉
4: TURN_RIGHT # 右轉
```

---

### 3. 補給模組（`resupply_env_v0.py`）

**任務**：讓坦克在燃油或彈藥不足時，主動移動至補給站。支援隨機化玩家與補給類型，增加訓練多樣性。

---

## 訓練方式

使用 [Stable Baselines3](https://stable-baselines3.readthedocs.io/) 的 PPO 演算法。

```bash
python TankMan_student/ml/train.py \
  --green-team-num 3 \
  --blue-team-num 3 \
  --total-time-steps 10000000 \
  --n-envs 4 \
  --batch-size 256 \
  --lr 3e-4 \
  --gamma 0.1
```

**主要超參數說明**：

| 參數 | 值 | 說明 |
|------|----|------|
| `total_timesteps` | 10,000,000 | 總訓練步數 |
| `n_envs` | 4 | 平行環境數量（加速訓練） |
| `batch_size` | 256 | 每次更新的樣本數 |
| `gamma` | 0.1 | 折扣因子（強調即時獎勵） |
| `net_arch` | [64, 64] | Policy/Value network 隱藏層大小 |

訓練過程使用 TensorBoard 記錄，並透過 `EvalCallback` 自動儲存最佳模型。

---

## 執行對戰

```bash
# 安裝依賴
pip install -r TankMan_student/requirements.txt

# 使用訓練好的模型對戰
python -m mlgame -f 120 \
  -i TankMan_student/ml/ml_play_model.py \
  -i TankMan_student/ml/ml_play_model.py \
  TankMan_student \
  --green_team_num 1 --blue_team_num 1 --frame_limit 1000
```

---

## 設計亮點

**角度離散化（Angle Discretization）**  
直接用連續角度值訓練 RL 容易造成 reward 稀疏。本專案將 360° 分成 8 個方向索引（0–7），讓 Agent 更容易學習「偏左就往右轉」的簡單規則，大幅加速收斂。

**問題分解策略**  
將「對戰」這個複雜任務拆成「瞄準」與「追擊」兩個子問題，分別訓練後整合。這個思路與自動駕駛中「感知」、「決策」、「控制」分層的設計哲學相通。

**獎勵函數的對稱設計**  
獎勵函數不只給正向回饋，也明確懲罰「對的時機做錯的事」（如對準時不射擊）和「錯的時機做任何事」（如未對準就射擊），讓 Agent 學到更精確的判斷邏輯。

---

## 與自動駕駛的關聯

本專案的核心技術與自動駕駛中的關鍵問題高度對應：

| 本專案 | 自動駕駛對應 |
|--------|-------------|
| 瞄準模組（砲管角度對準目標） | 方向盤轉向控制（Steering Control） |
| 追擊模組（車身朝向目標移動） | 路徑跟隨（Path Following） |
| 角度離散化觀察 | 感知資訊的特徵工程 |
| PPO 連續決策 | 端對端自動駕駛策略學習 |

需要說明的是：上表是**設計層面的類比**，用來說明這個專案處理的問題屬於哪一類，
並不代表本模組已被實際應用於自動駕駛系統。營隊結束後，成功大學資工系蘇文鈺教授
曾與作者討論此模組與自駕控制問題的關聯，並邀請作者參與其機器人研究的相關討論。

---

## 開發背景

本專案起源於 **2025 FunAI Winter Camp** 的坦克大作戰競賽，主辦單位為清華大學、
成功大學、陽明交通大學與臺灣科技大學，協辦包含 Google Research 與成大智慧運算學院。
參賽隊伍來自台灣各大學及高中，共 39 組；本隊（3 人）以此 RL 模組獲得**第一名**。

作者於本次競賽期間獨立完成模組的環境設計、獎勵函數與訓練流程調校。

---

## 技術棧

- Python 3.9
- [Stable Baselines3](https://stable-baselines3.readthedocs.io/)（PPO）
- [Gymnasium](https://gymnasium.farama.org/)
- [MLGame](https://github.com/PAIA-Playful-AI-Arena/mlgame) 10.2.5
- pygame 2.0.1
- TensorBoard

---

## 作者

**藍宥欣**  
2025 FunAI 坦克大作戰冠軍（3 人團隊，39 組參賽）  
[GitHub](https://github.com/acacapcs-star) | [Email](acacapcs@gmail.com)
