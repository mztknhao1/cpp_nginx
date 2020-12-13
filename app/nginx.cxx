#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

// 头文件路径，使用gcc -I参数指定
#include "ngx_func.h"
#include "ngx_signal.h"
#include "ngx_c_conf.h"

int main(int argc, char *const *argv){
    //在main中，先把配置读出来，供后续使用，不要在线程中第一次调用
    CConfig *p_config = CConfig::GetInstance();
    if(p_config->Load("/home/ubuntu/study_cpp/mztkn_study_nginx/nginx/nginx.conf")==false){
        printf("配置文件载入失败，退出\n");
        exit(1);
    }

    //获取配置文件信息的用法
    // int port = p_config->GetIntDefault("ListenPort",0);  //0是缺省值
    // const char* dbinfo = p_config->GetString("DBInfo");
    // printf("ListenPort = %d\n", port);
    // printf("DBInfo = %s\n", dbinfo);

    




    return 0;
}

