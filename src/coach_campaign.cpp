#include "coach_campaign.h"
#include <algorithm>

/* =========================================================================
   六個關卡的設定表。

   引導強度從 100% 遞減到 40%，落實成兩件事：
     1. nudge/point/reveal_after_turns —— 越後面越晚開口
     2. max_tier —— 越後面越不給答案

   Level 4 起 max_tier 降到 POINT_TO_AREA、Level 6 只剩 GENTLE_NUDGE，
   所以那幾關的 reveal_after_turns 設為 -1（永不觸發）。
   ========================================================================= */
static const LevelConfig kLevels[] = {
    {
        1, Technique::ATTACH_RUN, "接龍頭尾",
        "把手上的牌接到桌面既有 Run 的前面或後面。這是最基本、也最常用的一手——"
        "先學會看桌面，再學會看自己的手牌。",
        100,
        0, 1, 2, HintTier::REVEAL_MOVE,
        3, 1,
        -1, HintTier::REVEAL_MOVE      // L1 本來就給答案，不需要保底
    },
    {
        2, Technique::COMPLETE_GROUP, "補第四色",
        "桌面上已經有三張同數字不同色的 Group，補上剩下的那一色就能出牌。"
        "Rummikub 只有四種顏色，所以 Group 最多四張——看到三張就是機會。",
        88,
        1, 2, 4, HintTier::REVEAL_MOVE,
        3, 1,
        -1, HintTier::REVEAL_MOVE      // 同上
    },
    {
        3, Technique::JOKER_FILL, "Joker 補缺口",
        "手上有 Joker 時，它可以代替 Run 中間缺掉的那個數字。"
        "但 Joker 是稀有資源——同樣分數下能不用就不用，留到後面更有價值。",
        76,
        1, 3, 5, HintTier::REVEAL_MOVE,
        3, 1,
        -1, HintTier::REVEAL_MOVE      // 同上
    },
    {
        4, Technique::INITIAL_MELD, "破冰湊 30 分",
        "還沒破冰前，第一次出牌的總分必須達到 30 分，而且不能動桌面上的牌。"
        "這一關開始，系統只會指方向，不會直接講答案。",
        64,
        2, 4, -1, HintTier::POINT_TO_AREA,
        3, 2,
        10, HintTier::REVEAL_MOVE     // 卡 10 回合破例講答案
    },
    {
        5, Technique::BOARD_RESHUFFLE, "大風吹重組",
        "把桌面上的牌拆開重拼成更長的組合，讓自己手上的牌接得上去。"
        "重組後每一組都必須合法，而且桌面上原本的牌一張都不能少。",
        52,
        2, 5, -1, HintTier::POINT_TO_AREA,
        3, 2,
        10, HintTier::REVEAL_MOVE     // 同上
    },
    {
        6, Technique::RUN_SPLIT, "長龍切斷",
        "一條六張以上的 Run 可以切成兩段，切開之後就多了兩個可以接牌的位置。"
        "這一關系統只會輕推一下——剩下的要靠自己找。",
        40,
        3, -1, -1, HintTier::GENTLE_NUDGE,
        3, 3,
        12, HintTier::POINT_TO_AREA   // 卡 12 回合破例指方向，仍不講答案
    },
};

static const int kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);

// ─────────────────────────────────────────────────────────
CoachCampaign::CoachCampaign() {
    for (int i = 0; i < static_cast<int>(Technique::TECHNIQUE_COUNT); ++i) {
        TechniqueProgress p;
        p.technique = static_cast<Technique>(i);
        progress_.push_back(p);
    }
}

const LevelConfig& CoachCampaign::levelConfig(int level) {
    int idx = level - 1;
    if (idx < 0) idx = 0;
    if (idx >= kLevelCount) idx = kLevelCount - 1;
    return kLevels[idx];
}

int CoachCampaign::totalLevels() {
    return kLevelCount;
}

// ─────────────────────────────────────────────────────────
// 沉默也是一種設計：卡關回合數還沒到門檻時，系統不開口。
// 後段關卡的門檻拉得比較高，就是要留出自行發掘的空間。
bool CoachCampaign::shouldGiveHint(int stuck_turns, HintTier& out_tier) const {
    bool ignored = false;
    return shouldGiveHint(stuck_turns, out_tier, ignored);
}

bool CoachCampaign::shouldGiveHint(int stuck_turns, HintTier& out_tier,
                                   bool& out_from_safety_net) const {
    const LevelConfig& cfg = currentConfig();
    out_from_safety_net = false;

    // 保底優先判斷：卡太久時，破例把音量調高一格。
    if (cfg.safety_net_after_turns >= 0 &&
        stuck_turns >= cfg.safety_net_after_turns) {
        out_tier = cfg.safety_net_tier;
        out_from_safety_net = true;
        return true;
    }

    // 由深到淺檢查：先看夠不夠格拿到最深的那層。
    if (cfg.reveal_after_turns >= 0 &&
        stuck_turns >= cfg.reveal_after_turns &&
        cfg.max_tier == HintTier::REVEAL_MOVE) {
        out_tier = HintTier::REVEAL_MOVE;
        return true;
    }

    if (cfg.point_after_turns >= 0 &&
        stuck_turns >= cfg.point_after_turns &&
        cfg.max_tier != HintTier::GENTLE_NUDGE) {
        out_tier = HintTier::POINT_TO_AREA;
        return true;
    }

    if (cfg.nudge_after_turns >= 0 && stuck_turns >= cfg.nudge_after_turns) {
        out_tier = HintTier::GENTLE_NUDGE;
        return true;
    }

    return false;   // 還不到開口的時候
}

// ─────────────────────────────────────────────────────────
Mastery CoachCampaign::masteryFromUse(const TechniqueUse& use) {
    if (use.saw_reveal) return Mastery::COPIED;      // 看了答案才做出來
    if (use.saw_point)  return Mastery::PROMPTED;    // 只給了方向
    return Mastery::DISCOVERED;                      // 完全靠自己
}

bool CoachCampaign::isUpgrade(Mastery current, Mastery candidate) {
    return static_cast<int>(candidate) > static_cast<int>(current);
}

void CoachCampaign::recordUse(const TechniqueUse& use) {
    int idx = static_cast<int>(use.technique);
    if (idx < 0 || idx >= static_cast<int>(progress_.size())) return;

    TechniqueProgress& p = progress_[idx];
    ++p.total_uses;
    if (!use.saw_reveal) ++p.unassisted_uses;

    Mastery candidate = masteryFromUse(use);
    if (isUpgrade(p.mastery, candidate)) {
        p.mastery = candidate;   // 只升不降——學會了就是學會了
    }
}

const TechniqueProgress& CoachCampaign::progressOf(Technique t) const {
    return progress_[static_cast<int>(t)];
}

// ─────────────────────────────────────────────────────────
// 過關要同時滿足兩件事：用得夠多（熟練），且有幾次沒看答案（真的懂）。
bool CoachCampaign::canAdvance() const {
    const LevelConfig& cfg = currentConfig();
    const TechniqueProgress& p = progressOf(cfg.technique);
    return p.total_uses >= cfg.required_uses &&
           p.unassisted_uses >= cfg.required_unassisted;
}

bool CoachCampaign::advance() {
    if (current_level_ >= kLevelCount) return false;
    ++current_level_;
    return true;
}

// ─────────────────────────────────────────────────────────
std::string CoachCampaign::techniqueName(Technique t) {
    switch (t) {
        case Technique::ATTACH_RUN:      return "接龍頭尾";
        case Technique::COMPLETE_GROUP:  return "補第四色";
        case Technique::JOKER_FILL:      return "Joker 補缺口";
        case Technique::INITIAL_MELD:    return "破冰湊 30 分";
        case Technique::BOARD_RESHUFFLE: return "大風吹重組";
        case Technique::RUN_SPLIT:       return "長龍切斷";
        default:                         return "未知技巧";
    }
}

std::string CoachCampaign::masteryStars(Mastery m) {
    switch (m) {
        case Mastery::LOCKED:     return "";
        case Mastery::COPIED:     return "*";
        case Mastery::PROMPTED:   return "**";
        case Mastery::DISCOVERED: return "***";
        default:                  return "";
    }
}

// ─────────────────────────────────────────────────────────
CoachCampaign::AnswerResult CoachCampaign::judge(
    const McqQuestion& q, int chosen_index, int attempt) {

    if (chosen_index >= 0 &&
        chosen_index < static_cast<int>(q.options.size()) &&
        q.options[chosen_index].correct) {
        return AnswerResult::CORRECT;
    }
    // 答錯第一次只給提示，第二次才給解答——不讓玩家用猜的過關。
    return (attempt <= 1) ? AnswerResult::WRONG_FIRST_TRY
                          : AnswerResult::WRONG_SHOW_ANSWER;
}

// ─────────────────────────────────────────────────────────
// 每關 5 題，題型固定：辨識 / 判斷合法 / 選最佳 / 理解機制 / 本關招數。
// 第 5 題一律換一個情境問同一個技巧——測的是遷移，不是記憶。
std::vector<McqQuestion> CoachCampaign::recapFor(int level) {
    std::vector<McqQuestion> qs;

    if (level == 1) {
        qs.push_back({
            "桌面上有一排「紅 4、紅 5、紅 6」，你手上有紅 3、藍 7、黑 4。哪一張接得上去？",
            {{"紅 3", true}, {"藍 7", false}, {"黑 4", false}, {"都接不上", false}},
            "Run 要同色而且數字連續，先看顏色對不對。",
            "紅 3 接在紅 4 前面，變成紅 3-4-5-6。藍 7 顏色不對；黑 4 顏色不對且數字重複。"
        });
        qs.push_back({
            "「紅 5、紅 6、紅 8」是合法的 Run 嗎？",
            {{"是", false}, {"不是，數字不連續", true},
             {"不是，張數不夠", false}, {"不是，顏色不同", false}},
            "數一數 6 跟 8 之間少了什麼。",
            "6 和 8 中間缺了 7，數字不連續。除非有 Joker 補上那個位置，否則不成立。"
        });
        qs.push_back({
            "你手上有紅 3 和黑 9，兩張都接得上桌面。優先出哪一張比較好？",
            {{"紅 3，數字小失分少", false},
             {"黑 9，分數高先出掉", true},
             {"隨便都一樣", false},
             {"兩張都留著", false}},
            "遊戲結束時手上剩的牌會算成失分，想想哪一張留著比較虧。",
            "最後手牌會依面值計算失分，所以優先出掉大數字的牌。同樣接得上時，先出黑 9。"
        });
        qs.push_back({
            "為什麼提交一組牌時，桌面上原本的牌一張都不能少？",
            {{"因為規則禁止移動", false},
             {"因為要確保重組後每一組仍然合法、且沒有牌憑空消失", true},
             {"因為對手會抗議", false},
             {"因為程式會當掉", false}},
            "想想如果可以偷偷拿走桌上的牌會發生什麼事。",
            "允許牌消失就等於允許作弊。引擎會逐張比對，只要有一張不見就整批退回。"
        });
        qs.push_back({
            "桌面有「藍 10、藍 11、藍 12」，你手上有藍 9 和藍 13。這一手最多能出幾張？",
            {{"1 張", false}, {"2 張，接在頭跟尾", true},
             {"0 張", false}, {"3 張", false}},
            "Run 的兩端都可以接，不是只能接一邊。",
            "藍 9 接前面、藍 13 接後面，一次出兩張，變成藍 9 到藍 13 的五張 Run。"
        });
    }
    else if (level == 2) {
        qs.push_back({
            "桌面有「紅 7、藍 7、黑 7」，你手上有黃 7、紅 8、藍 7。哪一張補得上去？",
            {{"黃 7", true}, {"紅 8", false}, {"藍 7", false}, {"都不行", false}},
            "Group 要同數字、不同色，看看桌上缺哪一色。",
            "桌上已有紅藍黑，只缺黃色。藍 7 顏色重複，紅 8 數字不同。"
        });
        qs.push_back({
            "「紅 5、藍 5、黑 5、黃 5、紅 5」是合法的 Group 嗎？",
            {{"是，五張同數字", false},
             {"不是，Group 最多四張", true},
             {"不是，數字不同", false},
             {"不是，張數不夠", false}},
            "Rummikub 一共有幾種顏色？",
            "只有四種顏色，所以 Group 最多四張。第五張必然重複顏色，不合法。"
        });
        qs.push_back({
            "桌面同時有一個三張的 Group 和一條 Run，你的牌兩邊都接得上。先補哪個？",
            {{"補 Group，因為補滿之後就沒人能用了", true},
             {"接 Run，因為 Run 比較長", false},
             {"都可以，沒差別", false},
             {"先不出，留著觀察", false}},
            "想想哪一個位置比較稀有——補滿之後對手還有機會嗎？",
            "Group 補到四張就滿了，那個位置消失。Run 的兩端之後還可以繼續接，機會不會消失。"
        });
        qs.push_back({
            "為什麼 Group 規定顏色必須互異？",
            {{"為了美觀", false},
             {"因為同色同數字的牌只有兩張，允許重複會讓組合過於容易", true},
             {"因為規則書這樣寫", false},
             {"為了讓 Joker 有用武之地", false}},
            "想想每種「顏色＋數字」的組合，牌堆裡各有幾張。",
            "牌堆中每種顏色數字各有兩張。若允許同色重複，湊 Group 會變得太容易，遊戲失去難度。"
        });
        qs.push_back({
            "桌面有「紅 3、藍 3、黑 3」和「紅 9、藍 9、黃 9」，你手上有黃 3 和黑 9。這一手能出幾張？",
            {{"1 張", false}, {"2 張，兩個 Group 各補一張", true},
             {"0 張", false}, {"要看破冰了沒", false}},
            "兩組都是三張，各缺一色——你手上剛好都有。",
            "黃 3 補第一組、黑 9 補第二組，同一手出兩張。已破冰的情況下這是完全合法的。"
        });
    }
    else if (level == 3) {
        qs.push_back({
            "桌面有「紅 4、紅 5」，你手上有紅 7 和一張 Joker。能出牌嗎？",
            {{"能，Joker 當紅 6 接成 4-5-6-7", true},
             {"不能，缺太多張", false},
             {"能，但只能出 Joker", false},
             {"不能，Joker 不能放中間", false}},
            "Joker 可以代替任何一張牌，包括中間缺掉的那個數字。",
            "Joker 代替紅 6，加上手上的紅 7，湊成紅 4-5-6-7 的四張 Run。"
        });
        qs.push_back({
            "「紅 3、Joker、紅 6」是合法的 Run 嗎？",
            {{"是，Joker 補中間", false},
             {"不是，一張 Joker 補不了兩格缺口", true},
             {"不是，張數不夠", false},
             {"是，Joker 可以變兩張", false}},
            "3 跟 6 之間缺了幾個數字？一張 Joker 能代替幾張牌？",
            "3 和 6 之間缺 4 和 5 兩張，一張 Joker 只能代替一張牌，補不起來。"
        });
        qs.push_back({
            "你手上有 Joker，現在有兩個選擇：用它湊一組 30 分破冰，或留著等更好的機會。怎麼選？",
            {{"一定要留著，Joker 很珍貴", false},
             {"看還沒破冰的話就先破冰——不能出牌等於零分", true},
             {"一定先用掉，免得被對手搶", false},
             {"隨便，沒差", false}},
            "想想不破冰的話，你這一整局能做什麼。",
            "沒破冰就完全不能出牌，手牌只會越積越多。能破冰時先破冰，Joker 的價值要在能出牌之後才談得上。"
        });
        qs.push_back({
            "為什麼同樣分數的組合，會優先選擇用比較少 Joker 的那個？",
            {{"因為 Joker 分數比較低", false},
             {"因為 Joker 能代替任何牌，留在手上的彈性遠大於一般牌", true},
             {"因為規則限制 Joker 數量", false},
             {"因為 Joker 容易被對手拿走", false}},
            "想想哪一張牌能用在最多種不同的組合裡。",
            "一般牌只能用在特定位置，Joker 哪裡都能填。同分時留下 Joker，後面的選擇會多很多。"
        });
        qs.push_back({
            "桌面有「藍 11、藍 12、藍 13」，你手上只有一張 Joker。能接上去嗎？",
            {{"能，接在藍 13 後面", false},
             {"不能，13 已經是最大，後面沒有數字可以代替", true},
             {"能，接在藍 11 前面當藍 10", true},
             {"完全不能出", false}},
            "Run 的數字範圍是 1 到 13，Joker 代替的那個數字也必須落在這個範圍內。",
            "接在後面會變成 14，超出範圍不合法；接在前面當藍 10 則完全可以。"
            "這題有兩個選項描述同一個正確判斷——重點是理解 Joker 的值也受 1–13 限制。"
        });
    }
    else if (level == 4) {
        qs.push_back({
            "還沒破冰，你手上有「紅 10、紅 11、紅 12」。這一手能出嗎？",
            {{"能，剛好 33 分達標", true},
             {"不能，張數不夠", false},
             {"不能，破冰要四張以上", false},
             {"要看桌面狀況", false}},
            "把三張的面值加起來，跟 30 比較看看。",
            "10+11+12 = 33，超過 30 分門檻，而且是合法的 Run，可以破冰。"
        });
        qs.push_back({
            "破冰時可以把桌面上原有的牌拆開重組嗎？",
            {{"可以，只要重組後合法", false},
             {"不可以，破冰前不能動桌面", true},
             {"可以，但只能動自己出過的", false},
             {"要看對手同不同意", false}},
            "想想這條規則是在保護什麼。",
            "破冰前不能重組桌面。這條規則存在的意義是：還沒證明自己有 30 分實力前，"
            "不能借用別人已經打出來的牌。"
        });
        qs.push_back({
            "手上有兩種湊法：A 用掉一張 Joker 剛好 30 分，B 不用 Joker 但只有 28 分。選哪個？",
            {{"選 B，省下 Joker 比較划算", false},
             {"選 A——28 分不能破冰，等於完全不能出牌", true},
             {"兩個都不出，繼續等", false},
             {"看對手破冰了沒", false}},
            "28 分達得到門檻嗎？達不到的話，B 這個選項實際上存在嗎？",
            "門檻是 30 分，28 分不成立。這不是取捨題——B 根本不是一個可行的選項。"
        });
        qs.push_back({
            "為什麼要設破冰門檻這條規則？",
            {{"為了讓遊戲久一點", false},
             {"為了避免玩家一開局就用零星小牌不斷蹭桌面，讓前期需要真正的組合能力", true},
             {"為了保護先手玩家", false},
             {"為了讓 Joker 更有價值", false}},
            "想想如果沒有這條規則，第一手可以做什麼。",
            "沒有門檻的話，任何人都能靠單張牌接龍蹭分。門檻逼你先湊出有份量的組合才准入場。"
        });
        qs.push_back({
            "破冰計分時，Joker 算幾分？",
            {{"固定 30 分", false},
             {"算它所代替的那張牌的面值", true},
             {"算 0 分", false},
             {"算 10 分", false}},
            "想想如果 Joker 固定算 30 分，破冰會變得多容易。",
            "破冰計分時 Joker 算它代表的值。若固定算 30，單靠一張 Joker 就能破冰，門檻形同虛設。"
        });
    }
    else if (level == 5) {
        qs.push_back({
            "桌面有「紅 1-2-3」和「紅 5-6-7」，你手上有紅 4。最好的做法是？",
            {{"接在紅 3 後面", false},
             {"接在紅 5 前面", false},
             {"用紅 4 把兩條接成 紅 1 到 7 的一整條", true},
             {"留著不出", false}},
            "紅 4 剛好是兩條之間缺的那一張，想想能不能一次連起來。",
            "紅 4 填在中間，兩條併成 1-2-3-4-5-6-7。這比單純接在某一端更有價值——"
            "重組後的長 Run 之後能接的位置更多。"
        });
        qs.push_back({
            "重組桌面後，下列哪一項是必須成立的？",
            {{"總張數不變", false},
             {"原本桌上每一張牌都還在，且每一組都合法", true},
             {"組數不能變", false},
             {"顏色分布不能變", false}},
            "想想引擎會檢查什麼——什麼情況下重組會被判定為作弊？",
            "牌不能消失，每一組都要合法。組數和排列可以隨便變，但這兩條是底線。"
        });
        qs.push_back({
            "重組桌面時，為什麼要優先跳過已經合法的四張 Group，不把它拆進顏色堆？",
            {{"因為拆了會扣分", false},
             {"因為四張 Group 已經滿了，拆散之後很可能湊不回來", true},
             {"因為規則禁止拆 Group", false},
             {"因為 Group 比 Run 值錢", false}},
            "四張 Group 已經用掉四種顏色，拆開後那四張要重新找位置。",
            "四張 Group 是滿的，拆散後每張都要另尋歸宿，很容易變成拼不回去的死局。跳過它最安全。"
        });
        qs.push_back({
            "重組之後如果發現有一組不合法，應該怎麼處理？",
            {{"只退回那一組", false},
             {"整批退回，桌面完全恢復原狀", true},
             {"讓引擎自動修正", false},
             {"從手牌補一張進去", false}},
            "想想部分退回會發生什麼——那些已經被移動過的牌怎麼算？",
            "整批退回。重組是一個不可分割的動作，部分成立會讓桌面處於不確定的中間狀態。"
        });
        qs.push_back({
            "什麼時候最適合發動大風吹重組？",
            {{"每一回合都做，越多越好", false},
             {"對手連續抽牌好幾輪、明顯卡關的時候", true},
             {"剛破冰的時候", false},
             {"手上牌很少的時候", false}},
            "重組會把桌面變得更好接——想想這對誰有利。",
            "重組後的桌面對雙方都更好接。對手卡關時做，他來不及利用；他手順時做，等於幫他。"
        });
    }
    else {  // level 6
        qs.push_back({
            "桌面有一條「黑 1 到黑 8」的八張 Run，你手上有黑 4 和黑 5。直接接得上嗎？",
            {{"接得上，接在頭尾", false},
             {"接不上，但可以把長 Run 切開製造接點", true},
             {"接不上，只能抽牌", false},
             {"接得上，插進中間", false}},
            "中間的位置已經被佔滿了，想想有沒有辦法製造出新的頭尾。",
            "黑 4、5 在 1-8 的範圍內，兩端接不上。但把長 Run 切成 1-2-3-4 和 5-6-7-8 之後，"
            "就多出兩個新的端點可以接。"
        });
        qs.push_back({
            "一條 Run 至少要幾張，切開之後兩半才都還合法？",
            {{"4 張", false}, {"6 張", true}, {"8 張", false}, {"3 張", false}},
            "Run 的最小長度是幾張？切成兩半後兩邊都要滿足這個條件。",
            "Run 最少三張，切成兩半後兩邊都要有三張，所以原本至少要六張。"
        });
        qs.push_back({
            "一條九張的 Run 有多個切點，該選哪一個？",
            {{"永遠切正中間", false},
             {"選切完之後手牌能接上最多張的那個位置", true},
             {"隨便切都一樣", false},
             {"切在數字最小的地方", false}},
            "切開的目的是製造接點，那哪個切點對你自己最有利？",
            "切點的價值取決於切完後你能接上幾張牌。要模擬每個切點，選收益最高的。"
        });
        qs.push_back({
            "為什麼切斷長 Run 有風險？",
            {{"會扣分", false},
             {"新產生的端點對手也能用", true},
             {"引擎不允許", false},
             {"會讓自己的牌變少", false}},
            "切開之後多出來的那兩個接點，只有你能用嗎？",
            "桌面是公共的。你製造的接點對手同樣能接——所以要在自己能立刻獲利時才切。"
        });
        qs.push_back({
            "同時可以「切斷長 Run 出兩張」或「接在別條 Run 尾端出三張」，選哪個？",
            {{"切斷，因為技巧比較高級", false},
             {"接尾端，出三張比兩張多——除非切斷能連帶清掉更多手牌", true},
             {"都不做，先觀察", false},
             {"看對手怎麼做", false}},
            "先比較這一手實際出掉幾張，再想有沒有連帶效果。",
            "技巧本身沒有高下，看的是實際收益。出三張優於出兩張，"
            "除非切斷之後還能連帶接上更多牌。"
        });
    }

    return qs;
}
