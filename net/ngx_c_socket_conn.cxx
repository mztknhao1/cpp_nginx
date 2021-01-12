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
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//---------------------------------------------------------------
//连接池成员函数
ngx_connection_s::ngx_connection_s()//构造函数
{		
    iCurrsequence = 0;    
    pthread_mutex_init(&logicProcMutex, NULL); //互斥量初始化
}
ngx_connection_s::~ngx_connection_s()//析构函数
{
    pthread_mutex_destroy(&logicProcMutex);    //互斥量释放
}
//分配出去一个连接的时候初始化一些内容,原来内容放在 ngx_get_connection()里，现在放在这里
void ngx_connection_s::GetOneToUse()
{
    ++iCurrsequence;

    curStat                 = _PKG_HD_INIT;                     //收包状态处于 初始状态，准备接收数据包头【状态机】
    precvbuf                = dataHeadInfo;                     //收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
    irecvlen                = sizeof(comm_pkg_header_t);        //这里指定收数据的长度，这里先要求收包头这么长字节的数据

    precvMemPointer         = NULL;                             //既然没new内存，那自然指向的内存地址先给NULL
    iThrowsendCount         = 0;                                //原子的
    psendMemPointer         = NULL;                             //发送数据头指针记录
    events                  = 0;                                //epoll事件先给0 
    lastPingTime            = time(NULL);                       //上一次ping的时间

    FloodkickLastTime       = 0;
    FloodAttackCount        = 0;
    iSendCount              = 0;

}

//回收回来一个连接的时候做一些事
void ngx_connection_s::PutOneToFree()
{
    ++iCurrsequence;   
    if(precvMemPointer != NULL)                                 //我们曾经给这个连接分配过接收数据的内存，则要释放内存
    {        
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;        
    }
    if(psendMemPointer != NULL)                                 //如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;                                        //这个值还是很重要得赋值，多线程导致         
}

//---------------------------------------------------------------
//初始化连接池
void CSocket::initConnection()
{
    lpngx_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();   

    int ilenconnpool = sizeof(ngx_connection_t);    
    for(int i = 0; i < m_worker_connections; ++i)               //先创建这么多个连接，后续不够再增加
    {
        p_Conn = (lpngx_connection_t)p_memory->AllocMemory(ilenconnpool,true); //清理内存 , 因为这里分配内存new char，无法执行构造函数，所以如下：
        //手工调用构造函数，因为AllocMemory里无法调用构造函数
        p_Conn = new(p_Conn) ngx_connection_t();                //定位new，释放则显式调用p_Conn->~ngx_connection_t();		
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);                     //所有链接【不管是否空闲】都放在这个list
        m_freeConnectionList.push_back(p_Conn);                 //空闲连接会放在这个list
    } //end for
    m_free_connection_n = m_total_connection_n = m_connectionList.size(); //开始这两个列表一样大
    return;
}

//最终回收连接池，释放内存
void CSocket::clearConnection()
{
    lpngx_connection_t p_Conn;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_connectionList.empty())
	{
		p_Conn = m_connectionList.front();
		m_connectionList.pop_front(); 
        p_Conn->~ngx_connection_t();     //手工调用析构函数
		p_memory->FreeMemory(p_Conn);
	}
}

//从连接池中获取一个空闲连接【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
    //因为可能有其他线程要访问m_freeconnectionList，m_connectionList【比如可能有专门的释放线程要释放/或者主线程要释放】之类的，所以应该临界一下
    CLock lock(&m_connectionMutex);  

    if(!m_freeConnectionList.empty())
    {
        //有空闲的，自然是从空闲的中摘取
        lpngx_connection_t p_Conn = m_freeConnectionList.front(); //返回第一个元素但不检查元素存在与否
        m_freeConnectionList.pop_front();                         //移除第一个元素但不返回	
        p_Conn->GetOneToUse();
        --m_free_connection_n; 
        p_Conn->fd = isock;
        return p_Conn;
    }

    //走到这里，表示没空闲的连接了，那就考虑重新创建一个连接
    CMemory *p_memory = CMemory::GetInstance();
    lpngx_connection_t p_Conn = (lpngx_connection_t)p_memory->AllocMemory(sizeof(ngx_connection_t),true);
    p_Conn = new(p_Conn) ngx_connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn);                             //入到总表中来，但不能入到空闲表中来，因为凡是调这个函数的，肯定是要用这个连接的
    ++m_total_connection_n;             
    p_Conn->fd = isock;
    return p_Conn;
}

//归还参数pConn所代表的连接到到连接池中，注意参数类型是lpngx_connection_t
void CSocket::ngx_free_connection(lpngx_connection_t pConn) 
{
    //因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
    CLock lock(&m_connectionMutex);  

    //首先明确一点，连接，所有连接全部都在m_connectionList里；
    pConn->PutOneToFree();

    //扔到空闲连接列表里
    m_freeConnectionList.push_back(pConn);

    //空闲连接数+1
    ++m_free_connection_n;

    return;
}


//将要回收的连接放到一个队列中来，后续有专门的线程会处理这个队列中的连接的回收
//有些连接，我们不希望马上释放，要隔一段时间后再释放以确保服务器的稳定，所以，我们把这种隔一段时间才释放的连接先放到一个队列中来
void CSocket::inRecyConnectQueue(lpngx_connection_t pConn)
{
    //ngx_log_stderr(0,"CSocket::inRecyConnectQueue()执行，连接入到回收队列中.");
    
    CLock lock(&m_recyconnqueueMutex); //针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表；

    bool iffind = false;
    //如下判断，防止连接多次回收
    for(auto& item:m_recyConnectionList){
        if(item == pConn){
            iffind = true;
            break;
        }
    }
    if(iffind){
        return;
    }

    pConn->inRecyTime = time(NULL);         //记录回收时间
    ++pConn->iCurrsequence;
    m_recyConnectionList.push_back(pConn);  //等待ServerRecyConnectionThread线程自会处理 
    ++m_total_recyconnection_n;             //待释放连接队列大小+1
    --m_onlineUserCount;                    //连入数量减一
    return;
}

//处理连接回收的线程
void* CSocket::serverRecyConnectionThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;
    
    time_t currtime;
    int err;
    std::list<lpngx_connection_t>::iterator pos,posend;
    lpngx_connection_t p_Conn;
    
    while(1)
    {
        //为简化问题，我们直接每次休息200毫秒
        usleep(200 * 1000);  //单位是微妙,又因为1毫秒=1000微妙，所以 200 *1000 = 200毫秒

        //不管啥情况，先把这个条件成立时该做的动作做了
        if(pSocketObj->m_total_recyconnection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
            if(err != 0) ngx_log_stderr(err,"CSocket::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);

lblRRTD:
            pos    = pSocketObj->m_recyConnectionList.begin();
			posend = pSocketObj->m_recyConnectionList.end();
            for(; pos != posend; ++pos)
            {
                p_Conn = (*pos);
                if(
                    ( (p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime)  && (g_stopEvent == 0) //如果不是要整个系统退出，你可以continue，否则就得要强制释放
                    )
                {
                    continue; //没到释放的时间
                }    
                //到释放的时间了: 
                //......这将来可能还要做一些是否能释放的判断[在我们写完发送数据代码之后吧]，先预留位置
                //....
                if(p_Conn->iThrowsendCount > 0)
                {
                    //这确实不应该，打印个日志吧；
                    ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount!=0，这个不该发生");
                    //其他先暂时啥也不做，路程继续往下走，继续去释放吧。
                }

                //流程走到这里，表示可以释放，那我们就开始释放
                --pSocketObj->m_total_recyconnection_n;        //待释放连接队列大小-1
                pSocketObj->m_recyConnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢

                //ngx_log_stderr(0,"CSocket::ServerRecyConnectionThread()执行，连接%d被归还.",p_Conn->fd);

                pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                goto lblRRTD; 
            } //end for
            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
            if(err != 0)  ngx_log_stderr(err,"CSocket::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        } //end if

        if(g_stopEvent == 1) //要退出整个程序，那么肯定要先退出这个循环
        {
            if(pSocketObj->m_total_recyconnection_n > 0)
            {
                //因为要退出，所以就得硬释放了【不管到没到时间，不管有没有其他不 允许释放的需求，都得硬释放】
                err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
                if(err != 0) ngx_log_stderr(err,"CSocket::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);

        lblRRTD2:
                pos    = pSocketObj->m_recyConnectionList.begin();
			    posend = pSocketObj->m_recyConnectionList.end();
                for(; pos != posend; ++pos)
                {
                    p_Conn = (*pos);
                    --pSocketObj->m_total_recyconnection_n;        //待释放连接队列大小-1
                    pSocketObj->m_recyConnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢
                    pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                    goto lblRRTD2; 
                } //end for
                err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
                if(err != 0)  ngx_log_stderr(err,"CSocket::ServerRecyConnectionThread()pthread_mutex_unlock2()失败，返回的错误码为%d!",err);
            } //end if
            break; //整个程序要退出了，所以break;
        }  //end if
    } //end while    
    
    return (void*)0;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
//我们把ngx_close_accepted_connection()函数改名为让名字更通用，并从文件ngx_socket_accept.cxx迁移到本文件中，并改造其中代码，注意顺序
void CSocket::ngx_close_connection(lpngx_connection_t pConn)
{    
    //pConn->fd = -1; //官方nginx这么写，这么写有意义；    不要这个东西，回收时不要轻易东连接里边的内容
    ngx_free_connection(pConn); 
    if(close(pConn->fd) == -1)
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_connection()中close(%d)失败!",pConn->fd);  
    }
    return;
}
