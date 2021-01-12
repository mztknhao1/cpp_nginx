#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

// 头文件路径，使用gcc -I参数指定
#include "ngx_func.h"
#include "ngx_signal.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"
#include "HandlerFactory.h"
#include "LoginHandler.h"
#include "PingHandler.h"
#include "RegisterHandler.h"

static void freeresource();

// 和设置标题有关的全局量
size_t          g_argvneedmem=0;            //保存下这些argv参数所需要的内存大小
size_t          g_envneedmem=0;             //保存下环境变量需要的大小
int             g_os_argc;                  //参数个数
char            **g_os_argv;                //原始命令参数数组，在main中赋值
char            *gp_envmem = NULL;          //指向自己分配的env环境变量的内存
int             g_environlen = 0;           //环境变量所占内存大小

// socket相关
CLogicSocket    g_socket;                   //socket全局对象
CThreadPool     g_threadpool;               //线程池全局对象

// 和进程有关的全局量
pid_t           ngx_pid;                    //当前进程的pid
pid_t           ngx_parent;                 //父进程的pid
int             g_daemonized;               //是否启用守护进程
int             g_stopEvent;                //标志程序退出，0不退出，1退出
int             ngx_process;                //进程类型，比如master / worker
sig_atomic_t    ngx_reap;                   //标记子进程状态变化，一般是子进程发来SIGCHLD信号表示退出
                                            //sig_atomic_t  : 系统定义


HandlerFactory g_handlerFactory;


int main(int argc, char *const *argv){
    int exitcode = 0;
    int i = 0;

    //（0）先初始化的变量
    g_stopEvent = 0;

    // (1) 无伤大雅也不需要释放的放在最上面
    ngx_pid     = getpid();                 //取得进程的pid
    ngx_parent  = getppid();                //取得父进程的id          

    //统计argv所占内存
    g_argvneedmem = 0;
    for(i=0;i<argc;i++){
        g_argvneedmem += strlen(environ[i]) + 1;
    }
    //统计环境变量所占的内存。注意判断方法是environ[i]是否为空作为环境变量结束标记
    for(i = 0; environ[i]; i++) 
    {
        g_envneedmem += strlen(environ[i]) + 1; //+1是因为末尾有\0,是占实际内存位置的，要算进来
    } //end for

    // 初始化全局量
    g_os_argc = argc;                       //保存参数个数
    g_os_argv = (char **) argv;             //保存参数指针
    ngx_log.fd = -1;                        //-1 表示日志文件尚未打开
    ngx_process = NGX_PROCESS_MASTER;   
    ngx_reap = 0;

    //业务处理逻辑类在这里new, 这里可以通过使用配置文件来决定有哪些服务函数
    //不过c++没有反射机制，注册起来还是一个问题。。。
    IRequestHandler* loginHandler = new LoginHandler();
    IRequestHandler* registerHandler = new RegisterHandler();
    IRequestHandler* pingHandler = new PingHandler();
    g_handlerFactory.setHandler(std::string("0"), pingHandler);
    g_handlerFactory.setHandler(std::string("5"), loginHandler);
    g_handlerFactory.setHandler(std::string("6"), registerHandler);


    // (2) 初始化，如果初始化失败就直接退出
    CConfig *p_config = CConfig::GetInstance();
    if(p_config->Load("/home/ubuntu/study_cpp/mztkn_study_nginx/nginx/nginx.conf")==false){
        ngx_log_stderr(0, "配置文件[%s]载入失败， 退出!", "nginx.conf");
        exitcode = 2;  //标记找不到文件
        goto lblexit;
    }

    //收包用的单例内存类在这里初始化
    CMemory::GetInstance();	
    CCRC32::GetInstance();

    //一些初始化函数
    ngx_log_init();        //日志初始化（创建/打开日志文件）
    if(ngx_init_signals()!=0){
        exitcode = 1;
        goto lblexit;
    }

    //初始化监听端口
    if(g_socket.Initialize() == false){
        exitcode = 1;
        goto lblexit;
    }

    //设置新标题，这之前应该保证所有命令行参数都不用了
    ngx_init_setproctitle();  //把环境变量搬家

    // 创建守护进程
    if(p_config->GetIntDefault("Daemon",0) == 1){
        int cdaemonresult = ngx_daemon();
        if(cdaemonresult == -1){
            exitcode = 1;
            goto lblexit;
        }
        if(cdaemonresult == 1){
            //原始父进程
            freeresource();
            exitcode = 0;
            return exitcode;    //整个进程直接在这里退出
        }
        g_daemonized = 1;       //守护进程标记
    }

    //不管是父进程还是子进程，都在这个函数中循环往复
    ngx_master_process_cycle();


lblexit:
    freeresource(); //释放一些资源
    ngx_log_stderr(0, "程序退出，再见！\n");
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