#include "tlibc_everything.h"

//用来编译Tinylibc的app，这样甚至不需要make了
//之后app下会细分各级目录，我不想写新的Makefile规则，就用这个程序来编译app

char *build_path = "./build";
char *default_gcc_path = "/usr/bin/x86_64-linux-gnu-gcc";
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
char *default_ld_flags[]={
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

int compile_file(const char *file_path){
    int pid = fork();
    char *gcc_flags[256];
    int flag_num = copy_gcc_flags(gcc_flags);
    gcc_flags[flag_num++] = "-c";//只编译
    gcc_flags[flag_num++] = (char *)file_path;
    gcc_flags[flag_num++] = "-o";//指定目标文件路径
    //构造目标文件路径
    char obj_file_path[512];
    char *file_name = strrchr(file_path, '/');
    snprintf(obj_file_path, 512, "%s/%s.o", build_path, file_name ? file_name + 1 : file_path);
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

int link_library(){
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
    ret = mkdirat(AT_FDCWD, build_path, 0755);
    if(ret < 0){
        printf("无法创建build目录, 错误码: %d\n", ret);
        return 1;
    }
    printf("创建build目录成功\n");

    compile_file("./lib/core.c");

    //处理参数，可生成不同的目标
    return 0;
}