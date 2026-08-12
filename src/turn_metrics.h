#pragma once
#include <chrono>
#include <fstream>
#include <string>

struct TurnMetrics {
    static int turn_index;
    static bool regroup_attempted;
    static int  tiles_played;

    std::chrono::steady_clock::time_point t0;
    std::string agent;

    explicit TurnMetrics(const std::string& who) : agent(who) {
        t0 = std::chrono::steady_clock::now();
        regroup_attempted = false;
        tiles_played = 0;
        ++turn_index;
    }

    ~TurnMetrics() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        std::ofstream f("turn_metrics.csv", std::ios::app);
        f << agent << ',' << turn_index << ',' << us << ','
          << regroup_attempted << ',' << tiles_played << '\n';
    }
};
