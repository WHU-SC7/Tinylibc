#!/bin/bash
#
# run_test.sh — 编译器测试套件（根目录统一入口）
#
# 用法:
#   bash run_test.sh                      # 运行全部测试
#   bash run_test.sh 086 090              # 按编号选择
#   bash run_test.sh --phase 1            # 阶段 1 (001-020)
#   bash run_test.sh --list               # 列出
#   bash run_test.sh --count              # 统计
#   CC=./build/output/tcc_self bash run_test.sh  # 用自编译 tcc
#
# 阶段:
#   1: 001-020  基础    2: 021-040  指针/数组/函数
#   3: 041-060  结构体   4: 061-080  预处理/复杂
#   5: 081-093  自举模式
#   lib: lib/ 目录（库编译模式自检）
#
# 每个 .c 测试文件通过退出码验证，预期值写在 .EXPECT: <N> 注释中。

DIR="$(dirname "$0")/compiler-tests"
TCC="${CC:-./build/output/toyc}"
RT="${RT:-build/app/compiler/toyc_rt.o}"
START="${START:-build/app/compiler/toyc_rt_start.o}"
LD="${LD:-x86_64-linux-gnu-ld}"
LDFLAGS="-nostdlib -static -T ld.script"

R=$'\e[31m' G=$'\e[32m' Y=$'\e[33m' B=$'\e[34m' N=$'\e[0m'

usage() { sed -n '2,13p' "$0" | sed 's/^#//'; exit 0; }
list_tests() { for f in "$DIR"/[0-9]*.c; do echo "  $(basename "$f" .c)"; done; exit 0; }
count_tests() { ls "$DIR"/[0-9]*.c 2>/dev/null | wc -l; exit 0; }

select_tests() {
    local GLOB="[0-9][0-9][0-9]*_*.c"
    case "$1" in
        --phase) local r; r=$(ls "$DIR"/$GLOB 2>/dev/null | sort)
            case "$2" in 1) echo "$r" | head -20 ;; 2) echo "$r" | sed -n '21,40p' ;;
            3) echo "$r" | sed -n '41,60p' ;; 4) echo "$r" | sed -n '61,80p' ;;
            5) echo "$r" | sed -n '81,99p' ;;
            lib) ls "$DIR/lib"/[0-9]*_*.c 2>/dev/null | sort ;;
            *) echo "${R}未知阶段 $2${N}"; exit 1 ;; esac ;;
        "") ls "$DIR"/$GLOB | sort ;;
        *) for n in "$@"; do ls "$DIR"/$GLOB 2>/dev/null | grep -E "/0*${n}_"; done ;;
    esac
}

case "$1" in --count) count_tests ;; --list) list_tests ;; --help|-h) usage ;; esac

TESTS=()
[ $# -eq 0 ] && set -- ""
case "$1" in
    --phase) mapfile -t TESTS < <(select_tests "$@") ;;
    "") mapfile -t TESTS < <(select_tests) ;;
    *)  mapfile -t TESTS < <(select_tests "$@") ;;
esac

[ ${#TESTS[@]} -eq 0 ] && echo "${Y}没有匹配的测试${N}" && exit 0

PASS=0; FAIL=0; TOTAL=${#TESTS[@]}
printf "${B}=== %d 个测试 ===${N}\n\n" "$TOTAL"

for testfile in "${TESTS[@]}"; do
    name=$(basename "$testfile" .c)
    $TCC "$testfile" -o "/tmp/${name}.o" 2>/dev/null || {
        printf "  %-35s ${R}COMPILE_ERR${N}\n" "$name"; FAIL=$((FAIL+1)); continue
    }
    $LD $LDFLAGS "/tmp/${name}.o" "$RT" "$START" -o "/tmp/${name}" 2>/dev/null || {
        printf "  %-35s ${R}LINK_ERR${N}\n" "$name"; FAIL=$((FAIL+1)); continue
    }
    timeout 3 "/tmp/${name}" >/dev/null 2>&1; got=$?
    expected=$(sed -n 's/.*EXPECT: *\([0-9]*\).*/\1/p' "$testfile" | head -1)
    [ -z "$expected" ] && expected=0

    if [ "$got" = "$expected" ]; then
        printf "  %-35s ${G}ok${N} (%d)\n" "$name" "$got"
        PASS=$((PASS+1))
    else
        printf "  %-35s ${R}FAIL${N}: want %d got %d\n" "$name" "$expected" "$got"
        FAIL=$((FAIL+1))
    fi
done

printf "\n${B}结果:${N} ${G}%d/%d 通过${N}, " "$PASS" "$TOTAL"
[ "$FAIL" -eq 0 ] && printf "${G}%d 失败${N}\n" "$FAIL" || printf "${R}%d 失败${N}\n" "$FAIL"
[ "$FAIL" -eq 0 ]
