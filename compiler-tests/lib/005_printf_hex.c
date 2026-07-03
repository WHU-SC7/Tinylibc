/*
 * 005_printf_hex.c — 测试 printf.c 中的十六进制/移位模式
 *
 * 验证：nibble 提取 / 64 位移位
 * EXPECT: 0
 */

int main(void) {
    /* nibble 提取 */
    int byte = 0xAB;
    int hi = (byte >> 4) & 0xf;
    int lo = byte & 0xf;
    if (hi != 0xA || lo != 0xB) return 1;

    /* 64 位移位 */
    unsigned long val = 0xDEADBEEF;
    unsigned long shifted = (val >> 16) & 0xFFFF;
    if (shifted != 0xDEAD) return 2;

    /* 64 位大常数移位 */
    unsigned long big = 0xFFFF000000000000UL;
    unsigned long top16 = (big >> 48) & 0xFFFF;
    if (top16 != 0xFFFF) return 3;

    /* 指针转换 */
    char c = 'X';
    void *ptr = (void*)&c;
    unsigned long ptr_val = (unsigned long)ptr;
    if (ptr_val == 0) return 4;
    char *recovered = (char*)ptr_val;
    if (*recovered != 'X') return 5;

    return 0;
}
