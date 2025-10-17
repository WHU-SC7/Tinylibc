
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