#!/usr/bin/env bash
#
# run_tests.sh —— 一次跑完所有測試
#
# 用法：
#   ./run_tests.sh          跑全部
#   ./run_tests.sh coach    只跑名稱含 coach 的
#   ./run_tests.sh -q       只印摘要
#   ./run_tests.sh -k       第一個失敗就停
#
# 編譯產物放在 .testbuild/，不會弄髒工作目錄。

set -uo pipefail
cd "$(dirname "$0")"

BUILD=.testbuild
mkdir -p "$BUILD"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2"

# ── 顏色（沒有 tty 就不上色，例如導向到檔案時）──────────
if [ -t 1 ]; then
    R=$'\033[31m'; G=$'\033[32m'; Y=$'\033[33m'
    B=$'\033[1m'; D=$'\033[2m'; N=$'\033[0m'
else
    R=''; G=''; Y=''; B=''; D=''; N=''
fi

QUIET=0
KEEPGOING=1
FILTER=""

for arg in "$@"; do
    case "$arg" in
        -q|--quiet)  QUIET=1 ;;
        -k|--fail-fast) KEEPGOING=0 ;;
        -h|--help)
            sed -n '2,12p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) FILTER="$arg" ;;
    esac
done

TOTAL_PASS=0
TOTAL_FAIL=0
SUITES_OK=0
SUITES_BAD=0
FAILED_NAMES=()

# ── 跑一組測試 ───────────────────────────────────────────
# $1 顯示名稱   $2 產出的執行檔名   $3.. 要編譯的來源檔
run_suite() {
    local name="$1"; shift
    local bin="$1";  shift

    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        return 0
    fi

    printf "  %-26s" "$name"

    local log="$BUILD/$bin.log"
    if ! $CXX $CXXFLAGS "$@" -o "$BUILD/$bin" > "$log" 2>&1; then
        printf "%s編譯失敗%s\n" "$R" "$N"
        [ "$QUIET" -eq 0 ] && sed 's/^/      /' "$log" | head -12
        SUITES_BAD=$((SUITES_BAD+1))
        FAILED_NAMES+=("$name（編譯失敗）")
        [ "$KEEPGOING" -eq 0 ] && finish_and_exit
        return 1
    fi

    local out
    out=$("$BUILD/$bin" 2>&1)
    local rc=$?

    # 測試程式的最後一行是「通過 N 項，失敗 M 項」
    local pass fail
    pass=$(printf '%s' "$out" | grep -o '通過 [0-9]* 項' | tail -1 | grep -o '[0-9]*')
    fail=$(printf '%s' "$out" | grep -o '失敗 [0-9]* 項' | tail -1 | grep -o '[0-9]*')
    pass=${pass:-0}
    fail=${fail:-0}

    TOTAL_PASS=$((TOTAL_PASS+pass))
    TOTAL_FAIL=$((TOTAL_FAIL+fail))

    if [ "$rc" -eq 0 ] && [ "$fail" -eq 0 ]; then
        printf "%s✓%s  %3d 項\n" "$G" "$N" "$pass"
        SUITES_OK=$((SUITES_OK+1))
    else
        printf "%s✗%s  %3d 通過 / %s%d 失敗%s\n" "$R" "$N" "$pass" "$R" "$fail" "$N"
        SUITES_BAD=$((SUITES_BAD+1))
        FAILED_NAMES+=("$name")
        if [ "$QUIET" -eq 0 ]; then
            printf '%s' "$out" | grep 'FAIL' | sed 's/^/      /'
        fi
        [ "$KEEPGOING" -eq 0 ] && finish_and_exit
    fi
}

# ── 跑一個示範程式（不計分，只確認能跑）──────────────────
run_demo() {
    local name="$1"; shift
    local bin="$1";  shift

    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        return 0
    fi

    printf "  %-26s" "$name"
    local log="$BUILD/$bin.log"
    if ! $CXX $CXXFLAGS "$@" -o "$BUILD/$bin" > "$log" 2>&1; then
        printf "%s編譯失敗%s\n" "$R" "$N"
        [ "$QUIET" -eq 0 ] && sed 's/^/      /' "$log" | head -12
        SUITES_BAD=$((SUITES_BAD+1))
        FAILED_NAMES+=("$name（編譯失敗）")
        return 1
    fi
    if "$BUILD/$bin" > "$BUILD/$bin.out" 2>&1; then
        printf "%s✓%s  %s能執行%s\n" "$G" "$N" "$D" "$N"
        SUITES_OK=$((SUITES_OK+1))
    else
        printf "%s✗%s  執行失敗\n" "$R" "$N"
        SUITES_BAD=$((SUITES_BAD+1))
        FAILED_NAMES+=("$name（執行失敗）")
    fi
}

finish_and_exit() {
    echo
    printf "  %s─────────────────────────────────────────%s\n" "$D" "$N"
    if [ "$TOTAL_FAIL" -eq 0 ] && [ "$SUITES_BAD" -eq 0 ]; then
        printf "  %s%s全部通過%s   %d 組 · %d 項\n" \
               "$B" "$G" "$N" "$SUITES_OK" "$TOTAL_PASS"
        exit 0
    else
        printf "  %s%s有失敗%s   %d 組通過 / %s%d 組失敗%s   共 %d 項通過 / %s%d 項失敗%s\n" \
               "$B" "$R" "$N" "$SUITES_OK" "$R" "$SUITES_BAD" "$N" \
               "$TOTAL_PASS" "$R" "$TOTAL_FAIL" "$N"
        if [ ${#FAILED_NAMES[@]} -gt 0 ]; then
            echo
            printf "  失敗的：\n"
            for n in "${FAILED_NAMES[@]}"; do printf "    · %s\n" "$n"; done
        fi
        exit 1
    fi
}

# ══════════════════════════════════════════════════════════
printf "\n  %sRummikub AI Agent · 測試%s\n" "$B" "$N"
[ -n "$FILTER" ] && printf "  %s只跑名稱含「%s」的%s\n" "$D" "$FILTER" "$N"
echo

SRC_CORE="src/tile.cpp src/validator.cpp"

printf "  %s遊戲引擎%s\n" "$B" "$N"
run_suite "validator" "t_validator" \
    -I src tests/test_validator.cpp $SRC_CORE
run_suite "Board / Player / GM" "t_core" \
    -I src tests/test_engine_core.cpp src/board.cpp src/player.cpp \
    src/game_manager.cpp $SRC_CORE
run_suite "關卡與偵測器（舊版）" "t_campaign" \
    -I src tests/test_campaign.cpp src/coach_campaign.cpp \
    src/technique_detector.cpp $SRC_CORE

echo
printf "  %s教練引擎%s\n" "$B" "$N"
run_suite "抽象引擎（假領域）" "t_engine" \
    -I coach coach/test_engine.cpp
run_suite "拉密領域" "t_domain" \
    -I coach coach/domains/test_rummikub_domain.cpp $SRC_CORE
run_suite "五種模式" "t_modes" \
    -I coach coach/test_modes.cpp
run_suite "整合層" "t_session" \
    -I coach coach/test_session.cpp coach/recap/recap_bank.cpp $SRC_CORE

echo
printf "  %s教學模組%s\n" "$B" "$N"
run_suite "自訂挑戰" "t_battle" \
    -I coach/battle coach/battle/test_battle.cpp
run_suite "Recap MCQ" "t_recap" \
    -I coach/recap coach/recap/test_recap.cpp coach/recap/recap_bank.cpp

echo
printf "  %s強化學習%s\n" "$B" "$N"
run_suite "Actor-Critic" "t_rl" \
    -I rl rl/test_rl.cpp

echo
printf "  %s示範程式（只確認能跑）%s\n" "$B" "$N"
run_demo "雙領域對照" "d_demo" \
    -I coach coach/demo.cpp
run_demo "拉密領域" "d_rummi" \
    -I coach coach/domains/demo_rummikub.cpp $SRC_CORE
run_demo "五種模式" "d_modes" \
    -I coach coach/demo_modes.cpp
run_demo "整合流程" "d_session" \
    -I coach coach/demo_session.cpp coach/recap/recap_bank.cpp $SRC_CORE
run_demo "自訂挑戰" "d_battle" \
    -I coach/battle coach/battle/demo_battle.cpp

finish_and_exit
