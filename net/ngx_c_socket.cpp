#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>

#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"



// 在创建worker进程之前就要执行这个函数
bool CSocket::ngx_open_listening_sockets(){
    CConfig *p_config = CConfig::GetInstance();
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount", m_ListenPortCount);

    int                 isock;        //socket
    struct  sockaddr_in serv_addr;    //服务器的地址结构体
    int                 iport;        //端口
    char                strinfo[100]; //临时字符串
    
    // 初始化相关
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;             //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    for(int i = 0; i < m_ListenPortCount ; ++i){
        // 参数1：AF_INET IPv4
        // 参数2：SOCK_STREAM 使用TCP
        // 参数3：给0
        // 【1】创建socket
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if(isock == -1){
            ngx_log_stderr(errno, "CSocket::Initialize()中socket()失败，i=%d", i);
            return false;
        }

        // setsocketopt(): 设置一些套接字参数选项
        // 参数2：表示级别，和参数3配套使用
        // 参数3：允许重用本地地址
        // 设置SO_REUSEADDR 主要是解决TIME_WAIT的问题
        int reuseaddr = 1;
        if(setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*) &reuseaddr, sizeof(reuseaddr)) == -1){
            ngx_log_stderr(errno, "CSocket::Initialize()中setsockopt()失败，i=%d.",i);
            close(isock);
            return false;
        }

        // 设置该socket为非阻塞
        if(setnonblocking(isock) == false){
            ngx_log_stderr(errno,"CSocekt::Initialize()中setnonblocking()失败,i=%d.",i);
            close(isock);
            return false;            
        }

        // 设置本服务器要监听的地址和端口， 这样客户端才能连接到该地址和端口并发送数据
        strinfo[0] = 0;
        sprintf(strinfo, "ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo, 10000);
        serv_addr.sin_port = htons((in_port_t)iport);       // in_port_t是uint16_t;

        // 【2】 绑定服务器地址结构体
        if(bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1){
            ngx_log_stderr(errno, "CSocket::Initialize()中bind()失败，i=%d", i);
            close(isock);
            return false;
        }

        // 【3】开始监听, 最大已连接数设置为NGX_LISTENBACKLOG
        if(listen(isock, NGX_LISTEN_BACKLOG) == -1){
            ngx_log_stderr(errno, "CSocket::Initialize()中listen()失败, i=%d", i);
            close(isock);
            return false;
        }

        // 成功开启监听，放到监听队列中来
        lpngx_listening_t p_listenSocketItem = new ngx_listening_t;
        memset(p_listenSocketItem, 0, sizeof(ngx_listening_t));
        p_listenSocketItem->port = iport;
        p_listenSocketItem->fd   = isock;
        ngx_log_error_core(NGX_LOG_INFO, 0, "监听端口%d成功!", iport);
        m_ListenSocketList.push_back(p_listenSocketItem);
    }
    
    
    return false;
}