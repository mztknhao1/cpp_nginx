#ifndef __NGX_C_SOCKET_H__
#define __NGX_C_SOCKET_H__

#include <vector>
#include <sys/epoll.h>

// 一些专用的宏定义
#define NGX_LISTEN_BACKLOG 511
#define NGX_MAX_EVENTS     512

typedef struct ngx_listening_s  ngx_listening_t, *lpngx_listening_t;
typedef struct ngx_connection_s ngx_connection_t, *lpngx_connection_t;
typedef class   CSocket         CSocket;

typedef void    (CSocket::*ngx_event_handler_pt)(lpngx_connection_t c);     //成员函数指针

// 一些专用的结构定义在这里，暂时不放在ngx_global.h中
struct ngx_listening_s  //和监听端口有关的结构
{
    int                     port;           // 监听端口号
    int                     fd;             // 套接字句柄

    lpngx_connection_t      connection;     // 连接池中的一个连接，这是个指针
};

// 以下结构表示一个TCP连接【客户端主动发起的,Nginx服务器被动接受的TCP连接】
struct ngx_connection_s{
    int                     fd;             //套接字句柄
    lpngx_listening_t       listening;      //如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的那个lpngx_listening_t的内存首地址

    //----------------------------------
    unsigned                instance:1;     //【位域】 失败标志位：0，有效：1
    uint64_t                iCurrsequence;  //引入一个序号，每次分配出去时+1，此法也可能在一定程度上检测出错包
    struct sockaddr         s_sockaddr;     //保存对方地址信息
    //char                  addr_text[100]; //地址的文本信息

    //和读有关的标志---------------------
    uint8_t                 r_ready;        //读准备好标记
    uint8_t                 w_ready;        //写准备好标记

    ngx_event_handler_pt    rhandler;       //读事件的相关处理方法
    ngx_event_handler_pt    whandler;       //写事件的相关处理方法

    //----------------------------------------------------------
    lpngx_connection_t      data;           //链表指针
};

// socket相关类
class CSocket
{
public:
    CSocket();             
    virtual ~CSocket();

public:
    virtual bool Initialize();                                          //初始化函数
    int  ngx_epoll_init();                                              //初始化epoll，利用epoll_create()创建epoll对象

    //TODO 添加事件和处理事件
    int ngx_epoll_add_event(int fd, int readevent, int writeevent, uint32_t otherflag, uint32_t eventtype, lpngx_connection_t c);
    int ngx_epoll_process_events(int timer);

private:
    bool ngx_open_listening_sockets();                                  //监听必须的端口
    void ngx_close_listening_sockets();                                 //关闭监听套接字
    bool setnonblocking(int sockfd);                                    //设置非阻塞套接字
    void ReadConf();                                                    //读取各种配置项


//TODO
    //一些业务处理函数handler
    void ngx_event_accept(lpngx_connection_t oldc);
    void ngx_wait_request_handler(lpngx_connection_t c);
    void ngx_close_accept_aconnection(lpngx_connection_t c);

    //获取对端信息相关
    size_t  ngx_sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len);

    //连接池或连接相关
    lpngx_connection_t  ngx_get_connection(int isock);                  //从连接池获取一个空闲连接
    void                ngx_free_connection(lpngx_connection_t c);      //释放一个连接

private:
    int                                 m_ListenPortCount;              //监听端口数量
    int                                 m_worker_connections;           //每个worker进程监听端口数量
    int                                 m_epollhandle;                  //epoll描述符

    lpngx_connection_t                  m_pconnections;
    lpngx_connection_t                  m_pfree_connections;

    int                                 m_connection_n;                 //当前进程所有连接对象的总数【连接池大小】
    int                                 m_free_connection_n;            //当前连接池可用大小

    std::vector<lpngx_listening_t>      m_ListenSocketList;             //监听套接字队列

    struct epoll_event                  m_events[NGX_MAX_EVENTS];
};

#endif