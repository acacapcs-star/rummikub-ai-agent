/* =========================================================================
   ipc_demo.cpp —— 用 IPC 取代全域信箱的最小示範

   這個檔案跟主專案零耦合：不 include 任何既有標頭、不進 CMakeLists、
   不影響任何現有的建置。它只證明一件事——

     coach_hint_bridge.h 現在用兩個全域字串當信箱，
     那個設計換來了層與層的獨立，但代價是綁死在單一行程裡。
     用 IPC 可以同時拿到兩者。

   為什麼是這個問題：
     現在的 g_current_coach_hint 是 inline 全域變數。教練把提示寫進去，
     GameManager 匯出 state.json 時讀出來。兩個類別互不知道對方存在——
     那是它的好處。

     但那也表示：
       · 兩者必須在同一個行程、同一條執行緒
       · 測試之間要記得清空全域狀態
       · 求解器算得久的話，整個遊戲迴圈跟著卡住
       · 任何一邊崩潰，另一邊一起死

   IPC 保留獨立性，同時解決上面四件事。

   編譯：
     g++ -std=c++17 ipc_demo.cpp -o ipc_demo && ./ipc_demo

   只用 POSIX pipe 與 fork，macOS 與 Linux 內建，不需要任何函式庫。
   ========================================================================= */

#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

// ── 訊息格式 ─────────────────────────────────────────────
//
// 真實系統會用 JSON 或 protobuf。這裡用最簡單的長度前綴，
// 因為重點是「兩個行程能不能各自獨立」，不是序列化。
//
//   [4 bytes 長度][內容]
//
namespace wire {

bool sendMsg(int fd, const std::string& msg) {
    uint32_t len = static_cast<uint32_t>(msg.size());
    if (write(fd, &len, sizeof(len)) != sizeof(len)) return false;
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(len)) {
        ssize_t n = write(fd, msg.data() + written, len - written);
        if (n <= 0) return false;
        written += n;
    }
    return true;
}

bool recvMsg(int fd, std::string& out) {
    uint32_t len = 0;
    ssize_t n = read(fd, &len, sizeof(len));
    if (n != sizeof(len)) return false;      // 對方關了管線
    out.assign(len, '\0');
    ssize_t got = 0;
    while (got < static_cast<ssize_t>(len)) {
        ssize_t r = read(fd, &out[got], len - got);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

}  // namespace wire

// ── 教練行程 ─────────────────────────────────────────────
//
// 它只做一件事：收到局面，回傳提示。
// 它不知道遊戲主程式長什麼樣，也不知道提示會被寫進 state.json。
// 那正是原本全域信箱想達成的獨立性——只是現在它真的是獨立的行程。
//
void coachProcess(int readFd, int writeFd) {
    std::string request;
    while (wire::recvMsg(readFd, request)) {
        // 刻意的延遲，模擬求解器要算一段時間。
        // 在全域信箱的版本裡，這段時間整個遊戲迴圈是卡住的。
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::string hint;
        if (request.find("stuck=3") != std::string::npos) {
            hint = R"({"tier":"POINT_TO_AREA","text":"紅色那一區還有東西"})";
        } else if (request.find("stuck=1") != std::string::npos) {
            hint = R"({"tier":"GENTLE_NUDGE","text":"再看一次桌面"})";
        } else {
            hint = R"({"tier":"NONE","text":""})";
        }

        if (!wire::sendMsg(writeFd, hint)) break;
    }
    close(readFd);
    close(writeFd);
    _exit(0);
}

// ── 遊戲主行程 ───────────────────────────────────────────
//
// 它送出局面、繼續做自己的事、之後才去收提示。
// 送出跟收回之間的那段時間，它是活的——這是全域信箱做不到的。
//
int main() {
    int toCoach[2];      // 主程式 → 教練
    int fromCoach[2];    // 教練 → 主程式

    if (pipe(toCoach) < 0 || pipe(fromCoach) < 0) {
        std::cerr << "pipe 建立失敗\n";
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork 失敗\n";
        return 1;
    }

    if (pid == 0) {
        // ── 子行程：教練 ──
        close(toCoach[1]);
        close(fromCoach[0]);
        coachProcess(toCoach[0], fromCoach[1]);
        return 0;  // 不會走到
    }

    // ── 父行程：遊戲主程式 ──
    close(toCoach[0]);
    close(fromCoach[1]);

    std::cout << "── IPC 版本的教練橋接 ──\n\n";

    const char* turns[] = {
        "turn=1 stuck=0 board=[...]",
        "turn=2 stuck=1 board=[...]",
        "turn=3 stuck=3 board=[...]",
    };

    for (const char* state : turns) {
        auto t0 = std::chrono::steady_clock::now();

        wire::sendMsg(toCoach[1], state);
        std::cout << "送出局面：" << state << "\n";

        // 這裡是重點：送出之後主程式沒有停下來。
        // 全域信箱的版本在這個位置是同步呼叫，整個迴圈卡住 300ms。
        std::cout << "  （主程式繼續處理其他事，沒有等待）\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::string hint;
        if (!wire::recvMsg(fromCoach[0], hint)) break;

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();

        std::cout << "收到提示：" << hint << "\n";
        std::cout << "  這一輪共 " << ms << " ms\n\n";
    }

    close(toCoach[1]);
    close(fromCoach[0]);
    waitpid(pid, nullptr, 0);

    std::cout << "── 教練行程已結束，主程式仍然活著 ──\n";
    std::cout << "\n這一點在全域信箱的版本裡做不到：\n";
    std::cout << "  兩者共用同一個位址空間，一邊崩潰另一邊一起死。\n";
    return 0;
}
