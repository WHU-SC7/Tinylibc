#首先是x64的编译选项

export TOOLPREFIX =x86_64-linux-gnu-

export CC  = ${TOOLPREFIX}gcc
export AS  = ${TOOLPREFIX}gcc
export LD  = ${TOOLPREFIX}gcc
export OBJCOPY = ${TOOLPREFIX}objcopy
export OBJDUMP = ${TOOLPREFIX}objdump
export AR  = ${TOOLPREFIX}ar

export CFLAGS = -ggdb3 -Wall -Werror -O0 -fno-omit-frame-pointer
export CFLAGS += -MD #生成make的依赖文件到.d文件
export CFLAGS += -ffreestanding -fno-common -nostdlib -fno-stack-protector 
export CFLAGS += -static  # 添加静态编译标志
export CFLAGS += -fno-stack-protector	

CFLAGS += -Iarch -Iinternal -Iexternal
CFLAGS += -Iarch/x86_64
CFLAGS += -DX86_64_TLIBC=1

export LDFLAGS = -z max-page-size=4096
LD_SCRIPT = ld.scipt
#可执行文件
Tlibc_exe = $(WORKPATH)/build/tlibc_x64

export WORKPATH = $(shell pwd)

x64_c_srcs := $(wildcard *.c app/*.c) 
x64_c_objs := $(patsubst %.c,$(WORKPATH)/build/%.o,$(x64_c_srcs))

all: init_dir x86_64

init_dir:
	mkdir -p build
	mkdir -p build/app
	@echo $(x64_c_srcs)
	@echo $(x64_c_objs)
	
x86_64: $(x64_c_objs)
#然后链接
	$(LD) $(LDFLAGS) -o $(Tlibc_exe) $(x64_c_objs) 

$(x64_c_objs): $(WORKPATH)/build/%.o: %.c
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

Tlibc_exe_name = $(notdir $(Tlibc_exe)) 
run:
	./build/$(Tlibc_exe_name)

clean:
	rm -rf build



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