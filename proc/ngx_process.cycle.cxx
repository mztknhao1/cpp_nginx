#include <signal.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"
#include "ngx_global.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums, const char *pprocname);
static void ngx_worker_process_cycle(int inum, const char *pprocname);
static void ngx_worker_process_init(int inum);


// 变量声明
static u_char master_process[] = "master process";

// 创建worker子进程
void ngx_master_process_cycle(){
    sigset_t set;        //信号集

    sigemptyset(&set);   //清空信号集

    // 下面这些信号在执行本函数期间不希望被收到，建立fork()子进程时学习这种写法，防止信号干扰
    sigaddset(&set, SIGCHLD);   //子进程状态改变
    sigaddset(&set, SIGALRM);   //定时器超时
    sigaddset(&set, SIGIO);     // 异步I/O
    sigaddset(&set, SIGINT);    //终端中断符
    sigaddset(&set, SIGHUP);    //连接断开
    sigaddset(&set, SIGUSR1);   //用户定义信号
    sigaddset(&set, SIGUSR2);   //用户定义信号
    sigaddset(&set, SIGWINCH);  //终端窗口大小
    sigaddset(&set, SIGTERM);   //终止
    sigaddset(&set, SIGQUIT);   //终端退出符

    //设置此时无法接受的信号
    if(sigprocmask(SIG_BLOCK, &set, NULL) == -1){
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmaks()失败!");
    }

    //首先设置主进程标题
    size_t  size;
    int     i;
    size = sizeof(master_process);
    size += g_argvneedmem;              //argv参数加进来
    if(size < 1000) //长度小于这个才设置标题
    {
        char title[1000] = {0};
        strcpy(title, (const char*)master_process);
        strcat(title," ");
        for(i=0;i<g_os_argc;++i){
            strcat(title,g_os_argv[i]);
        }
        ngx_setproctitle(title);        // 设置主进程的标题
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 启动并开始运行......!",title,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }

    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance();
    int workprocess = p_config->GetIntDefault("WorkerProcesses", 1);
    ngx_start_worker_processes(workprocess);

    //创建子进程后，父进程执行的流程会返回到这里，子进程不会进来

    // 屏蔽字为空，表示不屏蔽任何信号
    sigemptyset(&set);

    for(;;){
        // ngx_log_error_core(0,0,"----这是父进程----，pid为%P",ngx_pid);
    }
    return;
}


// 创建给定数量的子进程，可能要增加参数扩展功能TODO
static void ngx_start_worker_processes(int threadnums){
    int i;
    // master进程走这个for循环，创建threadnums个子进程
    for(i=0;i<threadnums;++i){
        ngx_spawn_process(i, "worker process");
    }
    return;
}

//产生一个子进程
//inum: 进程编号
//pprocname: 子进程名字"worker process"
static int ngx_spawn_process(int inum, const char *pprocname){
    pid_t pid;
    pid = fork();       //分叉，从原来的master进程分成两个分支（原有的master以及新fork()出来的worker)进程
    switch (pid){
        case -1:
            ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_spawn_process()for()失败!");
            return -1;

        case 0:  //子进程分支
            ngx_parent = ngx_pid;       //现在是子进程，父进程变为原来的进程pid
            ngx_pid = getpid();         //重新获取pid
            ngx_worker_process_cycle(inum, pprocname);  //子进程在这个进程中持续进行，不往下走
            break;

        default:
            break;      //这个是父进程分支，直接break;
    }


    //父进程id会走到这里，子进程流程不往下走
    return pid;
}

// worker子进程在这里循环，（无限循环【处理网络事件和定时器事件以对外提供web服务】）
// 子进程分支才会走到这里
// inum：进程编号
static void ngx_worker_process_cycle(int inum, const char *pprocname){
    ngx_worker_process_init(inum);  //重新为子进程设置进程名，不与父进程重复
    ngx_setproctitle(pprocname);
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 启动并开始运行......!",pprocname,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志

    for(;;){                        //子进程在这里循环
    
        // 处理网络事件和定时器事件
        ngx_process_events_and_timers();
    
    }
}


// 子进程创建时调用本函数进行一些初始化工作
static void ngx_worker_process_init(int inum){
    sigset_t    set;        
    sigemptyset(&set);          // 子进程先把信号集清空->不屏蔽任何信号，允许接收所有信号

    if(sigprocmask(SIG_SETMASK, &set, NULL) == -1){
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_worker_process_init()中sigprocmask()失败!");
    }

    // 已经做好了三次握手的准备
    g_socket.ngx_epoll_init();

    //TODO 扩充代码

    return;
}
