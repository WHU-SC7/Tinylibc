#!/bin/bash
#
# gcc-link.sh — gcc 编译 + ld 链接单文件应用
#
# 用法：
#   bash gcc-link.sh app/coreutils/cat.c    # → build/cat
#   bash gcc-link.sh app/compiler/tcc.c     # → build/tcc
#
# 在项目根目录执行。gcc 编译到 build/<name>.o，
# 再链接 build/tlibc.a → build/<name>。

set -e

if [ $# -ne 1 ]; then
    echo "用法: $0 <源文件路径>"
    exit 1
fi

SRC="$1"
PROJECT="/mnt/d/Tinylibc"
CC="/usr/bin/x86_64-linux-gnu-gcc"
LD="/usr/bin/x86_64-linux-gnu-ld"
LD_SCRIPT="$PROJECT/ld.script"
TLIBC_A="$PROJECT/build/tlibc.a"

# 取 basename（去掉目录和 .c 后缀）
BASENAME=$(basename "$SRC" .c)
OBJ="$PROJECT/build/${BASENAME}.o"
OUT="$PROJECT/build/${BASENAME}"

cd "$PROJECT"

echo "=== gcc: $SRC → $OBJ ==="
"$CC" \
    -I./include -I./include/posix -I./include/tlibc \
    -I./arch -I./arch/x86_64 \
    -DX86_64_TLIBC=1 \
    -MD -fno-stack-protector -O0 -fno-common \
    -nostdlib -ffreestanding -fno-pie -mno-red-zone \
    -static \
    -c "$SRC" -o "$OBJ"

echo "=== ld: $OBJ + tlibc.a → $OUT ==="
"$LD" -z max-page-size=4096 -nostdlib -static \
    -T "$LD_SCRIPT" -u __tlibc_start \
    "$OBJ" "$TLIBC_A" \
    -o "$OUT"

echo "完成: $OUT"
ls -lh "$OUT"
