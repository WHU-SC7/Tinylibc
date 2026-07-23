#!/bin/bash
#
# toyc-link.sh — toyc 编译 + 链接单文件应用
#
# 用法：
#   bash toyc-link.sh app/shell.c          # → build/shell
#   bash toyc-link.sh app/compiler/tcc.c   # → build/tcc
#
# 环境变量：
#   TOYC_LINKER  链接器：toyld（默认）或 ld
#
# 在项目根目录执行。toyc 编译到 build/obj_toyc/<name>.o，
# 再链接 build/tlibc_toyc.a → build/<name>。

set -e

if [ $# -ne 1 ]; then
    echo "用法: $0 <源文件路径>"
    exit 1
fi

SRC="$1"
PROJECT="$(cd "$(dirname "$0")" && pwd)"
TOYC_DIR="${TOYC_DIR:-$(cd "$PROJECT/../tcc" && pwd)}"
TOYC="${TOYC:-$TOYC_DIR/build/toyc}"
TOYLD="${TOYLD:-$TOYC_DIR/build/toyld}"
LD="/usr/bin/x86_64-linux-gnu-ld"
LINKER="${TOYC_LINKER:-toyld}"

LD_SCRIPT="$PROJECT/ld.script"
TLIBC_A="$PROJECT/build/tlibc_toyc.a"
OBJDIR="$PROJECT/build/obj_toyc"

# 取 basename（去掉目录和 .c 后缀）
BASENAME=$(basename "$SRC" .c)
OBJ="$PROJECT/build/obj_toyc/${BASENAME}.o"
OUT="$PROJECT/build/${BASENAME}"

cd "$PROJECT"

echo "=== toyc: $SRC → $OBJ ==="
"$TOYC" -DX86_64_TLIBC=1 "$SRC" -o "$OBJ"

case "$LINKER" in
    toyld)
        echo "=== toyld: $OBJ + tlibc_toyc.a → $OUT ==="
        # toyld需要直接包含start.o才能找到__tlibc_start入口
        "$TOYLD" "$OBJ" "$OBJDIR/lib/init_start.o" "$TLIBC_A" -o "$OUT"
        echo "  (注: toyld 生成单个 RWE 段，仅用于功能验证)"
        ;;
    ld)
        echo "=== ld: $OBJ + tlibc_toyc.a → $OUT ==="
        "$LD" -z max-page-size=4096 -nostdlib -static \
            -T "$LD_SCRIPT" -u __tlibc_start \
            "$OBJ" "$TLIBC_A" \
            -o "$OUT"
        ;;
    *)
        echo "错误: 未知链接器 '$LINKER'，可选 toyld 或 ld"
        exit 1
        ;;
esac

echo "完成: $OUT"
ls -lh "$OUT"