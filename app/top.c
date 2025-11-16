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
#define TOP_MAX_PROC 1024//最多能处理的进程数，影响内存分配

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

int top_status_search_line(char *status_buf, char *search_str, char *buf, int buf_size)
{
    char *ptr = status_buf;
    while(1)
    {
        if(*ptr == 0)//到末尾还没有找到
        {
            // __memset(buf, 0, buf_size);
            return -1;
        }
        char str[64];
        int j=0;
        __memset(str, 0, 64);
        char *copy_ptr = ptr;
        while(*copy_ptr != ':')
            str[j++] = *copy_ptr++;
        str[j] = 0;
        //取这一行的字段
        if(strcmp(str, search_str) == 0)//是这一行，复制到buf
        {
            while(*ptr++ != ':');
            while(*ptr == ' ' || *ptr == '\t')
                ptr++;
            int i = 0;
            while(*ptr != '\n' && i < buf_size - 1)
            {
                *buf++ = *ptr++;
                i++;
            }
            return 0;
        }
        else//不是这一行,继续遍历
        {
            while(*ptr++ != '\n');//到下一行
        }
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

void sort_procs(char (*proc_pid)[8], unsigned long sort_buf[TOP_MAX_PROC])
{
    int i, j;
    char temp_pid[8];
    unsigned long temp_buf;
    
    // 冒泡排序（从大到小）
    for (i = 0; i < TOP_MAX_PROC - 1; i++) {
        for (j = 0; j < TOP_MAX_PROC - 1 - i; j++) {
            if (sort_buf[j] < sort_buf[j + 1]) {
                // 交换 sort_buf
                temp_buf = sort_buf[j];
                sort_buf[j] = sort_buf[j + 1];
                sort_buf[j + 1] = temp_buf;
                
                // 交换 proc_pid
                __memset(temp_pid, 0, 8);//啊，怀疑是这个地方没清零，导致交换时proc_pid错乱 deepseek误我!
                memcpy(temp_pid, proc_pid[j], 8);
                memcpy(proc_pid[j], proc_pid[j + 1], 8);
                memcpy(proc_pid[j + 1], temp_pid, 8);
            }
        }
    }
}

//时间单位是Jiffies，一般是10ms
//获取进程已运行时间
long top_get_run_time(char *pid)
{
    char stat_buf[512];
    top_get_file(pid, "/stat", stat_buf, 512);
    char *time = stat_buf; 
    //取stat的第14,15字段
    for(int j=0; j<13; j++)
    {
        while(*time != ' ')//跳过一个字段
            time++;
        time++;//下个字段
    }
    char utime[8];
    int count=0;
    __memset(utime, 0, 8);
    while(*time != ' ')
    {
        utime[count++] = *time;
        time++;
    }
    long run_time = tlibc_strtoul(utime);
    __memset(utime, 0, 8);//复用utime来存stime
    count = 0;
    while(*time != ' ')
    {
        utime[count++] = *time;//stime
        time++;
    }
    run_time += tlibc_strtoul(utime);
    return run_time;
}

struct top_proc_time{
    char proc_pid[8];
    unsigned long cpu_time;
    int have_refresh;//本轮是否被更新了
};

//在top_proc_time中查找指定pid的cpu time。没有找到返回-1
//proc_time的内容连续地存在头部
int top_search_proc_time(struct top_proc_time *proc_time, char *proc_pid)
{
    struct top_proc_time *time = proc_time;
    int count = 0;
    while(time->proc_pid[0])
    {
        if(strcmp(time->proc_pid, proc_pid)==0)
            return count;
        time++;
        count++;
    }
    return -1;
}

void top_refresh_proc_time(struct top_proc_time *proc_time, char *proc_pid, long run_time)
{
    struct top_proc_time *time = proc_time;
    int count = 0;
    while(time->proc_pid[0])//找到第一个空位或者找到Pid相同的表项
    {
        if(strcmp(time->proc_pid, proc_pid)==0)//找到pid相同的表项，更新之
        {
            time->cpu_time = run_time;
            time->have_refresh = 1;//标记已在本轮被更新
            return ;
        }
        if(count == TOP_MAX_PROC-1)
            panic("proc_time溢出, 考虑增大TOP_MAX_PROC");
        count++;
        time++;
    }
    strcpy(time->proc_pid, proc_pid);
    time->cpu_time = run_time;
    time->have_refresh = 1;//标记已在本轮被更新
}

void top_refresh_time_list(struct top_proc_time *proc_time)
{
    struct top_proc_time *time = proc_time;
    int count = 0;
    while(time->proc_pid[0])
    {
        if(count == TOP_MAX_PROC-1)
            panic("proc_time溢出, 考虑增大TOP_MAX_PROC");
        if(time->have_refresh==0)//清理
        {
            struct top_proc_time *ptr = time;
            char *move_ptr = (char *)time;
            while(ptr->proc_pid[0])//ptr指向第一个空位
                ptr++;
            for(int i=0; i<(char *)ptr - (char *)time; i++)
                move_ptr[i] = move_ptr[i+sizeof(struct top_proc_time)];
            //清除最后一项. 不，不需要清楚，已经被空位覆盖了
            // char *clear_ptr = (char *)(ptr--);
            // __memset(clear_ptr, 0, sizeof(struct top_proc_time));
        }
        else
            time->have_refresh=0;
        time++;
        count++;
    }
}

void top(int argc, char *argv[])
{
    int proc_fd;
    int proc_count;
    char *getdent_buf = (char *)tlibc_malloc(64*1024); //64k缓冲区
    char (*proc_pid)[8] = tlibc_malloc(8*TOP_MAX_PROC); //每个pid应该不会超过8字节
    unsigned long *sort_buf = tlibc_malloc(8*TOP_MAX_PROC);//存储每个进程用于排序的量
    struct top_proc_time *proc_time = (struct top_proc_time *)tlibc_malloc(sizeof(struct top_proc_time)*TOP_MAX_PROC);
    __memset(proc_time, 0, sizeof(struct top_proc_time)*TOP_MAX_PROC);


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
    int show_kernel_proc = 1;//默认不显示内核进程
    int show_line =12;//显示的行数
    int sort_option = 0;//排序选项，默认是按pid
    int top_stop = 0;
    long last_time = 0;//以10ms为单位

    #define START 9//某个光标起始位置
    #define NAME_WIDTH 16
    #define VM_SIZE_WIDTH 16
    #define VM_RSS_WIDTH 16
    #define RUN_TIME_WIDTH 12
    #define CPU_WIDTH 6
    #define EXE_WIDTH 128
    char name[NAME_WIDTH];
    char vm_size[VM_SIZE_WIDTH];
    char vm_rss[VM_RSS_WIDTH];
    char exe[EXE_WIDTH];
    char status_buf[2048];

    //初始化last_time,会有误差
    struct timespec tp;
    last_time = tp.st_atime_nsec*100 + tp.st_atime_sec;
    __clock_gettime(CLOCK_REALTIME, &tp);
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

        if(final_input == 'q')//优先检查退出
        {
            top_exit_handler(); //退出
        }
        if(final_input == 'p')//暂停
        {
            if(top_stop==0)
            {
                top_stop = 1;
                __printf(GREEN_COLOR_PRINT"已经暂停"COLOR_RESET);//提示暂停了
            }
            else
            {
                top_stop = 0;
                refresh_time = 50;
            }
        }
        if(top_stop==1)
        {
            continue;
        }
        if(final_input == 'r')//刷新
        {
            refresh_time += 10; //强制刷新
        }
        if(final_input == KEY_UP&&index > 0)//向上滚动
        {
            index--;
            refresh_time = 50;
        }
        if(final_input == KEY_DOWN&&index < proc_count-show_line)//向下滚动
        {
            index++;
            refresh_time = 50;
        }
        if(final_input == 'k')//显示内核进程
        {
            if(show_kernel_proc==0)
                show_kernel_proc=1;
            else
                show_kernel_proc=0;
        }
        if(final_input == 'w' && show_line<32)//多显示一行
        {
            show_line++;
            if(index > proc_count - show_line)
                index = proc_count-show_line;
            refresh_time = 50;
        }
        if(final_input == 's' && show_line>0)//少显示一行
        {
            show_line--;
            refresh_time = 50;
        }
        if(final_input == 'd')//到底部
        {
            index = proc_count-show_line;
            refresh_time = 50;
        }
        if(final_input == 'u')//到顶部
        {
            index = 0;
            refresh_time = 50;
        }
        if(final_input == '0')
        {
            sort_option = 0;
            refresh_time = 50;
        }
        if(final_input == '1')
        {
            sort_option = 1;//按照VMRSS排序
            refresh_time = 50;
        }
        if(final_input == '2')
        {
            sort_option = 2;
            refresh_time = 50;
        }
        if(final_input == '3')
        {
            sort_option = 3;
            refresh_time = 50;
        }
        if(final_input == '4')//按CPU占用排序
        {
            sort_option = 4;
        }
        if(refresh_time >= 50) //大约每1秒刷新一次
        {
            refresh_time = 0;
            //刷新进程列表
            proc_fd = __openat(AT_FDCWD, "/proc", O_RDONLY|O_DIRECTORY|O_CLOEXEC, 0644);
            __memset(getdent_buf, 0, 64*1024);
            __memset(proc_pid, 0, 8*TOP_MAX_PROC);
            __getdents64(proc_fd, (struct linux_dirent64 *)getdent_buf, 64*1024);
            __close(proc_fd);
            
            struct linux_dirent64 *dent = (struct linux_dirent64 *)getdent_buf;
            proc_count = 0;
            while(dent->d_off != 0)//找出所有进程
            {
                //进程目录名全是数字
                if(dent->d_name[0] >= '0' && dent->d_name[0] <= '9' && proc_count < TOP_MAX_PROC)
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

            //筛选内核进程
            if(show_kernel_proc==1)
            {
                char kthread[8];
                for(int i=0; i<proc_count; i++)
                {
                    __memset(status_buf, 0, 2048);
                    __memset(kthread, 0, 8);
                    top_get_process_status(proc_pid[i], status_buf, 2048);
                    top_status_search_line(status_buf, "Kthread", kthread, 8); //检查Kthread属性
                    if(kthread[0]=='1')
                    {
                        //覆盖前移，删除这个pid
                        for(int j=i; j<proc_count; j++)
                        {
                            __memset(proc_pid[j], 0, 8);
                            strcpy(proc_pid[j], proc_pid[j+1]);
                        }
                        proc_count--;
                        i--;
                    }
                }
            }

            //按VMRSS排序
            if(sort_option==1)
            {
                __memset(sort_buf, 0, 8*TOP_MAX_PROC);
                for(int i=0; i<proc_count; i++)//获取VMRSS，保存到sort_buf中
                {
                    __memset(status_buf, 0, 2048);
                    __memset(vm_rss, 0, VM_RSS_WIDTH);
                    top_get_process_status(proc_pid[i], status_buf, 2048);
                    int ret = top_status_search_line(status_buf, "VmRSS", vm_rss, VM_RSS_WIDTH);
                    if(ret < 0)//没有VMRSS字段
                    {
                        sort_buf[i] = 0;
                        continue;
                    }
                    char *ptr = vm_rss;
                    while(*ptr != ' ')//找到空格
                        ptr++;
                    __memset(ptr, 0, 3);//把" kB"清零
                    //转换成数字
                    sort_buf[i] = tlibc_strtoul(vm_rss);
                }
                sort_procs(proc_pid, sort_buf);
            }
            if(sort_option == 2)//按VMSIZE排序
            {
                __memset(sort_buf, 0, 8*TOP_MAX_PROC);
                for(int i=0; i<proc_count; i++)//获取VMSIZE，保存到sort_buf中
                {
                    __memset(status_buf, 0, 2048);
                    __memset(vm_rss, 0, VM_RSS_WIDTH);
                    top_get_process_status(proc_pid[i], status_buf, 2048);
                    int ret = top_status_search_line(status_buf, "VmSize", vm_rss, VM_RSS_WIDTH);
                    if(ret < 0)//没有VMSIZE字段
                    {
                        sort_buf[i] = 0;
                        continue;
                    }
                    char *ptr = vm_rss;
                    while(*ptr != ' ')//找到空格
                        ptr++;
                    __memset(ptr, 0, 3);//把" kB"清零
                    //转换成数字
                    sort_buf[i] = tlibc_strtoul(vm_rss);
                }
                sort_procs(proc_pid, sort_buf);
            }
            if(sort_option == 3)//按已运行时间排序
            {
                __memset(sort_buf, 0, 8*TOP_MAX_PROC);
                for(int i=0; i<proc_count; i++)
                    sort_buf[i] = top_get_run_time(proc_pid[i]);
                sort_procs(proc_pid, sort_buf);
            }
            if(sort_option == 4)//按CPU占用率排序
            {
                __memset(sort_buf, 0, 8*TOP_MAX_PROC);
                for(int i=0; i<proc_count; i++)
                {
                    int ret = top_search_proc_time(proc_time, proc_pid[i]);
                    if(ret == -1)//没有cpu占用信息，默认设为0
                        sort_buf[i] = 0;
                    else
                    {
                        struct timespec tp;
                        __clock_gettime(CLOCK_REALTIME, &tp);
                        long now_time = tp.st_atime_sec*100 + tp.st_atime_nsec/10000000;

                        long run_time = top_get_run_time(proc_pid[i]);
                        long cpu = run_time - proc_time[ret].cpu_time;//这一次的已运行时间减去上一次的已运行时间
                        cpu = cpu *100 / (now_time - last_time); //cpu最大是100,表示占用率
                        sort_buf[i] = cpu;
                    }
                }
                sort_procs(proc_pid, sort_buf);
            }

            __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角
            __printf("按q退出,按r加速刷新,按k控制内核进程显示,按w/s以增加/减少显示行数,按p暂停和回复,使用上下方向键滚动\n");
            __printf("按d滚动到底部,按u滚动到头部 ");
            __printf(MAGANTA_COLOR_PRINT"按4以CPU占用率排序\n"COLOR_RESET);
            __printf(BLUE_COLOR_PRINT"按1以VMRSS(物理内存占用)排序, 按2以VMSIZE排序, 按3以已运行时间排序, 按0以pid排序\n"COLOR_RESET);
            __printf("进程数: %d. index: %d. show_line: %d, sort_option: %d\n", proc_count, index, show_line, sort_option);

            SET_ROW_COLOR(5, PROMPT_LINE_BG_COLOR, PROMPT_LINE_FG_COLOR);//设置提示行颜色
            __printf("PID");
            __printf("\033[%dG", START);
            __printf("NAME");
            __printf("\033[%dG", START+NAME_WIDTH);
            __printf("VMSIZE");
            __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH);
            __printf("VMRSS");
            __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH);
            __printf("TIME+");
            __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH+RUN_TIME_WIDTH);
            __printf("CPU");
            __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH+RUN_TIME_WIDTH+CPU_WIDTH);
            __printf("EXE");
            __printf(COLOR_RESET "\n");

            for (int i = index; i < index+show_line; i++) 
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
                top_status_search_line(status_buf, "Name", name, NAME_WIDTH);
                top_status_search_line(status_buf, "VmSize", vm_size, VM_SIZE_WIDTH);
                top_status_search_line(status_buf, "VmRSS", vm_rss, VM_RSS_WIDTH);
                top_readlink(proc_pid[i], "/exe", exe, EXE_WIDTH);

                if(name[0]>128)//防止可能的段错误
                    continue;

                __write(1, proc_pid[i], 8);//输出pid
                __printf("\033[%dG", START); //到当前行的第9列
                __write(1, name, NAME_WIDTH);//Name
                __printf("\033[%dG", START+NAME_WIDTH); 
                __write(1, vm_size, VM_SIZE_WIDTH);//VmSize
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH);
                __write(1, vm_rss, VM_RSS_WIDTH);//VmRSS
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH);

                //输出已运行时间
                long run_time = top_get_run_time(proc_pid[i]);
                long t_time = run_time%100;
                long sec_time = run_time / 100;
                if(t_time<10)
                    __printf("%l.0%ls ", sec_time, t_time);//补齐小数点后的0,用整数来表示浮点数有点麻烦
                else
                    __printf("%l.%ls ", sec_time, t_time);
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH+RUN_TIME_WIDTH);

                //输出CPU占用
                int ret = top_search_proc_time(proc_time, proc_pid[i]);
                if(ret != -1)
                {
                    //刷新当前时间，以计算时间间隔
                    struct timespec tp;
                    __clock_gettime(CLOCK_REALTIME, &tp);
                    long now_time = tp.st_atime_sec*100 + tp.st_atime_nsec/10000000;

                    long cpu = run_time - proc_time[ret].cpu_time;//这一次的已运行时间减去上一次的已运行时间
                    cpu = cpu *100 / (now_time - last_time); //采样间隔内进程的运行时间 除以 采样间隔时间
                    // PRINT_COLOR(BLUE_COLOR_PRINT, "%l", proc_time[ret].cpu_time);//调试查看上一次保存的时间
                    __printf("%l% ",cpu);
                }
                else
                    __printf("0%");
                __printf("\033[%dG", START+NAME_WIDTH+VM_SIZE_WIDTH+VM_RSS_WIDTH+RUN_TIME_WIDTH+CPU_WIDTH);

                //输出进程的exe路径
                #define EXE_END 48
                if(exe[EXE_END]!=0)
                    for(int k=1; k<4; k++)
                        exe[EXE_END+k]='.';
                exe[EXE_END+4]=0;
                __printf("%s", exe); 
                __printf("\n");
            }
            //保存历史已运行时间
            for(int i=0; i<proc_count; i++)
            {
                long run_time = top_get_run_time(proc_pid[i]);
                //保存这一次的CPU time
                top_refresh_proc_time(proc_time, proc_pid[i], run_time);
            }
            //更新proc_time列表
            top_refresh_time_list(proc_time);
            //保存时间到last_time
            __clock_gettime(CLOCK_REALTIME, &tp);
            last_time = tp.st_atime_sec*100 + tp.st_atime_nsec/10000000;
        }
        refresh_time++;
    }
    //should never reach here
    __exit(0);
}