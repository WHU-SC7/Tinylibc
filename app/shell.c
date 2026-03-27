#include "core.h"
#include "tlibc_print.h"
#include "tlibc.h"
#include "errno.h"
//这里现在只放shell
int __internal_chdir(int argc, char *argv[])
{
    //检查参数
    if(argc == 1)
    {
        __printf("错误，缺少参数\n");
        return -1;
    }
    if(argc >2)
    {
        __printf("错误，参数超过两个，太多了\n");
        return -2;
    }
    if(*argv[1] == 0)
    {
        __printf("错误，传入路径是空\n");
        return -3;
    }
    //执行
    int ret = __chdir(argv[1]);
    if(ret < 0) //错误码处理
    {
        if(ret == -ENOENT)
        {
            __printf("错误，给定路径不存在\n");
            return -5;
        }
        if(ret == -ENOTDIR)
        {
            __printf("错误，给定路径不是文件夹\n");
            return -6;
        }
        __printf("错误,chdir系统调用失败,错误码: %d\n",ret);
        return -7;
    }
    return 0; //正常执行，结束
}

int __internal_help(int argc, char *argv[])
{
    __printf("Tlibc shell, 版本 0.1\n");
    __printf("下面是可用的应用列表:\n");
    __printf("  vim [file]                  - 使用Tinylibc的vim打开文件, 现在只能阅读, 使用w s上下滚动\n");
    __printf("  game                        - 玩游戏，现在只有吃豆人一个游戏\n");
    __printf("下面是可用的命令列表:\n");
    __printf("文件操作:\n");
    __printf("  cat [file]                  - 显示文件内容\n");
    __printf("  cp [in] [out]               - 复制文件\n");
    __printf("  touch [file]                - 创建空文件\n");
    __printf("  rm [file]                   - 删除文件\n\n");

    __printf("目录操作:\n");
    __printf("  ls                          - 列出当前目录内容\n");
    __printf("  ls [dir]                    - 列出目录内容\n");
    __printf("  mkdir [dir]                 - 创建目录\n");
    __printf("  rmdir [dir]                 - 删除空目录\n");
    __printf("  pwd                         - 显示当前目录\n\n");

    __printf("其他:\n");
    __printf("  echo [text]                 - 输出文本\n");
    __printf("  echo [string] > [text]      - 输出文本\n");
    __printf("  mv [old] [new]              - 移动/重命名文件\n\n");

    __printf("下面是可用的shell内置命令列表:\n");
    __printf("  cd [dir]                    - 切换工作目录\n");
    __printf("  help                        - 帮助信息\n");
    return 0;
}

#define MAX_COMMANDS 64

// 内置命令，shell按函数调用的方式执行
char *internal_command_table[] = {
    "cd",
    "help",
};

int (*internal_command_func_table[MAX_COMMANDS])(int argc, char *argv[]) = {
    __internal_chdir,
    __internal_help,
};

#define COMMAND_MAX_LEN 16 //命令的最大长度
//这个宏写的不好
#define COMMAND_NUM sizeof(command_table) / sizeof(command_table[0])                            //命令的个数
#define INTERNAL_COMMAND_NUM sizeof(internal_command_table) / sizeof(internal_command_table[0]) //内置命令的个数

#define MAX_ARGS 16
struct command{
    char *name;             //命令名，如ls
    char *args[MAX_ARGS];   //参数列表
    int argc;               //参数个数
};

/**
 * @brief 解析输入.破坏性解析，会改变input的某些' '为0
 * @return 返回0表示正常解析，返回负数表示解析错误，不同负数对应不同错误
 */
int parse_cmd(const char *input, struct command *command)
{
    //解析命令名
    char *str_start = (char *)input;
    char *str_end = (char *)input;
    if(!*str_start) //input是空字符串
        return -1;
    while(*str_end!=' '&& *str_end)
    {
        str_end++;
    }
    int command_len = str_end - str_start;
    if(command_len>COMMAND_MAX_LEN) //命令名过长
    {
        return -2;
    }
    command->name = str_start; //命令名正常
    command->args[0] = str_start; //第0个参数是命令名自身
    command->argc=1;

    //开始解析命令参数
    while(*str_end == ' ') //跳过所有的空格
        str_end++;
    if(!*str_end)//命令名后面没有带参数 
    {
        return 0;
    }
    //有参数，开始解析
    char *insert_ptr = str_start;//把命令名末尾的第一个空格改成0
    while(*insert_ptr != ' ')
        insert_ptr++;
    *insert_ptr = 0; //现在command->name指向的是以0结尾的字符串

    str_start = str_end;//已经跳过了所有空格，现在两个指针都指向第1个参数起始
    while(*str_start) //每一轮解析一个参数
    {
        command->args[command->argc] = str_start;
        command->argc++;
        while(*str_end!=' '&& *str_end)
        {
            str_end++;
        }
        // arg_len = str_end - str_start; //可以计算参数长度
        while(*str_end == ' ') //跳过所有空格
            str_end++;
        if(!*str_end)//这个参数之后读取到字符串末尾，结束
            return 0;
        else //还有后续参数
        {
            char *insert_ptr = str_start;//把这个参数末尾的第一个空格改成0
            while(*insert_ptr != ' ')
                insert_ptr++;
            *insert_ptr = 0; //现在command->name指向的是以0结尾的字符串
            str_start = str_end;
        }
    }
    return -3; //未知情况执行到末尾
}

void show_cmd_info(struct command *command)//显示struct command的信息
{
    __printf("命令名: %s, 命令个数: %d\n",command->name,command->argc);
    for(int i=0; i<command->argc; i++)
    {
        __printf("第%d个参数: %s\n",i,command->args[i]);
    }
}

/**
 * @brief 在命令表中匹配命令
 * 
 * @param input_str 要匹配的命令
 * @param command_table_to_search 要搜索的命令表
 * @param num 命令表的命令个数
 * @return 匹配的命令在表中的索引，失败返回-1
 */
int search_command(const char *input_str, char **command_table_to_search, int num)
{
    for(int i=0; i<num; i++)//依次匹配表中所有命令名
    {
        char *command = command_table_to_search[i];
        //字符串匹配
        char *ptr = (char *)input_str;
        while(1)
        {
            if((*ptr == 0) && (*command == 0)) // input_str匹配到末尾，command也匹配到末尾，认为匹配上了
                return i;
            if(*ptr == *command) // 当前字符匹配成功
            {
                ptr++;
                command++;
            }
            else //当前字符不匹配
            {
                break;
            }
        }        
    }
    return -1;
}

/**
 * @brief 根据给定的index执行命令,并传入args和argc
 */
void run_command(struct command *command)
{
    // show_cmd_info(command);

    // 创建子进程来执行命令
    int status = 0;
    int ret = 0;
    int pid = __fork();
    if(pid == 0) //子进程
    {
        ret = __execve(command->name, command->args, (void *)0);
        if(ret < 0)
        {
            __printf("execve调用失败, 路径: %s, 错误码: %d\n", command->name, ret);
            __exit(1); //执行失败，退出
        }
        __exit(0); //执行完正常退出，但一般不会到这。命令应该执行完自己正常退出
    }
    else
    {
        __waitpid(-1,&status,0); //两种wait都可以
        int signal_status = status & 0xff;
        int exit_status = status >> 8;
        if(exit_status & 0x80) //符号扩展到32位，便于print_int打印
        {
            exit_status |= 0xffffff00;
        }
        if(signal_status != 0)
            panic("意料之外的情况!子进程被信号杀死，信号号: %d\n", signal_status);
        else
        {
            if(exit_status == 0)
            {
                return; //正常执行
            }
            else
            {
                __printf("执行命令%s异常,退出的错误码: %d\n", command->name, exit_status);
                return;
            }
        }
    }    
}

void print_promt()
{
    char buf[256];
    for(int i=0; i<256; i++)
        buf[i]=0;
    __getcwd(buf,256);
    PRINT_COLOR(GREEN_COLOR_PRINT,"Tlibc Shell");
    __write(1,":",1);
    __printf(BLUE_COLOR_PRINT"%s$"COLOR_RESET,buf);
}

/**
 * @brief shell,现在只能接收输入
 */
int main(int argc, char *argv[])
{
    LOG("欢迎使用Tlibc Shell!\n");
    LOG("使用help查看支持的命令, 输入q退出shell\n");
    LOG("不要输入方向键好吗，这个版本不支持\n");

    while(1)
    {
        // 单字符输入，简单但是不标准。每个字符读取都要陷入内核，开销大
        // char input_c;
        // __read(0,&input_c,1);
        // if(input_c == 'q')
        //     break;
        // __printf("接收到输入，字符的码值: %d\n",input_c);

        // 一次读取完整的输入，以enter输入结尾
        // 疑问，内核返回的缓冲区是否应该以enter的码值结尾。 现在SC7不会
        print_promt();
        char buf[256];
        for(int i=0; i<256; i++) //每次都清空缓冲区，防止未定义行为
            buf[i] = 0;
        int read_count = __read(0,buf,256); //读取一次输入
        if(read_count < 0)
        {
            __printf("读取错误!重新读取\n");
            continue;
        }
//发现在x86_64的ubuntu上，读取一行输出时，读取的enter键输入会当成10(LF)写入缓冲区
//内核读取到entera键输入会自动换行
#if X86_64_TLIBC == 1
        // __printf("字面值: %d.",buf[read_count-1]);
        buf[read_count-1] = 0;
#endif
        if(buf[0]=='q' && buf[1]==0)    //输入是单字符就退出
            break;
        if(buf[0]==0)
        {
            continue;
        }
        // __printf("输入: %s, read返回值: %d\n",buf,read_count);

        //解析命令输入，获取命令名和参数
        struct command command;//在栈上分配空间给struct command
        char *ptr = (char *)&command; //清零，否则会异常
        for(int i=0; i<sizeof(struct command); i++)
            ptr[i]=0;
        int ret = parse_cmd(buf,&command);
        if(ret == 0) //解析成功，开始执行
        {
            // show_cmd_info(&command);
            //先检查是不是内置命令,内置命令直接执行
            ret = search_command(command.name,internal_command_table, INTERNAL_COMMAND_NUM);
            if(ret != -1)
            {
                // __printf("匹配到内置命令: %s,开始执行\n",internal_command_table[ret]);
                ret = internal_command_func_table[ret](command.argc,command.args);
                //ret是命令执行的返回值，可以进行处理
                continue;
            }
            //不是内置命令，检查是不是普通命令
            run_command(&command);
            continue;
        }
        else //解析失败
        {
            __printf("解析命令出错!, 错误码: %d\n",ret);
        }
    }
    return 0;
}