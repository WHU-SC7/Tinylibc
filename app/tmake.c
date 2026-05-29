#include "tlibc_everything.h"

//用来编译Tinylibc的app，这样甚至不需要make了
//之后app下会细分各级目录，我不想写新的Makefile规则，就用这个程序来编译app

#define LIB_DIR "lib"
#define LIB_OUTPUT "tlibc.a"//静态库的文件名

char *build_path = "./build";
char *build_lib_path = "./build/lib";
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

int compile_file(const char *file_path, char *output_path){
    int pid = fork();
    char *gcc_flags[256];
    int flag_num = copy_gcc_flags(gcc_flags);
    gcc_flags[flag_num++] = "-c";//只编译
    gcc_flags[flag_num++] = (char *)file_path;
    gcc_flags[flag_num++] = "-o";//指定目标文件路径
    //构造目标文件路径
    char obj_file_path[512];
    char *file_name = strrchr(file_path, '/');
    snprintf(obj_file_path, 512, "%s", build_path);
    if(output_path){
        snprintf(obj_file_path + strlen(obj_file_path), 512 - strlen(obj_file_path), "/%s", output_path);
    }
    if(file_name){
        snprintf(obj_file_path + strlen(obj_file_path), 512 - strlen(obj_file_path), "/%s", file_name + 1);
        //把.c和.S后缀改为.o
        char *dot = strrchr(obj_file_path, '.');
        if(dot){
            strcpy(dot, ".o");
        }
    }
    gcc_flags[flag_num++] = obj_file_path;
    gcc_flags[flag_num] = NULL;
    if(pid == 0){
        //打印出命令
        printf("执行命令: %s", default_gcc_path);
        for(int i = 0; gcc_flags[i] != NULL; i++){
            printf(" %s", gcc_flags[i]);
        }
        printf("\n");
        execve(default_gcc_path, gcc_flags, default_envp);
    }
    else{
        int status;
        waitpid(-1,&status,0);
        if(status == 0){
            printf("编译成功\n");
            return 0;
        }
        else{
            printf("编译失败, 状态码: %d\n", status);
            return -1;
        }
    }
    return 0;
}

//根据路径和文件名来编译
int compile_path_file(char *path, char *file_name, char *output_path){
    char file_path[512];
    snprintf(file_path, 512, "%s/%s", path, file_name);
    return compile_file(file_path, output_path);
}

//编译指定path下的所有文件
int compile_task(char *path){
    int files_count = tlibc_get_file_count(path);
    printf("要编译的文件数量: %d\n", files_count);
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
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(build_path, (uint64_t)file_name_list, files_count);
    //构建ar的参数
    char *ar_flags[1024];
    int flag_num = copy_default_ar_flags(ar_flags);
    ar_flags[flag_num++] = LIB_OUTPUT;//输出的库文件路径
    for(int i=0; i<files_count; i++){
        ar_flags[flag_num++] = file_name_list[i];
    }
    ar_flags[flag_num] = NULL;
    //打印参数
    for(int i=0; ar_flags[i] != NULL; i++){
        printf("ar参数%d: %s\n", i, ar_flags[i]);
    }
    int pid = fork();
    if(pid == 0){
        chdir(build_path); //切换到构建目录下执行ar命令
        printf("执行命令: %s", default_ar_path);
        for(int i = 0; ar_flags[i] != NULL; i++){
            printf(" %s", ar_flags[i]);
        }
        printf("\n");
        execve(default_ar_path, ar_flags, default_envp);
    }
    else{
        int status;
        waitpid(-1,&status,0);
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

//把指定的.o文件与静态库文件链接，输出到output_path中
int link_app(char *app_path, char *output_path){
    
    //构建ld的参数
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
    //去掉结尾的.o
    char *dot = strrchr(output, '.');
    if(dot){
        strcpy(dot, "");
    }
    ld_flags[flag_num++] = "-o";
    ld_flags[flag_num++] = output; //输出文件名

    ld_flags[flag_num] = NULL;
    //打印参数
    for(int i=0; ld_flags[i] != NULL; i++){
        printf("ld参数%d: %s\n", i, ld_flags[i]);
    }
    int pid = fork();
    if(pid == 0){
        printf("执行命令: %s", default_ld_path);
        for(int i = 0; ld_flags[i] != NULL; i++){
            printf(" %s", ld_flags[i]);
        }
        printf("\n");
        execve(default_ld_path, ld_flags, default_envp);
    }
    else{
        int status;
        waitpid(-1,&status,0);
        if(status == 0){
            printf("链接应用程序成功\n");
            return 0;
        }
        else{
            printf("链接应用程序失败, 状态码: %d\n", status);
            return -1;
        }
    }
    return 0;
}

//链接指定all_app_path下的所有.o文件，输出到output_path中
int link_task(char *all_app_path, char *output_path){
    int files_count = tlibc_get_file_count(all_app_path);
    printf("要链接的文件数量: %d\n", files_count);
    char **file_name_list = (char **)tlibc_malloc(files_count * sizeof(char *));
    char *file_name_buf = (char *)tlibc_malloc(files_count * 256);
    for(int i = 0; i < files_count; i++) {
        file_name_list[i] = file_name_buf + i * 256;
    }
    files_count = tlibc_get_file_name_list(all_app_path, (uint64_t)file_name_list, files_count);;
    for(int i=0; i<files_count; i++){
        char file_path[512];
        snprintf(file_path, 512, "%s/%s", all_app_path, file_name_list[i]);
        printf("链接文件%d: %s\n", i, file_path);
        link_app(file_path, output_path);
    }
    munmap(file_name_list, files_count * sizeof(char *));
    munmap(file_name_buf, files_count * 256);
    return 0;
}

int main(int argc, char *argv[]){
    
    //检查工作目录是否是Tinylibc项目，以tlibc_everything.h为标志
    int ret = tlibc_is_path_file("tlibc_commit_log.md");
    if(ret != 1){
        printf("当前目录不是Tinylibc项目! 切换到Tinylibc项目目录再尝试tmake生成!\n");
        return 1;
    }
    
    //删除build然后重新创建
    ret = tlibc_recursive_rm_dir("build");
    if(ret < 0){
        printf("无法删除build目录, 错误码: %d\n", ret);
        return 1;
    }
    printf("删除build目录成功\n");
    //创建build目录
    ret = tlibc_recursive_mkdir(build_path); //如果build目录的父目录不存在就创建父目录
    if(ret < 0){
        printf("无法创建build目录, 错误码: %d\n", ret);
        return 1;
    }
    printf("创建build目录成功\n");

    
    compile_task(LIB_DIR);
    ar_library(build_lib_path);
    copy_file("./build/lib/tlibc.a", "./build/tlibc.a");

    compile_task("app");
    tlibc_recursive_mkdir("build/output");
    link_task("build/app", "build/output");

    //处理参数，可生成不同的目标
    return 0;
}