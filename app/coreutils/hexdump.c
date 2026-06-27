#include "core.h"
#include "tlibc_print.h"
#include "tlibc_everything.h"

#define HEXDUMP_COLS 16
#define HEXDUMP_BUF_SIZE 4096

/* Print an unsigned long as zero-padded 8-digit hex to stdout */
static void print_hex8(unsigned long val)
{
    const char *hex = "0123456789abcdef";
    char buf[8];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    __write(STDOUT, buf, 8);
}

/* Print a single byte as 2-digit hex + space to stdout */
static void print_hex_byte(unsigned char val)
{
    const char *hex = "0123456789abcdef";
    char buf[3] = { hex[val >> 4], hex[val & 0xf], ' ' };
    __write(STDOUT, buf, 3);
}

/* Print a classic hexdump: hex bytes + ASCII sidebar */
static void hexdump_fd(int fd, const char *label)
{
    unsigned char buf[HEXDUMP_BUF_SIZE];
    unsigned long offset = 0;
    ssize_t n;

    if (label) {
        __printf("==> ");
        __printf((char *)label);
        __printf(" <==\n");
    }

    while ((n = __read(fd, buf, HEXDUMP_BUF_SIZE)) > 0) {
        for (ssize_t i = 0; i < n; i += HEXDUMP_COLS) {
            /* Offset - cyan, 8 hex digits + "  " */
            PRINT_COLOR(BRIGHT_CYAN_COLOR_PRINT, "");
            print_hex8(offset + i);
            __write(STDOUT, "  ", 2);

            /* Hex bytes */
            for (int j = 0; j < HEXDUMP_COLS; j++) {
                if (i + j < n) {
                    print_hex_byte(buf[i + j]);
                } else {
                    __write(STDOUT, "   ", 3);
                }
                if (j == 7) {
                    __write(STDOUT, " ", 1);
                }
            }

            /* ASCII sidebar */
            __printf("\033[36m |");
            for (int j = 0; j < HEXDUMP_COLS; j++) {
                if (i + j < n) {
                    unsigned char c = buf[i + j];
                    if (c >= 32 && c <= 126) {
                        __write(STDOUT, (char *)&c, 1);
                    } else {
                        __printf("\033[31;1m.\033[36m");
                    }
                } else {
                    __write(STDOUT, " ", 1);
                }
            }
            __printf("|\033[0m\n");
        }
        offset += n;
    }

    /* Final offset line */
    PRINT_COLOR(BRIGHT_CYAN_COLOR_PRINT, "");
    print_hex8(offset);
    __printf("\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        hexdump_fd(0, NULL);
        return 0;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int fd = __openat(AT_FDCWD, argv[i], O_RDONLY, 0);
        if (fd < 0) {
            __printf("hexdump: ");
            __printf(argv[i]);
            __printf(": No such file or directory\n");
            ret = 1;
            continue;
        }
        if (argc > 2) {
            hexdump_fd(fd, argv[i]);
        } else {
            hexdump_fd(fd, NULL);
        }
        __close(fd);
    }

    return ret;
}
