// template.c 模版程序，可以fork这个程序开始编写新的应用程序
// 只需要更改template_exit_handler和template的函数名为新程序函数名即可
#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "tlibc_ioctl.h"
#include "sig_num.h"
#include "tty.h"

#define TEMPLATE_ROW_WANTED 16
#define TEMPLATE_COL_WANTED 32
#define TEMPLATE_CHILD_EXIT_SIG 64

void template_exit_handler()
{
    __printf(ALT_SCREEN_OFF); //恢复原终端缓冲区
    tlibc_restore_term(0);
    __exit(0);
}

void template(int argc, char *argv[])
{
    //强制检查终端大小
    tlibc_check_term_size(1, TEMPLATE_ROW_WANTED, TEMPLATE_COL_WANTED);
    tlibc_set_term_raw_and_noecho(0);

    tlibc_sigaction(2, template_exit_handler);//先设置退出的处理函数

    int pipefd[2];
    __pipe2(pipefd, O_NONBLOCK); 
    int fork_ret = __fork();
    if(fork_ret == 0)
    {
        //子进程开始读取
        general_input_process(pipefd[PIPE_WRITE]); //pipefd[1]是写
    }
    else
    {
        //主进程继续执行
    }

    __printf(ALT_SCREEN_ON); //使用新的终端缓冲区输出
    __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角
    __printf("这是模版程序, 能读取所有输入并显示到终端上, 按q退出\n");

    while(1)
    {
        #define NOINPUT ((char)194)//没有输入的缺省值
        char final_input = NOINPUT;

        struct timespec time;
        time.st_atime_sec = 0;
        time.st_atime_nsec = 20000000; //0.02秒
        __nanosleep(&time, &time);
        //pipe被设置成NONBLOCK时，如果没有数据可读，read会返回-11. 有数据则返回读取的字节数
        //while会读取pipe直到最后一个字符
        //最终保留最后一个字符作为用户输入，或者没有输入final_input保持194
        while(__read(pipefd[PIPE_READ], &final_input, 1) != -11){}
        if(final_input == 'q')
        {
            template_exit_handler(); //退出
        }
        if(final_input == NOINPUT)
            __write(1, ".", 1);
        else
            __write(1, &final_input, 1);
    }
    //should never reach here
    __exit(0);
}