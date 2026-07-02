#!/bin/bash
#
# build_lib_tcc.sh — 用 tcc/tas 编译 lib/ 下所有源文件
#
# 硬编码路径，从项目根目录执行。
# 用法：
#   cd /mnt/d/Tinylibc
#   bash build_lib_tcc.sh
#
# 输出到 build/obj/lib/ 下，保持 lib/ 目录结构。
# 最后一步打成静态库 build/lib/tlibc.a。

set -e

TCC="$HOME/tlibc/bin/tcc"
TAS="$HOME/tlibc/bin/tas"
GCC="/usr/bin/x86_64-linux-gnu-gcc"
AR="/usr/bin/x86_64-linux-gnu-ar"
PROJECT="/mnt/d/Tinylibc"

# ─── 创建输出目录 ──────────────────────────────────────────────

mkdir -p "$PROJECT/build/obj/lib/core"
mkdir -p "$PROJECT/build/obj/lib/init"
mkdir -p "$PROJECT/build/obj/lib/math"
mkdir -p "$PROJECT/build/obj/lib/misc"
mkdir -p "$PROJECT/build/obj/lib/net"
mkdir -p "$PROJECT/build/obj/lib/stdio"
mkdir -p "$PROJECT/build/obj/lib/thread"

# ─── 编译所有 .c 文件（用 tcc） ───────────────────────────────

echo "=== lib/core/*.c ==="
"$TCC" "$PROJECT/lib/core/io.c"     -o "$PROJECT/build/obj/lib/core/io.o"
"$TCC" "$PROJECT/lib/core/mem.c"    -o "$PROJECT/build/obj/lib/core/mem.o"
"$TCC" "$PROJECT/lib/core/proc.c"   -o "$PROJECT/build/obj/lib/core/proc.o"
"$TCC" "$PROJECT/lib/core/signal.c" -o "$PROJECT/build/obj/lib/core/signal.o"
"$TCC" "$PROJECT/lib/core/sync.c"   -o "$PROJECT/build/obj/lib/core/sync.o"
"$TCC" "$PROJECT/lib/core/time.c"   -o "$PROJECT/build/obj/lib/core/time.o"

echo "=== lib/init/ ==="
"$TCC" "$PROJECT/lib/init/init.c"   -o "$PROJECT/build/obj/lib/init/init.o"

echo "=== lib/stdio/ ==="
"$TCC" "$PROJECT/lib/stdio/printf.c"   -o "$PROJECT/build/obj/lib/stdio/printf.o"
"$TCC" "$PROJECT/lib/stdio/snprintf.c" -o "$PROJECT/build/obj/lib/stdio/snprintf.o"

echo "=== lib/thread/ （用 gcc，tcc 不支持 TLS） ==="
"$GCC" -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common -c "$PROJECT/lib/thread/mempool.c" -o "$PROJECT/build/obj/lib/thread/mempool.o"
"$GCC" -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common -c "$PROJECT/lib/thread/pthread.c" -o "$PROJECT/build/obj/lib/thread/pthread.o"

echo "=== lib/net/ ==="
"$TCC" "$PROJECT/lib/net/dns.c"    -o "$PROJECT/build/obj/lib/net/dns.o"
"$TCC" "$PROJECT/lib/net/socket.c" -o "$PROJECT/build/obj/lib/net/socket.o"

echo "=== lib/misc/ ==="
"$TCC" "$PROJECT/lib/misc/assert.c" -o "$PROJECT/build/obj/lib/misc/assert.o"
"$TCC" "$PROJECT/lib/misc/envp.c"   -o "$PROJECT/build/obj/lib/misc/envp.o"
"$TCC" "$PROJECT/lib/misc/file.c"   -o "$PROJECT/build/obj/lib/misc/file.o"
"$TCC" "$PROJECT/lib/misc/path.c"   -o "$PROJECT/build/obj/lib/misc/path.o"
"$TCC" "$PROJECT/lib/misc/system.c" -o "$PROJECT/build/obj/lib/misc/system.o"

echo "=== lib/math/ ==="
"$TCC" "$PROJECT/lib/math/math.c"   -o "$PROJECT/build/obj/lib/math/math.o"

echo "=== lib/ 根目录 ==="
"$TCC" "$PROJECT/lib/ctype.c"  -o "$PROJECT/build/obj/lib/ctype.o"
"$TCC" "$PROJECT/lib/poll.c"   -o "$PROJECT/build/obj/lib/poll.o"
"$TCC" "$PROJECT/lib/procfs.c" -o "$PROJECT/build/obj/lib/procfs.o"
"$TCC" "$PROJECT/lib/string.c" -o "$PROJECT/build/obj/lib/string.o"
"$TCC" "$PROJECT/lib/time.c"   -o "$PROJECT/build/obj/lib/time.o"
"$TCC" "$PROJECT/lib/tty.c"    -o "$PROJECT/build/obj/lib/tty.o"

# ─── 编译所有 .S 文件（用 tas） ───────────────────────────────

echo "=== lib/init/ (assembly，用 gcc) ==="
"$GCC" -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common -c "$PROJECT/lib/init/start.S" -o "$PROJECT/build/obj/lib/init/start.o"

echo "=== lib/thread/ (assembly，用 gcc) ==="
"$GCC" -I./include -I./include/posix -I./include/tlibc -I./arch -I./arch/x86_64 -DX86_64_TLIBC=1 -nostdlib -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -static -fno-common -c "$PROJECT/lib/thread/clone.S" -o "$PROJECT/build/obj/lib/thread/clone.o"

# ─── 打包静态库 ────────────────────────────────────────────────

echo "=== ar: tlibc.a ==="
find "$PROJECT/build/obj/lib" -name '*.o' | sort |
  xargs "$AR" rcs "$PROJECT/build/tlibc.a"

echo ""
echo "完成。共 $(find "$PROJECT/build/obj/lib" -name '*.o' | wc -l) 个目标文件"
ls -lh "$PROJECT/build/tlibc.a"
