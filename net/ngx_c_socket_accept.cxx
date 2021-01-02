#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

// 建立连接专用函数，当新连接进入，本函数会被ngx_epoll_process_events()所调用
void CSocket::ngx_event_accept(lpngx_connection_t oldc){
    //listen套接字上用的不是ET【边缘触发】，而是LT【水平触发】，意味着哭护短连入如果我要不处理，这个函数会被多次调用，所以这里可以不比
    //多次执行accept()，可以只执行一次accept()
    //本函数应该尽快返回
    struct sockaddr     mysockaddr;         //远端服务器的sock地址
    socklen_t           socklen;
    int                 err;
    int                 level;
    int                 s;                  //用户端的socket
    static  int         use_accept4 = 1;
    lpngx_connection_t  newc;

    socklen = sizeof(mysockaddr);

    do{
        if(use_accept4){
            //这里就说明了，通过epoll_wait和事件驱动技术，得到olc->fd来了事件，
            //那么就调用accept来接收客户端的套接字
            //然后将获得的套接字以及相关信息，保存在连接池中
            //再然后将这个套接字有关的事件，放到epoll红黑树中监听
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);  
        }
        else{
            s = accept(oldc->fd, &mysockaddr, &socklen);    
        }

        if(s == -1){
            err = errno;

            if(err == EAGAIN){
                //accept()还没准备好
                return;
            }

            level = NGX_LOG_ALERT;
            if(err == ECONNABORTED){
                //"软件引起的连接终止"
                level = NGX_LOG_ERR;
            }else if(err == EMFILE || err == ENFILE){
                //EMFILE: 进程的fd已经耗尽
                level = NGX_LOG_CRIT;
            }
            ngx_log_error_core(level,errno,"CSocekt::ngx_event_accept()中accept4()失败!");

            if(use_accept4 && err == ENOSYS){
                //use_accept4在系统中没有实现
                use_accept4 = 0;
                continue;
            }

            if(err == ECONNABORTED){
                // 对方关闭连接
                // do nothing
            }

            if(err == EMFILE || err == ENFILE){
                //TODO 暂时不做处理
            }
            return;
        }//end for(s == -1)

        //走到这，表示accept4()成功
        newc = ngx_get_connection(s);       //针对新用户连接，加入到连接池
        if(newc == NULL){
            //连接池连接不够用，那就要把这个socket直接关闭并返回
            if(close(s) == -1){
                ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()中close(%d)失败!",s);                
            }
        }

        //...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

        memcpy(&newc->s_sockaddr,&mysockaddr,socklen);  //拷贝服务端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】

        if(!use_accept4){
            //如果用的不是accept4()，那么就要单独设置非阻塞
            if(setnonblocking(s) == false){
                ngx_close_accepted_connection(newc); //回收连接池中的连接（千万不能忘记），并关闭socket
                return; //直接返回
            }
        }

        newc->listening = oldc->listening;
        newc->w_ready = 1;

        newc->rhandler = &CSocket::ngx_wait_request_handler;    //设置数据来时的读处理函数

        // 下面将读事件加epoll监控

        if(ngx_epoll_add_event(s,
                                1, 0,                       //读，写，这里读设置为1
                                EPOLLET,                    //额外选项 EPOLLET 边缘出发
                                EPOLL_CTL_ADD,
                                newc) == -1){
            ngx_close_accepted_connection(newc);//回收连接池中的连接（千万不能忘记），并关闭socket
            return; //直接返回
        }

        break;      //一般循环一次就出去
    }while(1);

    return;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
void CSocket::ngx_close_accepted_connection(lpngx_connection_t c)
{
    int fd = c->fd;
    ngx_free_connection(c);
    c->fd = -1; //官方nginx这么写，这么写有意义；
    if(close(fd) == -1)
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_close_accepted_connection()中close(%d)失败!",fd);  
    }
    return;
}
