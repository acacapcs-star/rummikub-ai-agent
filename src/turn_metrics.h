#pragma once
#include <chrono>
#include <fstream>
#include <string>

// 每一手的行為紀錄。RAII：離開作用域時自動寫檔。
struct TurnMetrics {
    static int  turn_index;          // 第幾手（全域累計）
    static bool regroup_attempted;   // 這手有沒有走大風吹
    static int  tiles_played;        // 這手打出幾張
    static bool melded;              // 破冰是否已完成
    static int  meld_attempts;       // 破冰嘗試次數（首出延遲）
    static int  extend_calls;        // tryExtendBoard 被呼叫幾次
    static int  failed_applies;      // applyProposedSets 失敗次數（無效嘗試）
    static bool had_option;          // 桌面有可行動作嗎（錯過率的分母）

    std::chrono::steady_clock::time_point t0;
    std::string agent;

    explicit TurnMetrics(const std::string& who) : agent(who) {
        t0 = std::chrono::steady_clock::now();
        regroup_attempted = false;
        tiles_played  = 0;
        extend_calls  = 0;
        failed_applies = 0;
        had_option    = false;
        ++turn_index;
    }

    ~TurnMetrics() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        std::ofstream f("turn_metrics.csv", std::ios::app);
        f << agent << ',' << turn_index << ',' << us << ','
          << regroup_attempted << ',' << tiles_played << ','
          << melded << ',' << meld_attempts << ','
          << extend_calls << ',' << failed_applies << ','
          << had_option << '\n';
    }
};
