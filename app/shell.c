#include "core.h"
#include "tlibc_print.h"
#include "errno.h"
#include "tlibc_everything.h"

#define SHELL_BUF_SIZE TLIBC_BUF_SIZE

    #define MAX_FILE_NUM 1024
    #define FILE_NAME_MAX_LEN 256
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
        char command_path[1024];
        tlibc_restore_term(STDIN); //恢复规范模式
        if(tlibc_is_path_file(command->name) < 0){ //命令不存在，执行失败
            if(command->name[0] == '/' || command->name[0] == '.'){ //如果命令以/或.开头，直接报错
                __printf("错误: 可执行文件%s不存在\n", command->name);
                __exit_group(-2);
            }
            else{ //如果命令不以/或.开头，可能是PATH路径下的命令，尝试在PATH路径下寻找
                char path_buf[1024];
                char *path_ptr = path_buf;
                int find = 0;
                int read_config_path(char *path_buf, int buf_size);
                read_config_path(path_buf, 1024); //从配置文件读取PATH路径
                if(path_buf[0] == 0){ //如果配置文件里没有PATH路径，报错
                    __printf("读取配置文件中的PATH路径失败\n");
                    __exit_group(-4);
                }
                while(*path_ptr){
                    snprintf(command_path, 1024, "%s/%s", path_ptr, command->name); 
                    if(tlibc_is_path_file(command_path) == 1){ //在PATH路径下找到了这个命令，准备执行
                        printf("在PATH路径%s下找到了命令%s, 即将执行\n", path_ptr, command->name);
                        // strcpy(command->name, command_path); //不可以传，传了就破坏了
                        find = 1;
                        break;
                    }
                    printf("在PATH路径%s下没有找到命令%s\n", path_ptr, command->name);
                    path_ptr += strlen(path_ptr) + 1; //移动到下一个路径，路径之间以0分隔
                }

                if(find == 0){ //PATH路径下也没有这个命令，执行失败
                    __printf("在PATH下也没有找到命令%s, 错误!\n", command->name);
                    __exit_group(-3);
                }//如果找到了，就继续往下执行execve
            }
        }
        else{
            strcpy(command_path, command->name);
        }
        // show_cmd_info(command);
        ret = __execve(command_path, command->args, (void *)0);
        if(ret < 0)
        {
            __printf("execve调用失败, 路径: %s, 错误码: %d\n", command->name, ret);
            __exit_group(-1); //执行失败，退出
        }
        __exit_group(0); //执行完正常退出，但一般不会到这。命令应该执行完自己正常退出
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

void sigint_handler(int num)
{
    tlibc_restore_term(STDIN); //恢复终端设置
    exit_group(0);
}

#define SHELL_CONFIG_FILE_NAME "tlibc_shell_config"
#define CONFIG_PATH_KEY "PATH="
#define CONFIG_PATH_KEY_LEN 5
//如果没有配置文件,初始化配置文件
void shell_init_config()
{
    char tlibc_path[1024]; ///home/$(username)/tlibc
    memset(tlibc_path, 0, 1024);
    tlibc_get_user_dir(tlibc_path, 1024);
    strcat(tlibc_path, "/tlibc");
    if(tlibc_is_path_dir(tlibc_path) <= 0){ //tlibc目录不存在
        mkdirat(AT_FDCWD, tlibc_path, 0755);
    }

    char tlibc_shell_config_file_path[1024];
    snprintf(tlibc_shell_config_file_path, 1024, "%s/%s", tlibc_path, SHELL_CONFIG_FILE_NAME);
    if(tlibc_is_path_file(tlibc_shell_config_file_path) < 0){ //配置文件不存在
        int config_fd = creat(tlibc_shell_config_file_path, 0644);
        //写入默认配置, PATH设置为/home/$(username)/tlibc/bin
        char path[1024];
        snprintf(path, 1024, "PATH=%s/bin", tlibc_path);
        write(config_fd, path, strlen(path));
        close(config_fd); //只是创建
printf("已创建默认配置文件，路径: %s\n", tlibc_shell_config_file_path);
    }
}

int read_config_path(char *path_buf, int buf_size){
    char tlibc_path[1024];
    memset(tlibc_path, 0, 1024);
    tlibc_get_user_dir(tlibc_path, 1024);
    strcat(tlibc_path, "/tlibc");

    char config_file_path[1024];
    snprintf(config_file_path, 1024, "%s/%s", tlibc_path, SHELL_CONFIG_FILE_NAME);
    int config_fd = __openat(AT_FDCWD, config_file_path, O_RDONLY, 0644);
    if(config_fd < 0){
        shell_init_config(); //初始化配置文件
        config_fd = __openat(AT_FDCWD, config_file_path, O_RDONLY, 0644);
    }
// printf("打开配置文件%s, fd: %d\n", config_file_path, config_fd);
    int config_len = tlibc_get_file_len(config_file_path);
    char *buf = (char *)mmap(0, config_len + 1, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if(buf == MAP_FAILED){
        printf("内存映射失败, 错误码: %d\n", -ENOMEM);
        return -1;
    }
    int n = __read(config_fd, buf, config_len);
    if(n < 0){
        printf("读取配置文件%s失败, 错误码: %d\n", config_file_path, n);
        close(config_fd);
        return -1;
    }
    buf[config_len] = 0; //确保是字符串
    close(config_fd);
    //处理配置文件
    // 解析配置文件中的所有 PATH 设置
    char *current = buf;
    int offset = 0;
    int path_count = 0;
    
    while(*current && offset < buf_size){
        // 查找当前行的结束位置
        char *line_end = strchr(current, '\n');
        if(!line_end){
            line_end = current + strlen(current);
        }
        
        int line_len = line_end - current;
        
        // 检查是否以 "PATH=" 开头
        if(line_len > 5 && strncmp(current, "PATH=", 5) == 0){
            char *value_start = current + 5;
            char *value_end = line_end;
            
            // 去除末尾的换行符和回车符
            while(value_end > value_start && 
                  (*(value_end - 1) == '\n' || *(value_end - 1) == '\r')){
                value_end--;
            }
            
            int path_len = value_end - value_start;
            
            // 确保有足够空间（路径字符串 + 结尾的 '\0'）
            if(offset + path_len + 1 <= buf_size){
                memcpy(path_buf + offset, value_start, path_len);
                offset += path_len;
                path_buf[offset++] = '\0';  // 用 '\0' 分隔
                path_count++;
            } else {
                // 缓冲区不足，提前结束
                printf("缓冲区不足，已存储 %d 个路径\n", path_count);
                break;
            }
        }
        
        // 移动到下一行
        current = (*line_end == '\n') ? line_end + 1 : line_end;
    }
    
    // 添加一个额外的结束标记（两个连续的 '\0'）
    if(offset + 1 <= buf_size){
        path_buf[offset] = '\0';
    }
    
    munmap(buf, config_len + 1);
    
    if(path_count == 0){
        printf("WARNING: 配置文件中未找到 PATH 设置\n");
        return -1;
    }
    return 0;
}

void tab_complete(char *buf)
{
    int find_in_path = 1;//是否需要匹配PATH的命令，1表示需要，0表示不需要
    if(buf[0]==0){
        return;
    }
    int last_space_index = -1;
    for(int i=0; buf[i]; i++)
    {
        if(buf[i] == ' ')
        {
            find_in_path = 0; //有空格隔开，不在PATH下匹配
            last_space_index = i;
        }
    }
    char *str_to_match = &buf[last_space_index+1];

    if(str_to_match[0] == '/') //最后一个空格后没有输入，现在不匹配
    {
        find_in_path = 0;
    }
    char *final_string = NULL; //要匹配的部分，是路径最后一部分，后面不带'/'
    for(int i=strlen(str_to_match)-1; i>=0; i--)
    {
        if(str_to_match[i] == '/')
        {
            final_string = &str_to_match[i+1];
            break;
        }
    }
    if(final_string == NULL)
    {
        final_string = str_to_match;
    }
// printf("给定路径: %s, 要匹配的字符串: %s\n", str_to_match, final_string);

    if(str_to_match[0] == 0) //最后一个空格后没有输入，现在不匹配
    {
        return;
    }
    // printf("要匹配的字符串: %s, 长度: %d", str_to_match, strlen(str_to_match));
    char file_name[MAX_FILE_NUM][FILE_NAME_MAX_LEN];//最多支持1024个文件，每个文件名最多255字符
    int file_num = 0;
    //获取当前目录下的文件列表
    #define SHELL_LS_BUF_SIZE TLIBC_BUF_SIZE
    char *ls_buf = (char *)mmap(0, SHELL_LS_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    memset(ls_buf, 0, SHELL_LS_BUF_SIZE);
    //路径处理
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    char path_delete_slash[1024];//路径部分，去掉最后的'/'
    memset(path_delete_slash, 0, 1024);
    strcpy(path_delete_slash, str_to_match);
    if(path_delete_slash[0] == '/'){ //绝对路径
        for(int i=strlen(path_delete_slash)-1; i>=0; i--){
            if(path_delete_slash[i] == '/')
            {
                if(i == 0){ //第一个'/'不要删除
                    path_delete_slash[1] = 0;
                    break;
                }
                else{
                    path_delete_slash[i] = 0;
                    break;
                }
            }
        }
    }
    else{ //相对路径
        //去掉最后一个'/'
        int tmp = 0;
        for(int i=strlen(path_delete_slash)-1; i>=0; i--){
            if(path_delete_slash[i] == '/')
            {
                tmp = 1;
                path_delete_slash[i] = 0;
                break;
            }
        }
        if(tmp == 0){ //没有'/'，说明路径部分是当前目录
            strcpy(path_delete_slash, ".");
        }
    }
    

    //把buf中的路径和当前目录拼接成要列出的目录路径
    char absolute_path[1024];
    tlibc_cal_absolute_path(path_delete_slash, cwd, absolute_path, sizeof(absolute_path));
// printf("buf: %s, path_delete_slash: %s, cwd: %s, 计算得到的绝对路径: %s\n", buf, path_delete_slash, cwd, absolute_path);
    int compare_dir_fd = openat(AT_FDCWD, absolute_path, O_RDONLY|O_DIRECTORY|O_CLOEXEC, 0644);
    if(compare_dir_fd < 0){
        PRINT_COLOR(RED_COLOR_PRINT, "匹配失败，目录%s不存在, 错误码: %d\n", absolute_path, compare_dir_fd);
        munmap(ls_buf, SHELL_LS_BUF_SIZE);
        return;
    }
    
    int ret = __getdents64(compare_dir_fd, (struct linux_dirent64 *)ls_buf, SHELL_LS_BUF_SIZE);
    close(compare_dir_fd);
    if(ret < 0){
        panic("getdents64失败, 错误码: %d\n", ret);
    }
    struct linux_dirent64 *data = (struct linux_dirent64 *)ls_buf;
    while(data->d_off != 0){
        if(file_num < MAX_FILE_NUM)
        {
            strncpy(file_name[file_num], data->d_name, FILE_NAME_MAX_LEN-1);
            file_name[file_num][FILE_NAME_MAX_LEN-1] = 0; //确保文件名是以0结尾的字符串
            file_num++;
            // printf("找到文件: %s\n", data->d_name);
        }
        else
        {
            PRINT_COLOR(RED_COLOR_PRINT, "文件数量超过上限%d，只处理前%d个文件\n", MAX_FILE_NUM , MAX_FILE_NUM);
            break;
        }
        data = (struct linux_dirent64 *)((char *)data + data->d_reclen); //< 遍历
    }
    //不考虑PATH匹配
    int match_file_num = 0; //能匹配的文件名数量
    int match_file_index[MAX_FILE_NUM]; //能匹配的文件在file_name数组中的索引
    for(int i=0; i<file_num; i++){
        if(strncmp(file_name[i], final_string, strlen(final_string)) == 0)
        {
            // PRINT_COLOR(GREEN_COLOR_PRINT, "%s\n", file_name[i]);
            match_file_index[match_file_num] = i;
            match_file_num++;
        }
    }
    if(match_file_num == 1) //只有一个匹配，自动补全
    {
        char *match_file_name = file_name[match_file_index[0]];
        // int len_to_add = strlen(match_file_name) - strlen(final_string);
        strcat(buf, &match_file_name[strlen(final_string)]); //把匹配的文件名剩余部分添加到输入缓冲区
        PRINT_COLOR(GREEN_COLOR_PRINT, "唯一候选项，补全为: %s", match_file_name);
// printf("补全后buf: %s, str_to_match: %s\n", buf, str_to_match); 
        //应该使用str_to_match和cwd计算出绝对路径，然后判断是否是目录要加'/
        find_in_path = 0; //已经找到唯一匹配了，不需要在PATH路径下匹配了
    }
    else if(match_file_num > 1)
    {
        PRINT_COLOR(YELLOW_COLOR_PRINT, "有%d个匹配,将列出这些选项: ", match_file_num);
        for(int i=0; i<match_file_num; i++)
        {
            char *match_file_name = file_name[match_file_index[i]];
            printf("%s ", match_file_name);
        }
        find_in_path = 0;//有多个匹配了，不需要在PATH路径下匹配了
    }
    else
    {
        PRINT_COLOR(RED_COLOR_PRINT, "没有匹配，在PATH路径中继续查找");//没有匹配才考虑PATH路径下的匹配
    }

    if(find_in_path == 1){
        //添加PATH路径的文件
        char path_buf[4096];
        memset(path_buf, 0, 4096);
        if(read_config_path(path_buf, sizeof(path_buf)) == 0){
            char *path = path_buf;
            while(*path){
                memset(ls_buf, 0, SHELL_LS_BUF_SIZE);
                int fd = openat(AT_FDCWD, path, O_RDONLY|O_DIRECTORY|O_CLOEXEC, 0644);
                if(fd < 0){
// PRINT_COLOR(RED_COLOR_PRINT, "在补全匹配时，打开PATH路径%s失败, 错误码: %d\n", path, fd);
                    path += strlen(path) + 1; //跳过这个路径，继续下一个路径
                    continue;
                }
                __getdents64(fd, (struct linux_dirent64 *)ls_buf, SHELL_LS_BUF_SIZE);
                //遍历PATH路径下的文件
                struct linux_dirent64 *data = (struct linux_dirent64 *)ls_buf;
                while(data->d_off != 0){
                    if(file_num < MAX_FILE_NUM)
                    {
                        strncpy(file_name[file_num], data->d_name, FILE_NAME_MAX_LEN-1);
                        file_name[file_num][FILE_NAME_MAX_LEN-1] = 0; //确保文件名是以0结尾的字符串
                        file_num++;
                        // printf("找到文件: %s\n", data->d_name);
                    }
                    else
                    {
                        PRINT_COLOR(RED_COLOR_PRINT, "文件数量超过上限%d，只处理前%d个文件\n", MAX_FILE_NUM , MAX_FILE_NUM);
                        break;
                    }
                    data = (struct linux_dirent64 *)((char *)data + data->d_reclen); //< 遍历
                }
                close(fd);
                //清零一些变量？
                match_file_num = 0; //能匹配的文件名数量
                for(int i=0; i<file_num; i++){
                    if(strncmp(file_name[i], final_string, strlen(final_string)) == 0)
                    {
                        // PRINT_COLOR(GREEN_COLOR_PRINT, "%s\n", file_name[i]);
                        match_file_index[match_file_num] = i;
                        match_file_num++;
                    }
                }
                if(match_file_num == 1) //只有一个匹配，自动补全
                {
                    char *match_file_name = file_name[match_file_index[0]];
                    // int len_to_add = strlen(match_file_name) - strlen(final_string);
                    strcat(buf, &match_file_name[strlen(final_string)]); //把匹配的文件名剩余部分添加到输入缓冲区
                    PRINT_COLOR(GREEN_COLOR_PRINT, "在PATH路径%s中找到唯一候选项，补全为: %s", path, match_file_name);
                    // printf("补全后buf: %s, str_to_match: %s\n", buf, str_to_match); 
                    //应该使用str_to_match和cwd计算出绝对路径，然后判断是否是目录要加'/'
                }
                else if(match_file_num > 1)
                {
                    PRINT_COLOR(YELLOW_COLOR_PRINT, "在PATH路径%s中有%d个匹配,将停止进一步匹配并列出这些选项: ", path, match_file_num);
                    for(int i=0; i<match_file_num; i++)
                    {
                        char *match_file_name = file_name[match_file_index[i]];
                        printf("%s ", match_file_name);
                    }
                }
                else
                {
                    PRINT_COLOR(RED_COLOR_PRINT, "在PATH路径%s中没有匹配", path);
                }
                path += strlen(path) + 1; //跳过这个路径，继续下一个路径
            }
        }
    }
    //释放内存
    munmap(ls_buf, SHELL_LS_BUF_SIZE);
}

/**
 * @brief shell,现在只能接收输入
 */
int main(int argc, char *argv[])
{
    LOG("欢迎使用Tlibc Shell!\n");
    LOG("使用help查看支持的命令, 输入q退出shell\n");
    LOG("不要输入方向键好吗，这个版本不支持\n");

    //加载配置文件
    char path_buf[4096];
    memset(path_buf, 0, 4096);
    if(read_config_path(path_buf, sizeof(path_buf)) == 0){
        char *ptr = path_buf;
        int index = 0;
        
        printf("加载的 PATH 路径:\n");
        while(*ptr){
            printf("  [%d] %s\n", index++, ptr);
            ptr += strlen(ptr) + 1;
        }
    }

    /* 信号处理：外部 kill 时恢复终端 */
    tlibc_sigaction(SIGINT,  sigint_handler);
    tlibc_sigaction(SIGTERM, sigint_handler);

    char *buf = mmap(0, SHELL_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    while(1)
    {
        tlibc_set_term_raw_and_noecho(STDIN); //设置终端为raw模式，关闭回显
        /* raw 模式清除了 OPOST，重新启用 \n → \r\n 输出换行 */
        {
            struct termios term;
            tlibc_get_term_config(STDIN, &term);
            term.c_oflag |= OPOST | ONLCR;
            tlibc_set_term_config(STDIN, &term);
        }
        print_promt();

        memset(buf, 0, SHELL_BUF_SIZE); //每次都清空缓冲区，防止未定义行为
        int read_count = 0;
        while(1){
            //可能要改变光标位置
            char ch;
            int i= __read(0,&ch,1); //读取一次输入
            if(i < 0)
            {
                panic("读取错误!\n");
                continue;
            }
            // __printf("接收到输入，字符的码值: %d\n",buf[read_count]);
            if(ch == '\n' || ch == '\r') //输入一行结束（raw 模式下 Enter 发 \r）
            {
                buf[read_count] = 0; //把换行符改成0，构成字符串
                write(STDOUT,"\n",1); //回显输入（OPOST+ONLCR 转成 \r\n）
                break;
            }
            else if(ch == '\t') //补全
            {
                

                write(STDOUT,"\n",1);
                tab_complete(buf);
                write(STDOUT,"\n",1);
                print_promt();
                printf("%s", buf);
                read_count = strlen(buf); //更新count
                continue;
            }
            else if(ch == 3) // Ctrl+C — 取消当前行
            {
                write(STDOUT, "^C\n", 3);
                memset(buf, 0, SHELL_BUF_SIZE);
                read_count = 0;
                print_promt();
                continue;
            }
            else if(ch == 4 && read_count == 0) // Ctrl+D — EOF，空行时退出
            {
                write(STDOUT, "exit\n", 5);
                tlibc_restore_term(STDIN);
                munmap(buf, SHELL_BUF_SIZE);
                return 0;
            }
            else if(ch == 127) //退格
            {
                if(read_count>0)
                {
                    read_count--;
                    buf[read_count] = 0;
                    __write(1, "\b \b", 3);
                }
                continue;
            }
            else{//普通字符
                buf[read_count++] = ch;
                write(STDOUT,&ch,1); //回显输入
            }
            //右 27 91 67
            //左 27 91 68
            //上 27 91 65
            //下 27 91 66
        }
        

        if(buf[0]=='q' && buf[1]==0)    //输入是单字符就退出
        {
            tlibc_restore_term(STDIN); //恢复终端设置
            munmap(buf, SHELL_BUF_SIZE);
            break;
        }
        if(strcmp(buf, "exit") == 0) //输入是exit就退出
        {
            tlibc_restore_term(STDIN); //恢复终端设置
            munmap(buf, SHELL_BUF_SIZE);
            break;
        }
        if(buf[0]==0)
        {
            continue;
        }

        //解析命令输入，获取命令名和参数
        struct command command;//在栈上分配空间给struct command
        memset(&command, 0, sizeof(struct command)); //初始化为0，防止未定义行为
        int ret = parse_cmd(buf,&command);
        if(ret == 0) //解析成功，开始执行
        {
            // show_cmd_info(&command);
            //先检查是不是内置命令,内置命令直接执行
            ret = search_command(command.name,internal_command_table, INTERNAL_COMMAND_NUM);
            if(ret != -1)
            {
                // __printf("匹配到内置命令: %s,开始执行\n",internal_command_table[ret]);
                switch (ret) {
            case 0: ret = __internal_chdir(command.argc, command.args); break;
            case 1: ret = __internal_help(command.argc, command.args); break;
            default: ret = -1; break;
        }
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