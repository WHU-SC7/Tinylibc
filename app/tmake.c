#include "tlibc_everything.h"

//用来编译Tinylibc的app，这样甚至不需要make了
//之后app下会细分各级目录，我不想写新的Makefile规则，就用这个程序来编译app

#define LIB_DIR "lib"
#define LIB_OUTPUT "tlibc.a"//静态库的文件名

char pwd[512];
char *build_path = "./build";
char *default_gcc_path = "/usr/bin/x86_64-linux-gnu-gcc";
char *default_ar_path = "/usr/bin/x86_64-linux-gnu-ar";
char *default_ld_path = "/usr/bin/x86_64-linux-gnu-ld";
char *default_gcc_flags[]={
    //第一个参数是程序本身的名字
    "x86_64-linux-gnu-gcc",
    //头文件路径
    "-I./include",
    "-I./arch",
    "-I./arch/x86_64",
    //宏
    "-DX86_64_TLIBC=1",
    //选项
    "-MD",
    "-fno-stack-protector",
    "-O0",
    "-fno-common",
    "-nostdlib",
    "-ffreestanding",
    "-fno-pie",
    "-mno-red-zone",
    "-static",
    NULL
};
char *default_ar_flags[]={
    "x86_64-linux-gnu-ar",
    "rcs",
    NULL
};
char *default_ld_flags[]={
    "x86_64-linux-gnu-ld",
    "-z",
    "max-page-size=4096",
    "-nostdlib",
    "-static",
    "-T",
    "ld.script",
    NULL
};
char *default_envp[]={
    "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    NULL
};
int flag_count = 0;
static int g_max_jobs = 1;  // 并行编译任务数，默认串行
static char *g_build_target = NULL;  // 指定编译目标，NULL 表示全量构建

int copy_gcc_flags(char **dest){
    int i=0;
    while(default_gcc_flags[i]){
        dest[i] = default_gcc_flags[i];
        i++;
    }
    return i; //返回复制的参数数量
}

int copy_default_ar_flags(char **dest){
    int i=0;
    while(default_ar_flags[i]){
        dest[i] = default_ar_flags[i];
        i++;
    }
    return i; //返回复制的参数数量
}

int copy_default_ld_flags(char **dest){
    int i=0;
    while(default_ld_flags[i]){
        dest[i] = default_ld_flags[i];
        i++;
    }
    return i; //返回复制的参数数量
}

/* 构造 .o 路径并检查是否需要重新编译（增量构建用） */
static void get_obj_path(const char *file_path, const char *output_path, char *obj_path, int size)
{
    char *file_name = strrchr(file_path, '/');
    snprintf(obj_path, size, "%s", build_path);
    if(output_path){
        snprintf(obj_path + strlen(obj_path), size - strlen(obj_path), "/%s", output_path);
    }
    if(file_name){
        snprintf(obj_path + strlen(obj_path), size - strlen(obj_path), "/%s", file_name + 1);
        char *dot = strrchr(obj_path, '.');
        if(dot) strcpy(dot, ".o");
    }
}

/* 检查 .o 是否存在（简单增量：存在即跳过，不改源就不重编） */
static int needs_rebuild(const char *src, const char *obj)
{
    (void)src;  /* 预留给将来时间戳比较 */
    return tlibc_is_path_file(obj) != 1;
}

// Fork + execve gcc 编译一个源文件，返回子进程PID。
// 返回: PID(>0) 成功启动； -1 fork失败； -2 .o已存在跳过
int compile_file_start(const char *file_path, char *output_path){
    char obj_file_path[512];
    get_obj_path(file_path, output_path, obj_file_path, sizeof(obj_file_path));
    if(!needs_rebuild(file_path, obj_file_path))
        return -2;   /* .o 已更新，跳过编译 */
    int pid = fork();
    if(pid == 0){
        char *gcc_flags[256];
        int flag_num = copy_gcc_flags(gcc_flags);
        gcc_flags[flag_num++] = "-c";
        gcc_flags[flag_num++] = (char *)file_path;
        gcc_flags[flag_num++] = "-o";
        gcc_flags[flag_num++] = obj_file_path;
        gcc_flags[flag_num] = NULL;
        execve(default_gcc_path, gcc_flags, default_envp);
        exit(127);
    }
    return pid;
}

// 等待指定编译子进程结束，返回wait原始状态码。
int compile_file_wait(int pid){
    int status;
    waitpid(pid, &status, 0);
    return status;
}

// 串行编译：启动+等待。编译错误时保留exit_group(-1)行为。
int compile_file(const char *file_path, char *output_path){
    int pid = compile_file_start(file_path, output_path);
    if(pid == -2) return 0;   /* .o 已存在，跳过 */
    if(pid < 0){
        printf("编译失败, 无法创建子进程\n");
        return -1;
    }
    int status = compile_file_wait(pid);
    if(status == 0){
        return 0;
    }
    printf("编译失败, 状态码: %d\n", status);
    if(status == 256){
        exit_group(-1);
    }
    return -1;
}

//根据路径和文件名来编译
int compile_path_file(char *path, char *file_name, char *output_path){
    char file_path[512];
    snprintf(file_path, 512, "%s/%s", path, file_name);
    return compile_file(file_path, output_path);
}

// 并行编译指定path下的所有文件，最多同时运行max_jobs个gcc进程
int parallel_compile_task(char *path, int max_jobs){
    int files_count = tlibc_get_file_count(path);
    printf("要编译的文件数量: %d, 并行任务数: %d\n", files_count, max_jobs);
    if(files_count <= 0){
        printf("没有要编译的文件，或者获取文件数量失败, 跳过路径%s的编译: %d\n", path, files_count);
        return -1;
    }
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(path, (uint64_t)file_name_list, files_count);
    char buf[1024];
    snprintf(buf, 1024, "build/%s", path);
    tlibc_recursive_mkdir(buf);

    if(max_jobs > files_count) max_jobs = files_count;
    int pids[max_jobs];
    int active = 0, next = 0, failed = 0;

    while(next < files_count || active > 0){
        // 启动新编译任务，直到达到上限或全部启动
        while(active < max_jobs && next < files_count && !failed){
            char file_path[512];
            snprintf(file_path, 512, "%s/%s", path, file_name_list[next]);
            int pid = compile_file_start(file_path, path);
            if(pid == -2){
                printf("跳过文件%d: %s\n", next, file_name_list[next]);
            } else if(pid > 0){
                printf("编译文件%d: %s\n", next, file_name_list[next]);
                pids[active++] = pid;
            }
            next++;
        }
        if(active == 0) break;

        // 等待任意子进程结束
        int status;
        int done_pid = waitpid(-1, &status, 0);
        if(done_pid <= 0) break;

        // 从活跃列表中移除已完成的PID
        for(int i = 0; i < active; i++){
            if(pids[i] == done_pid){
                pids[i] = pids[active - 1];
                active--;
                break;
            }
        }

        if(status != 0){
            printf("编译失败, 状态码: %d\n", status);
            failed = 1;
        }
    }

    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return failed ? -1 : 0;
}

//编译指定path下的所有文件
int compile_task(char *path){
    if(g_max_jobs > 1){
        return parallel_compile_task(path, g_max_jobs);
    }
    int files_count = tlibc_get_file_count(path);
    printf("要编译的文件数量: %d\n", files_count);
    if(files_count <= 0){
        printf("没有要编译的文件，或者获取文件数量失败, 跳过路径%s的编译: %d\n", path, files_count);
        return -1;
    }
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(path, (uint64_t)file_name_list, files_count);
    char buf[1024];
    snprintf(buf, 1024, "build/%s", path);
    tlibc_recursive_mkdir(buf); //创建对应输出目录在build下
    for(int i=0; i<files_count; i++){
        printf("编译文件%d: %s\n", i, file_name_list[i]);
        compile_path_file(path, file_name_list[i], path);
    }
    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return 0;
}

//需要知道目标文件所在路径
int ar_library(char *build_path){
    int files_count = tlibc_get_file_count(build_path);
    if(files_count < 0){
        printf("获取文件数量失败, 路径%s, 跳过库的链接\n", build_path, files_count);
        return -1;
    }
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(build_path, (uint64_t)file_name_list, files_count);
    //构建ar的参数（只包含 .o 文件，跳过 .d 等）
    char *ar_flags[1024];
    int flag_num = copy_default_ar_flags(ar_flags);
    ar_flags[flag_num++] = LIB_OUTPUT;//输出的库文件路径
    for(int i=0; i<files_count; i++){
        char *dot = strrchr(file_name_list[i], '.');
        if(!dot || strcmp(dot, ".o") != 0) continue;
        ar_flags[flag_num++] = file_name_list[i];
    }
    ar_flags[flag_num] = NULL;
    //打印参数
    // for(int i=0; ar_flags[i] != NULL; i++){
    //     printf("ar参数%d: %s\n", i, ar_flags[i]);
    // }
    int pid = fork();
    if(pid == 0){
        chdir(build_path); //切换到构建目录下执行ar命令
        printf("执行命令:");
        for(int i = 0; ar_flags[i] != NULL; i++){
            printf(" %s", ar_flags[i]);
        }
        printf("\n");
        execve(default_ar_path, ar_flags, default_envp);
    }
    else{
        int status;
        waitpid(pid,&status,0);
        munmap(file_name_list, files_count * sizeof(char *));
        munmap(file_name_buf, files_count * 256);
        if(status == 0){
            printf("链接库文件成功\n");
            return 0;
        }
        else{
            printf("链接库文件失败, 状态码: %d\n", status);
            return -1;
        }
    }
    return 0;
}

// Fork + execve ld 链接一个.o文件，返回子进程PID
int link_app_start(char *app_path, char *output_path){
    char *ld_flags[1024];
    int flag_num = copy_default_ld_flags(ld_flags);
//必须把库文件放在应用程序后面，否则连接器会认为库文件没有用到，导致库文件中的函数无法链接成功，出现undefined reference错误
    ld_flags[flag_num++] = app_path;
    ld_flags[flag_num++] = "build/lib/tlibc.a";
    char output[1024];
    char *app_name = strrchr(app_path, '/');
    if(app_name == NULL){
        app_name = app_path;
    }
    else if(*(app_name+1) == 0){
        printf("WARNING! app路径%s不合法，不能以'/'结尾\n", app_path);
        return -1;
    }
    snprintf(output, 1024, "%s/%s", output_path, app_name + 1);
    char *dot = strrchr(output, '.');
    if(dot){
        strcpy(dot, "");
    }
    ld_flags[flag_num++] = "-o";
    ld_flags[flag_num++] = output;
    ld_flags[flag_num] = NULL;

    int pid = fork();
    if(pid == 0){
        execve(default_ld_path, ld_flags, default_envp);
        exit(127);
    }
    return pid;
}

// 等待指定链接子进程结束，返回0成功，-1失败
int link_app_wait(int pid){
    int status;
    waitpid(pid, &status, 0);
    if(status == 0){
        return 0;
    }
    printf("链接应用程序失败, 状态码: %d\n", status);
    return -1;
}

// 串行链接：启动+等待
int link_app(char *app_path, char *output_path){
    int pid = link_app_start(app_path, output_path);
    if(pid < 0) return -1;
    return link_app_wait(pid);
}

//链接指定all_app_path下的所有.o文件，输出到output_path中
int parallel_link_task(char *all_app_path, char *output_path, int max_jobs);
int link_task(char *all_app_path, char *output_path){
    if(g_max_jobs > 1){
        return parallel_link_task(all_app_path, output_path, g_max_jobs);
    }
    int files_count = tlibc_get_file_count(all_app_path);
    if(files_count < 0){
        printf("获取文件数量失败, 路径%s, 跳过app链接\n", all_app_path, files_count);
        return -1;
    }
    printf("要链接的文件数量: %d\n", files_count);
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(all_app_path, (uint64_t)file_name_list, files_count);;
    for(int i=0; i<files_count; i++){
        char *dot = strrchr(file_name_list[i], '.');
        if(!dot || strcmp(dot, ".o") != 0) continue;   /* 只链接 .o */
        char file_path[512];
        snprintf(file_path, 512, "%s/%s", all_app_path, file_name_list[i]);
        printf("链接文件%d: %s\n", i, file_path);
        link_app(file_path, output_path);
    }
    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return 0;
}

// 并行链接指定路径下的所有.o文件，最多同时运行max_jobs个ld进程
int parallel_link_task(char *all_app_path, char *output_path, int max_jobs){
    int files_count = tlibc_get_file_count(all_app_path);
    if(files_count < 0){
        printf("获取文件数量失败, 路径%s, 跳过app链接\n", all_app_path, files_count);
        return -1;
    }
    printf("要链接的文件数量: %d, 并行任务数: %d\n", files_count, max_jobs);
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(all_app_path, (uint64_t)file_name_list, files_count);

    if(max_jobs > files_count) max_jobs = files_count;
    int pids[max_jobs];
    int active = 0, next = 0, failed = 0;

    while(next < files_count || active > 0){
        while(active < max_jobs && next < files_count && !failed){
            char *dot = strrchr(file_name_list[next], '.');
            if(!dot || strcmp(dot, ".o") != 0){   /* 只链接 .o */
                next++;
                continue;
            }
            char file_path[512];
            snprintf(file_path, 512, "%s/%s", all_app_path, file_name_list[next]);
            printf("链接文件%d: %s\n", next, file_path);
            int pid = link_app_start(file_path, output_path);
            if(pid > 0){
                pids[active++] = pid;
            }
            next++;
        }
        if(active == 0) break;

        int status;
        int done_pid = waitpid(-1, &status, 0);
        if(done_pid <= 0) break;

        for(int i = 0; i < active; i++){
            if(pids[i] == done_pid){
                pids[i] = pids[active - 1];
                active--;
                break;
            }
        }

        if(status != 0){
            printf("链接应用程序失败, 状态码: %d\n", status);
            failed = 1;
        }
    }

    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return failed ? -1 : 0;
}

//把app_path下的文件复制到install_path下，默认应该安装到~/tlibc/bin
int install(char *app_path){
    int files_count = tlibc_get_file_count(app_path);
    if(files_count < 0){
        printf("获取文件数量失败, 路径%s, 跳过安装\n", app_path);
        return -1;
    }
    printf("要安装的文件数量: %d\n", files_count);
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(app_path, (uint64_t)file_name_list, files_count);
    char install_path[1024];
    tlibc_get_user_dir(install_path, 1024);
    strncat(install_path, "/tlibc/bin", 1024 - strlen(install_path) - 1);
    //删除，安装全新的
    tlibc_recursive_rm_dir(install_path);
    tlibc_recursive_mkdir(install_path); //以防安装目录不存在
    for(int i=0; i<files_count; i++){
        char file_path[512];
        snprintf(file_path, 512, "%s/%s", app_path, file_name_list[i]);
        char install_file_path[512];
        snprintf(install_file_path, 512, "%s/%s", install_path, file_name_list[i]);
        printf("安装文件%d: %s 到 %s\n", i, file_path, install_file_path);
        tlibc_copy_exe_file(file_path, install_file_path);
    }
    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return 0;
}

//递归编译指定路径下的所有文件和子目录下的所有文件和子目录的子目录等等
int compile_recursive_task(char *path){
    //获取目录下的所有目录
    int dir_count = tlibc_get_dir_count(path);
    if(dir_count < 0){
        printf("compile_recursive_task获取目录数量失败, 路径%s, 跳过路径的编译: %d\n", path, dir_count);
        return -1;
    }
    if(dir_count == 0){//只需要编译这个目录下的文件就行了
        printf("目录%s下没有子目录，直接编译这个目录下的文件\n", path);
        compile_task(path);
        return 0;
    }
    printf("目录%s下有%d个子目录，需要递归编译\n", path, dir_count);
    //有子目录，需要递归编译
    char **dir_name_list = (char **)tlibc_malloc(dir_count * sizeof(char *));
    char *dir_name_buf = (char *)tlibc_malloc(dir_count * 256);
    for(int i = 0; i < dir_count; i++) {
        dir_name_list[i] = dir_name_buf + i * 256;
    }
    tlibc_get_dir_name_list(path, (uint64_t)dir_name_list, dir_count);
    for(int i=0; i<dir_count; i++){
        char sub_path[512];
        snprintf(sub_path, 512, "%s/%s", path, dir_name_list[i]);
        compile_recursive_task(sub_path);
    }
    //再编译可能存在的文件
    compile_task(path);
    munmap(dir_name_list, dir_count * sizeof(char *));
    munmap(dir_name_buf, dir_count * 256);
    return 0;
}

//递归链接指定路径下的所有文件到output_path中
int link_recursive_task(char *path, char *output_path){
    //获取目录下的所有目录
    int dir_count = tlibc_get_dir_count(path);
    if(dir_count < 0){
        printf("link_recursive_task获取目录数量失败, 路径%s, 跳过路径的链接: %d\n", path, dir_count);
        return -1;
    }
    if(dir_count == 0){//只需要链接这个目录下的文件就行了
        printf("目录%s下没有子目录，直接链接这个目录下的文件\n", path);
        link_task(path, output_path);
        return 0;
    }
    printf("目录%s下有%d个子目录，需要递归链接\n", path, dir_count);
    //有子目录，需要递归链接
    char **dir_name_list = (char **)tlibc_malloc(dir_count * sizeof(char *));
    char *dir_name_buf = (char *)tlibc_malloc(dir_count * 256);
    for(int i = 0; i < dir_count; i++) {
        dir_name_list[i] = dir_name_buf + i * 256;
    }
    tlibc_get_dir_name_list(path, (uint64_t)dir_name_list, dir_count);
    for(int i=0; i<dir_count; i++){
        char sub_path[512];
        snprintf(sub_path, 512, "%s/%s", path, dir_name_list[i]);
        link_recursive_task(sub_path, output_path);
    }
    //再链接可能存在的文件
    link_task(path, output_path);
    munmap(dir_name_list, dir_count * sizeof(char *));
    munmap(dir_name_buf, dir_count * 256);
    return 0;
}

// 简单字符串转数字（项目无stdlib，自实现）
static int parse_int(const char *s){
    int n = 0;
    while(*s >= '0' && *s <= '9'){
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

// 读取 /proc/cpuinfo，统计 processor 行数得到在线 CPU 数量
static int get_nprocs(void){
    char buf[4096];
    int fd = openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    if(fd < 0) return 1;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) return 1;
    buf[n] = '\0';

    int count = 0;
    for(int i = 0; i < n - 8; i++){
        if(buf[i] == 'p' && buf[i+1] == 'r' && buf[i+2] == 'o' &&
           buf[i+3] == 'c' && buf[i+4] == 'e' && buf[i+5] == 's' &&
           buf[i+6] == 's' && buf[i+7] == 'o' && buf[i+8] == 'r'){
            count++;
        }
    }
    return count > 0 ? count : 1;
}

// 获取单调时间（毫秒），用于阶段计时
static long get_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// 统计 flat 目录下所有文件的总字节数
static long sum_file_sizes(const char *path){
    int n = tlibc_get_file_count(path);
    if(n <= 0) return 0;
    char **names = (char **)tlibc_malloc(n * sizeof(char *));
    char *buf = (char *)tlibc_malloc(n * 256);
    for(int i = 0; i < n; i++) names[i] = buf + i * 256;
    n = tlibc_get_file_name_list(path, (uint64_t)names, n);
    long total = 0;
    for(int i = 0; i < n; i++){
        char full[512];
        snprintf(full, 512, "%s/%s", path, names[i]);
        int len = tlibc_get_file_len(full);
        if(len > 0) total += len;
    }
    munmap(names, n * sizeof(char *));
    munmap(buf, n * 256);
    return total;
}

/* ================================================================== */
/*  单应用构建支持                                                     */
/* ================================================================== */

/* 在 app/ 树下递归查找 name.c，将路径写入 rel_path（如 "app/net/ndiscover.c"） */
static int find_app_source(const char *name, char *rel_path, int size)
{
    /* 先检查 app/name.c（根级程序如 shell, tmake） */
    snprintf(rel_path, size, "app/%s.c", name);
    if(tlibc_is_path_file(rel_path) == 1)
        return 0;

    /* 再扫描 app/ 下所有子目录 */
    int dir_count = tlibc_get_dir_count("app");
    if(dir_count <= 0) return -1;

    char **dir_names = (char **)tlibc_malloc(dir_count * sizeof(char *));
    char *dir_buf = (char *)tlibc_malloc(dir_count * 256);
    for(int i = 0; i < dir_count; i++)
        dir_names[i] = dir_buf + i * 256;
    dir_count = tlibc_get_dir_name_list("app", (uint64_t)dir_names, dir_count);

    int found = -1;
    for(int i = 0; i < dir_count; i++){
        snprintf(rel_path, size, "app/%s/%s.c", dir_names[i], name);
        if(tlibc_is_path_file(rel_path) == 1){
            found = 0;
            break;
        }
    }

    munmap(dir_names, dir_count * sizeof(char *));
    munmap(dir_buf, dir_count * 256);
    return found;
}

/* 只构建一个程序，不碰其他 app */
static int build_single_app(const char *name)
{
    /* 1. 确保 lib 已编译 */
    if(tlibc_is_path_file("build/tlibc.a") != 1){
        printf("Library not found, building lib first...\n");
        tlibc_recursive_mkdir("build/lib");
        compile_task("lib");
        ar_library("build/lib");
        tlibc_copy_file("build/lib/tlibc.a", "build/tlibc.a");
    }

    /* 2. 找源文件 */
    char src_path[512];
    if(find_app_source(name, src_path, sizeof(src_path)) != 0){
        printf("Error: source 'app/%s.c' not found!\n", name);
        return -1;
    }
    printf("Source: %s\n", src_path);

    /* 3. 计算输出子目录（如 "app/net" ← "app/net/ndiscover.c"） */
    char out_subdir[512];
    snprintf(out_subdir, sizeof(out_subdir), "%s", src_path);
    char *slash = strrchr(out_subdir, '/');
    if(slash)
        *slash = '\0';
    else
        snprintf(out_subdir, sizeof(out_subdir), "app");

    /* 4. 确保 build 子目录存在 */
    char build_dir[512];
    snprintf(build_dir, 512, "build/%s", out_subdir);
    tlibc_recursive_mkdir(build_dir);

    /* 5. 编译（检查 .o 是否已存在，实现增量跳过） */
    char obj_check[512];
    get_obj_path(src_path, out_subdir, obj_check, sizeof(obj_check));
    if(!needs_rebuild(src_path, obj_check)){
        printf("Skip %s (already up to date)\n", src_path);
    } else {
        printf("Compile %s ...\n", src_path);
        if(compile_file(src_path, out_subdir) < 0){
            printf("Compile failed.\n");
            return -1;
        }
    }

    /* 6. 链接 */
    char obj_path[512];
    snprintf(obj_path, 512, "build/%s/%s.o", out_subdir, name);
    tlibc_recursive_mkdir("build/output");
    printf("Linking %s ...\n", obj_path);
    if(link_app(obj_path, "build/output") < 0){
        printf("Link failed.\n");
        return -1;
    }

    /* 7. 安装 */
    char install_dir[1024];
    tlibc_get_user_dir(install_dir, 1024);
    size_t dlen = strlen(install_dir);
    snprintf(install_dir + dlen, 1024 - dlen, "/tlibc/bin");
    tlibc_recursive_mkdir(install_dir);

    char dst[1024];
    snprintf(dst, 1024, "%s/%s", install_dir, name);
    printf("Install: build/output/%s -> %s\n", name, dst);
    char exe_src[512];
    snprintf(exe_src, 512, "build/output/%s", name);
    tlibc_copy_exe_file(exe_src, dst);

    printf("\nDone: %s\n", name);
    return 0;
}

int main(int argc, char *argv[]){

    // 先处理 --help / -h，以及 -b（这两者都不依赖项目目录）
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            printf("Tinylibc 自托管构建工具\n");
            printf("\n用法: tmake [-j [N]] [-b <程序名>]\n");
            printf("\n选项:\n");
            printf("  -h, --help     显示本帮助信息\n");
            printf("  -j [N]         并行编译，N 为并行任务数\n");
            printf("                  不传 N 时自动检测 CPU 核数\n");
            printf("                  不传 -j 时串行编译（默认）\n");
            printf("  -b <程序名>    只构建指定程序（增量，跳过已有 .o）\n");
            printf("                  例如: tmake -b ndiscover\n");
            printf("                  重复调用跳过已编译文件，修改源码后自动重编\n");
            return 0;
        } else if(strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0){
            if(i + 1 < argc){
                g_build_target = argv[++i];
            } else {
                printf("Error: -b 需要指定程序名\n");
                return 1;
            }
        }
    }

    //检查工作目录是否是Tinylibc项目，以tlibc_everything.h为标志
    memset(pwd, 0, sizeof(pwd));
    getcwd(pwd, 512);
    int ret = tlibc_is_path_file("tlibc_commit_log.md");
    if(ret != 1){
        printf("当前目录不是Tinylibc项目! 切换到Tinylibc项目目录再尝试tmake生成!\n");
        return 1;
    }

    // 删除build然后重新创建（单应用构建跳过，加速）
    if(!g_build_target){
        ret = tlibc_recursive_rm_dir("build");
        printf("删除build目录成功\n");
    }
    //创建build目录
    ret = tlibc_recursive_mkdir(build_path); //如果build目录的父目录不存在就创建父目录
    if(ret < 0){
        printf("无法创建build目录, 错误码: %d\n", ret);
        return 1;
    }
    if(!g_build_target){
        printf("创建build目录成功\n");
    }

    // 解析 -j [N] 参数
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-j") == 0){
            if(i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9'){
                g_max_jobs = parse_int(argv[i + 1]);
            } else {
                g_max_jobs = get_nprocs();
                printf("检测到 CPU 核数: %d\n", g_max_jobs);
            }
            if(g_max_jobs < 1) g_max_jobs = 1;
            if(g_max_jobs > 64) g_max_jobs = 64;
            break;
        }
    }

    char *exe_path = "build/output";

    printf("当前目录: %s\n", pwd);

    /* ---- 单应用构建：编译+链接+安装指定目标，跳过全量流程 ---- */
    if(g_build_target){
        build_single_app(g_build_target);
        return 0;
    }

    long t_start, t_compile_lib, t_ar, t_compile_app, t_link, t_install, t_total;
    t_total = get_ms();

    t_start = get_ms();
    compile_task("lib");
    t_compile_lib = get_ms() - t_start;

    t_start = get_ms();
    ar_library("./build/lib");
    tlibc_copy_file("./build/lib/tlibc.a", "./build/tlibc.a");
    t_ar = get_ms() - t_start;

    t_start = get_ms();
    tlibc_recursive_mkdir("build/output");
    compile_recursive_task("app");
    t_compile_app = get_ms() - t_start;

    t_start = get_ms();
    link_recursive_task("build/app", "build/output");
    t_link = get_ms() - t_start;

    t_start = get_ms();
    install(exe_path);
    t_install = get_ms() - t_start;

    t_total = get_ms() - t_total;

    // 统计信息（只计 .o 文件，排除 .d 等）
    int lib_o_count = 0;
    {
        int n = tlibc_get_file_count("build/lib");
        if(n > 0){
            char **names = (char **)tlibc_malloc(n * sizeof(char *));
            char *buf = (char *)tlibc_malloc(n * 256);
            for(int i = 0; i < n; i++) names[i] = buf + i * 256;
            n = tlibc_get_file_name_list("build/lib", (uint64_t)names, n);
            for(int i = 0; i < n; i++){
                char *dot = strrchr(names[i], '.');
                if(dot && strcmp(dot, ".o") == 0) lib_o_count++;
            }
            munmap(names, n * sizeof(char *));
            munmap(buf, n * 256);
        }
    }
    int lib_a_size = tlibc_get_file_len("build/lib/tlibc.a");
    lib_a_size = lib_a_size < 0 ? 0 : lib_a_size;
    int exe_count = tlibc_get_file_count(exe_path);
    exe_count = exe_count < 0 ? 0 : exe_count;
    long exe_total_size = sum_file_sizes(exe_path);

    printf("\n========== 构建报告 ==========\n");
    printf("并行任务数:       %d\n", g_max_jobs);
    printf("-------------------------------\n");
    printf("阶段               耗时(ms)\n");
    printf("编译 lib           %ld\n", t_compile_lib);
    printf("归档 lib           %ld\n", t_ar);
    printf("编译 app           %ld\n", t_compile_app);
    printf("链接 app           %ld\n", t_link);
    printf("安装               %ld\n", t_install);
    printf("-------------------------------\n");
    printf("总计               %ld\n", t_total);
    printf("-------------------------------\n");
    printf("lib 目标文件数:    %d\n", lib_o_count);
    printf("tlibc.a 大小:      %d bytes\n", lib_a_size);
    printf("可执行文件数:      %d\n", exe_count);
    printf("可执行文件总大小:  %ld bytes\n", exe_total_size);
    printf("==============================\n");

    return 0;
}