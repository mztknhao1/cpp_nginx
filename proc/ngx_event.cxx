/*
 * @Author: your name
 * @Date: 2021-01-01 20:08:58
 * @LastEditTime: 2021-01-12 17:55:12
 * @LastEditors: your name
 * @Description: In User Settings Edit
 * @FilePath: /nginx/proc/ngx_event.cxx
 */
// 和开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"


// 处理网络事件和定时器事件
void ngx_process_events_and_timers(){
    g_socket.ngx_epoll_process_events(-1);  //-1表示卡着等待

    g_socket.printTDInfo();
    
}