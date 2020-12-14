/******************************************************
修改进程标题，master+多个worker，不改标题不好调试，不醒目
*******************************************************/
#include "ngx_func.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern int g_environlen;
extern char* gp_envmem;
extern char** g_os_argv;

//设置可执行程序标题相关函数，分配内存，并把环境变量搬家
void ngx_init_setproctitle(){
    int i;

    // 统计环境变量所占内存，判断方法时environment[i]是否为空
    for(i = 0;environ[i];++i){
        g_environlen += strlen(environ[i]) + 1;  // +1 是因为末尾有\0
    }

    gp_envmem = new char[g_environlen];
    memset(gp_envmem, 0, g_environlen);
    
    char *ptmp = gp_envmem;

    //这里把原来的内存内容搬到新的地方
    for(i=0;environ[i];++i){
        size_t size = strlen(environ[i]) + 1;
        strcpy(ptmp, environ[i]);      // 把原来的环境变量移动
        environ[i] = ptmp;             // 把环境变量指向新内存
        ptmp += size;
    }
    return;
}

// 设置可执行程序标题
void ngx_setproctitle(const char *title){
    //我们假设所有的命令行参数都不需要了，可以i随意覆盖了

    //(1) 计算新标题长度
    size_t ititlelen = strlen(title);

    //(2) 计算总的原始的argv那块内存的总长度（包括各种参数）
    size_t e_environlen = 0;
    for(int i=0;g_os_argv[i];i++){
        e_environlen += strlen(g_os_argv[i]) + 1;
    }

    size_t esy = e_environlen + g_environlen;

    if(esy <= ititlelen){
        return;
    }

    //(3) 设置后续的命令行参数为空，表示argv[] 只有一个元素了
    g_os_argv[1] = NULL;

    //(4) 把标题弄进来，注意原来的命令行参数都会被覆盖掉
    char *ptmp = g_os_argv[0];
    strcpy(ptmp, title);
    ptmp += ititlelen;

    //(5) 把剩余的原argv以及envion所占内存全部清零
    size_t cha = esy - ititlelen;
    memset(ptmp, 0, cha);
    return;
}