#ifndef __APP_H
#define __APP_H
void ls(int argc, char *argv[]);
void touch(int argc, char *argv[]);
void cat(int argc, char *argv[]);
void rm(int argc, char *argv[]);
void echo(int argc, char *argv[]);
void pwd(int argc, char *argv[]);

void mkdir(int argc, char *argv[]);
void rmdir(int argc, char *argv[]);

void mv(int argc, char *argv[]);
void cp(int argc, char *argv[]);

//shell内置命令
int __internal_chdir(int argc, char *argv[]);
int __internal_help(int argc, char *argv[]);

//研究测试
int __internal_test(int argc, char *argv[]);

//game
void game(int argc, char *argv[]);

//vim
void vim(int argc, char *argv[]);
#endif