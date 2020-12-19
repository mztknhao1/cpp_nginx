#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "ngx_signal.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"


// 一个信号有关的结构
typedef struct{
    int         signo;          //信号对应的数字编号
    const char  *signame;       //信号对应的名字

    // 函数指针，这个函数指针由我们提供，但是参数和返回值固定
    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);  //函数指针
}ngx_signal_t;

// 声明一个信号处理函数
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext);

// 数组，定义本系统处理的各种信号，我们取一小部分nginx中的信号
// TODO:可以增加更多的信号处理函数
ngx_signal_t signals[] = {
    // signo          signame             handler
    {SIGHUP,          "SIGHUP",           ngx_signal_handler},    //中端断开信号 SIGHUP
    {SIGINT,          "SIGINT",           ngx_signal_handler},
    {SIGTERM,         "SIGTERM",          ngx_signal_handler},
    {SIGCHLD,         "SIGCHLD",          ngx_signal_handler},    //中断断开信号
    {SIGQUIT,         "SIGQUIT",          ngx_signal_handler},
    {SIGIO,           "SIGIO",            ngx_signal_handler},
    {SIGSYS,          "SIGSYS, SIG_ING",  NULL},

    // ...根据需要添加
    {0,               NULL,               NULL}  //信号对应的数字至少是1，用0来表示结束
};


// 初始化信号的函数，用于注册信号处理程序
// 返回值 0表示成功，-1表示失败
int ngx_init_signals(){
    ngx_signal_t       *sig;    //指向信号结构体的指针
    struct sigaction   sa;

    for(sig = signals;sig->signo !=0 ; sig++){
        memset(&sa, 0, sizeof(struct sigaction));

        if(sig->handler){
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO;
        }else{
            sa.sa_handler = SIG_IGN;
        }

        sigemptyset(&sa.sa_mask);

        if(sigaction(sig->signo, &sa, NULL) == -1){
            ngx_log_error_core(NGX_LOG_EMERG, errno, "sigaction(%s) failed", sig->signame);
            return -1;
        }else{
            ngx_log_stderr(0, "sigaction(%s) succed!", sig->signame);
        }
    }
    return 0;
}

static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext){
    printf("信号来了!\n");
}
