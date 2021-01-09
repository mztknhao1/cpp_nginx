/*
 * @Author: your name
 * @Date: 2021-01-02 11:20:40
 * @LastEditTime: 2021-01-09 12:26:50
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /mztkn_study_nginx/nginx/net/ngx_c_socket_inet.cxx
 */
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

//将socket绑定的地址转换为文本格式【根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度】
//参数sa：客户端的ip地址信息一般在这里。
//参数port：为1，则表示要把端口信息也放到组合成的字符串里，为0，则不包含端口信息
//参数text：文本写到这里
//参数len：文本的宽度在这里记录
size_t CSocket::ngx_sock_ntop(struct sockaddr *sa,int port,u_char *text,size_t len)
{
    struct sockaddr_in   *sin;
    u_char               *p;

    switch (sa->sa_family)
    {
    case AF_INET:
        sin = (struct sockaddr_in *) sa;
        p = (u_char *) &sin->sin_addr;
        if(port)  //端口信息也组合到字符串里
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud:%d",p[0], p[1], p[2], p[3], ntohs(sin->sin_port)); //返回的是新的可写地址
        }
        else //不需要组合端口信息到字符串中
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud",p[0], p[1], p[2], p[3]);            
        }
        return (p - text);
        break;
    default:
        return 0;
        break;
    }
    return 0;
}