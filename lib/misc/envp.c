#include "tlibc_everything.h"

char **global_envp = NULL;

// 统计环境变量数量
/* Returns the number of entries in a NULL-terminated envp array */
int tlibc_envp_count(char *envp[])
{
    int count = 0;
    if (envp) {
        while (envp[count] != NULL)
            count++;
    }
    return count;
}

/* Prints all environment variables to stdout for debugging */
void tlibc_print_all_env_vars(char *envp[])
{
    if (!envp) {
        __printf("环境变量数组为空\n");
        return;
    }
    
    __printf("\n========== 环境变量列表 ==========\n");
    for (int i = 0; envp[i] != NULL; i++) {
        __printf("[%d] %s\n", i, envp[i]);
    }
    __printf("========== 总共 %d 个环境变量 ==========\n\n",
             tlibc_envp_count(envp));
}

// 根据名称查找环境变量（例如 "PATH=..."）
char *get_env_var(char *envp[], const char *name)
{
    if (!envp || !name)
        return NULL;
    
    size_t name_len = 0;
    while (name[name_len] != '\0')
        name_len++;
    
    for (int i = 0; envp[i] != NULL; i++) {
        // 检查是否以 "name=" 开头
        size_t j = 0;
        while (j < name_len && envp[i][j] == name[j])
            j++;
        
        if (j == name_len && envp[i][j] == '=')
            return &envp[i][j + 1];  // 返回等号后面的值
    }
    return NULL;  // 未找到
}