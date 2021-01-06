#ifndef __NGX_C_SOCKET_H__
#define __NGX_C_SOCKET_H__

#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <list>
#include "ngx_comm.h"

// 一些专用的宏定义
#define NGX_LISTEN_BACKLOG 511
#define NGX_MAX_EVENTS     512

typedef struct ngx_listening_s  ngx_listening_t, *lpngx_listening_t;
typedef struct ngx_connection_s ngx_connection_t, *lpngx_connection_t;
typedef class   CSocket         CSocket;
typedef struct msg_header_s     msg_header_t, *lpmsg_header_t;

typedef void    (CSocket::*ngx_event_handler_pt)(lpngx_connection_t c);     //成员函数指针

// 一些专用的结构定义在这里，暂时不放在ngx_global.h中
struct ngx_listening_s  //和监听端口有关的结构
{
    int                     port;           // 监听端口号
    int                     fd;             // 套接字句柄

    lpngx_connection_t      connection;     // 连接池中的一个连接，这是个指针
};

//以下三个结构是非常重要的三个结构，我们遵从官方nginx的写法；
//(1)该结构表示一个TCP连接【客户端主动发起的、Nginx服务器被动接受的TCP连接】
struct ngx_connection_s
{
	
	int                       fd;             //套接字句柄socket
	lpngx_listening_t         listening;      //如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的那个lpngx_listening_t的内存首地址		

	//------------------------------------	
	unsigned                  instance:1;     //【位域】失效标志位：0：有效，1：失效【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】  
	uint64_t                  iCurrsequence;  //我引入的一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包，具体怎么用，用到了再说
	struct sockaddr           s_sockaddr;     //保存对方地址信息用的
	//char                      addr_text[100]; //地址的文本信息，100足够，一般其实如果是ipv4地址，255.255.255.255，其实只需要20字节就够

	//和读有关的标志-----------------------
	//uint8_t                   r_ready;        //读准备好标记【暂时没闹明白官方要怎么用，所以先注释掉】
	uint8_t                   w_ready;        //写准备好标记

	ngx_event_handler_pt      rhandler;       //读事件的相关处理方法
	ngx_event_handler_pt      whandler;       //写事件的相关处理方法

    //和收包状态相关----------------------
    unsigned char             curStat;                          //当前收包状态
    char                      dataHeadInfo[_DATA_BUFSIZE_];     //用于保存收到的数据的头信息
    char                      *precvbuf;                        //接收数据的缓冲区的头指针，对收到的不全的包非常有用
    unsigned    int           irecvlen;                         //要收到多少数据
    bool                      ifnewrecvMem;                     //如果成功收到包头，那么我们就要分配内存
    char                      *pnewMemPointer;                  //new出来的用于收包的内存首地址
	
	//--------------------------------------------------
	lpngx_connection_t        data;           //这是个指针【等价于传统链表里的next成员：后继指针】，指向下一个本类型对象，用于把空闲的连接池对象串起来构成一个单向链表，方便取用
};

// 消息头，引入的目的是当收到数据包时，额外记录一些内容以备用-------------------------------------------
struct msg_header_s{
    lpngx_connection_t  pConn;              //指向连接结构体
    uint64_t            iCurrsequence;      //收到数据包时记录对应连接序号，将来能用于比较是否连接已经作废
};




// socket相关类-------------------------------------------------------------------------------------
class CSocket
{
public:
    CSocket();             
    virtual ~CSocket();
    virtual bool Initialize();                                          //初始化函数
    virtual void threadRecvProcFunc(char *pMsgBuf);
    
    int  ngx_epoll_init();                                              //初始化epoll，利用epoll_create()创建epoll对象

    //添加事件和处理事件
    int ngx_epoll_add_event(int fd, int readevent, int writeevent, uint32_t otherflag, uint32_t eventtype, lpngx_connection_t c);
    int ngx_epoll_process_events(int timer);

private:
    bool ngx_open_listening_sockets();                                  //监听必须的端口
    void ngx_close_listening_sockets();                                 //关闭监听套接字
    bool setnonblocking(int sockfd);                                    //设置非阻塞套接字
    void ReadConf();                                                    //读取各种配置项


    //一些业务处理函数handler
    void ngx_event_accept(lpngx_connection_t oldc);
    void ngx_wait_request_handler(lpngx_connection_t c);
    void ngx_close_connection(lpngx_connection_t c);

    ssize_t recvproc(lpngx_connection_t c, char *buff, ssize_t buflen); //接收从客户端来的数据
    void    ngx_wait_request_handler_proc_p1(lpngx_connection_t c);     //包头收完整后的处理，包处理阶段1
    void    ngx_wait_request_handler_proc_plast(lpngx_connection_t c);  //收到一个完整包后的处理
    void    inMsgRecvQueue(char *buf);                                  //收到一个完整消息后，进入消息队列
    void    tmpoutMsgRecvQueue();                                       //临时测试用函数
    void    clearMsgRecvQueue();                                        //清理接收消息队列



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

    int                                 m_iLenPkgHeader;                //包头占用字节数
    int                                 m_iLenMsgHeader;                //消息头占用字节数

    //消息队列
    std::list<char *>                   m_MsgRecvQueue;                 //接收消息的队列

    //多线程相关
    pthread_mutex_t                     m_recvMessageQueueMutex;        //收消息队列互斥量


};

#endif