#!/bin/bash
#
# build_lib_toyc.sh — 用 toyc/toyas 编译 lib/ 下所有源文件
#
# 基于 build_lib_tcc.sh 改造，使用 toyc 编译器代替 tcc。
# 用法：
#   cd /mnt/c/Users/15259/Desktop/Tinylibc
#   bash build_lib_toyc.sh
#
# 环境变量（可选）：
#   TOYC_DIR  toyc 项目目录，默认 ../tcc （相对于 Tinylibc）
#   TOYC       toyc 编译器路径，默认 $TOYC_DIR/build/toyc
#   TOYAS      toyas 汇编器路径，默认 $TOYC_DIR/build/toyas
#   TOYLD      toyld 链接器路径，默认 $TOYC_DIR/build/toyld
#   AR         归档器，默认 x86_64-linux-gnu-ar
#
# 输出到 build/obj_toyc/lib/ 下，保持 lib/ 目录结构。
# 最后一步打包为 build/tlibc_toyc.a。

set -e

# ─── 路径配置 ──────────────────────────────────────────────────

PROJECT="$(cd "$(dirname "$0")" && pwd)"
TOYC_DIR="${TOYC_DIR:-$(cd "$PROJECT/../tcc" && pwd)}"
TOYC="${TOYC:-$TOYC_DIR/build/toyc}"
TOYAS="${TOYAS:-$TOYC_DIR/build/toyas}"
TOYLD="${TOYLD:-$TOYC_DIR/build/toyld}"
AR="${AR:-x86_64-linux-gnu-ar}"

OUTDIR="$PROJECT/build/obj_toyc"
TLIBC_A="$PROJECT/build/tlibc_toyc.a"

# ─── 编译器标志 ──────────────────────────────────────────────────

# toyc 的默认 include 路径（运行时 CWD 为项目根目录）：
#   ./include ./include/posix ./include/tlibc ./arch ./arch/x86_64
# 恰好匹配 Tinylibc 的目录结构，从项目根运行即可。
# 仍需要 -DX86_64_TLIBC=1 定义目标平台。
CFLAGS="-DX86_64_TLIBC=1"
# toyc 静默忽略所有不识别的标志（-c -nostdlib -Wall 等），
# 所以可以直接传入兼容 CFLAGS（保留作为扩展点）。

# 创建输出目录
mk_outdir() {
    local dir="$1"
    mkdir -p "$dir"
}

# ─── 编译所有 .c 文件（用 toyc） ──────────────────────────────

echo ""
echo "=============================================="
echo "  toyc: Tinylibc 库编译"
echo "  TOYC = $TOYC"
echo "  TOYAS = $TOYAS"
echo "=============================================="
echo ""

echo "=== lib/core/*.c ==="
for src in "$PROJECT/lib/core/"*.c; do
    name="core/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/core"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/$(echo $name | tr '/' '_').o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/init/ ==="
for src in "$PROJECT/lib/init/"*.c; do
    name="init/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/init"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/init_$(basename "$src" .c).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/stdio/ ==="
for src in "$PROJECT/lib/stdio/"*.c; do
    name="stdio/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/stdio"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/stdio_$(basename "$src" .c).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/thread/ ==="
# thread 用 toyc 编译（toyc 不支持 TLS，但 thread 中只有 clone.S 和 pthread 需要）
for src in "$PROJECT/lib/thread/"*.c; do
    name="thread/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/thread"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/thread_$(basename "$src" .c).o" 2>&1 || {
        echo "WARN (rc=$?) — 用 gcc 回退"
        x86_64-linux-gnu-gcc -c $CFLAGS \
            -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common \
            "$src" -o "$OUTDIR/lib/thread_$(basename "$src" .c).o"
    }
    echo "ok"
done

echo "=== lib/net/ ==="
for src in "$PROJECT/lib/net/"*.c; do
    name="net/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/net"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/net_$(basename "$src" .c).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/misc/ ==="
for src in "$PROJECT/lib/misc/"*.c; do
    name="misc/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/misc"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/misc_$(basename "$src" .c).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/math/ ==="
for src in "$PROJECT/lib/math/"*.c; do
    name="math/$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib/math"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/math_$(basename "$src" .c).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

echo "=== lib/ 根目录 ==="
for src in "$PROJECT/lib/"*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"
    mk_outdir "$OUTDIR/lib"
    printf "  %-30s " "$name"
    "$TOYC" $CFLAGS "$src" -o "$OUTDIR/lib/${name}.o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
    echo "ok"
done

# ─── 编译所有 .S 文件（用 toyas） ───────────────────────────────

echo ""
echo "=== 汇编文件 (toyas) ==="

for src in "$PROJECT/lib/init/"*.S; do
    [ -f "$src" ] || continue
    name="init/$(basename "$src" .S)"
    printf "  %-30s " "$name"
    "$TOYAS" "$src" -o "$OUTDIR/lib/init_$(basename "$src" .S).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc) — 用 gcc 回退"
        x86_64-linux-gnu-gcc -c \
            -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 \
            -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common \
            "$src" -o "$OUTDIR/lib/init_$(basename "$src" .S).o"
    }
    echo "ok"
done

for src in "$PROJECT/lib/thread/"*.S; do
    [ -f "$src" ] || continue
    name="thread/$(basename "$src" .S)"
    printf "  %-30s " "$name"
    "$TOYAS" "$src" -o "$OUTDIR/lib/thread_$(basename "$src" .S).o" 2>&1 || {
        rc=$?; echo "FAIL (rc=$rc) — 用 gcc 回退"
        x86_64-linux-gnu-gcc -c \
            -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 \
            -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common \
            "$src" -o "$OUTDIR/lib/thread_$(basename "$src" .S).o"
    }
    echo "ok"
done

# ─── 打包静态库 ────────────────────────────────────────────────

echo ""
echo "=== ar: tlibc_toyc.a ==="
OBJ_COUNT=0
OBJ_LIST=""
for obj in $(find "$OUTDIR/lib" -name '*.o' | sort); do
    OBJ_LIST="$OBJ_LIST $obj"
    OBJ_COUNT=$((OBJ_COUNT + 1))
done

"$AR" rcs "$TLIBC_A" $OBJ_LIST

echo ""
echo "=============================================="
echo "  完成"
echo "  目标文件: $OBJ_COUNT"
echo "  静态库:   $TLIBC_A"
ls -lh "$TLIBC_A"
echo "=============================================="