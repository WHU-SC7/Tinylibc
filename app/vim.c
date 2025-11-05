#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "tlibc_ioctl.h"
#include "sig_num.h"

#define VIM_BUF_SIZE    16*1024
#define VIM_LINE_LIMIT  1024
#define FPS             5

int vim_termi_changed = 0;//是否改变了终端模式
static struct termios vim_orig_termios; //保存原终端控制模式
int vim_pid; //主进程id
int vim_child_pid; //读取输入的子进程

char buf[VIM_BUF_SIZE];
char *line_start_table[VIM_LINE_LIMIT];//存着buf中文件内容每行的起始位置
int max_line;//最大行数
struct winsize w; //存储终端长宽度

int vim_input_process(int pipe_write_fd)//专门读取键盘输入的进程
{
    while(1)
    {
        char input = 0;
        int ret = __read(0, &input, 1); //阻塞读取
        if(ret == 1)
        {
            int ret = __write(pipe_write_fd, &input, 1);
            if(input == 'q')
                __exit(0);
            if(ret != 1)
                panic("ret: %d", ret);
            // __write(0, &input, 1);
        }
        else
            panic("ret != 1");
    }
    __exit(0);
    return 0;
}

void exit_handler(int num)
{
    if(__getpid()==vim_pid) //主进程退出，还要让输入进程也退出
    {
        __printf(ALT_SCREEN_OFF);
        __printf("接受到退出信号, num: %d\n",num);
        if(vim_termi_changed)
        {
            __printf("恢复原终端设置\n");
            __ioctl(0, TCSETS, &vim_orig_termios);
        }
        __kill(vim_child_pid, SIGINT);
        __exit(0);
    }
    if(__getpid()==vim_child_pid)//似乎没用
    {
        __exit(0);
    }
}

void vim_calcu_line_table(char *buf, int line_length)
{
    __memset(line_start_table, 0, sizeof(line_start_table));
    line_start_table[0] = buf; //第一行开头就是文件的开头
    int line_count = 1;
    int char_count = 0;//已记录的字符，达到col计做一行
    char *ptr = buf;
    while(*ptr++)
    {
        if(*ptr == '\n') //到这一行结尾，记录下一行开始位置
        {
            line_start_table[line_count] = ptr+1;
            line_count++;
            char_count = 0;
            continue;
        }
        if((char_count) == line_length)
        {
            line_start_table[line_count] = ptr+1;
            line_count++;
            char_count = 0;
            continue;
        }
        char_count++;
    }
    line_start_table[line_count] = ptr+1;
    max_line = line_count;
}

void vim_render_line(int start_line, int line_num)
{
    for(int i=start_line; i<start_line+line_num; i++)
    {
        if(line_start_table[i])
            __write(1, line_start_table[i], line_start_table[i+1]-line_start_table[i]);
        else
            break;
    }
    //保证输出完了会换行
}

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413

void vim(int argc, char *argv[])
{
    __memset((void*)&w, 0, sizeof(w));
    if (__ioctl(1, TIOCGWINSZ, &w) < 0) {
        panic("获取终端信息失败\n"); // 失败
    }
    __printf("屏幕长度是%d, 宽度是%d\n", w.ws_col, w.ws_row);
    if (__ioctl(0, TCGETS, &vim_orig_termios) < 0) {
        __exit(0);
    }
    char *file_name;
    if(argc == 2)
    {
        if(*argv[1])
            file_name = argv[1];
        else
            panic("传入文件名为空");
    }
    // char *buf = (char *)tlibc_malloc(VIM_BUF_SIZE);
    __memset(buf, 0, VIM_BUF_SIZE);
    int fd = __openat(AT_FDCWD,file_name,O_RDWR,0644);
    struct stat statbuf;
    __memset((char *)&statbuf, 0, sizeof(statbuf));
    __fstat(fd,&statbuf);
    unsigned long file_size = statbuf.st_size;
    int read_ret = __read(fd, buf, file_size);
    if(read_ret < 0)
        __exit(-1);
    vim_calcu_line_table(buf, w.ws_col);//计算出行索引
    // __printf("文件%s大小%d,读取了%d\n", file_name, file_size, read_ret);
    // char tmp;
    // __read(0, &tmp, 1);

    __printf(ALT_SCREEN_ON); //使用新的终端缓冲区输出
    __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角

    //这一段应用设置之后，终端输入模式改变
    struct termios t;
    __ioctl(0, TCGETS, &t);
    t.c_lflag &= ~(ICANON | ECHO );//| ISIG); // 禁用规范模式、回显
    // t.c_iflag = 0; // 这个会导致输出变形
    t.c_oflag = 0; // 这个必须加上
    t.c_cc[VMIN] = 0;   // 不等待字符
    t.c_cc[VTIME] = 0;  // 无超时
    // 应用新设置
    __ioctl(0, TCSETS, &t);
    vim_termi_changed = 1;

    //创建管道用于子进程传递输入给游戏主进程
    int pipefd[2];
    #define O_NONBLOCK      0x800//如果管道没有数据，直接返回-11,不会阻塞
    #define PIPE_READ   0
    #define PIPE_WRITE  1
    __pipe2(pipefd, O_NONBLOCK); 

    // 创建进程读取输入
    // int fork_ret = __clone(0,0,0,0,0);//这样也可以
    int fork_ret = __fork();
    if(fork_ret == 0)
    {
        __setsid();
        __printf("开始读取");
        vim_input_process(pipefd[PIPE_WRITE]); //pipefd[1]是写
    }
    else
    {
        vim_child_pid = fork_ret;
        vim_pid = __getpid();
        __printf("子进程id: %d\n", fork_ret);
        __printf("父进程id: %d\n", vim_pid);
        //主进程继续执行
    }

    // vim_render_frame(buf, w.ws_row, w.ws_col);
    int first_line=0;//在终端显示的第一行在文件中的行数
    while(1)
    {
        char final_input = 194;

        struct timespec time;
        time.st_atime_sec = 0;
        time.st_atime_nsec = 20000000; //0.2秒
        __nanosleep(&time, &time);
        while(__read(pipefd[PIPE_READ], &final_input, 1) != -11){}
        if(final_input == 'q')
        {
            exit_handler(114); //退出
        }
        switch (final_input)
        {
        case 'w':
            if(first_line > 0)
                first_line--;
            break;
        case 's':
            if(first_line < max_line-2)
                first_line++;
            break;
        
        default:
            break;
        }

        __printf(CLEAR_SCREEN CURSOR_HOME);
        // __printf("宽度: %d", w.ws_row);
        vim_render_line(first_line, w.ws_row-1);//除了最后一行，都输出文件内容
        if(final_input != (char)-62) //在最下行显示用户输入
            __write(1, &final_input, 1);
    }
}