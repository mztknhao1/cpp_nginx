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
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//--------------------------------------------------------------------------
//构造函数
CSocket::CSocket()
{
    //配置相关
    m_worker_connections = 1;                               //epoll连接最大项数
    m_ListenPortCount = 1;                                  //监听一个端口
    m_RecyConnectionWaitTime = 60;                          //等待这么些秒后才回收连接

    //epoll相关
    m_epollhandle = -1;                                     //epoll返回的句柄

    //一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_iLenPkgHeader = sizeof(comm_pkg_header_t);            //包头的sizeof值【占用的字节数】
    m_iLenMsgHeader =  sizeof(msg_header_t);                //消息头的sizeof值【占用的字节数】 

    //各种队列相关
    m_iSendMsgQueueCount = 0;                               //发消息队列大小
    m_total_recyconnection_n = 0;                           //待释放连接队列大小

    //在线用户相关
    m_onlineUserCount        = 0;
    m_lastprintTime          = 0;     //上次打印统计信息的时间，先给0
    m_iDiscardSendPkgCount   = 0;

    return;	
}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CSocket::Initialize()
{
    ReadConf();  //读配置项
    if(ngx_open_listening_sockets() == false)  //打开监听端口    
        return false;  
    return true;
}

//子进程中才需要执行的初始化函数
bool CSocket::initializeSubproc()
{
    //发消息互斥量初始化
    if(pthread_mutex_init(&m_sendMessageQueueMutex, NULL)  != 0)
    {        
        ngx_log_stderr(0,"CSocket::Initialize()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        return false;    
    }
    //连接相关互斥量初始化
    if(pthread_mutex_init(&m_connectionMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocket::Initialize()中pthread_mutex_init(&m_connectionMutex)失败.");
        return false;    
    }    
    //连接回收队列相关互斥量初始化
    if(pthread_mutex_init(&m_recyconnqueueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocket::Initialize()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
        return false;    
    }

    //时间队列相关互斥量初始化
    if(pthread_mutex_init(&m_timequeueMutex, NULL) != 0){
        ngx_log_stderr(0, "CSocket::Initialize()中pthread_mutex_init(&m_timequeueMutex)失败.");
        return false;
    } 
   
    //初始化发消息相关信号量，信号量用于进程/线程 之间的同步，虽然 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，但
    //这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
    //第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
    //第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
    if(sem_init(&m_semEventSendQueue,0,0) == -1)
    {
        ngx_log_stderr(0,"CSocket::Initialize()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }

    //创建线程
    int err;
    //创建发送消息线程
    ThreadItem *pSendQueue = new ThreadItem(this);
    m_threadVector.push_back(pSendQueue);
    err = pthread_create(&pSendQueue->_Handle, NULL, serverSendQueueThread, pSendQueue);
    if(err != 0){
        return false;
    }

    //创建时间监视线程
    ThreadItem *pTimeQueue = new ThreadItem(this);
    m_threadVector.push_back(pTimeQueue);
    err = pthread_create(&pTimeQueue->_Handle, NULL, serverTimerQueueMonitorThread, pTimeQueue);
        if(err != 0)
    {
        return false;
    }

    //创建连接回收线程
    ThreadItem *pRecyconn = new ThreadItem(this);
    m_threadVector.push_back(pRecyconn); 
    err = pthread_create(&pRecyconn->_Handle, NULL, serverRecyConnectionThread,pRecyconn);
    if(err != 0)
    {
        return false;
    }
    return true;
}


//--------------------------------------------------------------------------
//释放函数
CSocket::~CSocket()
{
    //释放必须的内存
    //(1)监听端口相关内存的释放--------
    std::vector<lpngx_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) //vector
	{		
		delete (*pos); //一定要把指针指向的内存干掉，不然内存泄漏
	}//end for
	m_ListenSocketList.clear();    
    return;
}

//关闭退出函数[子进程中执行]
void CSocket::shutdownSubproc()
{
    //(0)在程序退出之前，可能需要让sem+1，让线程退出
    if(sem_post(&m_semEventSendQueue)==-1){
        ngx_log_stderr(0, "CSocket::Shutdown_subproc()中的sem_post失败.");
    }


    //(1)把干活的线程停止掉，注意 系统应该尝试通过设置 g_stopEvent = 1来 开始让整个项目停止
    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); //等待一个线程终止
    }
    //(2)释放一下new出来的ThreadItem【线程池中的线程】    
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_threadVector.clear();

    //(3)队列相关
    clearMsgSendQueue();
    clearConnection();
    
    //(4)多线程相关    
    pthread_mutex_destroy(&m_connectionMutex);          //连接相关互斥量释放
    pthread_mutex_destroy(&m_sendMessageQueueMutex);    //发消息互斥量释放    
    pthread_mutex_destroy(&m_recyconnqueueMutex);       //连接回收队列相关的互斥量释放
    pthread_mutex_destroy(&m_timequeueMutex);           //时间队列互斥量
    sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放
}

//清理TCP发送消息队列
void CSocket::clearMsgSendQueue()
{
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_MsgSendQueue.empty())
	{
		sTmpMempoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}

//专门用于读各种配置项
void CSocket::ReadConf()
{
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections        = p_config->GetIntDefault("worker_connections",m_worker_connections);              //epoll连接的最大项数
    m_ListenPortCount           = p_config->GetIntDefault("ListenPortCount",m_ListenPortCount);                    //取得要监听的端口数量
    m_RecyConnectionWaitTime    = p_config->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime); //等待这么些秒后才回收连接
    
    m_ifkickTimeCount           = p_config->GetIntDefault("Sock_WaitTimeEnable", 0);
    m_iWaitTime                 = p_config->GetIntDefault("Sock_MaxWaitTime", 0);
    m_iWaitTime                 = (m_iWaitTime > 5)?m_iWaitTime:5;                                                  //建议不低于5秒钟
    m_ifTimeOutKick             = p_config->GetIntDefault("Sock_TimeOutTick", 0);

    m_floodAkEnable             = p_config->GetIntDefault("Sock_FloodAttackKickEnable", 0);
    m_floodTimeInterval         = p_config->GetIntDefault("Sock_FloodTimeInterval", 100);
    m_floodKickCount            = p_config->GetIntDefault("Sock_FloodKickCounter",10);

    
    return;
}

//监听端口【支持多个端口】，这里遵从nginx的函数命名
//在创建worker进程之前就要执行这个函数；
bool CSocket::ngx_open_listening_sockets()
{    
    int                isock;                //socket
    struct sockaddr_in serv_addr;            //服务器的地址结构体
    int                iport;                //端口
    char               strinfo[100];         //临时字符串 
   
    //初始化相关
    memset(&serv_addr,0,sizeof(serv_addr));  //先初始化一下
    serv_addr.sin_family = AF_INET;                //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //监听本地所有的IP地址；INADDR_ANY表示的是一个服务器上所有的网卡（服务器可能不止一个网卡）多个本地ip地址都进行绑定端口号，进行侦听。

    //中途用到一些配置信息
    CConfig *p_config = CConfig::GetInstance();
    for(int i = 0; i < m_ListenPortCount; i++) //要监听这么多个端口
    {        
        //参数1：AF_INET：使用ipv4协议，一般就这么写
        //参数2：SOCK_STREAM：使用TCP，表示可靠连接【相对还有一个UDP套接字，表示不可靠连接】
        //参数3：给0，固定用法，就这么记
        isock = socket(AF_INET,SOCK_STREAM,0); //系统函数，成功返回非负描述符，出错返回-1
        if(isock == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize()中socket()失败,i=%d.",i);
            //其实这里直接退出，那如果以往有成功创建的socket呢？就没得到释放吧，当然走到这里表示程序不正常，应该整个退出，也没必要释放了 
            return false;
        }

        //setsockopt（）:设置一些套接字参数选项；
        //参数2：是表示级别，和参数3配套使用，也就是说，参数3如果确定了，参数2就确定了;
        //参数3：允许重用本地地址
        //设置 SO_REUSEADDR，目的第五章第三节讲解的非常清楚：主要是解决TIME_WAIT这个状态导致bind()失败的问题
        int reuseaddr = 1;  //1:打开对应的设置项
        if(setsockopt(isock,SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(reuseaddr)) == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.",i);
            close(isock); //无需理会是否正常执行了                                                  
            return false;
        }
        //设置该socket为非阻塞
        if(setnonblocking(isock) == false)
        {                
            ngx_log_stderr(errno,"CSocket::Initialize()中setnonblocking()失败,i=%d.",i);
            close(isock);
            return false;
        }

        //设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据        
        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo,10000);
        serv_addr.sin_port = htons((in_port_t)iport);   //in_port_t其实就是uint16_t

        //绑定服务器地址结构体
        if(bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize()中bind()失败,i=%d.",i);
            close(isock);
            return false;
        }
        
        //开始监听
        if(listen(isock,NGX_LISTEN_BACKLOG) == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize()中listen()失败,i=%d.",i);
            close(isock);
            return false;
        }

        //可以，放到列表里来
        lpngx_listening_t p_listensocketitem = new ngx_listening_t; //千万不要写错，注意前边类型是指针，后边类型是一个结构体
        memset(p_listensocketitem,0,sizeof(ngx_listening_t));      //注意后边用的是 ngx_listening_t而不是lpngx_listening_t
        p_listensocketitem->port = iport;                          //记录下所监听的端口号
        p_listensocketitem->fd   = isock;                          //套接字木柄保存下来   
        ngx_log_error_core(NGX_LOG_INFO,0,"监听%d端口成功!",iport); //显示一些信息到日志中
        m_ListenSocketList.push_back(p_listensocketitem);          //加入到队列中
    } //end for(int i = 0; i < m_ListenPortCount; i++)    
    if(m_ListenSocketList.size() <= 0)  //不可能一个端口都不监听吧
        return false;
    return true;
}

//设置socket连接为非阻塞模式【这种函数的写法很固定】：非阻塞，概念在五章四节讲解的非常清楚【不断调用，不断调用这种：拷贝数据的时候是阻塞的】
bool CSocket::setnonblocking(int sockfd) 
{    
    int nb=1; //0：清除，1：设置  
    if(ioctl(sockfd, FIONBIO, &nb) == -1) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
    {
        return false;
    }
    return true;
}

//关闭socket，什么时候用，我们现在先不确定，先把这个函数预备在这里
void CSocket::ngx_close_listening_sockets()
{
    for(int i = 0; i < m_ListenPortCount; i++) //要关闭这么多个监听端口
    {  
        //ngx_log_stderr(0,"端口是%d,socketid是%d.",m_ListenSocketList[i]->port,m_ListenSocketList[i]->fd);
        close(m_ListenSocketList[i]->fd);
        ngx_log_error_core(NGX_LOG_INFO,0,"关闭监听端口%d!",m_ListenSocketList[i]->port); //显示一些信息到日志中
    }//end for(int i = 0; i < m_ListenPortCount; i++)
    return;
}

//主动关闭一个连接时的要做些善后的处理函数
//这个函数是可能被多线程调用的，但是即便被多线程调用，也没关系，不影响本服务器程序的稳定性和正确运行性
void CSocket::zdClosesocketProc(lpngx_connection_t p_Conn)
{
    if(m_ifkickTimeCount == 1)
    {
        DeleteFromTimerQueue(p_Conn);               //从把用户从时间队列中把连接干掉
    }
    if(p_Conn->fd != -1)
    {   
        close(p_Conn->fd);                          //这个socket关闭，关闭后epoll就会被从红黑树中删除，所以这之后无法收到任何epoll事件
        p_Conn->fd = -1;
    }

    if(p_Conn->iThrowsendCount > 0)  
        --p_Conn->iThrowsendCount;                  //归0

    inRecyConnectQueue(p_Conn);                     //扔到待回收连接池中去
    return;
}

//将一个待发送消息入到发消息队列中
void CSocket::msgSend(char *psendbuf) 
{
    CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_sendMessageQueueMutex);  //互斥量

    //发送消息队列过大也可能给服务器带来风险
    if(m_iSendMsgQueueCount > 50000)
    {
        //发送队列过大，比如客户端恶意不接受数据，就会导致这个队列越来越大
        //那么可以考虑为了服务器安全，干掉一些数据的发送，虽然有可能导致客户端出现问题，但总比服务器不稳定要好很多
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
		return;
    }
    
    //总体数据并无风险，不会导致服务器崩溃，要看看个体数据，找一下恶意者了    
    lpmsg_header_t pMsgHeader = (lpmsg_header_t)psendbuf;
	lpngx_connection_t p_Conn = pMsgHeader->pConn;
    if(p_Conn->iSendCount > 400)
    {
        //该用户收消息太慢【或者干脆不收消息】，累积的该用户的发送队列中有的数据条目数过大，认为是恶意用户，直接切断
        ngx_log_stderr(0,"CSocekt::msgSend()中发现某用户%d积压了大量待发送数据包，切断与他的连接！",p_Conn->fd);      
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
        zdClosesocketProc(p_Conn); //直接关闭
		return;
    }

    ++p_Conn->iSendCount; //发送队列中有的数据条目数+1；
    m_MsgSendQueue.push_back(psendbuf);     
    ++m_iSendMsgQueueCount;   //原子操作

    //将信号量的值+1,这样其他卡在sem_wait的就可以走下去
    if(sem_post(&m_semEventSendQueue)==-1)  //让ServerSendQueueThread()流程走下来干活
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");      
    }
    return;
}

//--------------------------------------------------------------------
//(1)epoll功能初始化，子进程中进行 ，本函数被ngx_worker_process_init()所调用
int CSocket::ngx_epoll_init()
{
    //(1)很多内核版本不处理epoll_create的参数，只要该参数>0即可
    //创建一个epoll对象，创建了一个红黑树，还创建了一个双向链表
    m_epollhandle = epoll_create(m_worker_connections);   //直接以epoll连接的最大项数为参数，肯定是>0的； 
    if (m_epollhandle == -1) 
    {
        ngx_log_stderr(errno,"CSocket::ngx_epoll_init()中epoll_create()失败.");
        exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
    }

    //(2)创建连接池【数组】、创建出来，这个东西后续用于处理所有客户端的连接
    initConnection();

    //(3)遍历所有监听socket【监听端口】，我们为每个监听socket增加一个 连接池中的连接【说白了就是让一个socket和一个内存绑定，以方便记录该sokcet相关的数据、状态等等】
    std::vector<lpngx_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
    {
        lpngx_connection_t p_Conn = ngx_get_connection((*pos)->fd); //从连接池中获取一个空闲连接对象
        if (p_Conn == NULL)
        {
            //这是致命问题，刚开始怎么可能连接池就为空呢？
            ngx_log_stderr(errno,"CSocket::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
        }
        p_Conn->listening = (*pos);   //连接对象 和监听对象关联，方便通过连接对象找监听对象
        (*pos)->connection = p_Conn;  //监听对象 和连接对象关联，方便通过监听对象找连接对象

        //rev->accept = 1; //监听端口必须设置accept标志为1  ，这个是否有必要，再研究

        //对监听端口的读事件设置处理方法，因为监听端口是用来等对方连接的发送三路握手的，所以监听端口关心的就是读事件
        p_Conn->rhandler = &CSocket::ngx_event_accept;

        //往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
        if(ngx_epoll_oper_event(
                                (*pos)->fd,         //socekt句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭
                                0,                  //对于事件类型为增加的，不需要这个参数
                                p_Conn              //连接池中的连接 
                                ) == -1) 
        {
            exit(2); //有问题，直接退出，日志 已经写过了
        }
    } //end for 
    return 1;
}

//对epoll事件的具体操作
//返回值：成功返回1，失败返回-1；
int CSocket::ngx_epoll_oper_event(
                        int                fd,               //句柄，一个socket
                        uint32_t           eventtype,        //事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，说白了就是操作epoll红黑树的节点(增加，修改，删除)
                        uint32_t           flag,             //标志，具体含义取决于eventtype
                        int                bcaction,         //补充动作，用于补充flag标记的不足  :  0：增加   1：去掉
                        lpngx_connection_t pConn             //pConn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
                        )
{
    struct epoll_event ev;    
    memset(&ev, 0, sizeof(ev));

    if(eventtype == EPOLL_CTL_ADD) //往红黑树中增加节点；
    {
        //红黑树从无到有增加节点
        // ev.data.ptr = (void *)pConn;
        ev.events = flag;      //既然是增加节点，则不管原来是啥标记
        pConn->events = flag;  //这个连接本身也记录这个标记
    }
    else if(eventtype == EPOLL_CTL_MOD)
    {
        //节点已经在红黑树中，修改节点的事件信息
        ev.events = pConn->events;                          //先把标记恢复
        if(bcaction == 0){
            //增加某个标记
            ev.events |= flag;
        }else if(bcaction == 1){
            //去掉某个标记
            ev.events &= ~flag;
        }else{
            //完全覆盖某个标记
            ev.events = flag;
        }
        pConn->events = ev.events;

    }
    else
    {
        //删除红黑树中节点，目前没这个需求，所以将来再扩展
        return  1;  //先直接返回1表示成功
    } 

    ev.data.ptr = (void*)pConn;

    if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)
    {
        ngx_log_stderr(errno,"CSocket::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,eventtype,flag,bcaction);    
        return -1;
    }
    return 1;
}

//开始获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长，单位是毫秒；
//返回值，1：正常返回  ,0：有问题返回，一般不管是正常还是问题返回，都应该保持进程继续运行
//本函数被ngx_process_events_and_timers()调用，而ngx_process_events_and_timers()是在子进程的死循环中被反复调用
int CSocket::ngx_epoll_process_events(int timer) 
{   
    //等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件【因为我只提供了这些内存】；
    //如果两次调用epoll_wait()的事件间隔比较长，则可能在epoll的双向链表中，积累了多个事件，所以调用epoll_wait，可能取到多个事件
    //阻塞timer这么长时间除非：a)阻塞时间到达 b)阻塞期间收到事件【比如新用户连入】会立刻返回c)调用时有事件也会立刻返回d)如果来个信号，比如你用kill -1 pid测试
    //如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
    //返回值：有错误发生返回-1，错误在errno中，比如你发个信号过来，就返回-1，错误信息是(4: Interrupted system call)
    //       如果你等待的是一段时间，并且超时了，则返回0；
    //       如果返回>0则表示成功捕获到这么多个事件【返回值里】
    int events = epoll_wait(m_epollhandle,m_events,NGX_MAX_EVENTS,timer);
    
    if(events == -1)
    {
        //有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
        //#define EINTR  4，EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
               //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if(errno == EINTR) 
        {
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocket::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 1;  //正常返回
        }
        else
        {
            //这被认为应该是有问题，记录日志
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 0;  //非正常返回 
        }
    }

    if(events == 0) //超时，但没事件来
    {
        if(timer != -1)
        {
            //要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
            return 1;
        }
        //无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
        ngx_log_error_core(NGX_LOG_ALERT,0,"CSocket::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
        return 0; //非正常返回 
    }

    //会惊群，一个telnet上来，4个worker进程都会被惊动，都执行下边这个
    //ngx_log_stderr(errno,"惊群测试1:%d",events); 

    //走到这里，就是属于有事件收到了
    lpngx_connection_t c;
    //uintptr_t          instance;
    uint32_t           revents;
    for(int i = 0; i < events; ++i)    //遍历本次epoll_wait返回的所有事件，注意events才是返回的实际事件数量
    {
        c = (lpngx_connection_t)(m_events[i].data.ptr);           //ngx_epoll_add_event()给进去的，这里能取出来

        //能走到这里，我们认为这些事件都没过期，就正常开始处理
        revents = m_events[i].events;//取出事件类型
        

        if(revents & EPOLLIN)  //如果是读事件
        {
            //ngx_log_stderr(errno,"数据来了来了来了 ~~~~~~~~~~~~~.");
            //一个客户端新连入，这个会成立，
            //已连接发送数据来，这个也成立；
            //c->r_ready = 1;               //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
            (this->* (c->rhandler) )(c);    //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
                                              //如果新连接进入，这里执行的应该是CSocket::ngx_event_accept(c)】            
                                              //如果是已经连入，发送数据到这里，则这里执行的应该是 CSocket::ngx_read_request_handler
        }
        
        if(revents & EPOLLOUT) //如果是写事件【对方关闭连接也触发这个，再研究。。。。。。】，注意上边的 if(revents & (EPOLLERR|EPOLLHUP))  revents |= EPOLLIN|EPOLLOUT; 读写标记都给加上了
        {
            if(revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)){   //客户端关闭，如果服务端挂着一个写事件通知，这个条件可能成立
                --c->iThrowsendCount;
            }else{
                (this->*(c->whandler))(c);
            }

        }
    } //end for(int i = 0; i < events; ++i)
    return 1;
}

void *CSocket::serverSendQueueThread(void *threadData){
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;
    int err;
    std::list<char *>::iterator pos, pos2, posend;

    char *pMsgBuf;
    lpmsg_header_t      pMsgHeader;
    lpcomm_pkg_header_t pPkgHeader;
    lpngx_connection_t  pConn;
    unsigned short      itmp;
    ssize_t             sendsize;

    CMemory *pMemroy = CMemory::GetInstance();

    while(g_stopEvent == 0){         //不退出
        if(sem_wait(&pSocketObj->m_semEventSendQueue) == -1){
            //失败？及时报告，其他的也不好干啥
            if(errno != EINTR) //这个我就不算个错误了【当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。】
                ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");            
        }

        if(g_stopEvent != 0){
            break;
        }

        if(pSocketObj->m_iSendMsgQueueCount > 0){
            err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);
            if(err != 0){
                ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
            }

            pos = pSocketObj->m_MsgSendQueue.begin();
            posend = pSocketObj->m_MsgSendQueue.end();

            while(pos != posend){
                pMsgBuf = (*pos);
                pMsgHeader = (lpmsg_header_t)pMsgBuf;
                pPkgHeader = (lpcomm_pkg_header_t)(pMsgBuf+pSocketObj->m_iLenMsgHeader);
                pConn = pMsgHeader->pConn;

                //包过期
                if(pConn->iCurrsequence != pMsgHeader->iCurrsequence){
                    //本包保存的序列号与pConn中的实际序列号不同，丢弃此消息
                    pos2 = pos;
                    pos++;
                    pSocketObj->m_MsgSendQueue.erase(pos2);
                    --pSocketObj->m_iSendMsgQueueCount;
                    pMemroy->FreeMemory(pMsgBuf);
                    continue;
                }

                if(pConn->iThrowsendCount>0){
                    //靠系统驱动来发消息，所以这里不能再发送
                    pos++;
                    continue;
                }

                //走到这里，可以发送消息，一些必须的信息记录，要发送的东西也要从发送队列中干掉
                pConn->psendMemPointer = pMsgBuf;
                pos2 = pos;
                pos++;
                pSocketObj->m_MsgSendQueue.erase(pos2);
                --pSocketObj->m_iSendMsgQueueCount;
                pConn->psendbuf = (char *)pPkgHeader;
                itmp = ntohs(pPkgHeader->pkgLen);
                pConn->isendLen = itmp;

                // ngx_log_stderr(errno, "即将发送数据%ud",pConn->isendLen);
                //(1)直接用write或send发送数据
                sendsize = pSocketObj->sendproc(pConn, pConn->psendbuf, pConn->isendLen);

                if(sendsize > 0){
                    if(sendsize == pConn->isendLen){        //成功发送出去了数据
                        pMemroy->FreeMemory(pConn->psendMemPointer);
                        pConn->psendMemPointer = NULL;
                        pConn->iThrowsendCount = 0;
                        // ngx_log_stderr(0,"CSocekt::ServerSendQueueThread()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉
                    }else{                                      //没有全部发送完毕
                        pConn->psendbuf = pConn->psendbuf + sendsize;
                        pConn->isendLen = pConn->isendLen - sendsize;

                        //标记缓冲区满了，需要通过epoll事件来驱动消息继续发送
                        ++pConn->iThrowsendCount;
                        if(pSocketObj->ngx_epoll_oper_event(
                            pConn->fd,
                            EPOLL_CTL_MOD,
                            EPOLLOUT,
                            0,
                            pConn
                        )==-1){
                            ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
                        }

                        ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中数据没发送完毕【发送缓冲区满】，整个要发送%d，实际发送了%d。",pConn->isendLen,sendsize);
                    }
                    continue;       //继续处理其它消息
                }
                else if(sendsize==0){
                    pMemroy->FreeMemory(pConn->psendMemPointer);
                    pConn->psendMemPointer = NULL;
                    pConn->iThrowsendCount = 0;
                    continue;
                }
                else if(sendsize==-1){
                    //发送缓冲区满了，且一个字节都没发送出去
                    ++pConn->iThrowsendCount;
                    if(pSocketObj->ngx_epoll_oper_event(
                                pConn->fd,         //socket句柄
                                EPOLL_CTL_MOD,      //事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                pConn              //连接池中的连接
                    ) == -1){
                        //有这情况发生？这可比较麻烦，不过先do nothing
                        ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");
                    }
                    continue;
                }
                else{
                    //能走到这里，一般就是返回值-2，认为断开，等待recv()来做断开回收资源
                    pMemroy->FreeMemory(pConn->psendMemPointer);
                    pConn->psendMemPointer = NULL;
                    pConn->iThrowsendCount = 0;
                    continue;
                }
            }//end while
            err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex);
            if(err != 0){
                ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
            }
        }//if(pSocketObj->m_iSendMsgQueueCount > 0)
    }//end while
    return (void *)0;
}


/**
 * @description: 测试是否是flood攻击 
 * @param {*}
 * @return {*}
 */
bool CSocket::TestFlood(lpngx_connection_t pConn){
    struct timeval  sCurrTime;              //当前时间结构
    uint64_t        iCurrTime;              //当前时间
    bool            reco    = false;        

    gettimeofday(&sCurrTime, NULL);         //取得当前时间
    iCurrTime       =   (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);  //毫秒
    if((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval){
        //两次收到包的时间小于设置的间隔
        pConn->FloodAttackCount++;
        pConn->FloodkickLastTime = iCurrTime;
    }
    else{
        pConn->FloodAttackCount = 0;
        pConn->FloodkickLastTime = iCurrTime;
    }

    if(pConn->FloodAttackCount >= m_floodKickCount){
        reco = true;
    }

    return reco;
}

//打印统计信息
void CSocket::printTDInfo()
{
    //return;
    time_t currtime = time(NULL);
    if( (currtime - m_lastprintTime) > 10)
    {
        //超过10秒我们打印一次
        int tmprmqc = g_threadpool.getRecvMsgQueueCount(); //收消息队列

        m_lastprintTime = currtime;
        int tmpoLUC = m_onlineUserCount;    //atomic做个中转，直接打印atomic类型报错；
        int tmpsmqc = m_iSendMsgQueueCount; //atomic做个中转，直接打印atomic类型报错；
        ngx_log_stderr(0,"------------------------------------begin--------------------------------------");
        ngx_log_stderr(0,"当前在线人数/总人数(%d/%d)。",tmpoLUC,m_worker_connections);        
        ngx_log_stderr(0,"连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。",m_freeConnectionList.size(),m_connectionList.size(),m_recyConnectionList.size());
        ngx_log_stderr(0,"当前时间队列大小(%d)。",m_timerQueuemap.size());        
        ngx_log_stderr(0,"当前收消息队列/发消息队列大小分别为(%d/%d)，丢弃的待发送数据包数量为%d。",tmprmqc,tmpsmqc,m_iDiscardSendPkgCount);        
        if( tmprmqc > 100000)
        {
            //接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
            ngx_log_stderr(0,"接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！",tmprmqc);
        }
        ngx_log_stderr(0,"-------------------------------------end---------------------------------------");
    }
    return;
}