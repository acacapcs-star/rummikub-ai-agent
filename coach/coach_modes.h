#pragma once
#include "coach_engine.h"
#include <string>
#include <vector>

/* =========================================================================
   coach_modes.h —— 五種遊玩模式

   同一個引擎、同一套六關卡，差別只在「Coach 的音量」。

     新手練組    引導完整，該教就教
     炫酷組      提示克制，但積極偵測玩家自己發現的招數
     較量組      提示更少——給了答案就不叫較量
     挑戰極限組  幾乎不出手，只在完全死局時給一句
     高手過招    Coach 退場，改成賽後分析

   這五種不是五套程式，是五組門檻。
   實作上就是把 LevelSpec 的四個數字乘上一個係數——
   **同一個設計，換個音量。**

   為什麼要有五種：
     一個「什麼時候該說話」的系統，如果只有一種音量，
     那它其實沒有回答那個問題，只是選了一個答案。
     模式的存在讓「該說多少」變成使用者可以決定的事。
   ========================================================================= */

enum class CoachMode {
    BEGINNER,     // 新手練組
    STYLISH,      // 炫酷組
    COMPETITIVE,  // 較量組
    EXTREME,      // 挑戰極限組
    SPARRING      // 高手過招
};

// ── 一個模式的設定 ───────────────────────────────────────
struct ModeProfile {
    CoachMode mode;
    std::string name;
    std::string description;

    /* 門檻的縮放：把 LevelSpec 的 *_after_turns 乘上這個倍率。
       倍率越大，系統越晚開口。1.0 就是原本的關卡設定。            */
    double turn_multiplier;

    /* 音量上限：無論關卡設定多深，這個模式最多只給到這一層。
       這是硬性的天花板——例如較量組永遠不給答案。                */
    HintTier volume_cap;

    /* 是否在遊戲中給提示。
       高手過招是 false——那個模式的 Coach 完全不出聲，
       所有回饋留到賽後。                                        */
    bool hint_during_play;

    /* 是否積極偵測玩家自創的招數（Private 技巧）。
       炫酷組是主場：系統少說話，但認真看你做了什麼。            */
    bool detect_private;

    /* 單手出幾張會觸發「這一手值得記下來」的詢問。
       炫酷組門檻低（多記錄），較量組高（少打擾）。              */
    int private_threshold;

    /* 保底機制是否啟用。
       挑戰極限組關掉——那個模式的意思就是「不會有人來救你」。     */
    bool safety_net;

    /* 賽後分析的詳細程度 0–2。
       高手過招是 2，因為它把所有回饋都挪到賽後。                */
    int postgame_detail;
};

class CoachModes {
public:
    static const ModeProfile& get(CoachMode m) {
        static const ModeProfile PROFILES[] = {
            {
                CoachMode::BEGINNER, "新手練組",
                "引導完整。卡住就會有人告訴你該看哪裡，"
                "真的想不出來時會直接講答案。",
                1.0, HintTier::REVEAL_MOVE,
                true, false, 5, true, 1
            },
            {
                CoachMode::STYLISH, "炫酷組",
                "系統少說話，但認真看你做了什麼。"
                "出得漂亮的一手會被記下來，你可以幫它取名字。",
                1.5, HintTier::POINT_TO_AREA,
                true, true, 3, true, 2
            },
            {
                CoachMode::COMPETITIVE, "較量組",
                "提示只到「這裡有東西」。"
                "給了答案就不叫較量了。",
                2.0, HintTier::GENTLE_NUDGE,
                true, true, 5, true, 1
            },
            {
                CoachMode::EXTREME, "挑戰極限組",
                "幾乎不出手。只有在完全死局、真的沒有任何牌可以出時，"
                "才會告訴你一聲。沒有保底。",
                3.0, HintTier::GENTLE_NUDGE,
                true, false, 7, false, 1
            },
            {
                CoachMode::SPARRING, "高手過招",
                "遊戲中完全安靜。所有回饋留到賽後——"
                "那時才告訴你哪幾手可以更好。",
                99.0, HintTier::GENTLE_NUDGE,
                false, true, 7, false, 2
            },
        };
        int i = static_cast<int>(m);
        if (i < 0 || i > 4) i = 0;
        return PROFILES[i];
    }

    static std::vector<CoachMode> all() {
        return { CoachMode::BEGINNER, CoachMode::STYLISH,
                 CoachMode::COMPETITIVE, CoachMode::EXTREME,
                 CoachMode::SPARRING };
    }

    /* 把關卡設定套上模式的音量。

       兩個動作：
         ① 門檻乘上倍率——系統變得比較晚開口
         ② 上限取兩者較低的——模式的天花板不能被關卡突破

       第二點很重要：較量組的 cap 是「輕推」，
       即使 L1 的關卡設定寫著可以給答案，套上模式之後也不行。       */
    static LevelSpec apply(const LevelSpec& base, CoachMode m) {
        const ModeProfile& p = get(m);
        LevelSpec s = base;

        s.nudge_after_turns  = scale(base.nudge_after_turns,  p.turn_multiplier);
        s.point_after_turns  = scale(base.point_after_turns,  p.turn_multiplier);
        s.reveal_after_turns = scale(base.reveal_after_turns, p.turn_multiplier);

        // 音量上限取較低者
        if (deeper(base.max_tier, p.volume_cap)) s.max_tier = p.volume_cap;

        if (!p.safety_net) {
            s.safety_net_after_turns = -1;
        } else {
            s.safety_net_after_turns = scale(base.safety_net_after_turns,
                                             p.turn_multiplier);
            if (deeper(base.safety_net_tier, p.volume_cap))
                s.safety_net_tier = p.volume_cap;
        }

        // 高手過招在遊戲中完全不出聲
        if (!p.hint_during_play) {
            s.nudge_after_turns  = -1;
            s.point_after_turns  = -1;
            s.reveal_after_turns = -1;
            s.safety_net_after_turns = -1;
        }
        return s;
    }

private:
    // -1 代表「不啟用」，乘倍率之後仍然是 -1
    static int scale(int turns, double mult) {
        if (turns < 0) return -1;
        double v = turns * mult;
        if (v > 90) return -1;              // 大到實務上不會觸發，直接關掉
        int r = static_cast<int>(v + 0.5);
        // 原本是 0（立刻給）的話，倍率再大也至少要留一點延遲才有意義
        if (turns == 0 && mult > 1.0) r = static_cast<int>(mult);
        return r;
    }

    static bool deeper(HintTier a, HintTier b) {
        return static_cast<int>(a) > static_cast<int>(b);
    }
};
