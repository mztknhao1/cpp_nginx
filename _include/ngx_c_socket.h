#ifndef __NGX_C_SOCKET_H__
#define __NGX_C_SOCKET_H__

#include <vector>

// 一些专用的宏定义
#define NGX_LISTEN_BACKLOG 511


// 一些专用的结构定义在这里，暂时不放在ngx_global.h中
typedef struct ngx_listening_s  //和监听端口有关的结构
{
    int         port;   // 监听端口号
    int         fd;     // 套接字句柄
}ngx_listening_t, *lpngx_listening_t;

// socket相关类
class CSocket
{
public:
    CSocket();             
    virtual ~CSocket();

public:
    virtual bool Initialize();                                          //初始化函数

private:
    bool ngx_open_listening_sockets();                                  //监听必须的端口
    void ngx_close_listening_sockets();                                 //关闭监听套接字
    bool setnonblocking(int sockfd);                                    //设置非阻塞套接字

private:
    int                                 m_ListenPortCount;              //监听端口数量
    std::vector<lpngx_listening_t>      m_ListenSocketList;             //监听套接字队列

};

#endif