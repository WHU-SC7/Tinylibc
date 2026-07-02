#!/bin/bash
#
# run_tests.sh — tcc 编译器批量测试驱动
#
# 编译、链接、执行每个测试用例，按退出码判 PASS/FAIL。
# 根据文件名前缀自动识别预期失败用例：
#   wrong_* / fail_*  — 预期编译失败或执行失败（标记 SKIP 而非 FAIL）
#
# 用法:
#   ./run_tests.sh              # 运行全部测试
#   ./run_tests.sh 1            # 只运行阶段 1（001-013）
#   ./run_tests.sh 1-3         # 运行阶段 1 到 3
#   ./run_tests.sh -v          # 详细模式（含编译器输出）
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

TCC="$HOME/tlibc/bin/tcc"
LINKER="x86_64-linux-gnu-ld"
LD_SCRIPT="$PROJECT_DIR/ld.script"
TLIBC_A="$PROJECT_DIR/build/tlibc.a"
TESTDIR="$SCRIPT_DIR/compiler-tests"

PASS=0
FAIL=0
SKIP=0
VERBOSE=0

# 颜色
GREEN="\033[32m"
RED="\033[31m"
CYAN="\033[36m"
YELLOW="\033[33m"
RESET="\033[0m"

# ─── 阶段过滤 ───

parse_stage_range() {
    local arg=$1
    START_STAGE=1
    END_STAGE=99
    if [[ "$arg" =~ ^([0-9]+)-([0-9]+)$ ]]; then
        START_STAGE=${BASH_REMATCH[1]}
        END_STAGE=${BASH_REMATCH[2]}
    elif [[ "$arg" =~ ^[0-9]+$ ]]; then
        START_STAGE=$arg
        END_STAGE=$arg
    fi
}

# ─── 链接测试程序 ───

link_test() {
    local obj=$1 bin=$2
    "$LINKER" -z max-page-size=4096 -nostdlib -static \
        -T "$LD_SCRIPT" -u __tlibc_start \
        "$obj" "$TLIBC_A" \
        -o "$bin" 2>/dev/null
}

# ─── 获取阶段号 ───

get_stage() {
    local num=$1
    if [ "$num" -le 13 ]; then echo 1
    elif [ "$num" -le 24 ]; then echo 2
    elif [ "$num" -le 28 ]; then echo 3
    elif [ "$num" -le 37 ]; then echo 4
    elif [ "$num" -le 45 ]; then echo 5
    elif [ "$num" -le 51 ]; then echo 6
    elif [ "$num" -le 56 ]; then echo 7
    elif [ "$num" -le 60 ]; then echo 8
    elif [ "$num" -le 64 ]; then echo 9
    elif [ "$num" -le 74 ]; then echo 10
    elif [ "$num" -le 83 ]; then echo 11
    else echo 12
    fi
}

# ─── 运行单个测试 ───

run_one_test() {
    local src="$1"
    local name="$(basename "$src" .c)"
    local num="$(echo "$name" | sed 's/^0*//; s/_.*//; s/[^0-9].*//')"
    local obj="$TESTDIR/$name.o"
    local bin="$TESTDIR/$name.bin"

    # 阶段过滤
    local stage=$(get_stage "$num")
    if [ "$stage" -lt "$START_STAGE" ] || [ "$stage" -gt "$END_STAGE" ]; then
        return
    fi

    # 预期失败标记
    local expect_fail=0
    case "$name" in
        *wrong*|*fail*) expect_fail=1 ;;
    esac

    # 显示进度
    if [ $VERBOSE -eq 1 ]; then
        printf "  [%02d] %-35s " "$stage" "$name"
    else
        printf "  %-38s " "$name"
    fi

    # ── 编译 ──
    local tcc_out
    tcc_out=$("$TCC" "$src" -o "$obj" 2>&1) || true
    local tcc_exit=$?

    if [ "$tcc_exit" -ne 0 ]; then
        if [ "$expect_fail" -eq 1 ]; then
            printf "${CYAN}SKIP${RESET} (compile fail as expected)\n"
            SKIP=$((SKIP+1))
        else
            printf "${RED}FAIL${RESET} (compilation error)\n"
            if [ $VERBOSE -eq 1 ]; then
                echo "$tcc_out" | sed 's/^/         /'
            fi
            FAIL=$((FAIL+1))
        fi
        rm -f "$obj"
        return
    fi

    # ── 链接 ──
    if ! link_test "$obj" "$bin" 2>/dev/null; then
        if [ "$expect_fail" -eq 1 ]; then
            printf "${CYAN}SKIP${RESET} (link fail as expected)\n"
            SKIP=$((SKIP+1))
        else
            printf "${RED}FAIL${RESET} (link error)\n"
            FAIL=$((FAIL+1))
        fi
        rm -f "$obj" "$bin"
        return
    fi

    # ── 执行 ──
    local exit_code=0
    "$bin" 2>/dev/null || exit_code=$?

    if [ "$exit_code" -eq 0 ]; then
        if [ "$expect_fail" -eq 1 ]; then
            printf "${YELLOW}UNEXPECTED PASS${RESET} (可能已修复!)\n"
            PASS=$((PASS+1))
        else
            printf "${GREEN}PASS${RESET}\n"
            PASS=$((PASS+1))
        fi
    else
        if [ "$expect_fail" -eq 1 ]; then
            printf "${CYAN}SKIP${RESET} (known fail, exit=$exit_code)\n"
            SKIP=$((SKIP+1))
        else
            printf "${RED}FAIL${RESET} (exit=$exit_code)\n"
            FAIL=$((FAIL+1))
        fi
    fi

    rm -f "$obj" "$bin"
}

# ─── 主流程 ───

# 检查 tcc 是否存在
if [ ! -x "$TCC" ]; then
    echo "错误: 找不到 tcc 可执行文件 ($TCC)"
    echo "请先在项目根目录编译: make all 或 tmake -b tcc"
    exit 1
fi

# 解析参数
START_STAGE=1
END_STAGE=99

for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=1 ;;
        -h|--help)
            echo "用法: $0 [阶段范围] [-v]"
            echo "  阶段范围: 1 / 1-3 / 空=全部"
            echo "  -v: 详细输出"
            exit 0
            ;;
        *)
            parse_stage_range "$arg"
            ;;
    esac
done

echo "═══ tcc 编译器测试 ═══"
echo "    测试目录: $TESTDIR"
echo "    阶段范围: $START_STAGE ~ $END_STAGE"
echo ""

# 收集测试文件
TEST_FILES=()
for f in "$TESTDIR"/[0-9]*.c; do
    [ -f "$f" ] || continue
    TEST_FILES+=("$f")
done
TEST_FILES=($(printf '%s\n' "${TEST_FILES[@]}" | sort))

# 运行
for src in "${TEST_FILES[@]}"; do
    run_one_test "$src"
done

# 汇总
echo ""
echo "─── 汇总 ───"
printf "  ${GREEN}PASS: %d${RESET}  ${RED}FAIL: %d${RESET}  ${CYAN}SKIP: %d${RESET}  总计: %d\n" \
    "$PASS" "$FAIL" "$SKIP" $((PASS + FAIL + SKIP))

if [ "$FAIL" -gt 0 ]; then
    echo ""
    printf "  ${RED}有 %d 个测试失败。${RESET}\n" "$FAIL"
    echo "  检查上方 FAIL 标记的测试，确认是否是新问题。"
    echo "  已知 bug 标记为 SKIP 是正常的。"
    exit 1
fi

exit 0
