#ifndef __NGX_C_SOCKET_H__
#define __NGX_C_SOCKET_H__

#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <atomic>
#include <semaphore.h>      //信号量
#include <list>
#include "ngx_comm.h"

// 一些专用的宏定义
#define NGX_LISTEN_BACKLOG 511
#define NGX_MAX_EVENTS     512

typedef struct ngx_listening_s  ngx_listening_t, *lpngx_listening_t;
typedef struct ngx_connection_s ngx_connection_t, *lpngx_connection_t;
typedef struct msg_header_s     msg_header_t, *lpmsg_header_t;
typedef class CSocket           CSocket;
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
    ngx_connection_s();
    virtual ~ngx_connection_s();
    void    GetOneToUse();
    void    PutOneToFree();
	
	int                       fd;                                           //套接字句柄socket
	lpngx_listening_t         listening;                                    //如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的那个lpngx_listening_t的内存首地址		

	//------------------------------------	
	//unsigned                  instance:1;                                   //【位域】失效标志位：0：有效，1：失效【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】  
	uint64_t                  iCurrsequence;                                //引入的一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包，具体怎么用，用到了再说
	struct sockaddr           s_sockaddr;                                   //保存对方地址信息用的
	//char                      addr_text[100];                             //地址的文本信息，100足够，一般其实如果是ipv4地址，255.255.255.255，其实只需要20字节就够

	//和读有关的标志-----------------------
	//uint8_t                   r_ready;                                    //读准备好标记【暂时没闹明白官方要怎么用，所以先注释掉】
	//uint8_t                   w_ready;                                      //写准备好标记

	ngx_event_handler_pt      rhandler;                                     //读事件的相关处理方法
	ngx_event_handler_pt      whandler;                                     //写事件的相关处理方法

    //和用户操作有关的互斥量
    pthread_mutex_t           logicProcMutex;                               //逻辑处理相关互斥量

    //和epoll事件相关
    uint32_t                  events;                                       //和epoll事件相关

    //和收包状态相关----------------------
    unsigned char             curStat;                                      //当前收包状态
    char                      dataHeadInfo[_DATA_BUFSIZE_];                 //用于保存收到的数据的头信息
    char                      *precvbuf;                                    //接收数据的缓冲区的头指针，对收到的不全的包非常有用
    unsigned    int           irecvlen;                                     //要收到多少数据
    char                      *precvMemPointer;                             //new出来的用于收包的内存首地址
	
    //和发包状态有关----------------------
    std::atomic<int>          iThrowsendCount;                                  //发送消息，如果发送缓冲区满了，则需要通过epoll时间驱动消息继续发送；用这个变量来标记
    char                      *psendMemPointer;                             //发送完以后释放用的，整个数据的头指针
    char                      *psendbuf;                                    //发送数据的缓冲区头指针
    unsigned int              isendLen;                                     //要发送多少数据

    //和回收相关
    time_t                    inRecyTime;                                   //入到资源回收占里去的时间

	//--------------------------------------------------
	lpngx_connection_t        next;                                         //这是个指针【等价于传统链表里的next成员：后继指针】，指向下一个本类型对象，用于把空闲的连接池对象串起来构成一个单向链表，方便取用
};

// 消息头，引入的目的是当收到数据包时，额外记录一些内容以备用-------------------------------------------
struct msg_header_s{
    lpngx_connection_t  pConn;                                              //指向连接结构体
    uint64_t            iCurrsequence;                                      //收到数据包时记录对应连接序号，将来能用于比较是否连接已经作废
};




// socket相关类-------------------------------------------------------------------------------------
class CSocket
{
public:
    CSocket();             
    virtual ~CSocket();
    virtual bool Initialize();                                          //初始化函数
    virtual bool initializeSubproc();                                   //子进程才需要执行这个函数
    virtual void shutdownSubproc();                                     //子进程中才执行这个函数
    virtual void threadRecvProcFunc(char *pMsgBuf);                     //处理客户端请求，虚函数
    

    //epoll 相关 初始化、添加事件和处理事件
    int ngx_epoll_init();                                               //初始化epoll，利用epoll_create()创建epoll对象
    int ngx_epoll_process_events(int timer);                            //epoll等待接收和处理事件
    int ngx_epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpngx_connection_t pConn);
                                                                        //epoll操作事件
    //TODO数据发送相关
    void msgSend(char *psendbuf);

private:
    void ReadConf();                                                    //读取各种配置项
    bool ngx_open_listening_sockets();                                  //监听必须的端口
    void ngx_close_listening_sockets();                                 //关闭监听套接字
    bool setnonblocking(int sockfd);                                    //设置非阻塞套接字


    //一些业务处理函数handler
    void ngx_event_accept(lpngx_connection_t oldc);
    void ngx_read_request_handler(lpngx_connection_t c);
    void ngx_write_request_handler(lpngx_connection_t c);
    void ngx_close_connection(lpngx_connection_t c);

    ssize_t recvproc(lpngx_connection_t c, char *buff, ssize_t buflen); //接收从客户端来的数据
    void    ngx_read_request_handler_proc_p1(lpngx_connection_t c);     //包头收完整后的处理，包处理阶段1
    void    ngx_read_request_handler_proc_plast(lpngx_connection_t c);  //收到一个完整包后的处理
    void    clearMsgSendQueue();                                        //清理接收消息队列

    ssize_t sendproc(lpngx_connection_t c, char *buff, ssize_t size);   //TODO发送数据到客户端

    //获取对端信息相关
    size_t  ngx_sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len);

    //连接池或连接相关
    void                initConnection();                               //初始化连接池
    void                clearConnection();                              //释放连接池
    lpngx_connection_t  ngx_get_connection(int isock);                  //从连接池获取一个空闲连接
    void                ngx_free_connection(lpngx_connection_t pConn);  //释放一个连接
    void                inRecyConnectQueue(lpngx_connection_t pConn);   //将要回收的连接放到队列中来

    //线程相关函数
    static  void*       serverRecyConnectionThread(void *threadData);   //专门用来回收连接队列中的待回收线程
    static  void*       serverSendQueueThread(void *threadData);

protected:
    int                                 m_iLenPkgHeader;                //包头占用字节数
    int                                 m_iLenMsgHeader;                //消息头占用字节数

private:
    class  ThreadItem
    {
    public:
        pthread_t                       _Handle;                        //线程句柄
        CSocket                         *_pThis;                        //记录线程池指针
        bool                            ifrunning;                      //标记是否正式启动

        ThreadItem(CSocket *pthis):_pThis(pthis), ifrunning(false){}
        ~ThreadItem(){}
    };
    
    
    int                                 m_ListenPortCount;              //监听端口数量
    int                                 m_worker_connections;           //每个worker进程监听端口数量
    int                                 m_epollhandle;                  //epoll描述符

    //和连接池相关
    std::list<lpngx_connection_t>       m_connectionList;               //连接列表【连接池】
    std::list<lpngx_connection_t>       m_freeConnectionList;           //空闲连接
    std::atomic<int>                    m_total_connection_n;           //连接池总数
    std::atomic<int>                    m_free_connection_n;            //空闲连接数
    pthread_mutex_t                     m_connectionMutex;              //连接相关互斥量
    pthread_mutex_t                     m_recyconnqueueMutex;           //连接回收相关互斥量
    std::list<lpngx_connection_t>       m_recyConnectionList;           //待释放的连接池
    std::atomic<int>                    m_total_recyconnection_n;       //总的回收连接池中的个数
    int                                 m_RecyConnectionWaitTime;       //等待这么久才回收连接

    std::vector<lpngx_listening_t>      m_ListenSocketList;             //监听套接字队列
    struct epoll_event                  m_events[NGX_MAX_EVENTS];
    

    //消息队列
    std::list<char *>                   m_MsgSendQueue;                 //发送消息队列
    std::atomic<int>                    m_iSendMsgQueueCount;           //发送消息队列大小
    //多线程相关
    std::vector<ThreadItem*>            m_threadVector;                 //?线程容器，和之前的接收请求的线程池不太一样
    pthread_mutex_t                     m_sendMessageQueueMutex;        //发消息队列互斥量
    sem_t                               m_semEventSendQueue;            //发送消息的信号量
};

#endif