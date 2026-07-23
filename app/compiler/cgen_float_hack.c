/*
 * cgen_float_hack.c — SSE 浮点辅助函数
 *
 * 在单独的编译单元中以避开 toyc 的 double ABI bug（tcc 用 RDI
 * 传 double 而非 XMM0）和 struct 字段偏移 bug。
 */

#include "toyc.h"

/* 接受高低 32 位 IEEE 754 双精度原始位模式，发射
 * mov rax, imm64; movq xmm0, rax 以加载 double 到 xmm0。
 * hi/lo 通过 32 位整数寄存器传递（toyc 可正确处理）。 */
void load_double_bits_halves(unsigned int hi, unsigned int lo) {
    e1(0x48); e1(0xB8);
    e1(lo & 0xFF); e1((lo>>8) & 0xFF); e1((lo>>16) & 0xFF); e1((lo>>24) & 0xFF);
    e1(hi & 0xFF); e1((hi>>8) & 0xFF); e1((hi>>16) & 0xFF); e1((hi>>24) & 0xFF);
    e1(0x66); e1(0x48); e1(0x0F); e1(0x6E); e1(0xC0);
}

/* 从 double 值的内存表示中读取 IEEE 754 原始位模式，发射
 * mov rax, imm64; movq xmm0, rax 加载 double 到 xmm0。
 * 参数 p 指向一个 double 值的内存（由调用者取地址传入）。 */
void emit_double_from_ptr(const unsigned int *p) {
    unsigned int lo = p[0];
    unsigned int hi = p[1];
    e1(0x48); e1(0xB8);
    e1(lo & 0xFF); e1((lo>>8) & 0xFF); e1((lo>>16) & 0xFF); e1((lo>>24) & 0xFF);
    e1(hi & 0xFF); e1((hi>>8) & 0xFF); e1((hi>>16) & 0xFF); e1((hi>>24) & 0xFF);
    e1(0x66); e1(0x48); e1(0x0F); e1(0x6E); e1(0xC0);
}
