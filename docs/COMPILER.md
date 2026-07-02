# Tinylibc C 编译器 (tcc)

自托管 C 编译器，x86_64 Linux 目标，输出 ELF64 可重定位目标文件 (`.o`)。
约 7,300 行 C 代码（含独立预处理器 tpp 和独立汇编器 tas），9/9 自编译通过。

## 编译流水线

```
input.c → [read_file] → 源文本
        → [preprocess] → 预处理后文本 (宏展开/头文件/include)
        → [lexer_init + parser_init] → AST
        → [cgen_program] → 机器码 + 符号表 + 重定位表
        → [elf_write_object] → output.o
```

### 各阶段详细职责

| 阶段 | 文件 | 行数 | 输入 → 输出 |
|------|------|------|------------|
| 读取 | `tcc.c` | 206 | 文件路径 → 字符数组（上限 1 MB） |
| 预处理 | `preproc.c` | 691 | 源文本 + include 路径 → 预处理后文本 |
| 词法 | `lex.c` | 549 | 字符流 → Token 流（104 种 Token 类型） |
| 解析 | `parse.c` | 1,889 | Token 流 → AST（递归下降，完整 C 运算符优先级链） |
| 代码生成 | `cgen.c` + `cgen_expr.c` | 1,849 | AST → `code_buf` 机器码 + `syms[]` 符号表 + `rels[]` 重定位表 |
| 内联汇编 | `cgen_asm.c` | 186 | AST_ASM → 硬编码机器码（模式匹配 ~8 种模板） |
| ELF 写入 | `elf_write.c` | 282 | 全局缓冲区 → `.o` 文件（6 个节区） |

### 输出结构

生成一个 `.o` 文件，含 6 个节区：

| 索引 | 节区名 | 内容 |
|------|--------|------|
| 1 | `.text` | 机器码 + 字符串池（混合） |
| 2 | `.rela.text` | `R_X86_64_PLT32` / `R_X86_64_PC32` / `R_X86_64_32` 重定位 |
| 3 | `.bss` | 未初始化全局变量 |
| 4 | `.symtab` | 符号表（局部符号在前，全局符号在后） |
| 5 | `.strtab` | 符号名字符串 |
| 6 | `.shstrtab` | 节区名字符串 |

### 构建

```bash
# 完整构建（tmake 自动识别 tmakelist 中的多文件规则）
tmake -b tcc

# 运行
tcc input.c -o output.o    # 编译单个 C 文件为目标文件
tcc -d input.c             # 调试：输出预处理后文本
tcc input.c                # 自动输出 input.o
```

include 路径固定为：`.`、`./include`、`./include/posix`、`./include/tlibc`、`./arch`、`./arch/x86_64`。

未实现 `-I` 选项，未实现链接器，需配合外部 `ld` 链接。

---

## 词法分析器 (`lex.c`) — Token 类型

104 种 Token 类型，分类如下：

### 关键字 (35)

`int` `void` `char` `short` `long` `unsigned` `signed` `return` `if` `else`
`while` `for` `do` `double` `break` `continue` `switch` `case` `default` `goto`
`sizeof` `struct` `union` `enum` `typedef` `const` `volatile` `restrict` `register`
`static` `extern` `inline` `__attribute__` `__asm__` `__builtin_va_list`

### 变参内建 (3)

`__builtin_va_start` `__builtin_va_arg` `__builtin_va_end`

### 字面量 (3)

`TOK_IDENT` `TOK_NUMBER` `TOK_STRING`

### 标点符号 (37)

标准 C 全套运算符：算数/关系/逻辑/位/赋值/复合赋值/自增自减/成员/箭头/三目/逗号/省略号
包括 `<<=` `>>=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `++` `--` `->` `...`

### 非标准映射

| C 标准关键字 | tcc 处理方式 |
|-------------|-------------|
| `float` | 映射为 `TOK_IDENT`（不支持） |
| `auto` | 映射为 `TOK_IDENT`（不支持） |
| `_Bool` | 无 Token（不支持） |
| `_Complex` / `_Imaginary` | 无 Token（不支持） |

### 字面量解析

- **整数**：十进制和十六进制 (`0x`)，不支持八进制和二进制
- **浮点**：十进制含 `.` 和 `e`/`E` 科学计数法，支持 `u`/`U`/`l`/`L`/`f`/`F` 后缀（被消费但不影响类型）
- **字符**：`'a'` 和标准转义序列（`\n` `\t` `\\` `\'` `\"` `\0` `\xAB`），映射为 `TOK_NUMBER`
- **字符串**：相邻字符串字面量自动拼接

---

## 解析器 (`parse.c`) — 语法规则

递归下降解析器，完整运算符优先级链（14 级）：

| 优先级 | 方向 | 解析函数 | 运算符 |
|--------|------|---------|--------|
| 1 (最低) | L→R | `parse_expr_comma` | `,` |
| 2 | R→L | `parse_assign` | `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `\|=` |
| 3 | R→L | `parse_ternary` | `? :` |
| 4 | L→R | `parse_or` | `\|\|` |
| 5 | L→R | `parse_and` | `&&` |
| 6 | L→R | `parse_bitor` | `\|` |
| 7 | L→R | `parse_bitxor` | `^` |
| 8 | L→R | `parse_bitand` | `&` |
| 9 | L→R | `parse_eq` | `==` `!=` |
| 10 | L→R | `parse_rel` | `<` `>` `<=` `>=` |
| 11 | L→R | `parse_shift` | `<<` `>>` |
| 12 | L→R | `parse_add` | `+` `-` |
| 13 | L→R | `parse_mul` | `*` `/` `%` |
| 14 (最高) | R→L | `parse_unary` + `parse_postfix` + `parse_primary` | `!` `~` `+` `-` `*` `&` `++` `--` `sizeof` 类型转换 `f()` `a[i]` `s.m` `p->m` `x++` `x--` |

### 语句解析

| 语法结构 | 函数 | 状态 |
|---------|------|------|
| `return expr;` | `parse_return_statement` | ✅ |
| `if (cond) stmt` / `if (cond) stmt else stmt` | `parse_if_statement` | ✅ |
| `while (cond) stmt` | `parse_while_statement` | ✅ |
| `for (init; cond; step) stmt` | `parse_for_statement` | ✅ 含声明式 init |
| `do stmt while (cond);` | `parse_do_while` | ✅ |
| `switch (expr) { case: ... }` | `parse_switch_statement` | ✅ |
| `break;` / `continue;` | `parse_break` / `parse_continue` | ✅ |
| `goto label;` / `label:` | `parse_statement` | ✅ |
| `{ ... }` | `parse_compound_statement` | ✅ 含局部变量声明 |
| `__asm__(template);` | `parse_statement` | ✅ |

### 声明和类型

| 语法结构 | 状态 |
|---------|------|
| 基本类型：`int` `char` `short` `long` `double` `void` | ✅ |
| 类型修饰：`unsigned` `signed` `const` `volatile` `restrict` `register` | ✅ 词法识别，代码生成忽略 |
| `struct [tag] { members }` / `struct tag` | ✅ |
| `union [tag] { members }` | ✅ 解析为 struct（无重叠存储） |
| `enum [tag] { vals }` | ✅ |
| `typedef ... name` | ✅ |
| 指针声明 `*name` | ✅ |
| 函数指针 `(*name)(params)` | ✅ |
| 数组声明 `name[size]` | ✅ 多维数组 |
| 函数参数 `...` 变参 | ✅ |
| `extern "C" { ... }` | ✅ 忽略 linkage，解析内部内容 |
| `__attribute__((...))` | ✅ 被消费并丢弃 |
| `static` / `extern` 存储类 | ✅ 影响符号表输出 |
| `inline` 函数 | ✅ 生成正常调用代码（不内联） |
| 函数参数 `register type var __asm__("reg")` | ✅ |

### 未实现的语法

| 语法 | 原因 |
|------|------|
| `_Bool` / `bool` | 无对应 Token |
| `float` / `_Complex` / `_Imaginary` | 只有 `double` 浮点 |
| `long double` | 按 `long` 解析（8 字节） |
| `auto` | 映射为标识符 |
| 设计初始化器 `{.member = val}` | 被跳过 |
| 复合字面量 `(type){ ... }` | 未解析 |
| `_Static_assert` | 无对应 Token |
| `_Atomic` | 无对应 Token |
| `_Alignas` / `_Alignof` | 无对应 Token |
| `_Noreturn` | 无对应 Token |
| `_Thread_local` | 无对应 Token |
| `constexpr` | C23，无对应 Token |

---

## 类型系统

tcc 的类型系统是**退化**的：解析器为每个表达式计算一个 `type_size`（字节数）和 `elem_size`（数组/指针的元素大小），但不存在完整的类型推导和类型检查。

### 类型大小映射

| 声明 | type_size | 说明 |
|------|-----------|------|
| `int` | 4 | 32 位 |
| `char` | 1 | |
| `short` | 2 | |
| `long` | 8 | |
| `double` | 8 | 浮点运算使用 SSE |
| 指针 `T*` | 8 | |
| `void` | 0 | |
| `unsigned` | 4 | 视为 `unsigned int`，无符号语义无差异 |
| `long long` | 8 | 视为 `long` |
| struct | sizeof(each member) | 对齐简单求和 |

### 类型检查缺失

- 无类型兼容性验证（`int *p = "hello"` 不报错）
- 无算术类型转换（`int + double` 不会自动提升）
- 无函数参数类型匹配
- 无 const 语义
- 无 volatile 语义
- 无 restrict 分析
- `unsigned` 无符号语义——所有整数运算都是 32 位有符号

---

## 代码生成

### 函数调用约定 (x86_64 System V ABI)

| 参数位置 | 整型 | 浮点 |
|---------|------|------|
| 第 1 个 | `rdi` | `xmm0` |
| 第 2 个 | `rsi` | `xmm1` |
| 第 3 个 | `rdx` | `xmm2` |
| 第 4 个 | `rcx` | `xmm3` |
| 第 5 个 | `r8` | `xmm4` |
| 第 6 个 | `r9` | `xmm5` |
| 第 7+ 个 | 栈（从右到左压入） | 栈 |

函数帧结构：`push rbp; mov rbp, rsp; sub rsp, frame_size`

### 表达式代码生成能力

| 表达式 | 整数 | double |
|--------|------|--------|
| 常量 | ✅ `mov eax, imm32` | ✅ `mov rax, imm64; movq xmm0, rax` |
| 变量读取 | ✅ local/global | ✅ |
| 变量赋值 | ✅ local/global | ✅ |
| `+ - * / %` | ✅ | ✅ (SSE addsd/subsd/mulsd/divsd) |
| `<< >>` | ✅ | ❌ (无浮点移位) |
| `& \| ^ ~` | ✅ | ❌ |
| `== != < > <= >=` | ✅ `cmp; setcc` | ✅ `ucomisd; setcc` |
| `&& \|\|` | ✅ | ✅ |
| `,` | ✅ | ✅ |
| `!` | ✅ | ✅ |
| `-` (负) | ✅ `neg` | ✅ XOR 符号位 |
| `++x` `--x` | ✅ | ❌ |
| `x++` `x--` | ⚠️ 返回新值而非旧值 | ❌ |
| `*ptr` (解引用) | ⚠️ 固定 1 字节 | ❌ |
| `&x` (取地址) | ✅ local/global | ✅ |
| `s.m` / `p->m` | ✅ | ✅ |
| `a[i]` | ✅ | ✅ |
| `f(args)` | ✅ 7+ 参数栈传递 | ✅ 浮点参数 xmm0-xmm5 |
| `sizeof(type)` | ✅ | |
| `sizeof(var)` | ⚠️ 有限支持，复杂表达式默认 8 | |
| `(type)expr` (类型转换) | ⚠️ 丢弃类型，只解析内层表达式 | |
| `a ? b : c` | ✅ | ✅ |
| 复合赋值 `+= -= *=` | ❌ **生成错误代码**（当作 `=`） | ❌ |

### 控制流代码生成

| 语句 | 实现 |
|------|------|
| `if/else` | `test eax,eax; je else; then-block; jmp end` |
| `while` | 条件测试 + 循环体 + 回跳 |
| `for` | init → test → body → step → 回跳 |
| `do-while` | body → test → 条件回跳 |
| `switch` | O(N) 线性 `cmp eax,imm; je label` 指令链（无跳转表） |
| `return` | 表达式求值 + 函数 epilogue |
| `break` / `continue` | 跳转到循环的结束/开头标签 |
| `goto label` | `jmp` 到函数内标签 |
| `;` (空语句) | 无代码 |

### 函数调用代码生成

```
; 调用前：参数按序存入寄存器
mov rdi, arg1
mov rsi, arg2
...
call func           ; 优先 R_X86_64_PLT32 重定位
; 或函数指针调用：
call *rcx           ; 先 mov rcx, func_ptr
```

### 全局变量

未初始化全局变量 → `.bss` 节区。
初始化全局变量 → 运行时代码执行赋值（非 `.data` 节区静态数据）。

---

## 内联汇编 (`cgen_asm.c`)

非通用内联汇编器。通过硬编码模式匹配 `__asm__` 模板字符串，仅支持约 8 种已知模板：

| 模板模式 | 生成代码 | 可靠性 |
|---------|---------|--------|
| `"syscall"` | `syscall` | ⚠️ 依赖局部变量名为 `n,a1..a6,ret` |
| `"lock xaddq %0, %1"` | `F0 48 0F C1 02` | ⚠️ ModRM 硬编码 |
| `"lock xaddl %0, %1"` | `F0 0F C1 02` | ⚠️ ModRM 硬编码 |
| `"lock cmpxchgq %2, %1"` | `F0 48 0F B1 02` | ⚠️ ModRM 硬编码 |
| `"lock cmpxchgl %2, %1"` | `F0 0F B1 02` | ⚠️ ModRM 硬编码 |
| `"mov %%fs:0, %0"` | `64 48 8B 04 25 00 00 00 00` | ✅ |
| `"mov $N, %%rax; ... syscall"` | `48 C7 C0 imm32; 0F 05` | ⚠️ 简单立即数解析 |
| `"addl ..."` (通用) | 尝试 ModRM 编码 | ⚠️ 实验性 |

未知模板 → 报错退出。

---

## 预处理器 (`preproc.c`)

### 已实现

| 功能 | 状态 |
|------|------|
| `#define FOO value` (对象宏) | ✅ |
| `#define FOO(x) body` (函数宏) | ✅ 最多 64 参数 |
| 宏递归展开（保护深度 64） | ✅ |
| 参数预展开 (standard C semantics) | ✅ |
| 变参宏 `...` / `__VA_ARGS__` | ✅ |
| GCC `,##__VA_ARGS__`（逗号吞噬） | ✅ |
| `#include "file"` / `#include <file>` | ✅ 6 路径搜索 |
| `#ifdef` / `#ifndef` / `#else` / `#endif` | ✅ |
| `#undef` | ✅ |
| `#error` | ✅ |
| `__FILE__` `__LINE__` 预定义宏 | ✅ |
| `__x86_64__` 预定义宏 | ✅ |
| 续行符 `\` | ✅ |
| 注释删除 `//` `/* */` | ✅ |
| CRLF 行尾处理 | ✅ |

### 未实现

| 功能 | 影响 |
|------|------|
| `#if expr` 整数常量表达式 | **高** — 常用，如 `#if 1`、`#if defined(FOO)` |
| `#elif` | 中 |
| `#` stringify 操作符 | **高** — 常用，如 `#define STR(x) #x` |
| `##` token 粘贴（除 `,##__VA_ARGS__`） | **高** — 常用，如 `#define CONCAT(a,b) a##b` |
| `#pragma` | 低 |
| `#line` | 低 |
| `#warning` | 低 |
| `_Pragma()` | 低 |
| `__STDC__` `__DATE__` 等预定义宏 | 低 |

---

## 常量与限制

| 常量 | 值 | 用途 |
|------|-----|------|
| `ARENA_SIZE` | 16 MB | AST 节点分配器 |
| `CODE_BUF_SIZE` | 256 KB | 代码生成缓冲区 |
| `STRPOOL_SIZE` | 256 KB | 字符串字面量池 |
| `STRTAB_SIZE` | 256 KB | ELF 符号名表 |
| `MAX_SYMS` | 8192 | 符号表上限 |
| `MAX_RELS` | 16384 | 重定位条目上限 |
| `MAX_LOCALS` | 256 | 单函数局部变量上限 |
| `MAX_MEMBERS` | 128 | 结构体成员上限 |
| `MAX_TAGS` | 512 | struct/union/enum 标签上限 |
| `MAX_TYPEDEFS` | 1024 | typedef 条目上限 |
| `MAX_ENUM_VALS` | 2048 | enum 常量上限 |
| `MAX_STRINGS` | 1024 | 字符串字面量上限 |
| `MAX_PVARS` | 4096 | 解析时变量表上限 |
| 源文件 | 1 MB | read_file 上限 |

---

## 编译器的独立工具

### tpp — 独立预处理器

```bash
tpp input.c                 # 输出 input.i
tpp input.c -o output.i     # 指定输出文件
```

功能同 tcc 内嵌预处理器，用于调试宏展开。

### tas — x86_64 汇编器

AT&T 语法汇编器，将汇编代码编译为 `.o` 文件。

13 条指令、5 种伪操作；支持 REX 前缀、ModRM/SIB 编码、局部标签（1:, 2:, ... + Nf/Nb 引用）、重定位生成。

---

## 已知限制和 Bug

按严重程度排列：

### 致命（产生错误代码）

1. **复合赋值被当成赋值** — `x += 5` 生成 `x = 5`，而非 `x = x + 5`
   - 原因：`cgen_expr.c` 处理 `AST_ASSIGN` 时，忽略 `node->op`，始终直接存储右值
   - 影响：所有 `op=` 运算符

2. **指针解引用固定 1 字节** — `*int_ptr` 生成 `movsbl (%rax), %eax`（只读 1 字节并符号扩展）
   - 原因：`cgen_expr.c` 中 `deref_size` 硬编码为 1
   - 修复：需要推导目标类型大小来设置加载宽度

3. **后置 `x++`/`x--` 返回新值** — `a = x++` 得到递增后的值，而非 C 标准要求的递增前值
   - 原因：代码生成复用前缀路径，未保存旧值

4. **帧偏移限 disp8** — 函数局部变量总大小超过 127 字节时，`[rbp+offset]` 寻址会生成错误编码
   - 原因：所有局部变量偏移用 `e1(offset & 0xFF)` 截断到 8 位有符号
   - 影响：大局部变量的函数生成非法指令

5. **32 位整数运算** — `long` 类型变量赋值使用 `mov [rbp+off], eax`（32 位），高位截断
   - `mul`/`div` 使用 32 位形式（`mul ecx; div ecx`）
   - 大于 2³¹ 的值在高 32 位丢失

### 高影响

6. **`float` 关键字未实现** — 解析为标识符，`float x;` 编译失败
7. **`#if expr` 未实现** — 只能 `#ifdef`/`#ifndef`，不能 `#if 1`
8. **`#` stringify 未实现** — `#define STR(x) #x` 不工作
9. **`##` token 粘贴未实现** — `#define CONCAT(a,b) a##b` 不工作
10. **类型转换被忽略** — `(int)3.14` 和 `(double)42` 不生成转换代码
11. **`int` + `double` 混合运算** — 无自动提升，结果不可预测

### 中影响

12. **无 `.data` 节区** — 初始化全局变量通过运行时赋值而非静态数据
13. **`&global` 使用 R_X86_64_32** — 地址 >4GB 时链接失败
14. **switch O(N)** — 不是跳转表，大量 case 编译膨胀
15. **无 `long double`** — 解析为 `long`（8 字节整数）
16. **union 被视为 struct** — 成员不重叠
17. **`static` 局部变量无效** — 视为普通自动变量
18. **`inline` 不内联** — 生成正常函数调用
19. **`sizeof` 表达式不可靠** — 复杂表达式默认返回 8
20. **无 `-I` 选项** — include 路径固定

### 低影响

21. **八进制字面量解析错误** — `0777` 解析为 `0` 然后 `777`
22. **字符串后缀 `f`/`F`** — 作为浮点字面量的一部分被消费
23. **`auto` 为标识符** — C23 兼容性问题
24. **`goto` 标签函数间冲突** — 标签表是文件级而非函数级
25. **参数变量名依赖** — 内联汇编 syscall 依赖特定局部变量名

---

## 扩展指南

### 添加新关键字

1. `tcc.h` `TokenKind` 枚举中添加新值
2. `lex.c` 关键字查找表中添加条目
3. `parse.c` `parse_type_specifier()` 或相关解析函数中处理
4. `cgen_expr.c` 中生成对应代码（如需要）

### 添加新 AST 节点

1. `tcc.h` `AstKind` 枚举中添加
2. `AstNode` 结构体添加联合字段（或复用现有字段）
3. `parse.c` 中创建新节点的解析函数
4. `cgen.c` / `cgen_expr.c` 中生成代码

### 修复复合赋值

`cgen_expr.c` 的 `AST_ASSIGN` 分支需：
- 读取 `node->op` 判断运算符类型
- 对 `op=` 运算符：先求值左值地址并保存 → 读取左值当前值 → 求值右值 → 执行运算 → 写回

### 修复指针解引用

`cgen_expr.c` 的 `AST_UNARY` 分支需推导指针指向类型的大小，而非硬编码 1。

### 添加新内联汇编模板

1. 分析 `__asm__` 模板字符串模式
2. `cgen_asm.c` 中新增 `if (match(...))` 分支
3. 发射对应机器码
