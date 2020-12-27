#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
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
static void ngx_process_get_status(void);


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
            // ngx_log_stderr(0, "sigaction(%s) succed!", sig->signame);
        }
    }
    return 0;
}

static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext){
    //printf("来信号了\n");    
    ngx_signal_t    *sig;    //自定义结构
    char            *action; //一个字符串，用于记录一个动作字符串以往日志文件中写
    
    for (sig = signals; sig->signo != 0; sig++) //遍历信号数组    
    {         
        //找到对应信号，即可处理
        if (sig->signo == signo) 
        { 
            break;
        }
    } //end for

    action = (char *)"";  //目前还没有什么动作；

    if(ngx_process == NGX_PROCESS_MASTER)      //master进程，管理进程，处理的信号一般会比较多 
    {
        //master进程的往这里走
        switch (signo)
        {
        case SIGCHLD:  //一般子进程退出会收到该信号
            ngx_reap = 1;  //标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;

        //.....其他信号处理以后待增加

        default:
            break;
        } //end switch
    }
    else if(ngx_process == NGX_PROCESS_WORKER) //worker进程，具体干活的进程，处理的信号相对比较少
    {
        //worker进程的往这里走
        //......以后再增加
        //....
        ;
    }
    else
    {
        //非master非worker进程，先啥也不干
        //do nothing
    } //end if(ngx_process == NGX_PROCESS_MASTER)

    //这里记录一些日志信息
    //siginfo这个
    if(siginfo && siginfo->si_pid)  //si_pid = sending process ID【发送该信号的进程id】
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action); 
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s",signo, sig->signame, action);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }

    //.......其他需要扩展的将来再处理；

    //子进程状态有变化，通常是意外退出【既然官方是在这里处理，我们也学习官方在这里处理】
    if (signo == SIGCHLD) 
    {
        ngx_process_get_status(); //获取子进程的结束状态
    } //end if

    return;
}

static void ngx_process_get_status(void){
    pid_t   pid;
    int     status;
    int     err;
    int     one=0;      //标记信号正常处理一次

    //当kill一个子进程的时候，父进程会收到这个SIGCHLD信号
    for(;;){
        //waitpid, 获取子进程的终止状态
        //第一次waitpid返回一个>0的值，表示成功
        //第二次循环回来，再调用waitpid返回一个0，表示子进程还没结束
        pid = waitpid(-1, &status, WNOHANG);    //-1, 表示等待任何子进程

        if(pid == 0){   //子进程还没结束会返回这个数字，但是应该不会是这个数字
            return;
        }
        
        //----------------------------------------------
        if(pid == -1){
            err = errno;
            if(err == EINTR){       //表示被某个信号中断
                continue;
            }
            if(err == ECHILD && one){   //没有子进程
                return;
            }

            if(err == ECHILD){
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }
        }
        one = 1;
        if(WTERMSIG(status)){   //获取使子进程终止的编号
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status)); //获取使子进程终止的信号编号
        }else{
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status)); //WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }
    }
    return;
}
