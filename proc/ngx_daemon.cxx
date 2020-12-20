#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <error.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"


// 在worker 子进程之前创建守护进程

int ngx_daemon(){
    switch(fork()){
    case -1:
        ngx_log_error_core(NGX_LOG_EMERG,errno,"Ngx_daemon()中fork()失败!");
        return -1;
    case 0:
        // 子进程，走到这里直接break
        break;
    default:
        //父进程 直接退出exit(0) 希望回到主流程释放一些资源
        return 1;
    }

    ngx_parent = ngx_pid;
    ngx_pid = getpid();

    if(setsid() == -1){     //脱离终端，终端关闭与子进程无关
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中setsid()失败");
        return -1;
    }

    umask(0);       //设置为0， 不让它来限制文件权限


    //(4) 打开黑洞设备，以读写方式打开
    int fd = open("/dev/null", O_RDWR);
    if(fd == -1){
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中open(\"/dev/null\")失败!");
        return -1;
    }
    if(dup2(fd, STDIN_FILENO) == -1){
        // 先关闭STDIN_FILNO，已打开的文件描述符，动它之前，先close
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中dup2(STDOU)失败!");
        return -1;
    }
    if(dup2(fd, STDOUT_FILENO) == -1){
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中dup2(STDOUT)失败！");
        return -1;
    }
    if(fd > STDERR_FILENO){     //fd正常为3，一般都成立
        if(close(fd) == -1){
            ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中close(fd)失败!");
            return -1;
        }
    }
    return 0;
}

