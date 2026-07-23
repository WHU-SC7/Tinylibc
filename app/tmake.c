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
    "-I./include/posix",
    "-I./include/tlibc",
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
static int g_max_jobs = 1;     // 并行编译任务数，默认自动检测 CPU 核数
static int g_jobs_explicit = 0; // 用户是否显式指定了 -j
static char *g_build_target = NULL;  // 指定编译目标，NULL 表示全量构建
static int g_self_host = 0;    // -T: 使用 toyc/toyas 自托管编译，不依赖 gcc

/* 核心工具链：全量构建后额外安装到 ~/.local/bin/ 方便日常调用 */
static char *g_core_tools[] = {
    "tmake",
    "shell",
    "toyc",
    "toypp",
    "toyas",
    NULL   /* 结尾哨兵，新增工具在上一行追加即可 */
};

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

/* 比较源文件和所有依赖的修改时间，决定是否需要重新编译
 * 返回: 1=需要编译, 0=跳过(.o已最新), -1=源文件不存在
 * 依赖信息来自 gcc -MD 生成的 .d 文件（含头文件依赖链）*/
static int needs_rebuild(const char *src, const char *obj)
{
    struct stat obj_s;
    if(tlibc_stat(obj, &obj_s) < 0)
        return 1;   /* .o 不存在，需要编译 */

    struct stat src_s;
    if(tlibc_stat(src, &src_s) < 0)
        return -1;  /* 源文件不存在 */

    if(src_s.st_mtim.tv_sec > obj_s.st_mtim.tv_sec) return 1;
    if(src_s.st_mtim.tv_sec == obj_s.st_mtim.tv_sec &&
       src_s.st_mtim.tv_nsec > obj_s.st_mtim.tv_nsec) return 1;

    /* 从 .d 文件读取头文件依赖，检查是否有头文件变更 */
    char dep[512];
    snprintf(dep, sizeof(dep), "%s", obj);
    char *dot = strrchr(dep, '.');
    if(dot) strcpy(dot, ".d");

    int fd = openat(AT_FDCWD, dep, O_RDONLY, 0);
    if(fd < 0) return 0;  /* 无 .d 文件，只比较源文件 */

    char buf[16384];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) return 0;
    buf[n] = '\0';

    /* .d 格式: "target.o: dep1 dep2 \\\n  dep3 ..." 。跳过 "target.o:" */
    char *p = buf;
    while(*p && *p != ':') p++;
    if(!*p || !*(p+1)) return 0;
    p++;  /* 跳过 ':' */

    while(*p){
        /* 跳过空白、换行、续行符 */
        while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if(*p == '\\'){ p++; continue; }
        if(!*p) break;

        char dep_file[512];
        int i = 0;
        while(*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\\'){
            if(i < (int)sizeof(dep_file) - 1) dep_file[i++] = *p;
            p++;
        }
        dep_file[i] = '\0';
        if(i == 0) continue;

        /* 跳过 .o 自身 */
        if(strcmp(dep_file, obj) == 0) continue;

        struct stat dep_s;
        if(tlibc_stat(dep_file, &dep_s) == 0){
            if(dep_s.st_mtim.tv_sec > obj_s.st_mtim.tv_sec ||
               (dep_s.st_mtim.tv_sec == obj_s.st_mtim.tv_sec &&
                dep_s.st_mtim.tv_nsec > obj_s.st_mtim.tv_nsec))
                return 1;
        }
    }

    return 0;  /* 所有依赖都是最新的 */
}

// Fork + execve gcc 编译一个源文件，返回子进程PID。
// 返回: PID(>0) 成功启动； -1 fork失败； -2 .o已存在跳过
int compile_file_start(const char *file_path, char *output_path){
    char obj_file_path[512];
    get_obj_path(file_path, output_path, obj_file_path, sizeof(obj_file_path));
    if(!needs_rebuild(file_path, obj_file_path))
        return -2;   /* .o 已更新，跳过编译 */

    const char *ext = strrchr(file_path, '.');
    int is_asm = (ext && (strcmp(ext, ".S") == 0 || strcmp(ext, ".s") == 0));

    /* 打印使用的编译命令 */
    {
        const char *tool;
        if (g_self_host)
            tool = is_asm ? "toyas" : "toyc";
        else
            tool = "gcc";
        printf("  %-40s → %s\n", file_path, tool);
    }

    int pid = fork();
    if(pid == 0){
        /* ── 自托管模式：优先用 toyc/toyas ── */
        if (g_self_host) {
            if (is_asm) {
                char *argv[8];
                argv[0] = "./build/output/toyas";
                argv[1] = (char *)file_path;
                argv[2] = "-o";
                argv[3] = obj_file_path;
                argv[4] = NULL;
                execve("./build/output/toyas", argv, default_envp);
            } else {
                char *argv[8];
                argv[0] = "./build/output/toyc";
                argv[1] = (char *)file_path;
                argv[2] = "-o";
                argv[3] = obj_file_path;
                argv[4] = NULL;
                execve("./build/output/toyc", argv, default_envp);
            }
            /* toyc/toyas 不存在时回退到 gcc */
        }
        /* ── 回退：gcc 编译 ── */
        {
            char *gcc_flags[256];
            int flag_num = copy_gcc_flags(gcc_flags);
            gcc_flags[flag_num++] = "-c";
            gcc_flags[flag_num++] = (char *)file_path;
            gcc_flags[flag_num++] = "-o";
            gcc_flags[flag_num++] = obj_file_path;
            gcc_flags[flag_num] = NULL;
            execve(default_gcc_path, gcc_flags, default_envp);
        }
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

    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
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
    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
    return 0;
}

//需要知道目标文件所在路径
static int collect_files_recursive(const char *path, const char *ext,
                                   char (*files)[512], int max);

int ar_library(char *build_path){
#define MAX_LIB_O_FILES 4096
    char (*files)[512] = (char (*)[512])tlibc_malloc(MAX_LIB_O_FILES * 512);
    int total = collect_files_recursive(build_path, ".o", files, MAX_LIB_O_FILES);
    if(total <= 0){
        printf("没有找到 .o 文件, 路径=%s\n", build_path);
        tlibc_free(files);
        return -1;
    }
    int prefix_len = strlen(build_path) + 1;
    char *ar_flags[1024];
    int flag_num = copy_default_ar_flags(ar_flags);
    ar_flags[flag_num++] = LIB_OUTPUT;
    for(int i = 0; i < total; i++)
        ar_flags[flag_num++] = files[i] + prefix_len;
    ar_flags[flag_num] = NULL;
    int pid = fork();
    if(pid == 0){
        chdir(build_path);
        printf("执行命令:");
        for(int i = 0; ar_flags[i] != NULL; i++){
            printf(" %s", ar_flags[i]);
        }
        printf("\n");
        execve(default_ar_path, ar_flags, default_envp);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    tlibc_free(files);
    if(status == 0){
        printf("链接库文件成功\n");
        return 0;
    } else {
        printf("链接库文件失败, 状态码: %d\n", status);
        return -1;
    }
#undef MAX_LIB_O_FILES
}

// Fork + execve ld 链接一个.o文件，返回子进程PID
int link_app_start(const char *app_path, const char *output_path){
    char *ld_flags[1024];
    int flag_num = copy_default_ld_flags(ld_flags);
//必须把库文件放在应用程序后面，否则连接器会认为库文件没有用到，导致库文件中的函数无法链接成功，出现undefined reference错误
    ld_flags[flag_num++] = (char *)app_path;
    ld_flags[flag_num++] = "build/lib/tlibc.a";
    char output[1024];
    const char *app_name = strrchr(app_path, '/');
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
int link_app(const char *app_path, const char *output_path){
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
    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
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

    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
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
    tlibc_free(file_name_list);
    tlibc_free(file_name_buf);
    return 0;
}

// 从文件路径中提取输出子目录（如 "app/net/ndiscover.c" → "app/net"）
// 写入 out，返回末尾不含 '/'
static void
out_subdir_from_path(const char *file_path, char *out, int out_size)
{
    snprintf(out, out_size, "%s", file_path);
    char *slash = strrchr(out, '/');
    if(slash) *slash = '\0';
}

/* ── tmakelist 前向声明（compile/link 阶段使用，定义见文件末尾） ── */
#define MAX_TMAKELIST_TARGETS 16
#define MAX_TARGET_SOURCES 32

typedef struct {
    char name[64];
    char srcs[MAX_TARGET_SOURCES * 64];  /* flat array */
    int src_count;
} TmakelistTarget;

typedef struct {
    TmakelistTarget targets[MAX_TMAKELIST_TARGETS];
    int target_count;
} Tmakelist;

static int read_tmakelist(const char *app_dir, Tmakelist *tl);
static int tmakelist_has_source(Tmakelist *tl, const char *name);
static void get_file_basename(const char *path, char *name, int size);
static void get_file_dir(const char *path, char *dir, int size);
static void install_core_tools(void);

// 递归编译：先收集所有源文件，再统一并行分派

int compile_recursive_task(const char *path){
#define MAX_FLAT_FILES 4096
    char (*files)[512] = (char (*)[512])tlibc_malloc(MAX_FLAT_FILES * 512);
    int total = collect_files_recursive(path, ".c", files, MAX_FLAT_FILES);
    /* 追加 .S 汇编文件（如 clone.S、start.S） */
    int s_total = collect_files_recursive(path, ".S", files + total, MAX_FLAT_FILES - total);
    total += s_total;
    if(total <= 0){
        printf("没有找到需要编译的文件, 路径=%s\n", path);
        tlibc_free(files);
        return 0;
    }
    printf("共找到 %d 个源文件\n", total);

    /* ── tmakelist 过滤：目录有合法 tmakelist 时只编译其中引用的文件 ── */
    {
        int idx = 0;
        while (idx < total) {
            char dir[256], name[64];
            get_file_dir(files[idx], dir, sizeof(dir));
            get_file_basename(files[idx], name, sizeof(name));

            Tmakelist tl;
            int ret = read_tmakelist(dir, &tl);
            if (ret > 0 && !tmakelist_has_source(&tl, name)) {
                /* 合法 tmakelist 中未引用此文件 → 跳过 */
                snprintf(files[idx], 512, "%s", files[total - 1]);
                total--;
                continue;
            }
            idx++;
        }
    }

    // 确保所有 build 子目录存在
    for(int i = 0; i < total; i++){
        char out[256];
        out_subdir_from_path(files[i], out, sizeof(out));
        char build_dir[512];
        snprintf(build_dir, 512, "build/%s", out);
        tlibc_recursive_mkdir(build_dir);
    }

    if(g_max_jobs <= 1){
        for(int i = 0; i < total; i++){
            char out[256];
            out_subdir_from_path(files[i], out, sizeof(out));
            printf("编译文件%d: %s\n", i, files[i]);
            if(compile_file(files[i], out) < 0){
                tlibc_free(files);
                return -1;
            }
        }
        tlibc_free(files);
        return 0;
    }

    // 并行编译：所有文件通过同一个工作池分派
    int max_jobs = g_max_jobs;
    if(max_jobs > total) max_jobs = total;
    int pids[max_jobs];
    int active = 0, next = 0, failed = 0;

    while(next < total || active > 0){
        while(active < max_jobs && next < total && !failed){
            char out[256];
            out_subdir_from_path(files[next], out, sizeof(out));
            int pid = compile_file_start(files[next], out);
            if(pid == -2){
                printf("跳过文件%d: %s\n", next, files[next]);
            } else if(pid > 0){
                printf("编译文件%d: %s\n", next, files[next]);
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
            printf("编译失败, 状态码: %d\n", status);
            failed = 1;
        }
    }

    tlibc_free(files);
    return failed ? -1 : 0;
}

/* ── 链接辅助前向声明 ── */
static void obj_to_src_dir(const char *obj_path, char *dir, int size);
static int  link_multi_app(char **obj_paths, int count,
                            const char *output_name, const char *output_path);
static int  link_multi_app_start(char **obj_paths, int count,
                                  const char *output_name, const char *output_path);

// 递归链接：先收集 path 下所有 .o，再统一并行分派
//   path: 搜索目录（如 "build/app"）
//   output_path: 可执行文件输出目录（如 "build/output"）
//   支持多文件模块：检测源目录下的 tmakelist 文件，将多个 .o 联合链接
int link_recursive_task(const char *path, const char *output_path){
    char (*files)[512] = (char (*)[512])tlibc_malloc(MAX_FLAT_FILES * 512);
    int total = collect_files_recursive(path, ".o", files, MAX_FLAT_FILES);
    if(total <= 0){
        printf("没有找到需要链接的 .o 文件 路径=%s\n", path);
        tlibc_free(files);
        return 0;
    }
    printf("共找到 %d 个目标文件\n", total);

    /* 标记已处理的文件（属于某个模块组） */
    char *handled = (char *)tlibc_malloc(total);
    memset(handled, 0, total);

    /* ── Phase 1: 收集 tmakelist 多文件目标 → 标记 handled ── */
    #define MAX_LINK_JOBS 128
    static char *job_objs[MAX_LINK_JOBS][MAX_TARGET_SOURCES + 1];
    static int   job_obj_counts[MAX_LINK_JOBS];
    static char  job_names[MAX_LINK_JOBS][64];
    int job_total = 0;

    for (int i = 0; i < total; i++) {
        if (handled[i]) continue;

        /* 从 build 路径反推源目录 */
        char src_dir[256];
        obj_to_src_dir(files[i], src_dir, sizeof(src_dir));

        Tmakelist tl;
        int tl_ret = read_tmakelist(src_dir, &tl);

        if (tl_ret > 0) {
            /* 跳过已处理过的目录 */
            {
                char main_obj[512];
                snprintf(main_obj, sizeof(main_obj), "build/%s/%s.o",
                         src_dir, tl.targets[0].name);
                int already = 0;
                for (int j = 0; j < total; j++) {
                    if (strcmp(files[j], main_obj) == 0 && handled[j]) {
                        already = 1; break;
                    }
                }
                if (already) continue;
            }

            /* 遍历 tmakelist 中每个目标，收集 .o 并生成链接任务 */
            for (int t = 0; t < tl.target_count && job_total < MAX_LINK_JOBS; t++) {
                TmakelistTarget *tgt = &tl.targets[t];
                int obj_idx = 0;

                /* 目标名自身 → .o */
                {
                    char expected[512];
                    snprintf(expected, sizeof(expected), "build/%s/%s.o",
                             src_dir, tgt->name);
                    for (int j = 0; j < total; j++) {
                        if (strcmp(files[j], expected) == 0) {
                            job_objs[job_total][obj_idx++] = files[j];
                            handled[j] = 1;
                            break;
                        }
                    }
                }

                /* 依赖源文件 → .o */
                for (int s = 0; s < tgt->src_count; s++) {
                    char expected[512];
                    snprintf(expected, sizeof(expected), "build/%s/%s.o",
                             src_dir, tgt->srcs + s * 64);
                    for (int j = 0; j < total; j++) {
                        if (strcmp(files[j], expected) == 0) {
                            job_objs[job_total][obj_idx++] = files[j];
                            handled[j] = 1;
                            break;
                        }
                    }
                }

                if (obj_idx > 0) {
                    job_obj_counts[job_total] = obj_idx;
                    {
                        int k;
                        for (k = 0; tgt->name[k] && k < 63; k++)
                            job_names[job_total][k] = tgt->name[k];
                        job_names[job_total][k] = '\0';
                    }
                    job_total++;
                }
            }
            continue;
        }
    }

    /* ── Phase 2: 统一并行链接（tmakelist 目标 + 单文件应用） ── */
    int failed = 0;
    int max_jobs = g_max_jobs;
    if (max_jobs < 1) max_jobs = 1;

    /* 统计剩余单文件应用 */
    int remaining = 0;
    for (int i = 0; i < total; i++)
        if (!handled[i]) remaining++;

    int all_jobs = job_total + remaining;
    if (all_jobs == 0) {
        tlibc_free(handled);
        tlibc_free(files);
        return 0;
    }
    if (max_jobs > all_jobs) max_jobs = all_jobs;

    if (max_jobs <= 1) {
        /* 串行 */
        for (int j = 0; j < job_total; j++) {
            int ret = link_multi_app(job_objs[j], job_obj_counts[j],
                                      job_names[j], output_path);
            if (ret < 0) { failed = 1; break; }
        }
        if (!failed) {
            for (int i = 0; i < total; i++) {
                if (handled[i]) continue;
                if (link_app(files[i], output_path) < 0) {
                    failed = 1; break;
                }
            }
        }
    } else {
        /* 并行池：tmakelist 目标 + 单文件混合 */
        int pids[max_jobs];
        int active = 0;
        int next_job = 0;     /* tmakelist 任务游标 */
        int next_file = 0;    /* 单文件游标 */

        while (next_job < job_total || next_file < total || active > 0) {
            /* 启动任务直到池满 */
            while (active < max_jobs && !failed) {
                /* 优先启动 tmakelist 目标 */
                if (next_job < job_total) {
                    int pid = link_multi_app_start(job_objs[next_job],
                                                    job_obj_counts[next_job],
                                                    job_names[next_job],
                                                    output_path);
                    if (pid > 0) {
                        pids[active++] = pid;
                    }
                    next_job++;
                    continue;
                }
                /* 然后启动单文件 */
                while (next_file < total && !failed) {
                    if (handled[next_file]) { next_file++; continue; }
                    int pid = link_app_start(files[next_file], output_path);
                    if (pid > 0) {
                        pids[active++] = pid;
                    }
                    next_file++;
                    break;
                }
                if (next_job >= job_total && next_file >= total) break;
            }
            if (active == 0) break;

            int status;
            int done_pid = waitpid(-1, &status, 0);
            if (done_pid <= 0) break;

            for (int k = 0; k < active; k++) {
                if (pids[k] == done_pid) {
                    pids[k] = pids[active - 1];
                    active--;
                    break;
                }
            }
            if (status != 0) {
                printf("链接失败, 状态码: %d\n", status);
                failed = 1;
            }
        }
    }

    tlibc_free(handled);
    tlibc_free(files);
    return failed ? -1 : 0;
    #undef MAX_LINK_JOBS
}

#undef MAX_FLAT_FILES

/* ================================================================== */
/* ── 文件路径工具 ── */

/* 提取文件名（不含后缀），如 "app/compiler/lex.c" → "lex" */
static void get_file_basename(const char *path, char *name, int size)
{
    const char *slash = strrchr(path, '/');
    const char *start = slash ? slash + 1 : path;
    int i;
    for (i = 0; i < size - 1 && start[i] && start[i] != '.'; i++)
        name[i] = start[i];
    name[i] = '\0';
}

/* 提取文件所在目录，如 "app/compiler/lex.c" → "app/compiler" */
static void get_file_dir(const char *path, char *dir, int size)
{
    snprintf(dir, size, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
}

/* ── tmakelist 解析 ── */

/* 读取 <app_dir>/tmakelist，解析目标定义。
 * 返回: >0 合法（目标数），0 文件不存在，-1 格式非法（已打印警告） */
static int read_tmakelist(const char *app_dir, Tmakelist *tl)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/tmakelist", app_dir);
    int fd = openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return 0;

    char buf[4096];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    tl->target_count = 0;
    int line = 0;
    char *p = buf;
    int has_error = 0;

    while (*p) {
        /* 跳过行首空白 */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\r') { p++; line++; continue; }
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (!*p) break;

        /* 解析目标名（直到 ':'） */
        int i = 0;
        char target_name[64];
        while (*p && *p != ':' && *p != '\n' && *p != '\r') {
            if (*p != ' ' && *p != '\t') {
                if (i < 63) target_name[i++] = *p;
            }
            p++;
        }

        if (*p != ':') {
            /* 行不含 ':'，非法（非注释非空行必须含 ':'） */
            if (i > 0) {
                target_name[i] = '\0';
                printf("tmakelist: 语法错误 ('%s' 行缺少 ':'), 退化到默认构建\n",
                       target_name);
                has_error = 1;
                break;
            }
            /* 空行（只有空白），跳过 */
            while (*p && *p != '\n') p++;
            if (*p) { p++; line++; }
            continue;
        }
        p++; /* 跳过 ':' */

        if (i == 0) {
            printf("tmakelist: 语法错误 (行 %d 目标名为空), 退化到默认构建\n", line + 1);
            has_error = 1;
            break;
        }
        target_name[i] = '\0';

        /* 解析源文件列表 */
        if (tl->target_count >= MAX_TMAKELIST_TARGETS) {
            printf("tmakelist: 目标数超过上限 %d\n", MAX_TMAKELIST_TARGETS);
            has_error = 1;
            break;
        }

        TmakelistTarget *t = &tl->targets[tl->target_count];
        {
            int j;
            for (j = 0; j < i && j < 63; j++)
                t->name[j] = target_name[j];
            t->name[j] = '\0';
        }
        t->src_count = 0;

        while (*p && *p != '\n' && *p != '\r') {
            /* 跳过空白 */
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\n' || *p == '\r' || *p == '#' || !*p) break;

            /* 读取源文件名 */
            int j = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '#') {
                if (j < 63) *(t->srcs + t->src_count * 64 + (j++)) = *p;
                p++;
            }
            if (j > 0) {
                *(t->srcs + t->src_count * 64 + j) = '\0';
                t->src_count++;
                if (t->src_count >= MAX_TARGET_SOURCES) break;
            }
        }

        if (t->src_count == 0) {
            printf("tmakelist: 语法错误 ('%s' 没有源文件), 退化到默认构建\n",
                   t->name);
            has_error = 1;
            break;
        }

        tl->target_count++;

        /* 跳过行尾 */
        while (*p && *p != '\n') p++;
        if (*p) { p++; line++; }
    }

    if (has_error) {
        tl->target_count = 0;
        return -1;
    }
    if (tl->target_count == 0) return 0;
    return tl->target_count;
}

/* 检查 name（不含 .c）是否被 tmakelist 中任一目标引用（包含目标名自身） */
static int tmakelist_has_source(Tmakelist *tl, const char *name)
{
    int t;
    for (t = 0; t < tl->target_count; t++) {
        if (strcmp(tl->targets[t].name, name) == 0)
            return 1;
        int s;
        for (s = 0; s < tl->targets[t].src_count; s++) {
            if (strcmp(tl->targets[t].srcs + s * 64, name) == 0)
                return 1;
        }
    }
    return 0;
}

/* 从 build .o 路径反推源目录（如 "build/app/compiler/tcc.o" → "app/compiler"） */
static void obj_to_src_dir(const char *obj_path, char *dir, int size)
{
    /* 跳过 "build/" 前缀 */
    const char *p = obj_path;
    if (strncmp(p, "build/", 6) == 0) p += 6;
    snprintf(dir, size, "%s", p);
    /* 去掉最后的 /xxx.o，得到目录 */
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
}

/* 链接多个 .o 文件到一个可执行文件，返回子进程 PID（不等待） */
static int link_multi_app_start(char **obj_paths, int count,
                                 const char *output_name, const char *output_path)
{
    char *ld_flags[1024];
    int flag_num = copy_default_ld_flags(ld_flags);

    /* 添加所有 .o 文件 */
    int has_toyc_rt = 0;
    for (int i = 0; i < count; i++) {
        ld_flags[flag_num++] = obj_paths[i];
        if (strstr(obj_paths[i], "toyc_rt"))
            has_toyc_rt = 1;
    }

    /* 库文件（toyc 套件自带独立运行时，不依赖 tlibc.a） */
    if (!has_toyc_rt)
        ld_flags[flag_num++] = "build/lib/tlibc.a";

    /* 输出 */
    char output[1024];
    snprintf(output, sizeof(output), "%s/%s", output_path, output_name);
    ld_flags[flag_num++] = (char *)"-o";
    ld_flags[flag_num++] = output;
    ld_flags[flag_num] = NULL;

    int pid = fork();
    if (pid == 0) {
        printf("链接 %s ...\n", output_name);
        execve(default_ld_path, ld_flags, default_envp);
        exit(127);
    }
    return pid;
}

/* 链接多个 .o 文件到一个可执行文件（同步，等价于 start + wait） */
static int link_multi_app(char **obj_paths, int count,
                           const char *output_name, const char *output_path)
{
    int pid = link_multi_app_start(obj_paths, count, output_name, output_path);
    if (pid < 0) return -1;
    int status;
    waitpid(pid, &status, 0);
    return (status == 0) ? 0 : -1;
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
    char buf[65536];
    int fd = openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    if(fd < 0) return 1;
    int total = 0, n;
    while(total < (int)sizeof(buf) - 1 &&
          (n = read(fd, buf + total, sizeof(buf) - total - 1)) > 0){
        total += n;
    }
    close(fd);
    if(total <= 0) return 1;
    buf[total] = '\0';

    int count = 0;
    for(int i = 0; i < total - 8; i++){
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
    tlibc_free(names);
    tlibc_free(buf);
    return total;
}

/* ================================================================== */
/*  递归文件收集                                                       */
/* ================================================================== */

/* 递归收集 path 下所有扩展名为 ext（如 ".c"）的文件，写入 files[0..max) 中。
 * files 为二维数组 char files[max][512]。
 * 返回收集到的文件总数（可能超过 max）。 */
static int
collect_files_recursive(const char *path, const char *ext,
                        char (*files)[512], int max)
{
    int total = 0;

    /* 当前目录下的文件 */
    int n = tlibc_get_file_count(path);
    if(n > 0){
        char **names = (char **)tlibc_malloc(n * sizeof(char *));
        char *buf   = (char *)tlibc_malloc(n * 256);
        for(int i = 0; i < n; i++) names[i] = buf + i * 256;
        n = tlibc_get_file_name_list(path, (uint64_t)names, n);

        for(int i = 0; i < n && total < max; i++){
            char *dot = strrchr(names[i], '.');
            if(dot && strcmp(dot, ext) == 0)
                snprintf(files[total++], 512, "%s/%s", path, names[i]);
        }
        tlibc_free(names);
        tlibc_free(buf);
    }

    /* 递归子目录 */
    int dir_cnt = tlibc_get_dir_count(path);
    if(dir_cnt > 0 && total < max){
        char **dirs = (char **)tlibc_malloc(dir_cnt * sizeof(char *));
        char *dbuf = (char *)tlibc_malloc(dir_cnt * 256);
        for(int i = 0; i < dir_cnt; i++) dirs[i] = dbuf + i * 256;
        dir_cnt = tlibc_get_dir_name_list(path, (uint64_t)dirs, dir_cnt);

        for(int i = 0; i < dir_cnt && total < max; i++){
            char sub[512];
            snprintf(sub, 512, "%s/%s", path, dirs[i]);
            total += collect_files_recursive(sub, ext, files + total, max - total);
        }
        tlibc_free(dirs);
        tlibc_free(dbuf);
    }

    return total;
}

/* ================================================================== */
/*  单应用构建支持                                                     */
/* ================================================================== */

/* 递归查找 app/ 下 name.c，写入 rel_path（如 "app/net/old/client.c"） */
static int
find_app_source_in_dir(const char *dir, const char *name,
                       char *rel_path, int size)
{
    /* 检查当前目录文件 */
    int n = tlibc_get_file_count(dir);
    if(n > 0){
        char **names = (char **)tlibc_malloc(n * sizeof(char *));
        char *buf   = (char *)tlibc_malloc(n * 256);
        for(int i = 0; i < n; i++) names[i] = buf + i * 256;
        n = tlibc_get_file_name_list(dir, (uint64_t)names, n);

        for(int i = 0; i < n; i++){
            char *dot = strrchr(names[i], '.');
            if(dot && strcmp(dot, ".c") == 0){
                *dot = '\0';
                int match = (strcmp(names[i], name) == 0);
                *dot = '.';
                if(match){
                    snprintf(rel_path, size, "%s/%s.c", dir, name);
                    tlibc_free(names);
                    tlibc_free(buf);
                    return 0;
                }
            }
        }
        tlibc_free(names);
        tlibc_free(buf);
    }

    /* 递归子目录 */
    int dir_cnt = tlibc_get_dir_count(dir);
    if(dir_cnt > 0){
        char **dirs = (char **)tlibc_malloc(dir_cnt * sizeof(char *));
        char *dbuf = (char *)tlibc_malloc(dir_cnt * 256);
        for(int i = 0; i < dir_cnt; i++) dirs[i] = dbuf + i * 256;
        dir_cnt = tlibc_get_dir_name_list(dir, (uint64_t)dirs, dir_cnt);

        int found = -1;
        for(int i = 0; i < dir_cnt && found != 0; i++){
            char sub[512];
            snprintf(sub, 512, "%s/%s", dir, dirs[i]);
            found = find_app_source_in_dir(sub, name, rel_path, size);
        }
        tlibc_free(dirs);
        tlibc_free(dbuf);
        return found;
    }

    return -1;
}

/* 在 app/ 树下递归查找 name.c，将路径写入 rel_path（如 "app/net/ndiscover.c"） */
static int find_app_source(const char *name, char *rel_path, int size)
{
    /* 先检查 app/name.c（根级程序如 shell, tmake） */
    snprintf(rel_path, size, "app/%s.c", name);
    if(tlibc_is_path_file(rel_path) == 1)
        return 0;

    /* 递归搜索所有子目录 */
    return find_app_source_in_dir("app", name, rel_path, size);
}

/* 只构建一个程序，不碰其他 app */
static int build_single_app(const char *name)
{
    /* 1. 确保 lib 已编译 */
    if(tlibc_is_path_file("build/tlibc.a") != 1){
        printf("Library not found, building lib first...\n");
        tlibc_recursive_mkdir("build/lib");
        compile_recursive_task("lib");
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

    /* 5. 检查 tmakelist 多文件模块 */
    Tmakelist tl;
    int tl_ret = read_tmakelist(out_subdir, &tl);

    if (tl_ret > 0) {
        /* 在 tmakelist 中查找匹配的目标 */
        TmakelistTarget *found_tgt = NULL;
        for (int t = 0; t < tl.target_count; t++) {
            if (strcmp(tl.targets[t].name, name) == 0) {
                found_tgt = &tl.targets[t];
                break;
            }
        }

        if (found_tgt) {
            /* ── tmakelist 多文件目标：编译全部依赖 → 联合链接 ── */
            int total_srcs = 1 + found_tgt->src_count;
            printf("检测到 tmakelist 多文件目标 (%d 个源文件)\n", total_srcs);

            /* 编译目标自身 */
            if (compile_file(src_path, out_subdir) < 0) return -1;

            /* 编译依赖源文件 */
            for (int s = 0; s < found_tgt->src_count; s++) {
                char dep_src[512];
                snprintf(dep_src, sizeof(dep_src), "%s/%s.c",
                         out_subdir, found_tgt->srcs + s * 64);
                if (tlibc_is_path_file(dep_src) == 1) {
                    if (compile_file(dep_src, out_subdir) < 0) return -1;
                }
            }

            /* 收集 .o 路径 */
            char *group_objs[MAX_TARGET_SOURCES + 1];
            int found_objs = 0;

            /* 目标自身 */
            {
                char expected[512];
                snprintf(expected, sizeof(expected), "build/%s/%s.o",
                         out_subdir, found_tgt->name);
                if (tlibc_is_path_file(expected) == 1) {
                    group_objs[0] = (char *)tlibc_malloc(512);
                    snprintf(group_objs[0], 512, "%s", expected);
                    found_objs++;
                }
            }

            /* 依赖 */
            for (int s = 0; s < found_tgt->src_count; s++) {
                char expected[512];
                snprintf(expected, sizeof(expected), "build/%s/%s.o",
                         out_subdir, found_tgt->srcs + s * 64);
                group_objs[1 + s] = NULL;
                if (tlibc_is_path_file(expected) == 1) {
                    group_objs[1 + s] = (char *)tlibc_malloc(512);
                    snprintf(group_objs[1 + s], 512, "%s", expected);
                    found_objs++;
                }
            }

            tlibc_recursive_mkdir("build/output");
            /* 紧凑排列 */
            char *compact[MAX_TARGET_SOURCES + 1];
            int cc = 0;
            for (int k = 0; k < total_srcs; k++)
                if (k == 0 ? group_objs[0] : group_objs[k])
                    compact[cc++] = (k == 0) ? group_objs[0] : group_objs[k];
            /* 为避免双重释放，只在 group_objs[0] 存了 compact 也用到的指针；
             * compact 指向 group_objs 数组中的元素，释放时需遍历 group_objs 非 NULL 项 */
            if (link_multi_app(compact, cc, found_tgt->name, "build/output") < 0) {
                for (int i = 0; i < total_srcs; i++) {
                    if (i == 0 && group_objs[0]) tlibc_free(group_objs[0]);
                    else if (i > 0 && group_objs[i]) tlibc_free(group_objs[i]);
                }
                printf("Link failed.\n");
                return -1;
            }
            for (int i = 0; i < total_srcs; i++) {
                if (i == 0 && group_objs[0]) tlibc_free(group_objs[0]);
                else if (i > 0 && group_objs[i]) tlibc_free(group_objs[i]);
            }

            /* 安装 */
            char install_dir[1024];
            tlibc_get_user_dir(install_dir, 1024);
            size_t dlen = strlen(install_dir);
            snprintf(install_dir + dlen, 1024 - dlen, "/tlibc/bin");
            tlibc_recursive_mkdir(install_dir);
            char dst[1024];
            snprintf(dst, 1024, "%s/%s", install_dir, found_tgt->name);
            printf("Install: build/output/%s -> %s\n", found_tgt->name, dst);
            char exe_src[512];
            snprintf(exe_src, 512, "build/output/%s", found_tgt->name);
            tlibc_copy_exe_file(exe_src, dst);
            printf("\nDone: %s\n", found_tgt->name);
            install_core_tools();
            return 0;
        }
        /* tmakelist 存在但目标不匹配 → 回退到单文件 */
    }

    /* 6. 单文件应用：编译 + 链接 */
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

    /* 7. 链接 */
    char obj_path[512];
    snprintf(obj_path, 512, "build/%s/%s.o", out_subdir, name);
    tlibc_recursive_mkdir("build/output");
    printf("Linking %s ...\n", obj_path);
    if(link_app(obj_path, "build/output") < 0){
        printf("Link failed.\n");
        return -1;
    }

    /* 8. 安装 */
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
    install_core_tools();
    return 0;
}

/* ── 核心工具链安装到 ~/.local/bin/ ── */

/* 判断 ~/.local/bin 是否在 PATH 中 */
static int is_in_path(const char *dir)
{
    extern char **global_envp;
    char *path = get_env_var(global_envp, "PATH");
    if (!path) return 0;

    char *p = path;
    while (*p) {
        char seg[512];
        int s = 0;
        while (*p && *p != ':') {
            if (s < 511) seg[s++] = *p;
            p++;
        }
        seg[s] = '\0';
        if (strcmp(seg, dir) == 0) return 1;
        if (*p == ':') p++;
    }
    return 0;
}

/* 安装核心工具链（tmake, shell, toyc, toypp）到 ~/.local/bin/。
 * 仅安装 build/output/ 中已存在的，因此全量构建和 -b 单目标都可以调用。 */
static void install_core_tools(void)
{
    char local_bin[1024];
    tlibc_get_user_dir(local_bin, 1024);
    {
        size_t dl = strlen(local_bin);
        snprintf(local_bin + dl, 1024 - dl, "/.local/bin");
    }
    tlibc_recursive_mkdir(local_bin);

    int count = 0;
    for (int i = 0; g_core_tools[i] != NULL; i++) {
        char src[512];
        snprintf(src, sizeof(src), "build/output/%s", g_core_tools[i]);
        if (tlibc_is_path_file(src) != 1) continue;

        char dst[1024];
        snprintf(dst, sizeof(dst), "%s/%s", local_bin, g_core_tools[i]);

        /* 先写到 .new 临时文件再 rename 到目标。
         * 直接 O_TRUNC 正在运行的二进制会触发 ETXTBSY。 */
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s/.%s.new", local_bin, g_core_tools[i]);
        if (tlibc_copy_exe_file(src, tmp) == 0)
            rename(tmp, dst);
        count++;
    }

    if (count == 0) return;

    printf("\n核心工具: ");
    for (int i = 0; g_core_tools[i] != NULL; i++) {
        char src[512];
        snprintf(src, sizeof(src), "build/output/%s", g_core_tools[i]);
        if (tlibc_is_path_file(src) == 1)
            printf("%s ", g_core_tools[i]);
    }
    printf("→ %s\n", local_bin);

    if (!is_in_path(local_bin)) {
        printf("  ! %s 不在 PATH 中。请将下面几行加到 ~/.bashrc 末尾：\n",
               local_bin);
        printf("      # ~/.local/bin — XDG 标准用户可执行文件路径\n");
        printf("      if [ -d \"$HOME/.local/bin\" ] ; then\n");
        printf("          PATH=\"$HOME/.local/bin:$PATH\"\n");
        printf("      fi\n");
        printf("    （不要从 ~/.bashrc 里 source ~/.profile，Ubuntu 的\n");
        printf("     ~/.profile 默认会 source ~/.bashrc，双向 source 会死循环。）\n");
    }
}

int main(int argc, char *argv[]){

    // 先处理 --help / -h，以及 -b / -T（这些不依赖项目目录）
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            printf("Tinylibc 自托管构建工具\n");
            printf("\n用法: tmake [-j [N]] [-b <程序名>] [-T]\n");
            printf("\n选项:\n");
            printf("  -h, --help     显示本帮助信息\n");
            printf("  -j [N]         并行编译，N 为并行任务数（默认自动检测 CPU 核数）\n");
            printf("                  不传 -j 时自动检测，-j 1 可切回串行\n");
            printf("  -b <程序名>    只构建指定程序（增量，跳过已有 .o）\n");
            printf("                  例如: tmake -b ndiscover\n");
            printf("                  重复调用跳过已编译文件，修改源码后自动重编\n");
            printf("  -T             自托管模式：用 toyc/toyas 编译，不依赖 gcc\n");
            printf("                  toyc/toyas 不存在时自动回退到 gcc\n");
            printf("\n多文件模块:\n");
            printf("  子目录下可放 tmakelist 文件定义多文件目标:\n");
            printf("    <目标名>: <源文件1> <源文件2> ...\n");
            printf("  有合法 tmakelist 的目录只编译其中引用的源文件。\n");
            return 0;
        } else if(strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0){
            if(i + 1 < argc){
                g_build_target = argv[++i];
            } else {
                printf("Error: -b 需要指定程序名\n");
                return 1;
            }
        } else if(strcmp(argv[i], "-T") == 0){
            g_self_host = 1;
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
    // 自托管模式(-T)：保留 build/output 中的 toyc/toyas/tmake 工具
    if(!g_build_target){
        if (g_self_host) {
            /* 只清理 lib/app 子目录，保留 build/output */
            tlibc_recursive_rm_dir("build/lib");
            tlibc_recursive_rm_dir("build/app");
            printf("清理build目录成功 (保留工具链)\n");
        } else {
            ret = tlibc_recursive_rm_dir("build");
            printf("删除build目录成功\n");
        }
    }
    //创建build目录
    ret = tlibc_recursive_mkdir(build_path);
    if(ret < 0){
        printf("无法创建build目录, 错误码: %d\n", ret);
        return 1;
    }
    if(!g_build_target){
        printf("创建build目录成功\n");
    }

    /* 解析 -j [N] 参数（显式指定时覆盖自动检测） */
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-j") == 0){
            g_jobs_explicit = 1;
            if(i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9'){
                g_max_jobs = parse_int(argv[i + 1]);
            } else {
                g_max_jobs = get_nprocs();
            }
            if(g_max_jobs < 1) g_max_jobs = 1;
            if(g_max_jobs > 64) g_max_jobs = 64;
            break;
        }
    }

    /* 默认自动检测 CPU 核数 */
    if (!g_jobs_explicit) {
        g_max_jobs = get_nprocs();
        printf("检测到 CPU 核数: %d\n", g_max_jobs);
        if (g_max_jobs < 1) g_max_jobs = 1;
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
    compile_recursive_task("lib");
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
    install_core_tools();
    t_install = get_ms() - t_start;

    t_total = get_ms() - t_total;

    // 统计信息（只计 .o 文件，排除 .d 等）
    int lib_o_count = 0;
    {
        char (*_ofiles)[512] = (char (*)[512])tlibc_malloc(4096 * 512);
        int n = collect_files_recursive("build/lib", ".o", _ofiles, 4096);
        lib_o_count = n > 0 ? n : 0;
        tlibc_free(_ofiles);
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