# tcc 编译器测试计划

通过一系列最小 C 程序，逐项验证 tcc 的编译器能力边界。
每个测试文件独立、自包含，只测试一个特性。

## 测试机制

### 文件结构

```
compiler-tests/
  ├── 001_empty.c
  ├── 002_return_int.c
  ├── 003_arith.c
  ├── ...
  ├── 050_compound_assign.c     # 预期失败 - 已知 bug
  ├── ...
  └── run_tests.sh              # 编译+链接+运行驱动
```

### 命名约定

`NNN_name.c` — 三位编号按依赖顺序排列，预期失败的测试在文件名中标注：
- `NNN_name.c` — 预期通过
- `NNN_fail_name.c` — 预期编译失败（语法不支持）
- `NNN_wrong_name.c` — 预期编译但结果错误（已知 bug）

### 如何运行

每个测试文件的 `main()` 返回 0 表示通过，非 0 表示失败（不同值可标识不同失败点）。

```bash
# 单个测试
cd /path/to/Tinylibc
tcc compiler-tests/005_arith.c -o compiler-tests/005_arith.o
x86_64-linux-gnu-gcc compiler-tests/005_arith.o -o compiler-tests/005_arith -no-pie -nostdlib -L build -ltlibc -static
./compiler-tests/005_arith
echo $?    # 应为 0

# 批量运行
bash compiler-tests/run_tests.sh
```

`run_tests.sh` 批量驱动编译 + 链接 + 执行 + 结果比对。

---

## 测试用例

### 阶段 1：基础能力（空程序 → 基本运算 → 控制流）

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 001 | `001_empty.c` | 空 main 返回 0 | ✅ 通过 |
| 002 | `002_return_int.c` | `return` 整数常量 | ✅ 通过 |
| 003 | `003_local_var.c` | 局部变量声明与赋值 | ✅ 通过 |
| 004 | `004_arith.c` | `+ - * / %` 整数运算 | ✅ 通过 |
| 005 | `005_if_else.c` | `if/else` 条件分支 | ✅ 通过 |
| 006 | `006_while.c` | `while` 循环 | ✅ 通过 |
| 007 | `007_for.c` | `for` 循环（含声明式 init） | ✅ 通过 |
| 008 | `008_do_while.c` | `do-while` 循环 | ✅ 通过 |
| 009 | `009_logical.c` | `&& || !` 逻辑运算 | ✅ 通过 |
| 010 | `010_comparison.c` | `== != < > <= >=` 比较 | ✅ 通过 |
| 011 | `011_bitwise.c` | `& | ^ ~ << >>` 位运算 | ✅ 通过 |
| 012 | `012_ternary.c` | `? :` 三目运算 | ✅ 通过 |
| 013 | `013_comma.c` | `,` 逗号表达式 | ✅ 通过 |

**阶段 1 文件示例：**

```c
// 002_return_int.c — return 整数常量
int main() { return 42; }
// 期望：退出码 42
```

```c
// 005_if_else.c — if/else 分支
int main() {
    int x = 1;
    int y = 0;
    if (x)
        y = 10;
    else
        y = 20;
    return y == 10 ? 0 : 1;
}
```

---

### 阶段 2：类型系统（char/short/long/double/指针）

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 014 | `014_char.c` | `char` 类型的声明和运算 | ✅ |
| 015 | `015_short.c` | `short` 类型的声明和运算 | ✅ |
| 016 | `016_long.c` | `long` 类型的声明和运算 | ⚠️ 可能 32 位截断 |
| 017 | `017_unsigned.c` | `unsigned` 类型 | ✅ |
| 018 | `018_double_arith.c` | `double` 加减乘除 | ✅ |
| 019 | `019_double_cmp.c` | `double` 比较运算 | ✅ |
| 020 | `020_pointer_arith.c` | 指针算术（int*+1 → +4） | ✅ |
| 021 | `021_pointer_assign.c` | 指针变量赋值 | ✅ |
| 022 | `022_sizeof_type.c` | `sizeof(int)`、`sizeof(char)` 等 | ✅ |
| 023 | `023_sizeof_expr.c` | `sizeof` 表达式 | ⚠️ 复杂表达式可能错 |
| 024 | `024_cast.c` | 类型转换（`(int)d` 等） | ⚠️ 不生成转换代码 |

**边界测试：**

```c
// 016_long.c — long 类型
int main() {
    long x = 0x100000001L;  // 超出 32 位
    // 如果发生 32 位截断，x 将等于 1
    return (x == 0x100000001L) ? 0 : 1;
}
```

---

### 阶段 3：自增自减与赋值

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 025 | `025_prefix_inc.c` | `++x --x` 前缀 | ✅ |
| 026 | `026_wrong_postfix_inc.c` | `x++ x--` 后置值捕获 | ❌ **已知 bug** — `a=x++` 返回新值 |
| 027 | `027_wrong_compound_assign.c` | `+= -= *= /= %=` | ❌ **已知 bug** — `x+=5` 等于 `x=5` |
| 028 | `028_assign_chain.c` | 链式赋值 `a = b = c` | ✅ |

**验证方法：**

```c
// 026_wrong_postfix_inc.c — 验证后置 ++ 的值捕获
// 预期行为：a = x++ 后 a == 原 x，x == 原 x+1
// 已知 bug：tcc 返回递增后的 x 值
int main() {
    int x = 5;
    int a = x++;  // 标准 C: a=5, x=6
    // tcc bug: a=6, x=6
    return (a == 5) ? 0 : 1;  // tcc 上会 FAIL
}
```

```c
// 027_wrong_compound_assign.c — 验证复合赋值
// 已知 bug：x += 3 被当作 x = 3
int main() {
    int x = 5;
    x += 3;       // 应该 x=8, 但 tcc 生成 x=3
    return (x == 8) ? 0 : 1;  // tcc 上会 FAIL
}
```

---

### 阶段 4：数组与指针

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 029 | `029_array_decl.c` | 数组声明与初始化 | ✅ |
| 030 | `030_array_index.c` | 数组下标访问 `a[i]` | ✅ |
| 031 | `031_array_assign.c` | 数组元素赋值 `a[i]=v` | ✅ |
| 032 | `032_multidim_array.c` | 多维数组声明与访问 | ✅ |
| 033 | `033_wrong_deref.c` | `*ptr` 解引用 | ❌ **已知 bug** — 固定 1 字节 |
| 034 | `034_addr_of_local.c` | `&x` 取局部变量地址 | ✅ |
| 035 | `035_addr_of_global.c` | `&global` 取全局地址 | ⚠️ R_X86_64_32 可能链接失败 |
| 036 | `036_string_literal.c` | 字符串字面量和 `printf` | ✅ |
| 037 | `037_pointer_sub.c` | 指针减法 `p2 - p1` | ✅ |

**关键边界测试：**

```c
// 033_wrong_deref.c — 指针解引用
// 已知 bug：*int_ptr 只读 1 字节
int main() {
    int x = 0x12345678;
    int *p = &x;
    int v = *p;  // 应该得到 0x12345678
    // tcc bug: 只读 1 字节，符号扩展到 0x78 或 0xffffff78
    return (v == 0x12345678) ? 0 : 1;  // tcc 上会 FAIL
}
```

---

### 阶段 5：函数调用

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 038 | `038_func_call_0.c` | 无参函数调用 | ✅ |
| 039 | `039_func_call_1.c` | 1 个参数 | ✅ |
| 040 | `040_func_call_6.c` | 6 个参数（全寄存器） | ✅ |
| 041 | `041_func_call_7.c` | 7+ 个参数（栈传递） | ✅ |
| 042 | `042_func_call_expr.c` | 函数调用作为表达式 | ✅ |
| 043 | `043_func_ptr.c` | 函数指针调用 | ✅ |
| 044 | `044_recursion.c` | 递归函数（阶乘） | ✅ |
| 045 | `045_nested_call.c` | 嵌套函数调用 `f(g(x))` | ✅ |

```c
// 041_func_call_7.c — 7+ 参数的函数调用
int sum7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}
int main() {
    return sum7(1, 2, 3, 4, 5, 6, 7) == 28 ? 0 : 1;
}
```

---

### 阶段 6：struct 与 union

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 046 | `046_struct_decl.c` | struct 声明与成员访问 | ✅ |
| 047 | `047_struct_assign.c` | struct 成员赋值 | ✅ |
| 048 | `048_struct_ptr.c` | `p->member` 指针访问 | ✅ |
| 049 | `049_struct_func_arg.c` | struct 作为函数参数 | ✅ |
| 050 | `050_struct_return.c` | struct 作为返回值 | ⚠️ 可能有问题 |
| 051 | `051_union.c` | union 声明与访问（作为 struct） | ⚠️ 无重叠存储 |

---

### 阶段 7：switch/goto/enum/typedef

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 052 | `052_switch.c` | `switch/case` | ✅ 线性 cmp 链 |
| 053 | `053_switch_default.c` | `switch/case/default` | ✅ |
| 054 | `054_goto.c` | `goto label` | ✅ |
| 055 | `055_enum.c` | `enum` 声明与使用 | ✅ |
| 056 | `056_typedef.c` | `typedef` 类型别名 | ✅ |

---

### 阶段 8：全局变量与 static/extern

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 057 | `057_global_var.c` | 全局变量读写 | ✅ |
| 058 | `058_global_init.c` | 全局变量初始化 | ✅ 运行时赋值 |
| 059 | `059_static_func.c` | `static` 函数 | ✅ |
| 060 | `060_extern_decl.c` | `extern` 声明 | ✅ |

---

### 阶段 9：浮点精算

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 061 | `061_double_const.c` | double 常量与科学计数法 | ✅ |
| 062 | `062_double_func.c` | double 作为函数参数和返回值 | ✅ |
| 063 | `063_double_to_int.c` | double → int（SSE cvttsd2si） | ✅ |
| 064 | `064_mixed_arith.c` | `int + double` 混合运算 | ⚠️ 无自动提升 |

---

### 阶段 10：预处理

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 065 | `065_define_obj.c` | `#define FOO val` 对象宏 | ✅ |
| 066 | `066_define_func.c` | `#define FOO(x) (x+1)` 函数宏 | ✅ |
| 067 | `067_ifdef.c` | `#ifdef` / `#ifndef` | ✅ |
| 068 | `068_fail_if.c` | `#if expr` 条件编译 | ❌ **未实现** — 编译失败 |
| 069 | `069_fail_stringify.c` | `#x` 字符串化 | ❌ **未实现** — 宏不展开 |
| 070 | `070_fail_paste.c` | `a##b` Token 粘贴 | ❌ **未实现** — 宏不展开 |
| 071 | `071_include.c` | `#include "file"` | ✅ |
| 072 | `072_variadic_macro.c` | `__VA_ARGS__` 变参宏 | ✅ |
| 073 | `073_file_line.c` | `__FILE__` / `__LINE__` | ✅ |
| 074 | `074_nested_macro.c` | 宏嵌套展开 | ✅ |

---

### 阶段 11：复杂边界测试

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 075 | `075_many_locals.c` | 函数 >127 字节局部变量 | ❌ **已知 bug** — disp8 溢出 |
| 076 | `076_nested_loop.c` | 嵌套循环 + break/continue | ✅ |
| 077 | `077_nested_if.c` | 多层 if-else 嵌套 | ✅ |
| 078 | `078_complex_expr.c` | 复杂混合表达式 | ✅ |
| 079 | `079_escape_seq.c` | 字符转义序列 `\n` `\t` `\xAB` | ✅ |
| 080 | `080_hex_literal.c` | 十六进制字面量 | ✅ |
| 081 | `081_fail_octal.c` | 八进制字面量 `0777` | ❌ **已知 bug** — 解析为 0+777 |
| 082 | `082_block_scope.c` | 块作用域局部变量 | ✅ |
| 083 | `083_var_init_expr.c` | 变量初始化用表达式 `int x = f()+1` | ✅ |

---

### 阶段 12：基准测试（自编译验证）

| # | 文件 | 测试目标 | 预期结果 |
|---|------|---------|---------|
| 084 | `084_self_compile.c` | 编译一个简单的 C 文件（含 tcc 自身代码） | ⚠️ 需要特殊驱动 |
| 085 | `085_fib.c` | 递归斐波那契（性能基准） | ✅ |

---

## 测试运行脚本

```bash
# run_tests.sh — 批量编译、链接、执行编译器测试
# 用法: bash app/compiler/tests/run_tests.sh [阶段号]

set -e

TCC="$HOME/tlibc/bin/tcc"             # 已编译的 tcc
TLIBC_DIR=.
LINKER=x86_64-linux-gnu-gcc
TESTDIR=compiler-tests

PASS=0
FAIL=0
SKIP=0

# 链接测试程序（用 tlibc 静态库）
link_test() {
    local obj=$1 bin=$2
    $LINKER $obj -o $bin \
        -no-pie -nostdlib \
        -L $TLIBC_DIR/build -ltlibc \
        -static 2>/dev/null
}

run_one_test() {
    local src=$1
    local name=$(basename $src .c)
    local obj=$TESTDIR/$name.o
    local bin=$TESTDIR/$name.bin

    # 阶段过滤
    if [ -n "$1" ]; then
        local num=$(echo $name | cut -d'_' -f1)
        local stage=$(( (num - 1) / 10 + 1 ))
        [ "$stage" -ne "$1" ] && return
    fi

    # 预期失败标记
    local expect_fail=0
    case $name in
        *wrong*|*fail*) expect_fail=1 ;;
    esac

    # 编译
    $TCC $src -o $obj
    local tcc_exit=$?
    if [ $tcc_exit -ne 0 ]; then
        if [ $expect_fail -eq 1 ]; then
            echo "  SKIP (compile fail as expected): $name"
            SKIP=$((SKIP+1))
            return
        fi
        echo "  FAIL (compilation error): $name"
        FAIL=$((FAIL+1))
        return
    fi

    # 链接
    if ! link_test $obj $bin; then
        if [ $expect_fail -eq 1 ]; then
            echo "  SKIP (link fail as expected): $name"
            SKIP=$((SKIP+1))
            return
        fi
        echo "  FAIL (link error): $name"
        FAIL=$((FAIL+1))
        return
    fi

    # 执行
    $bin
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        if [ $expect_fail -eq 1 ]; then
            echo "  UNEXPECTED PASS (预期失败但通过了): $name → 可能已修复!"
            PASS=$((PASS+1))
        else
            echo "  PASS: $name"
            PASS=$((PASS+1))
        fi
    else
        if [ $expect_fail -eq 1 ]; then
            echo "  SKIP (known fail as expected): $name"
            SKIP=$((SKIP+1))
        else
            echo "  FAIL (exit=$exit_code): $name"
            FAIL=$((FAIL+1))
        fi
    fi

    rm -f $obj $bin
}

echo "═══ tcc 编译器测试 ═══"
echo ""

for src in $TESTDIR/[0-9]*.c; do
    [ -f "$src" ] || continue
    run_one_test $src $1
done

echo ""
echo "─── 汇总 ───"
echo "  PASS: $PASS  FAIL: $FAIL  SKIP: $SKIP"
[ $FAIL -eq 0 ] || echo "  有 $FAIL 个失败，详见上方日志"
```

---

## 各测试阶段说明

### 阶段划分

| 阶段 | 测试范围 | 测试数 | 核心关注点 |
|------|---------|--------|-----------|
| 1 | 基础能力 | 13 | 编译器基本工作 |
| 2 | 类型系统 | 11 | 各类型支持程度 |
| 3 | 自增自减与赋值 | 4 | 已知 bug 验证 |
| 4 | 数组与指针 | 9 | 内存访问能力边界 |
| 5 | 函数调用 | 8 | ABI 合规性 |
| 6 | struct/union | 6 | 复合类型 |
| 7 | switch/goto/enum | 5 | 跳转与枚举 |
| 8 | 全局变量 | 4 | 链接能力 |
| 9 | 浮点精算 | 4 | SSE 指令边界 |
| 10 | 预处理 | 10 | 预处理边界 |
| 11 | 复杂边界 | 9 | 边界和已知 bug |
| 12 | 基准 | 2 | 性能 |

### 发现已知 bug 的方法

一些测试用于确认已知 bug 仍然存在（避免误以为修复了还没修的东西）：

1. **compound_assign (027)**：`x += 3` 后 `x == 3` — 确认 bug 仍然活跃
2. **postfix_inc (026)**：`a = x++` 后 `a == x` — 确认 bug 仍然活跃
3. **deref (033)**：`*int_ptr` 只取 1 字节 — 确认 bug 仍然活跃
4. **many_locals (075)**：>127 字节局部变量编译崩溃 — 确认 bug 仍然活跃
5. **octal (081)**：`0777` 解析错误 — 确认 bug 仍然活跃

当这些测试从 FAIL 变成 PASS 时，表示 bug 已修复（应更新测试标记）。

### 测试模板

每个测试文件的完整模板：

```c
/*
 * NNN_feature.c — 测试目标说明
 *
 * 验证：具体验证什么
 * 预期：各分支的预期行为
 * 已知限制：如果知道 bug，注明
 */

int main() {
    // Arrange
    int x = 5;
    int y = 3;

    // Act
    int result = x + y;

    // Assert
    if (result != 8) return 1;  // 基本计算结果错误
    if (x != 5) return 2;       // 原变量被修改
    if (y != 3) return 3;       // 原变量被修改

    return 0;  // 全部通过
}
```

---

## 优先级推荐

### 第 1 轮（快速摸清现状）

运行阶段 1-3（约 28 个测试），确认编译器基础能力。这些测试覆盖了最关键的基本功能：
空程序、整数运算、控制流、类型系统、自增自减和赋值。

**预计耗时：几分钟。**

### 第 2 轮（深入边界）

运行阶段 4-7（约 28 个测试），深入数组、指针、struct、函数调用等复杂特性。

### 第 3 轮（完整覆盖）

运行阶段 1-12 全部 85 个测试，获得完整能力矩阵。

---

## 持续更新

测试是活的文档。每次修复 bug 后：

1. 运行对应的测试确认修复
2. 更新测试注释（将 "已知 bug" 改为 "已修复"）
3. 考虑添加回归测试（更复杂的场景确保不复发）

每次添加新特性后：

1. 添加对应的测试文件
2. 确保阶段归属合理
3. 更新此文档
