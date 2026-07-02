#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build-self.sh — 用 tcc 自编译 tcc 自身
#
# 编译链：tcc（.c）+ as（.S）→ .o → ld → 可执行文件
# 不依赖 tlibc.a（使用 tcc_rt 独立运行时）
#
# 用法：
#   cd <项目根目录> && bash build-self.sh
#   build/output/tcc_self input.c  # 自编译出的编译器
#
# 修复记录：
#   2026-07-02  修复 add_sym() 合并 UNDEF 符号（cgen.c）
#   2026-07-02  替换 INPUT_END / NULL 为字面值（lex.c）

set -e

SRC=app/compiler
BUILD=build/app/compiler
OUT=build/output
RT_START=tcc_rt_start

mkdir -p "$BUILD" "$OUT"

# ── 编译 .c 文件（tcc → .o）──
echo "=== 编译 .c 文件 ==="
for f in tcc lex parse preproc cgen cgen_expr cgen_asm elf_write tcc_rt; do
    printf "  %-20s → " "$f.c"
    tcc "$SRC/$f.c" -o "$BUILD/${f}_self.o"
done

# ── 汇编 .S 文件（系统 as）──
echo "=== 汇编 .S 文件 ==="
printf "  %-20s → " "$RT_START.S"
x86_64-linux-gnu-as "$SRC/$RT_START.S" -o "$BUILD/${RT_START}_self.o" && echo "OK"

# ── 链接 ──
echo ""
echo "=== 链接（无 tlibc.a）==="
x86_64-linux-gnu-ld -z max-page-size=4096 \
    -nostdlib -static \
    -T ld.script \
    "$BUILD/tcc_self.o" \
    "$BUILD/lex_self.o" \
    "$BUILD/parse_self.o" \
    "$BUILD/preproc_self.o" \
    "$BUILD/cgen_self.o" \
    "$BUILD/cgen_expr_self.o" \
    "$BUILD/cgen_asm_self.o" \
    "$BUILD/elf_write_self.o" \
    "$BUILD/tcc_rt_self.o" \
    "$BUILD/${RT_START}_self.o" \
    -o "$OUT/tcc_self" 2>&1 | tee build-self.log

if [ -f "$OUT/tcc_self" ]; then
    SZ=$(stat -c%s "$OUT/tcc_self")
    echo ""
    echo "=== 完成: $OUT/tcc_self (${SZ} bytes) ==="
    echo ""

    # 快速验证：无参数运行（不触发有 bug 的全局数组代码）
    if "$OUT/tcc_self" 2>&1 | grep -q usage; then
        echo "  ✔ 无参数运行正常（显示 usage）"
    fi

    echo ""
    echo "┌────────────────────────────────────────────────────────────┐"
    echo "│ 已知限制：自编译 tcc 有全局数组代码生成 bug               │"
    echo "│ 带参数运行（打开文件等）会因错误的数组寻址而 segfault。   │"
    echo "│ 此 bug 存在于 tcc 的 cgen_expr.c AST_VAR 全局符号处理：   │"
    echo "│ 对全局数组变量生成 mov（取值）而非 lea（取地址）。        │"
    echo "│ 修复后即可实现完整自举。                                  │"
    echo "└────────────────────────────────────────────────────────────┘"
    echo ""
    echo "对比验证：gcc 编译的 tcc 仍正常工作："
    ./build/output/tcc "$SRC/tcc_rt.c" -o /tmp/tcc_rt_vrfy.o && \
        echo "  ✔ tcc_rt.c → .o 成功"
else
    echo "链接失败，详见 build-self.log"
fi
