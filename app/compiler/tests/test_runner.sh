#!/bin/bash
# test_runner.sh — 编译器测试套件
#
# 每个 .c 文件是独立测试用例，编译后运行验证退出码。
# 失败时打印实际输出。
#
# 用法:
#   bash test_runner.sh              # 运行所有测试
#   bash test_runner.sh 086_*        # 运行单个测试
#   CC=./build/output/tcc_self bash test_runner.sh  # 用自编译 tcc

TCC="${CC:-./build/output/tcc}"
RT=build/app/compiler/tcc_rt.o
START=build/app/compiler/tcc_rt_start.o
LD=x86_64-linux-gnu-ld

PASS=0
FAIL=0

for testfile in "${@:-./app/compiler/tests/0"*".c}"; do
    name=$(basename "$testfile" .c)
    echo -n "  $name ... "

    # 编译
    $TCC "$testfile" -o "/tmp/${name}.o" 2>/tmp/compile.log
    if [ $? -ne 0 ]; then
        echo "COMPILE FAIL"; cat /tmp/compile.log
        FAIL=$((FAIL + 1)); continue
    fi

    # 链接
    $LD -nostdlib -static -T ld.script \
        "/tmp/${name}.o" "$RT" "$START" -o "/tmp/${name}" 2>/tmp/link.log
    if [ $? -ne 0 ]; then
        echo "LINK FAIL"; cat /tmp/link.log
        FAIL=$((FAIL + 1)); continue
    fi

    # 运行
    timeout 3 /tmp/"${name}" >/tmp/run.log 2>&1
    got=$?
    expected=$(grep -m1 'EXPECT:' "$testfile" | sed 's/.*EXPECT: *//' || echo "0")
    if [ "$got" = "$expected" ]; then
        echo "PASS (exit=$got)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: expected $expected, got $got"
        cat /tmp/run.log
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "结果: $PASS 通过, $FAIL 失败"
[ "$FAIL" -eq 0 ]
