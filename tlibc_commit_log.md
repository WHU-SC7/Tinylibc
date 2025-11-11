# Git提交规范
feat：新功能（feature）。

fix/to：修复bug，可以是QA发现的BUG，也可以是研发自己发现的BUG。
fix：产生diff并自动修复此问题。适合于一次提交直接修复问题
to：只产生diff不自动修复此问题。适合于多次提交。最终修复问题提交时使用fix

docs：文档（documentation）。

style：格式（不影响代码运行的变动）。

refactor：重构（即不是新增功能，也不是修改bug的代码变动）。

perf：优化相关，比如提升性能、体验。

test：增加测试。

chore：构建过程或辅助工具的变动。

revert：回滚到上一个版本。

merge：代码合并。

sync：同步主线或分支的Bug。

示例:
> [feat] 添加了锁
> 1. 添加了自旋锁
> 2. 添加了睡眠锁

注意下面进行详细解释的时候需要带上编号`1., 2., ...`

# 代码与注释规范
## 函数前注释
```c
/**
 * @brief 这里写对函数功能的简单介绍
 *
 * 这里可以根据有需要是否要详细说明函数功能
 *
 * @param 这里对参数1进行介绍
 * @param 这里对参数2进行介绍
 * ...
 * @retval 这里对返回值进行介绍
 */
 void            ///< 返回值参数另起一行，防止出现staic unsigned int这种比较长的情况
 happy (void)    ///< 如果参数为空，最好还是带上void，K&R标准在现代C语言标准中已经不再推荐
 {               ///< 大括号另起一行，方便对应

 }
```
介绍可以带上参数的类型(比如int啥的)，也可以不带

## 函数内注释
### 功能注释，单行注释和补充注释
对于每一个需要介绍的功能块，前后需要用空行包裹，然后前面带上注释。
如果文字较少，用单行注释格式，否则用多行注释格式。
补充注释在后面用`///<`。
示例:
```c
void
happy (void)
{
    /* 
     * 这里循环一百遍                  ///< 文字多，多行注释示例
     */              
    for (int i = 0; i < 100; ++i)
    {
        printf ("I am happy.");     ///< 补充注释示例
    }
                                    ///< 空白行
    /* 这里循环一百遍 */              ///< 文字少，单行注释示例
    for (int i = 0; i < 100; ++i)
    {
        printf ("I am happy.");     ///< 补充注释示例
    }
}
```

## 结构体注释
注意对参数的解释说明时，所有注释要对齐。
示例:
```c
/**
 * @brief 基数树结构体
 * 
 * 包含指向根节点的指针和已使用的内存页计数。
 */
typedef struct 
{
    RadixNode *root;  		    ///< 根节点
    size_t used_pages;  		///< 已使用的内存页计数
} RadixTree;

```

## 代码、括号规范
使用大括号的时候，换行另起一行。一行代码可以不要大括号，注意缩进即可，尽量不要压行。一般
不超过整个VSCODE界面一般，如果超过就换行，注意对齐。比如:
```c
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
```

## 其他情况
其他情况遵循结构清晰即可。尽量不要使用`//`注释。注释主要是清晰明确，写完代码后加上即可也不需要过分详细，占用太多时间，主要时间应该是在code上，注释是方便交流，不论对象是对别人还是明天的自己: )。

# 2025.9.24
[feat] TinyLibc的第一次提交

[feat] 编译出core.o和test.o链接到用户程序
1. 但是做的不理想，本来想自动检测Tinylibc目录下的c文件的。本来写出来了... 但是我修改之后改不回来了 :(
2. core.c放库函数，test.c放测试函数。用户程序会自动执行tlibc_test

# 2025.9.30
[feat] 独立实现了printf,并且没有使用stdarg。现在只支持%d
1. 探索了几种获取参数的方法，目前这种能正确运行。我还没完全理解函数调用规范和机制
2. 写了一点printf的测试，可以正常运行。在栈上取参数也正常
3. 把Tinylibc_write改名为__write,以后凡是和SC7冲突的函数名都加下划线处理

# 2025.10.2
[feat] 增加read,openat,creat和对应的测试程序，正常运行
1. 可以打开文件读取内容，可以创建文件写入内容。
2. printf增加%s选项，输出正常。增加在qemu关机的功能，现在test.c执行后会自动关机
3. 增加tlibc.h文件，目前用来放文件操作相关的宏定义
[todo] close;删除功能;目录操作;

# 2025.10.3
[feat] 增加close,修复print_int的bug,更新项目结构
1. 更新项目结构。README.md大更新，添加了很多项目介绍
1. 修复了print_int对0处理的bug
2. close正常。添加了getdents64，还没写用户程序验证

# 2025.10.5
[refactor] 优化makefile 自动编译Tinylibc下的文件，git记录不会有initcode了
[feat] 增加app.c, 目前只有简陋的shell
[bug] LOG宏使用起来有问题，之后修

[fix] 修复了__printf对LOG宏的问题
1. 原来是%s处理部分count变量没有初始化，此外修改了内层重复的变量名str
    本来把kernel的strlen复制过来解决了问题，但不知道为什么。deepseek找到了问题，用指针或者数组遍历是一样的，只是数组for循环初始化了变量

# 2025.10.6
[feat] tlibc的shell一次读取完整的输入，内核支持sys_read的len大于1
1. 必须更改内核了，内核支持了，用户程序才能支持。按照规范，读取输入大概是sys_read(stdin,buf,1024)，现在可以了

# 2025.10.9
[feat] shell能执行命令,支持ls和touch,扩充内核的getdents，修复print_int对负数
1. 巨量更新，shell能解析命令并传入参数，顺便做了getdents和ls
2. 重命名tlibc_shutdown,有时候命名与user.c重复很麻烦，但user.c及原有程序和tlibc毕竟用途不同，用同一份文件不好
[todo] 要把ls,touch从app.c迁移出去，代码有点多了

# 2025.10.10
[refactor] 新增app目录，应用都放到app目录下，app.h提供应用程序声明，app.c只放shell
1. 然后些许修改了makefile，以适应结构变化

[feat] 增加fstat以获取文件信息,增加cat命令。还有相关定义和测试代码
1. 现在主要使用fstat获取的stat的文件长度信息
2. 修改内核的fstat部分的kstat结构体，删去了填充字段，与man 2 fstat一致

# 2025.10.11
[feat] 增加fork,wait等调用。优化shell,现在通过函数指针执行命令，
1. shell通过fork,wait来执行命令，可以处理错误值。这样还能避免命令崩溃影响到shell自身，比直接函数调用更好
2. 修改命令函数，现在通过__exit退出，
3. 添加命令只需要修改命令名表，命令函数指针表的条目就行
4. 为了清晰和标准起见，SYS_wait的名称改为SYS_wait4

# 2025.10.12
[feat] 简单地规范返回值类型

[feat] 增加rm命令，unlinkat调用，错误码处理

[feat] 修改内核的O_TRUNC，增加内核sys_openat对O_TRUNC和O_APPEND的处理。增加echo命令，重整test.c
1. echo命令采取了复杂的方式。标准的应该由shell来处理输出重定向。之后再考虑做
2. 内核的O_TRUNC改为和linux的include/uapi/asm-generic/fcntl.h一致。
3. 内核的sys_openat增加O_TRUNC和O_APPEND的处理，经过测试是符合预期的
4. test.c代码多了，拆成多个测试函数，减少主测试函数的内容

# 2025.10.13
[feat] 修改内核sys_chdir的目录合法检查。新增内置命令如chdir, 增加pwd命令。
1. 填了内核五个月之前的坑 :)
2. 新增内置命令chdir,因为chdir要改变shell自身的状态，不能用fork,wait的父子进程方式

[feat] 修复内核sys_unlinkat的小bug. 增加mkdir和rmdir
1. sys_unlinkat中, get_parent_path发现父目录是'/'时把pdir设置为0, 但是vfs_ext4_stat应该接受"/"的path
2. 增加mkdir,rmdir命令
3. 命令chdir改名为cd更合理些
4. 少许杂项修改。userlib.h的mkdir改名，修改sys_unlinkat的注释等

[feat] 增加mv,cp,help命令，增加__renameat调用。Tlibc已经具有完整的文件操作功能
1. 并且增加了主提示符Tlibc Shell:/$ 颜色样式借鉴Ubuntu的bash :)

# 2025.10.17
[refactor] 准备支持x86_64架构，重整项目架构
1. 头文件加上条件宏，避免重复包含
2. arch下设置riscv64文件夹，精简调用号到16个(目前使用的)
3. __printf的架构相关代码用条件宏框起来

# 从现在开始Tlibc作为单独的项目
# 2025.10.17
[init] 从SC7项目中分离出来，尝试支持x86_64架构
1. 现在还不能自己编译运行。当然了！PC是x86_64架构的，又不是riscv64架构
2. 这次分离只修改了三个md文件

[feat] 支持在x86_64架构运行. 可以在自己的电脑上运！行！了！
1. 首先添加了x64架构的调用号和系统调用的汇编函数
1. 修改__printf适配x64架构，使用很笨的方法，之后得重写
2. 修改shell读取输入的逻辑，以符合ubuntu内核的标准
3. 修改struct stat的布局。然后各命令基本正常了！
4. 更新help的提示信息

# 2025.10.19
[feat] 规范makefile。彻底去除标准库
1. 注释了每个编译选项的作用，调整编译选项
1. 现在链接出的程序是纯净的了，一点标准库都没有。增加链接脚本，在x64最好从0x400000开始链接
# 2025.10.30
[feat] 增加获取终端长宽度的功能，准备开发命令行游戏
1. 所有游戏以game函数作为入口，有更多游戏之后把game作为游戏管理器程序

# 2025.11.4
[feat] 第一个命令行游戏! 吃豆人! 增加很多调用
1. 新调用都是游戏需要的
2. 增加make debug

# 2025.11.5
[feat] 增加vim,能阅读文件
1. 还有一些小修改。game增加提示信息，增加tlibc_malloc，更新help信息

# 2025.11.5
[feat] vim能修改文件。还有一些小问题

# 2025.11.6
[feat] vim可以插入内容，能滚动屏幕并正确修改文件!
1. vim按i进入插入模式，在光标处插入字符(光标不会移动)，方向键移动光标和滚动屏幕，ESC退出
2. 按c进入修改模式，替换光标处的字符，不能滚动屏幕，ESC退出
3. 输入':'进入命令模式，现在只有一个命令":w",会把修改过的内容写入文件。输入:

# 2025.11.7
[feat] vim增加删除功能，修复一些bug 编辑纯英文文件很正常
1. 只是对中文的支持不佳。一个中文字符是3字节，在终端却占两个英文字符的位置，这会导致vim的显示错位，产生未定义行为。
    目前还没有想到好的方案解决这个。不过我感觉支持英文的编辑基本足够了
2. 按i进入插入模式，这个模式功能很完全了，对英文文件的增删都正常
3. 按c的修改模式停止维护
4. 多数模式下按q退出

# 2025.11.7
[feat] 测试系统调用耗时