#首先是x64的编译选项

export TOOLPREFIX =x86_64-linux-gnu-

export CC  = ${TOOLPREFIX}gcc
export AS  = ${TOOLPREFIX}gcc
#使用gcc作为LD会自动添加标准库文件: _init _start, 运行时支持等。为了只包含我们的代码，使用ld
export LD  = ${TOOLPREFIX}ld
export OBJCOPY = ${TOOLPREFIX}objcopy
export OBJDUMP = ${TOOLPREFIX}objdump
export AR  = ${TOOLPREFIX}ar

#自动生成依赖文件，支持增量编译
CFLAGS += -MD
#关闭栈保护机制，危险的字符串和指针操作会触发机制导致编译失败。开启可以查看哪里有危险的操作
CFLAGS += -fno-stack-protector
#!!!只需要添加上面两个选项就可以通过编译

#维护帧指针，可以栈回溯
CFLAGS += -fno-omit-frame-pointer
#带gdb调试信息，启用所有警告，警告是错误，不优化
CFLAGS += -ggdb3 -Wall -Werror -O0 
#禁止重复定义全局变量
CFLAGS += -fno-common
#不使用标准库。-nostdlib包含后两者的效果
CFLAGS += -nostdlib -nostartfiles -nodefaultlibs
#比-nostdlib,不仅不使用标准库，还影响一些宏
CFLAGS += -ffreestanding
#生成绝对地址代码，而不是位置无关代码
CFLAGS += -fno-pie
#禁用Red Zone, Red Zone是栈指针(%rsp)下方128字节的安全区域. 函数可以在不调整栈指针的情况下使用这个区域存储局部变量。
CFLAGS += -mno-red-zone


#头文件
CFLAGS += -Iarch -Iinclude
CFLAGS += -Iarch/x86_64
#宏定义
CFLAGS += -DX86_64_TLIBC=1

#链接，段按4k大小对其
LDFLAGS += -z max-page-size=4096
#不链接标准库
LDFLAGS += -nostdlib
#静态链接
LDFLAGS += -static
#链接脚本
LD_SCRIPT = ld.script

#可执行文件
Tlibc_exe = $(WORKPATH)/build/tlibc_x64

export WORKPATH = $(shell pwd)

x64_c_srcs := $(wildcard *.c app/*.c lib/*.c) 
x64_c_objs := $(patsubst %.c,$(WORKPATH)/build/%.o,$(x64_c_srcs))

all: __x86_64

#先建立目录，再多线程编译。否则可能出错
__x86_64: clean init_dir 
	make x86_64 -j

init_dir:
	mkdir -p build
	mkdir -p build/app build/lib
	@echo $(x64_c_srcs)
	@echo $(x64_c_objs)
	
#这个目标会编译并链接出可执行文件
x86_64: $(x64_c_objs)
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $(Tlibc_exe) $(x64_c_objs) 

$(x64_c_objs): $(WORKPATH)/build/%.o: %.c
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

Tlibc_exe_name = $(notdir $(Tlibc_exe)) 
run:
	./build/$(Tlibc_exe_name)

clean:
	rm -rf build

dis_file := build/dis_tlibc
disassemb:
	$(OBJDUMP) -S -C $(Tlibc_exe) > $(dis_file)
	@echo "反汇编得到文件"$(dis_file)

debug: all
	strace $(Tlibc_exe)

#再向下是原来用于riscv的部分


TLIBC_INCLUDE_FLAGS := -Iarch -Iinternal -Iexternal
RISCV_TLIBC_INCLUDE_FLAGS := -Iarch/riscv64
RISCV_CFLAGS += $(TLIBC_INCLUDE_FLAGS) $(RISCV_TLIBC_INCLUDE_FLAGS) -DRISCV_TLIBC=1
RISCV_ASFLAGS += $(TLIBC_INCLUDE_FLAGS) $(RISCV_TLIBC_INCLUDE_FLAGS)

c_src := $(wildcard *.c app/*.c) 
s_src := $(wildcard *.S)

# card obj file name
c_obj = $(patsubst %.c,$(WORKPATH)/user/build/riscv/%.o,$(c_src))
s_obj = $(patsubst %.S,$(WORKPATH)/user/build/riscv/%.o,$(s_src))

riscv: init_path $(c_obj) $(s_obj)

$(c_obj): $(WORKPATH)/user/build/riscv/%.o: %.c 
	$(RISCV_CC) -c $(RISCV_CFLAGS) -MF $(WORKPATH)/user/build/riscv/$*.d -o $@ $<

$(s_obj): $(WORKPATH)/user/build/riscv/%.o: %.S 
	$(RISCV_CC) -c $(RISCV_ASFLAGS) -MF $(WORKPATH)/user/build/riscv/$*.d -o $@ $<

init_path:
	mkdir -p $(WORKPATH)/user/build/riscv/
	mkdir -p $(WORKPATH)/user/build/riscv/app/