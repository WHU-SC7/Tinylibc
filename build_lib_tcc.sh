#!/bin/bash
#
# build_lib_tcc.sh — 用 toyc/toyas 编译 lib/ 下所有源文件
#
# 原 tcc 已被 toyc 取代（ToyCCompiler），此脚本现在是 build_lib_toyc.sh 的别名。
# 用法：
#   cd /mnt/c/Users/15259/Desktop/Tinylibc
#   bash build_lib_tcc.sh
#
# 环境变量：
#   TOYC_DIR   toyc 项目目录，默认 ../tcc

set -e

PROJECT="$(cd "$(dirname "$0")" && pwd)"
TOYC_DIR="${TOYC_DIR:-$(cd "$PROJECT/../tcc" && pwd)}"
TOYC="${TOYC:-$TOYC_DIR/build/toyc}"
TOYAS="${TOYAS:-$TOYC_DIR/build/toyas}"
AR="${AR:-x86_64-linux-gnu-ar}"

# 检查 toyc 是否存在
if [ ! -x "$TOYC" ]; then
    echo "错误: toyc 未找到 ($TOYC)"
    echo "请在 $TOYC_DIR 执行 make 编译"
    exit 1
fi

echo "=============================================="
echo "  toyc (原 tcc): Tinylibc 库编译"
echo "  TOYC  = $TOYC"
echo "  TOYAS = $TOYAS"
echo "=============================================="
echo ""

OUTDIR="$PROJECT/build/obj_toyc"
TLIBC_A="$PROJECT/build/tlibc_toyc.a"

# 编译所有 .c 文件
compile_dir() {
    local src_dir="$1" out_prefix="$2"
    for src in "$PROJECT/lib/$src_dir"*.c; do
        [ -f "$src" ] || continue
        local name="$out_prefix$(basename "$src" .c)"
        printf "  %-30s " "$name"
        "$TOYC" -DX86_64_TLIBC=1 "$src" -o "$OUTDIR/lib/${name}.o" 2>&1 && echo "ok" || {
            rc=$?; echo "FAIL (rc=$rc)"; exit $rc
        }
    done
}

mkdir -p "$OUTDIR/lib"

echo "=== lib/core/*.c ==="
compile_dir "core/" "core_"

echo "=== lib/init/ ==="
compile_dir "init/" "init_"

echo "=== lib/stdio/ ==="
compile_dir "stdio/" "stdio_"

echo "=== lib/thread/ ==="
compile_dir "thread/" "thread_"

echo "=== lib/net/ ==="
compile_dir "net/" "net_"

echo "=== lib/misc/ ==="
compile_dir "misc/" "misc_"

echo "=== lib/math/ ==="
compile_dir "math/" "math_"

echo "=== lib/ 根目录 ==="
for src in "$PROJECT/lib/"*.c; do
    [ -f "$src" ] || continue
    local name="$(basename "$src" .c)"
    printf "  %-30s " "$name"
    "$TOYC" -DX86_64_TLIBC=1 "$src" -o "$OUTDIR/lib/${name}.o" 2>&1 && echo "ok" || {
        rc=$?; echo "FAIL (rc=$rc)"; exit $rc
    }
done

# 汇编文件
echo ""
echo "=== 汇编文件 (toyas) ==="
for src in "$PROJECT/lib/init/"*.S "$PROJECT/lib/thread/"*.S; do
    [ -f "$src" ] || continue
    local base="$(basename "$src" .S)"
    local dir_part="$(basename "$(dirname "$src")")"
    printf "  %-30s " "$dir_part/$base"
    "$TOYAS" "$src" -o "$OUTDIR/lib/${dir_part}_${base}.o" 2>&1 && echo "ok" || {
        rc=$?; echo "FAIL (rc=$rc) — 用 gcc 回退"
        x86_64-linux-gnu-gcc -c \
            -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 \
            -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common \
            "$src" -o "$OUTDIR/lib/${dir_part}_${base}.o"
    }
done

# 打包静态库
echo ""
echo "=== ar: tlibc_toyc.a ==="
OBJ_LIST=$(find "$OUTDIR/lib" -name '*.o' | sort | tr '\n' ' ')
OBJ_COUNT=$(find "$OUTDIR/lib" -name '*.o' | wc -l)
"$AR" rcs "$TLIBC_A" $OBJ_LIST

echo ""
echo "=============================================="
echo "  完成"
echo "  目标文件: $OBJ_COUNT"
echo "  静态库:   $TLIBC_A"
ls -lh "$TLIBC_A"
echo "=============================================="
