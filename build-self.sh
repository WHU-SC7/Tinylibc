#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build-self.sh — 用 toyc 自编译 toyc 自身
#
# 编译链：toyc（.c）+ as（.S）→ .o → ld → 可执行文件
# 不依赖 tlibc.a（使用 toyc_rt 独立运行时）
#
# 用法：
#   cd <项目根目录> && bash build-self.sh
#   build/output/toyc_self input.c  # 自编译出的编译器

set -e

SRC=app/compiler
BUILD=build/app/compiler
OUT=build/output
TOYC="${TOYC:-../tcc/build/toyc}"

mkdir -p "$BUILD" "$OUT"

# ── 编译 .c 文件（toyc → .o）──
echo "=== 编译 .c 文件 ==="
for f in toyc lex parse preproc cgen cgen_expr cgen_asm cgen_float_hack elf_write toyc_rt; do
    printf "  %-20s → " "$f.c"
    "$TOYC" -DX86_64_TLIBC=1 "$SRC/$f.c" -o "$BUILD/${f}_self.o"
done

# ── 汇编 .S 文件（系统 as）──
echo "=== 汇编 .S 文件 ==="
printf "  %-20s → " "toyc_rt_start.S"
x86_64-linux-gnu-as "$SRC/toyc_rt_start.S" -o "$BUILD/toyc_rt_start_self.o" && echo "OK"

# ── 链接 ──
echo ""
echo "=== 链接（无 tlibc.a）==="
x86_64-linux-gnu-ld -z max-page-size=4096 \
    -nostdlib -static \
    -T ld.script \
    "$BUILD/toyc_self.o" \
    "$BUILD/lex_self.o" \
    "$BUILD/parse_self.o" \
    "$BUILD/preproc_self.o" \
    "$BUILD/cgen_self.o" \
    "$BUILD/cgen_expr_self.o" \
    "$BUILD/cgen_asm_self.o" \
    "$BUILD/cgen_float_hack_self.o" \
    "$BUILD/elf_write_self.o" \
    "$BUILD/toyc_rt_self.o" \
    "$BUILD/toyc_rt_start_self.o" \
    -o "$OUT/toyc_self" 2>&1 | tee build-self.log

if [ -f "$OUT/toyc_self" ]; then
    SZ=$(stat -c%s "$OUT/toyc_self")
    echo ""
    echo "=== 完成: $OUT/toyc_self (${SZ} bytes) ==="
    echo ""

    # 快速验证：无参数运行
    if "$OUT/toyc_self" 2>&1 | grep -q usage; then
        echo "  ✔ 无参数运行正常（显示 usage）"
    fi
else
    echo "链接失败，详见 build-self.log"
fi
