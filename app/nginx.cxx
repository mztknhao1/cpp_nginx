#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// 头文件路径，使用gcc -I参数指定
#include "ngx_func.h"
#include "ngx_signal.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"

static void freeresource();

// 和设置标题有关的全局量
char **g_os_argv;    //原始命令参数数组，在main中赋值
char *gp_envmem = NULL;  //指向自己分配的env环境变量的内存
int g_environlen = 0;   //环境变量所占内存大小

// 和进程有关的全局量
pid_t ngx_pid;          //当前进程的pid

int main(int argc, char *const *argv){
    int exitcode = 0;

    ngx_pid = getpid();
    g_os_argv = (char **) argv;

    // 初始化，如果初始化失败就直接退出
    CConfig *p_config = CConfig::GetInstance();
    if(p_config->Load("/home/ubuntu/study_cpp/mztkn_study_nginx/nginx/nginx.conf")==false){
        ngx_log_stderr(0, "配置文件[%s]载入失败， 退出!", "nginx.conf");
        exitcode = 2;  //标记找不到文件
        goto lblexit;
    }

    //一些初始化函数
    ngx_log_init();        //日志初始化（创建/打开日志文件）


    //设置新标题，这之前应该保证所有命令行参数都不用了
    ngx_init_setproctitle();
    ngx_setproctitle("nginx: master process");

    // for(;;){
    //     sleep(1);
    //     printf("休息1s\n");
    // }

    ngx_log_stderr(0,"InValid Info: %d", 10);
    ngx_log_error_core(1, 2, "这里工作出了问题:%s --- %.2f", "WHAT", 12.3);


lblexit:
    freeresource(); //释放一些资源
    printf("程序退出，再见！\n");
    return exitcode;
}

void freeresource(){
    if(gp_envmem){
        delete[] gp_envmem;
        gp_envmem = NULL;
    }

    //关闭日志文件
    if(ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1){
        close(ngx_log.fd);
        ngx_log.fd = -1;
    }
}