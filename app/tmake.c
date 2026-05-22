#include "tlibc_everything.h"

//用来编译Tinylibc的app，这样甚至不需要make了
//之后app下会细分各级目录，我不想写新的Makefile规则，就用这个程序来编译app

char *gcc_path;
char **gcc_flags; //除了-o的通用选项

int compile_file(const char *file_path){
    // char full_flags[512]; //加上-o选项
    //对file_path处理，在build目录下生成编辑后的文件
    execve(gcc_path, gcc_flags, NULL);
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
    //处理参数，可生成不同的目标
    return 0;
}