#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "tlibc_ioctl.h"
#include "sig_num.h"
#include "tty.h"
#include "string.h"

#define TOP_ROW_WANTED 16
#define TOP_COL_WANTED 32
#define TOP_CHILD_EXIT_SIG 64
#define TOP_PROCESS_NUM_SHOW 12//每次显示的进程数
#define PROMPT_LINE_BG_COLOR BG_BLACK
#define PROMPT_LINE_FG_COLOR FG_CYAN

void top_exit_handler()
{
    __printf(ALT_SCREEN_OFF); //恢复原终端缓冲区
    tlibc_restore_term(0);
    __exit(0);
}

void top_get_process_status(const char* proc_pid, char* status_buf, int buf_size)
{
    char path[64];
    __memset(path, 0, 64);
    strcpy(path, "/proc/");
    strcat(path, proc_pid);
    strcat(path, "/status");
    int status_fd = __openat(AT_FDCWD,path, O_RDONLY, 0644);
    __memset(status_buf, 0, buf_size);
    __read(status_fd, status_buf, buf_size);
    __close(status_fd);
}

//0是name
//2是state
void top_status_get_line(char *status_buf, int line, char *buf, int buf_size)
{
    char *ptr = status_buf;
    while(1)
    {
        if(line == 0)
            break;
        if(*ptr == '\n')
            line--;
        ptr++;
    }
    while(*ptr++ != ':');
    while(*ptr == ' ' || *ptr == '\t')
        ptr++;
    int i = 0;
    while(*ptr != '\n' && i < buf_size - 1)
    {
        *buf++ = *ptr++;
        i++;
    }
}

void top_readlink(const char *proc_pid, char *link, char *buf, int buf_size)
{
    char path[64];
    __memset(path, 0, 64);
    strcpy(path, "/proc/");
    strcat(path, proc_pid);
    strcat(path, link);
    __readlinkat(AT_FDCWD, path, buf, buf_size);
}

void top_get_file(const char *proc_pid, char *file, char *file_buf, int buf_size)
{
    char path[64];
    __memset(path, 0, 64);
    strcpy(path,"/proc/");
    strcat(path,proc_pid);
    strcat(path,file);
    int comm_fd = __openat(AT_FDCWD,path,O_RDONLY,0644);
    __read(comm_fd,file_buf,buf_size);
    __close(comm_fd);
}

void top(int argc, char *argv[])
{
    int proc_fd;
    char *getdent_buf = (char *)tlibc_malloc(64*1024); //8k缓冲区
    char (*proc_pid)[8] = tlibc_malloc(256*8); //每个pid应该不会超过8字节
    int proc_count;

    tlibc_check_term_size(1, TOP_ROW_WANTED, TOP_COL_WANTED);
    tlibc_set_term_raw_and_noecho(0);

    tlibc_sigaction(2, top_exit_handler);//先设置退出的处理函数

    int pipefd[2];
    __pipe2(pipefd, O_NONBLOCK); 
    int fork_ret = __fork();
    if(fork_ret == 0)
    {
        __printf("子进程开始读取");
        general_input_process(pipefd[PIPE_WRITE]); //pipefd[1]是写
    }
    else
    {
        //主进程继续执行
    }

    __printf(ALT_SCREEN_ON); //使用新的终端缓冲区输出
    __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角

    int refresh_time = 50;//刷新计时器
    int index = 0;
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
            top_exit_handler(); //退出
        }
        if(final_input == 'r')
        {
            refresh_time = 50; //强制刷新
        }
        if(final_input == KEY_UP&&index > 0)
        {
            index--;
            refresh_time = 50;
        }
        if(final_input == KEY_DOWN&&index < proc_count-1)
        {
            index++;
            refresh_time = 50;
        }

        if(refresh_time >= 50) //每1秒刷新一次
        {
            refresh_time = 0;
            //刷新进程列表
            proc_fd = __openat(AT_FDCWD, "/proc", O_RDONLY|O_DIRECTORY|O_CLOEXEC, 0644);
            __memset(getdent_buf, 0, 64*1024);
            __memset(proc_pid, 0, 256*8);
            __getdents64(proc_fd, (struct linux_dirent64 *)getdent_buf, 64*1024);
            __close(proc_fd);
            
            struct linux_dirent64 *dent = (struct linux_dirent64 *)getdent_buf;
            proc_count = 0;
            while(dent->d_off != 0)//找出所有进程
            {
                //进程目录名全是数字
                if(dent->d_name[0] >= '0' && dent->d_name[0] <= '9')
                {
                    int i;
                    for(i=0; dent->d_name[i] != 0 && i < 7; i++)
                    {
                        proc_pid[proc_count][i] = dent->d_name[i];
                    }
                    proc_pid[proc_count][i+1] = 0; //结束符
                    proc_count++;
                }
                dent = (struct linux_dirent64 *)((char *)dent + dent->d_reclen); //< 遍历
            }

            __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角
            __printf("按q退出,按r刷新,使用上下方向键滚动\n");
            __printf("进程数: %d\n", proc_count);
            SET_ROW_COLOR(3, PROMPT_LINE_BG_COLOR, PROMPT_LINE_FG_COLOR);//设置提示行颜色
            __printf("PID     NAME            VMSIZE  VMRSS    EXE");
            __printf(COLOR_RESET "\n");

            #define START 9//某个光标起始位置
            #define NAME_WIDTH 16
            #define VM_SIZE_WIDTH 8
            #define VM_RSS_WIDTH 8
            #define EXE_WIDTH 32
            char name[NAME_WIDTH];
            char vm_size[VM_SIZE_WIDTH];
            char vm_rss[8];
            char exe[EXE_WIDTH];
            char status_buf[2048];
            for (int i = index; i < index+12; i++) 
            {
                if(proc_pid[i][0] == 0)
                    break;
                __memset(name, 0, NAME_WIDTH);
                __memset(vm_size, 0, VM_SIZE_WIDTH);
                __memset(vm_rss, 0, VM_RSS_WIDTH);
                __memset(exe, 0, EXE_WIDTH);
                __memset(status_buf, 0, 2048);
                top_get_process_status(proc_pid[i], status_buf, 2048);
                top_status_get_line(status_buf, 0, name, NAME_WIDTH);//获取name
                top_status_get_line(status_buf, 19, vm_size, VM_SIZE_WIDTH); //19行获取vm_size
                top_status_get_line(status_buf, 23, vm_rss, VM_RSS_WIDTH);//23行获取vm_rss
                top_readlink(proc_pid[i], "/exe", exe, EXE_WIDTH);

                __printf("%s", proc_pid[i]); //占位8字符
                __printf("\033[%dG", START); //到当前行的第9列
                __printf("%s", name); //占位NAME_WIDTH个字符
                __printf("\033[%dG", START+NAME_WIDTH); 

                __printf("%s", vm_size); //占位VM_SIZE_WIDTH个字符
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH);

                __printf("%s", vm_rss); //占位VM_RSS_WIDTH个字符
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH);

                __printf("%s", exe); 
                __printf("\n");
            }
        }
        refresh_time++;
    }
    //should never reach here
    __exit(0);
}