#include "tcc.h"

int main(int argc, char *argv[]) {
    const char *s = argv[1];
    Lexer lex;
    lex.start = s;
    return (lex.start == s) ? 42 : 0;
}
