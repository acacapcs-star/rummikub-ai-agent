#pragma once
#include <string>
#include <functional>

/* =========================================================================
   CoachHintBridge —— CognitiveCoachAgent 跟 GameManager 之間的溝通橋樑

   問題：CognitiveCoachAgent 算出提示文字的當下，只有 std::cout 這條路徑
   能輸出，沒辦法直接把文字塞進 GameManager::buildStateJSON() 產生的
   state.json 裡，因為兩個類別互不知道對方存在。

   解法：用一個全域字串當作「信箱」——CognitiveCoachAgent 算完提示後把
   文字放進這個信箱，GameManager 匯出 state.json 時讀取信箱內容一起寫入。
   這是刻意選擇的最小侵入性做法，不是說這是「最漂亮」的架構——如果之後
   要接更多種前端可見的即時資訊，會值得改成正式的 Observer/事件系統，
   但目前只有一個欄位，用全域信箱換取改動範圍最小是合理的取捨。

   時序上還有一個真實的問題：GameManager::exportState() 是在呼叫
   playTurn() 之前執行的，代表提示算出來的當下，那一輪的 state.json
   已經匯出過了——如果不做任何處理，提示會慢一拍、要等到下一輪才會
   顯示在畫面上。g_trigger_state_reexport 就是用來解決這個時序問題：
   GameManager 在建構時把自己的 exportState() 註冊進來，
   CognitiveCoachAgent 算完提示、寫進信箱後，立刻呼叫這個回呼，
   讓這一輪的 state.json 能馬上反映最新的提示內容。

   用 C++17 的 inline 變數，避免多個 .cpp 檔案引入時的重複定義問題。
   ========================================================================= */
inline std::string g_current_coach_hint;
inline std::function<void()> g_trigger_state_reexport;
