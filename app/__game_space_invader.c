#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
#include "tlibc_ioctl.h"
#include "sig_num.h"

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413

//命令行的空战游戏
void __game_space_invader(int argc, char *argv[])
{
    struct winsize w;

    // 读取终端的行数和列数，这对游戏渲染很重要
    if (__ioctl(1, TIOCGWINSZ, &w) < 0) {
    panic("获取终端信息失败\n"); // 失败
    }
    __printf("获取到终端宽度: %d, 长度: %d\n", w.ws_row, w.ws_col);
    __printf("space invader正在开发中, 敬请期待!\n");
    __exit(0);
}


#define SNAKE_SCREEN_LENGTH     16//游戏屏幕列数
#define SNAKE_SCREEN_WIDTH      10//游戏屏幕行数
#define FPS                     4//游戏每秒帧数,最好不要大于50
char screen[SNAKE_SCREEN_WIDTH][SNAKE_SCREEN_LENGTH];
int score; //游戏得分
int snake_head_x = 0;//蛇头横坐标，左上角是(0,0),右下角是(20,20)
int snake_head_y = 0;//蛇头纵坐标
int food_x = SNAKE_SCREEN_LENGTH / 2;//食物横坐标
int food_y = SNAKE_SCREEN_WIDTH / 2;//食物纵坐标
enum direction{UP, DOWN, RIGHT, LEFT};
enum direction direction = RIGHT; //初始方向

int termi_changed = 0;//是否改变了终端模式
static struct termios orig_termios; //保存原终端控制模式
char thread_read; //input_thread读取到输入就存入这里
int pid; //主进程id
int child_pid; //读取输入的子进程

void render_frame()
{
    char render_buf[4096];
    int count = 0;
    __memset((void *)render_buf, 0, 4096);
    for(int i=0; i<SNAKE_SCREEN_WIDTH; i++)
    {
        for(int j=0; j<SNAKE_SCREEN_LENGTH; j++)
        {
            // __write(1, screen[i][j], 1);
            render_buf[count++] = screen[i][j];
            render_buf[count++] = ' ';
        }
        render_buf[count++] = '\n';
        render_buf[count++] = '\n';
    }
    render_buf[count++] = '\b';
    __write(1, render_buf, count);
}

void sigint_handler(int num)
{
    if(__getpid()==pid) //主进程退出，还要让输入进程也退出
    {
        __printf(ALT_SCREEN_OFF);
        __printf("接受到退出信号, num: %d\n",num);
        if(termi_changed)
        {
            __printf("恢复原终端设置\n");
            __ioctl(0, TCSETS, &orig_termios);
        }
        __kill(child_pid, SIGINT);
        __exit(0);
    }
    if(__getpid()==child_pid)//似乎没用
    {
        __exit(0);
    }
    
}

int game_input_process(int pipe_write_fd)//专门读取键盘输入的进程
{
    while(1)
    {
        char input = 0;
        int ret = __read(0, &input, 1); //阻塞读取
        if(ret == 1)
        {
            int ret = __write(pipe_write_fd, &input, 1);//传给游戏主进程
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

//命令行吃豆人
void __game_pacman()
{
    struct winsize w;
    if (__ioctl(1, TIOCGWINSZ, &w) < 0) {
        panic("获取终端信息失败\n"); // 失败
    }
    if(w.ws_row < SNAKE_SCREEN_WIDTH*2)
    {
        PRINT_COLOR(RED_COLOR_PRINT, "屏幕宽度是%d, 小于游戏的要求: %d\n", w.ws_row, SNAKE_SCREEN_WIDTH*2);
        __exit(0);
    }
    if(w.ws_col < SNAKE_SCREEN_LENGTH*2)
    {
        PRINT_COLOR(RED_COLOR_PRINT, "屏幕长度是%d, 小于游戏的要求: %d\n", w.ws_col, SNAKE_SCREEN_LENGTH*2);
        __exit(0);
    }

    if (__ioctl(0, TCGETS, &orig_termios) < 0) {
        return;
    }
    __printf("原termios信息, c_iflag: %d\n", orig_termios.c_iflag);
    
    // long ret = __brk(0);
    // char *ptr  = (char *)__brk((void *)(ret+1024*16)); //分配16k内存示例

    __printf(ALT_SCREEN_ON); //使用新的终端缓冲区输出
    __printf(CLEAR_SCREEN CURSOR_HOME); // 清屏并移动到左上角
    __memset(screen, '.', SNAKE_SCREEN_LENGTH*SNAKE_SCREEN_WIDTH); //初始化地图空位
    screen[food_y][food_x] = 'O';//放置初始食物
    tlibc_sigaction(2,sigint_handler);//SIGINT

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
    termi_changed = 1;

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
        game_input_process(pipefd[PIPE_WRITE]); //pipefd[1]是写
    }
    else
    {
        child_pid = fork_ret;
        pid = __getpid();
        __printf("子进程id: %d\n", fork_ret);
        __printf("父进程id: %d\n", pid);
        //游戏主进程继续执行
    }

    //游戏主循环
    while(1)
    {
        char final_input='.'; //渲染画面时使用的用户输入
        for(int i=0; i<50/FPS; i++) // 近似每秒渲染FPS次画面
        {
            struct timespec time;
            time.st_atime_sec = 0;
            time.st_atime_nsec = 20000000; //0.02秒，这个速度快到能吃掉按住键盘时的所有输入
            __nanosleep(&time, &time);
            // __write(1, ".", 1);
            char input;
            int ret = __read(pipefd[PIPE_READ], &input, 1);//从管道读取输入
            if(ret == -11)
            {
                // __printf("没有读取到输入");
            }
            else
            {
                // __write(1, &input, 1);
                final_input = input;
            }
            if(input == 'q')
            {
                sigint_handler(114); //退出
            }
        }
        char snake_head_char;//蛇头的字符
        switch (final_input)//确定方向
        {
        case 'w':
            direction = UP;
            break;
        case 's':
            direction = DOWN;
            break;
        case 'a':
            direction = LEFT;
            break;
        case 'd':
            direction = RIGHT;
            break;
        default://direction不变
            break;
        }
        //更改原蛇头位置
        screen[snake_head_y][snake_head_x] = '.';
        switch (direction)//根据方向更改蛇头位置
        {
        case UP:
            snake_head_char = 'v';
            if(snake_head_y>0)
                snake_head_y--;
            break;
        case DOWN:
            snake_head_char = '^';
            if(snake_head_y<SNAKE_SCREEN_WIDTH-1)
                snake_head_y++;
            break;
        case LEFT:
            snake_head_char = '>';
            if(snake_head_x>0)
                snake_head_x--;
            break;
        case RIGHT:
            snake_head_char = '<';
            if(snake_head_x<SNAKE_SCREEN_LENGTH-1)
                snake_head_x++;
            break;
        default:
            break;
        }
        if(screen[snake_head_y][snake_head_x] == 'O') //吃到食物，生成新食物并且加分
        {
            score++;
            unsigned int random;
            __getrandom(&random, sizeof(random), 0);
            food_x = random % (SNAKE_SCREEN_LENGTH-1);

            __getrandom(&random, sizeof(random), 0);
            food_y = random % (SNAKE_SCREEN_WIDTH-1);
            screen[food_y][food_x] = 'O';
        }
        screen[snake_head_y][snake_head_x] = snake_head_char;
        __printf(CLEAR_SCREEN CURSOR_HOME);
        __printf("得分: %d\n", score);
        render_frame();
        __write(1, &final_input, 1);
    }
    __exit(0); //不会到这
}



//游戏管理程序
void game(int argc, char *argv[])
{
    __printf("正在启动space invader...\n");
    // __game_space_invader(argc, argv);
    __game_pacman();
}
