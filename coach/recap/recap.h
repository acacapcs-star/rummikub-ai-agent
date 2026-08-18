#pragma once
#include <algorithm>
#include <random>
#include <string>
#include <vector>

/* =========================================================================
   recap.h —— 關卡結束的 5 題 MCQ

   為什麼要有這個：
     過關條件是「用出 3 次、其中 N 次自主」——那量的是「做得出來」。
     但做得出來不等於知道為什麼。

     Recap 問的是後者：換一個情境，你還認得出這一招嗎？

   三種過關條件（本檔同時實作，由實驗決定用哪個）：

     ALL_CORRECT   五題全對
     THRESHOLD     答對 N 題以上（預設 3）
     NO_GATE       只是給你看，答錯不影響

   答錯的處理是兩階段：
     第一次錯 → 給提示，可以再試
     第二次錯 → 給完整解答，該題記為錯
   這跟遊戲內的引導邏輯一致——不讓玩家用猜的過關。
   ========================================================================= */

// ── 一個選項 ─────────────────────────────────────────────
struct McqOption {
    std::string text;
    bool correct = false;
};

// ── 一道題 ───────────────────────────────────────────────
struct McqQuestion {
    std::string prompt;
    std::vector<McqOption> options;
    std::string hint;          // 答錯第一次給的——不直接講答案
    std::string explanation;   // 答錯第二次才給的完整解釋

    // 難度 1–3，用來模擬答對機率；實際遊戲中不顯示給玩家
    int difficulty = 2;

    int correctIndex() const {
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].correct) return static_cast<int>(i);
        return -1;
    }
};

// ── 過關條件 ─────────────────────────────────────────────
enum class RecapGate {
    ALL_CORRECT,   // 五題全對
    THRESHOLD,     // 答對 N 題以上
    NO_GATE        // 不擋，只是看
};

/* 預設值來自模擬實驗（docs/strategies/10）：

     5/5 可重試   鑑別度 96.6，很清楚的人 99.9% 過關
     4/5 不重試   鑑別度 92.0（次佳）
     3/5 可重試   鑑別度 22.2 ← 看起來合理，實際上形同虛設

   選 5/5 可重試的理由不只是鑑別度最高，還有成本的分布：
   不懂的人平均重試 4.92 次，懂的人 1.31 次——
   **代價落在該承擔的人身上，而重試五次之後該做的就是回去複習。**             */
struct RecapConfig {
    RecapGate gate = RecapGate::ALL_CORRECT;
    int threshold = 5;
    bool allow_retry = true;
    bool count_first_only = true;   // 重試時只記第一次的成績
    int max_retries = 5;
};

// ── 一次作答的結果 ───────────────────────────────────────
struct RecapResult {
    int correct_first_try = 0;   // 第一次就對的題數
    int correct_with_hint = 0;   // 看了提示才對的
    int wrong = 0;               // 兩次都錯
    int total = 0;
    bool passed = false;

    double score() const {
        return total == 0 ? 0.0 : 100.0 * correct_first_try / total;
    }
};

// ── 單題的作答狀態 ───────────────────────────────────────
enum class AnswerVerdict {
    CORRECT_FIRST,     // 第一次就對
    WRONG_SHOW_HINT,   // 錯了，給提示，可再試
    CORRECT_AFTER_HINT,// 看提示後對了
    WRONG_SHOW_ANSWER  // 兩次都錯，給解答
};

class Recap {
public:
    /* 選項會被打亂。

       原因是題庫寫出來的時候，正解幾乎都落在第二個選項——
       30 題裡有 24 題。玩家玩兩關就會發現「不知道就選 B」，
       那道關卡的鑑別度會直接歸零。

       修法選擇「發題時洗牌」而不是「手動改題庫」，
       因為前者是結構性保證：之後新增題目也不會再出問題。            */
    explicit Recap(std::vector<McqQuestion> qs, RecapConfig cfg = {},
                   unsigned seed = 0)
        : questions_(std::move(qs)), config_(cfg) {
        if (seed != 0) shuffleOptions(seed);
        attempts_.assign(questions_.size(), 0);
        outcomes_.assign(questions_.size(), AnswerVerdict::WRONG_SHOW_HINT);
        answered_.assign(questions_.size(), false);
    }

    int questionCount() const { return static_cast<int>(questions_.size()); }
    const McqQuestion& question(int i) const { return questions_[i]; }

    // 作答一題
    AnswerVerdict answer(int qIndex, int choice) {
        if (qIndex < 0 || qIndex >= questionCount()) return AnswerVerdict::WRONG_SHOW_ANSWER;
        if (answered_[qIndex]) return outcomes_[qIndex];   // 已定案，不重複計分

        bool right = (choice == questions_[qIndex].correctIndex());
        ++attempts_[qIndex];

        if (right) {
            outcomes_[qIndex] = (attempts_[qIndex] == 1)
                              ? AnswerVerdict::CORRECT_FIRST
                              : AnswerVerdict::CORRECT_AFTER_HINT;
            answered_[qIndex] = true;
        } else if (attempts_[qIndex] == 1) {
            outcomes_[qIndex] = AnswerVerdict::WRONG_SHOW_HINT;   // 還能再試一次
        } else {
            outcomes_[qIndex] = AnswerVerdict::WRONG_SHOW_ANSWER;
            answered_[qIndex] = true;
        }
        return outcomes_[qIndex];
    }

    // 全部答完之後結算
    RecapResult finish() const {
        RecapResult r;
        r.total = questionCount();
        for (auto o : outcomes_) {
            switch (o) {
                case AnswerVerdict::CORRECT_FIRST:      ++r.correct_first_try; break;
                case AnswerVerdict::CORRECT_AFTER_HINT: ++r.correct_with_hint; break;
                default:                                 ++r.wrong;            break;
            }
        }
        r.passed = judge(r);
        return r;
    }

    // 過關判定——三種條件的差別只在這一個函式
    bool judge(const RecapResult& r) const {
        switch (config_.gate) {
            case RecapGate::ALL_CORRECT:
                // 全對：看了提示才對的也算，因為最終還是答對了
                return r.correct_first_try + r.correct_with_hint == r.total;
            case RecapGate::THRESHOLD:
                return r.correct_first_try + r.correct_with_hint >= config_.threshold;
            case RecapGate::NO_GATE:
                return true;
        }
        return true;
    }

    const RecapConfig& config() const { return config_; }

    void reset() {
        attempts_.assign(questions_.size(), 0);
        outcomes_.assign(questions_.size(), AnswerVerdict::WRONG_SHOW_HINT);
        answered_.assign(questions_.size(), false);
    }

    // 手動洗牌（測試用；正常流程在建構時做）
    void shuffleOptions(unsigned seed) {
        std::mt19937 rng(seed);
        for (auto& q : questions_)
            std::shuffle(q.options.begin(), q.options.end(), rng);
    }

private:
    std::vector<McqQuestion> questions_;
    RecapConfig config_;
    std::vector<int> attempts_;
    std::vector<AnswerVerdict> outcomes_;
    std::vector<bool> answered_;
};

/* =========================================================================
   六關的題目

   每一關五題，題型固定：
     1. 辨識     給一個局面，問哪張接得上
     2. 判斷合法 這個組合成不成立
     3. 選最佳   兩個都能出，哪個對後續有利
     4. 理解機制 為什麼規則是這樣設計的
     5. 本關招數 換一個情境問同一個技巧

   第 5 題是關鍵：它測的是遷移，不是記憶。
   學了一招之後最容易犯的錯，就是想用它——
   所以有幾題的正解刻意是「不要用剛學的那招」。
   ========================================================================= */
class RecapBank {
public:
    static std::vector<McqQuestion> forLevel(int level);
};
