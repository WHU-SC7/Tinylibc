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

int cursor_x=1;
int cursor_y=1;
int file_line=0;

//自定义键值
#define KEY_UP     0x11    // DC1 - 设备控制1
#define KEY_DOWN   0x12    // DC2 - 设备控制2  
#define KEY_LEFT   0x13    // DC3 - 设备控制3
#define KEY_RIGHT  0x14    // DC4 - 设备控制4
int vim_input_process(int pipe_write_fd)//专门读取键盘输入的进程
{
    while(1)
    {
        char input[16];
        int ret = __read(0, input, 3); //阻塞读取
        if(ret == 1)
        {
            int ret = __write(pipe_write_fd, input, 1);
            if(input[0] == 'q')
                __exit(0);
            if(ret != 1)
                panic("ret: %d", ret);
            // __write(0, &input, 1);
        }
        else if(ret == 2)
        {
            // __creat("vim出现错误!", 0644);
            input[0] = 'q';
            __write(pipe_write_fd, input, 1);
            __exit(0);
        }
        else if(ret == 3) //1次三个字节，是转义序列
        {
            if(input[0] == 27)//0x1b(ESC) 转义序列开头，读取完整的序列
            {
                // char tmp;
                // __write(pipe_write_fd, &input, 1);
                // __read(0, &tmp, 1);
                if(input[1] == 91) //0x5b [
                {
                    // __read(0, &tmp, 1);
                    switch (input[2])
                    {
                    case 0x41: //A, 方向上
                        input[0] = KEY_UP;
                        __write(pipe_write_fd, input, 1);
                        // __creat("转义序列上", 0644);
                        break;
                    case 0x42: //B, 方向下
                        input[0] = KEY_DOWN;
                        __write(pipe_write_fd, input, 1);
                        break;
                    case 0x43: //C, 方向右
                        input[0] = KEY_RIGHT;
                        __write(pipe_write_fd, input, 1);
                        break;
                    case 0x44: //D, 方向左
                        input[0] = KEY_LEFT;
                        __write(pipe_write_fd, input, 1);
                        break;
                    default:
                        continue; //忽略
                        break;
                    }
                }
            }
        }
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
        __exit(0);
    }
    if(__getpid()==vim_child_pid)//似乎没用
    {
        __exit(0);
    }
}

void exit()//主进程让读取输入进程退出
{
    // __creat("进程受到信号退出", 0644);
    __exit(0);
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
    line_start_table[line_count] = ptr+1; //让文件最后一行能输出
    file_line = line_count;
    max_line = line_count;
    // __printf("file_line: %d\n",file_line);
    // __printf("最大行索引: %d\n", max_line);
    // for(int i=0; i<max_line; i++)
    // {
    //     __printf("%d", i);
    //     if(line_start_table[i])
    //     __write(1, line_start_table[i], line_start_table[i+1]-line_start_table[i]);
    // }
    // __write(1, "\n", 1);
    // while(1);
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

static int vim_get_line_length(int i)
{
    if(line_start_table[i] && line_start_table[i+1])
        return line_start_table[i+1] - line_start_table[i];
    else
        return -1;
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
    if(fd < 0)
    {
        __exit(-2);
    }
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
    tlibc_sigaction(2,exit_handler);//SIGINT
    tlibc_sigaction(40,exit);

    //这一段应用设置之后，终端输入模式改变
    struct termios t;
    __ioctl(0, TCGETS, &t);
    //t.c_cflag = 0x4BF;// 保持不变，原终端设置是这么多
    t.c_lflag &= ~(ICANON | ECHO );
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
    int input_mode=0;
    int command_mode=0;
    char command_buf[64];
    int command_char_count=0;
    __memset(command_buf, 0, 64);
    int char_num=0;//某一行的字符数量
    int insert_mode=0;
    while(1)
    {
        char final_input = 194;

        struct timespec time;
        time.st_atime_sec = 0;
        time.st_atime_nsec = 20000000; //0.02秒
        __nanosleep(&time, &time);
        while(__read(pipefd[PIPE_READ], &final_input, 1) != -11){}
        if(final_input == 'q')
        {
            exit_handler(114); //退出
        }

        if(input_mode)//不支持滚动屏幕，会替换光标处的字符
        {
            int limit;
            switch (final_input)
            {
            case KEY_UP :
                if(cursor_y>1)
                    cursor_y--;
                char_num = vim_get_line_length(cursor_y-1);
                if(cursor_x > char_num)
                    cursor_x = char_num;
                //向上翻页逻辑
                break;
            case KEY_DOWN :
                if(max_line - first_line < w.ws_row-1)
                    limit = max_line - first_line;
                else
                    limit = w.ws_row-1;
                if(cursor_y<limit)
                    cursor_y++;
                char_num = vim_get_line_length(cursor_y-1);
                if(cursor_x > char_num)
                    cursor_x = char_num;
                //向下翻页逻辑
                break;
            case KEY_RIGHT :
                char_num = vim_get_line_length(cursor_y-1);//这一行的字符数
                if(cursor_x<char_num)
                    cursor_x++;
                break;
            case KEY_LEFT :
                if(cursor_x>1)
                    cursor_x--;
                break;
            case 27 ://ESC
                // __creat("ESC!", 0644);
                // __kill(vim_child_pid, 40); //让读取输入进程退出
                input_mode=0;
                break;
            default:
                if(final_input != (char)-62) //替换
                {
                    line_start_table[cursor_y-1][cursor_x-1] = final_input;
                    __write(1, &final_input, 1);
                }
                if(final_input == 194)
                    continue;
                break;
            }
                __printf("\033[%d;%dH", cursor_y, cursor_x);
            // __printf(CLEAR_SCREEN CURSOR_HOME);
            // vim_render_line(first_line, w.ws_row-1);//除了最后一行，都输出文件内容
            // if(final_input != (char)-62) //在最下行显示用户输入
            //     __write(1, &final_input, 1);

            continue;
        }

        if(insert_mode)//插入模式
        {
            switch (final_input)
            {
            case KEY_UP :
                //接近顶部时翻页
                if(cursor_y<4 && first_line>0)//需要翻页
                {
                    first_line--;
                    __printf(CLEAR_SCREEN CURSOR_HOME);
                    vim_render_line(first_line, w.ws_row-1);
                    continue;
                }
                if(cursor_y>1)
                    cursor_y--;
                char_num = vim_get_line_length(first_line + cursor_y-1);//光标横坐标不超过最后一个字符
                if(cursor_x > char_num)
                    cursor_x = char_num;
                break;
            case KEY_DOWN :
                //插入模式不允许在底部继续翻页
                if(max_line - first_line < w.ws_row-1)
                {
                    if(cursor_y<max_line - first_line)//到底了，不改变first_line
                    {
                        cursor_y++;
                        char_num = vim_get_line_length(first_line + cursor_y-1);
                        if(first_line + cursor_y == max_line)//总之vim设计上，最后一个字符不能修改，否则会异常
                            char_num--;
                        if(cursor_x > char_num)
                            cursor_x = char_num;
                        __printf("\033[%d;%dH", cursor_y, cursor_x);
                        continue;
                    }
                    else
                        continue;
                }
                //接近底部时翻页
                if(cursor_y>w.ws_row-4 && max_line - first_line > w.ws_row-1)//翻页需要重新渲染一次
                {
                    first_line++;
                    __printf(CLEAR_SCREEN CURSOR_HOME);
                    vim_render_line(first_line, w.ws_row-1);
                    char_num = vim_get_line_length(first_line + cursor_y-1);
                    if(cursor_x > char_num) //光标y不需要动，改变x适应新行的长度
                        cursor_x = char_num;
                    __printf("\033[%d;%dH", cursor_y, cursor_x);
                    continue;
                }
                if(cursor_y<w.ws_row-1)
                    cursor_y++;
                char_num = vim_get_line_length(first_line + cursor_y-1);
                if(first_line + cursor_y == max_line)//总之vim设计上，最后一个字符不能修改，否则会异常
                    char_num--;
                if(cursor_x > char_num)
                    cursor_x = char_num;
                __printf("\033[%d;%dH", cursor_y, cursor_x);
                continue;
                break;
            case KEY_RIGHT :
                char_num = vim_get_line_length(first_line + cursor_y-1);//这一行的字符数
                if(first_line + cursor_y == max_line)//总之vim设计上，最后一个字符不能修改，否则会异常
                    char_num--;
                if(cursor_x<char_num)//移动光标，不能超过这一行
                    cursor_x++;
                __printf("\033[%d;%dH", cursor_y, cursor_x);
                continue;
                break;
            case KEY_LEFT :
                if(cursor_x>1)
                    cursor_x--;
                break;
            case 127 : //backspace输入是0x7f
                int back_line_x = vim_get_line_length(first_line+cursor_y-2); //保存上一行的字符数，如果退格退到上一行，就到这个位置
                if(first_line==0 && cursor_x==1 && cursor_y==1) //文件开头，不删除
                    continue;
                char *ptr = &line_start_table[first_line + cursor_y-1][cursor_x-1]; //前移一格
                unsigned long move_size = file_size - (ptr - buf);
                for(int i=0; i<move_size+2; i++) //src和dest重叠的memmove
                {
                    *(ptr-1+i) = *(ptr+i);
                }
                buf[file_size-1]=0;
                file_size--;
                //移动字符后，行索引失效了。重新计算行索引，并更新屏幕
                vim_calcu_line_table(buf, w.ws_col);
                __printf(CLEAR_SCREEN CURSOR_HOME);
                vim_render_line(first_line, w.ws_row-1);
                //更新光标
                if(cursor_x==1)//在行首，如果有上一行，
                {
                    if(cursor_y>1)
                    {
                        cursor_y--;
                        cursor_x = back_line_x+1; //为什么+1? 不是算出来的，是因为测试之后发现+1合适
                    }
                }
                if(cursor_x>1)
                    cursor_x--;
                __printf("\033[%d;%dH", cursor_y, cursor_x);//更新光标
                continue;
                break;
            case 27 ://ESC
                insert_mode=0;
                break;
            default:
                if(final_input != (char)-62) //插入字符
                {
                    //从line_start_table[cursor_y-1][cursor_x-1]开始，所有字符后移1字节
                    char *ptr = &line_start_table[first_line + cursor_y-1][cursor_x-1];
                    unsigned long move_size = file_size - (ptr - buf);
                    for(int i=0; i<move_size+1; i++) //src和dest重叠的memmove
                    {
                        buf[file_size+1-i] = buf[file_size-i];
                    }
                    file_size++;
                    *ptr = final_input;
                    //移动字符后，行索引失效了。重新计算行索引，并更新屏幕
                    vim_calcu_line_table(buf, w.ws_col);
                    __printf(CLEAR_SCREEN CURSOR_HOME);
                    vim_render_line(first_line, w.ws_row-1);
                    if(final_input=='\n')
                    {
                        cursor_y++;
                        cursor_x=1;
                    }
                    else
                        cursor_x++;
                    __printf("\033[%d;%dH", cursor_y, cursor_x);
                    continue;
                }
                if(final_input == 194)
                    continue;
                break;
            }
            __printf("\033[%d;%dH", cursor_y, cursor_x);

            continue;
        }

        if(command_mode) //命令模式
        {
            switch (final_input)
            {
            case KEY_UP :
                break;
            case KEY_DOWN :
                break;
            case KEY_RIGHT :
                break;
            case KEY_LEFT :
                break;
            case 10 ://enter
                command_mode=0;
                //执行输入的命令
                if(command_buf[0]=='w'&&command_buf[1]==0) // :w保存文件
                {
                    __lseek(fd, 0, SEEK_SET);
                    __ftruncate(fd, file_size); //改变文件大小，变小需要这样;变大无影响因为write会改变大小
                    __write(fd, buf, file_size);
                    __memset(command_buf, 0, 64);
                    command_char_count=0;
                }
                continue;
                break;
            default:
                break;
            }
            // __printf(CLEAR_SCREEN CURSOR_HOME);
            // vim_render_line(first_line, w.ws_row-1);//除了最后一行，都输出文件内容
            if(final_input != (char)-62)
            {
                __write(1, &final_input, 1);
                command_buf[command_char_count++] = final_input;
            }
            continue;
        }

        switch (final_input) //普通模式
        {
        case 'w': 
        case KEY_UP :
            if(first_line > 0)
                first_line--;
            break;
        case 's':
        case KEY_DOWN :
            if(first_line < max_line-6) //至少显示三行文件内容
                first_line++;
            break;

        case 'i' :
            insert_mode=1;
            break;
        case ':' :
            command_mode=1;
            break;
        case 'c' :
            input_mode=1;
            break;
        case 27 ://ESC
            // __creat("ESC!", 0644);
            // __kill(vim_child_pid, 40); //让读取输入进程退出
            input_mode=0;
            break;
        
        default:
            break;
        }

        __printf(CLEAR_SCREEN CURSOR_HOME);
        // __printf("宽度: %d", w.ws_row);
        vim_render_line(first_line, w.ws_row-1);//除了最后一行，都输出文件内容
        __printf("\033[%d;%dH", w.ws_row, 1); //移动到最下面一行
        if(final_input != (char)-62) //在最下行显示用户输入
            __write(1, &final_input, 1);
    }
}