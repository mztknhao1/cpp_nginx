#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"


CSocket::CSocket():m_ListenPortCount(1),m_worker_connections(1024){

}

void CSocket::ReadConf(){
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections = p_config->GetIntDefault("worker_connections", m_worker_connections);
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount", m_ListenPortCount);
}


CSocket::~CSocket(){
    std::vector<lpngx_listening_t>::iterator pos;
    for(pos = m_ListenSocketList.begin();pos!=m_ListenSocketList.end();++pos){
        delete (*pos);
    }
    m_ListenSocketList.clear();
    return;
}


// 初始化函数【fork()子进程之前干这个事情】
// 成功返回true, 失败返回false
bool CSocket::Initialize(){
    ReadConf();
    return ngx_open_listening_sockets();
}


// 在创建worker进程之前就要执行这个函数
bool CSocket::ngx_open_listening_sockets(){
    int                 isock;        //socket
    struct  sockaddr_in serv_addr;    //服务器的地址结构体
    int                 iport;        //端口
    char                strinfo[100]; //临时字符串
    
    CConfig *p_config = CConfig::GetInstance();

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
            close(isock);   // 如果前面的已经分配了，而这里退出了程序，会有一定的内存泄漏
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
    
    
    return true;
}

bool CSocket::setnonblocking(int sockfd){
    int nb=1;   // 0：清除；1：设置
    if(ioctl(sockfd, FIONBIO, &nb) == -1){
        return false;
    }
    return true;
}




// 关闭socket
void CSocket::ngx_close_listening_sockets(){
    for(int i=0;i < m_ListenPortCount; i++){
        close(m_ListenSocketList[i]->fd);
        ngx_log_error_core(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_ListenSocketList[i]->fd);
    }
    return;
}

int CSocket::ngx_epoll_init(){

    //(1) 创建一个epoll对象，创建了一个红黑树和一个双向链表
    m_epollhandle = epoll_create(m_worker_connections);
    if(m_epollhandle == -1){
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中epoll_create()失败.");
        exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
    }

    //(2) 链接池【数组】、创建出来，后面用于处理所有的客户端连接
    m_connection_n = m_worker_connections;      //记录当前连接池中连接总数
    //连接池【数组，每个元素是一个对象】
    m_pconnections = new ngx_connection_t[m_connection_n];  // new 不可以失败，失败了直接报异常，不用判断

    int i = m_connection_n;                     //连接池中的连接数
    lpngx_connection_t  next = nullptr;
    lpngx_connection_t  c = m_pconnections;     //连接池数组首地址

    // 下面在把链表串起来,链表的目的是把空闲的连接池连起来，每次释放就加入，增加就改变空闲链表，这是为了快速找到空闲连接池中的地方
    do{
        i--;

        c[i].data = next;
        c[i].fd   = -1;
        c[i].instance = 1;
        c[i].iCurrsequence = 0;

        next = &c[i];
    }while(i);

    // 因为现在还没有任何链接，所以空闲的值为最大值
    m_pfree_connections = next;
    m_free_connection_n = m_connection_n;

    //(3) 遍历所有监听端口，为每个监听socket增加一个连接池中的连接【让一个个的socket和一个内存绑定，方便记录socket相关的数据】
    std::vector<lpngx_listening_t>::iterator pos;
    for(pos = m_ListenSocketList.begin(); pos!=m_ListenSocketList.end();++pos){
        c = ngx_get_connection((*pos)->fd);
        if(c == nullptr){
            //这是致命问题，刚开始怎么可能连接池就为空呢？
            ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
        }

        c->listening = (*pos);      //连接对象 和监听对象关联，方便通过连接对象找监听对象
        (*pos)->connection = c;     //监听对象 和连接对象关联，方便通过监听对象找连接对象

        
        // 对监听端口设置事件处理方法，因为监听端口是用来等对方连接的发送三次握手的，所以监听端口关心的是读事件
        c->rhandler = &CSocket::ngx_event_accept;

        // 往监听socket上增加监听事件， 从而开始让监听端口履行职责
        //【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
    
        if(ngx_epoll_add_event((*pos)->fd,
                                1,0,
                                0,
                                EPOLL_CTL_ADD,
                                c
                                ) == -1)
        {
            exit(2);        //有问题，直接退出，日志已经谢过了（在ngx_epoll_add_event中写日志)
        }
    } // end for
    return 1;
}

// epoll 增加事件，可能被ngx_epoll_init()等函数调用,把监听端口感兴趣的事件增加到epoll管理的（红黑树）中去
// fd: 句柄，一个socket
// readevent: 表示是否是个读事件，0是，1不是
// writeevent: 表示是否是个写事件，0是，1不是
// otherflag: 其他额外补充标记
// eventtype: 事件类型，一般就用系统的枚举值，增加，删除，修改
// c : 对应连接池的指针
// 返回值： 成功返回1，否则返回-1
int CSocket::ngx_epoll_add_event(int fd, 
                                 int readevent, int writeevent,
                                 uint32_t otherflag,
                                 uint32_t eventtype,
                                 lpngx_connection_t c
                                )
{
    struct epoll_event ev;

    memset(&ev, 0, sizeof(ev));

    if(readevent==1){
        //读事件
        // EPOLLRDHUP 客户端关闭连接，断连
        ev.events = EPOLLIN|EPOLLRDHUP; //EPOLLIN读事件，也就是read ready【客户端三次握手连接进来】


    }else{
        //其他事件类型待处理
        //...
    }

    if(otherflag != 0){
        ev.events |= otherflag;
    }

    //event可以指定一段内存，将来通过ev.data.ptr可以把这段内存得到，nginx给了一个技巧，把地址和最后的instance或起来，地址最低位一定不是1，所以可以用这个来判断状态
    //比如c是个地址，可能是0x00af8578, 对应的二进制101011110000010101111000 | 0x1
    ev.data.ptr = (void *)((uintptr_t)c | c->instance); //把连接的地址保存，并通过或1，最后一位可置1或0，获得c->instance状态

    if(epoll_ctl(m_epollhandle, eventtype, fd, &ev)==-1){
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_add_event()中epoll_ctl(%d,%d,%d,%u,%u)失败.",fd,readevent,writeevent,otherflag,eventtype);
        //exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦，后来发现不能直接退；
        return -1;
    }
    return 1;
}


// 本函数被ngx_process_events_and_timers()调用，这个函数是子进程不断调用的
// 正常返回1， 有问题返回-1
// 参数unsigned int timer；epoll_wait()阻塞时长，单位为毫秒
int CSocket::ngx_epoll_process_events(int timer){
    // 等待事件，事件会返回到m_events中，最多返回NGX_MAX_EVENTS事件
    // timer = -1 表示一直阻塞，如果timer为0则表示立即返回
    // 如果两次调用epoll_wait()的事件间隔比较长，则可能在epoll双向链表中，积累了多个事件
    // epoll_wait返回的可能原因：1）阻塞时间到达 2）阻塞期间收到时间 3）调用时有事件 4）收到中断信号
    int events = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);

    if(-1 == events){
        //有错误发生，发送某个信号给本进程

        // #define EINTR 4   这个信号问题不是很大，正常返回
        if(errno == EINTR){
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 1;  //正常返回
        }
        else{
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 0;  //非正常返回 
        }
    }

    if(0 == events){
        // 超时，但是没有事件来
        if(timer != -1){
            // 阻塞一段事件，正常返回
            return 1;
        }
        // 无线等待，但却没有返回任何事件，这里不正常
        ngx_log_error_core(NGX_LOG_ALERT,0,"CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
        return 0; //非正常返回 
    }

    // 会惊群，一个telnet上来，四个worker进程都会被惊动
    // ngx_log_stderr(errno, "惊群测试1")

    // 走到这里，就表示有事件收到了
    lpngx_connection_t  c;
    uintptr_t           instance;
    uint32_t            revents;

    // 遍历本次epoll_wait()返回的事件
    for(int i = 0;i<events;++i){
        c = (lpngx_connection_t)(m_events[i].data.ptr);
        instance = (uintptr_t)c & 1;
        c = (lpngx_connection_t)((uintptr_t)c & (uintptr_t)~1); //把最后一位去除，得到真正的地址


        // 过滤过期事件
        if(c->fd == -1){    //一个套接字，当关联一个 连接池中的连接【对】时
            // 比如我们使用epoll_wait取得3个事件，但是由于业务需要，
            // 我们把第1个事件连接关闭
            
            //这里可以增加个日志，也可以不增加日志
            ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了fd=-1的过期事件:%p.",c); 
            continue; //这种事件就不处理即可
        }


        // 能走到这里，我们认为这些事件都没过期，正常处理
        revents = m_events[i].events;   //取出事件类型
        if(revents & (EPOLLERR | EPOLLHUP)){
            // 如果发生了错误，或者客户端断连
            // 这里加上读写标记，方便后续代码处理
            revents |= EPOLLIN | EPOLLOUT;
        }

        if(revents & EPOLLIN){
            // 如果是读事件
            // ngx_log_stderr(errno,"数据来了来了来了 ~~~~~~~~~~~~~.");
            //一个客户端新连入，这个会成立，
            //已连接发送数据来，这个也成立；
            //c->r_ready = 1;               //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
            // 下面是个函数调用
            (this->*(c->rhandler))(c);      // c->rhandler ，对于连接事件来说，是CSocket::ngx_event_accept
                                            // *(c->rhandler)是解引用，表示调用这个函数
                                            // 但是这个函数是CSocket成员函数，所以需要用this->*(c->rhandler)取出
        }

        if(revents & EPOLLOUT){
            // 如果是写事件

            //TODO 待扩展

            ngx_log_stderr(errno, "1111111111111111111");
        }

        return 1;


    }

}