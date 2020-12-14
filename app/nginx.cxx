#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// 头文件路径，使用gcc -I参数指定
#include "ngx_func.h"
#include "ngx_signal.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"

// 和设置标题有关的全局量
char **g_os_argv;    //原始命令参数数组，在main中赋值
char *gp_envmem = NULL;  //指向自己分配的env环境变量的内存
int g_environlen = 0;   //环境变量所占内存大小

int main(int argc, char *const *argv){

    g_os_argv = (char **) argv;

    ngx_init_setproctitle();

    //在main中，先把配置读出来，供后续使用，不要在线程中第一次调用
    CConfig *p_config = CConfig::GetInstance();
    if(p_config->Load("/home/ubuntu/study_cpp/mztkn_study_nginx/nginx/nginx.conf")==false){
        printf("配置文件载入失败，退出\n");
        exit(1);
    }

    //获取配置文件信息的用法
    int port = p_config->GetIntDefault("ListenPort",0);  //0是缺省值
    const char* dbinfo = p_config->GetString("DBInfo");
    printf("ListenPort = %d\n", port);
    printf("DBInfo = %s\n", dbinfo);

    // 设置新标题，这之前应该保证所有命令行参数都不用了
    // ngx_setproctitle("nginx: master process");

    // for(;;){
    //     sleep(1);
    //     printf("休息1s\n");
    // }
    


    if(gp_envmem){
        delete []gp_envmem;
        gp_envmem = NULL;
    }
    printf("程序退出，再见！\n");
    return 0;
}

