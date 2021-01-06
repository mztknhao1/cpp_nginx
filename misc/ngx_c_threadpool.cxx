#include "ngx_c_threadpool.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

#include <unistd.h>
#include <stdarg.h>

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  CThreadPool::m_pthreadCond  = PTHREAD_COND_INITIALIZER;
bool            CThreadPool::m_shutdown     = false;

CThreadPool::CThreadPool(){
    m_iRuningThreadNum = 0; //正在运行的线程，开始给个0，原子对象给0可以直接赋值
    m_iLastEmgTime      = 0;
    m_iRecvMsgQueueCount = 0;   //收到消息的队列
}

CThreadPool::~CThreadPool(){
    //资源释放再StopAll()中进行
    clearMsgRecvQueue();
}

void CThreadPool::clearMsgRecvQueue(){
    char *sTmpMempoint;
    CMemory *p_memory = CMemory::GetInstance();
    while(!m_MsgRecvQueue.empty()){
        sTmpMempoint = m_MsgRecvQueue.front();
        m_MsgRecvQueue.pop_front();
        p_memory->FreeMemory(sTmpMempoint);
    }
}

//创建线程池中的线程，手工调用，不在构造函数中调用
bool   CThreadPool::Create(int threadNum){
    ThreadItem* pNew;
    int err;
    m_iThreadNum = threadNum;                               //保存要创建线程的数量

    for(int i = 0;i < m_iThreadNum; ++i){
        pNew = new ThreadItem(this);
        m_threadVector.push_back(pNew);  //创建 一个新线程对象，并加入到容器中
        err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);
        if(err != 0){
            ngx_log_stderr(err, "CThreadPool::create()创建线程%d失败，返回错误码为%d", i, err);
            return false;
        }
        else{
            //创建线程成功
            //ngx_log_stderr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->_Handle);
        }
    }

    //我们必须保证每个线程都启动并运行到pthread_cond_wait()，本函数才返回，只有这样，这几个线程才能正常工作
lblfor:
    for(auto item:m_threadVector){
        if(item->ifrunning == false){
            //这说明没有启动完全的线程
            usleep(100 * 1000);
            goto lblfor;
        }
    }
    return true;
}

//线程入口函数，当用pthread_create()创建后，这个ThreadFunc()函数会被立即执行
void* CThreadPool::ThreadFunc(void* threadData){
    //这个是静态成员函数，不存在this指针
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool *pThreadPoolObj = pThread->_pThis;                  //这样就拿到了this指针

    CMemory *p_memory = CMemory::GetInstance();
    int err;

    pthread_t   tid = pthread_self();                               //线程自己的tid

    while(true){

        //必须在pthread_mutex_wait()之前，先上个锁，然后在while()中调用pthread_cond_wait()

        //TODO 这里的一些关于pthread的函数可以改成c++11的多线程函数，暂时还没改，改了可以跨平台
        err = pthread_mutex_lock(&m_pthreadMutex);
        if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_mutex_lock()失败，返回的错误码为%d!",err);//有问题，要及时报告

        //这个while循环很关键
        while((pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false){
            if(pThread->ifrunning == false)
                pThread->ifrunning = true;      //测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱
                                                //所以加了这个判断
            
            //!线程卡在这里，等待条件变量被激发，且这句会把m_pthreadMutex释放
            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
        }

        //能走下来，必然是拿到了真正的消息队列中的数据 或者m_shutdown == true

        if(m_shutdown){                         //关闭线程
            pthread_mutex_unlock(&m_pthreadMutex);
            break;                      
        }

        char *jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();      //走到这里必然有消息
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;

        //可以解锁互斥量了,取出了队列中的信息就解锁互斥量!
        err = pthread_mutex_unlock(&m_pthreadMutex);
        if(err != 0){
            ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",err);//有问题，要及时报告
        }

        ++pThreadPoolObj->m_iRuningThreadNum;       //原子+1【记录正在干活的线程数量】比互斥量快的多

        //TODO 处理消息队列中来的消息
        g_socket.threadRecvProcFunc(jobbuf);

        p_memory->FreeMemory(jobbuf);
        --pThreadPoolObj->m_iRuningThreadNum;
    }

    return (void*)0;
}


//停止所有线程
void CThreadPool::StopAll(){
    if(m_shutdown == true){
        return;
    }
    m_shutdown = true;

    //唤醒等待条件【卡在pthread_cond_wait()的所有线程，一定要在改变条件状态以后再给线程发送信号
    int err = pthread_cond_broadcast(&m_pthreadCond);
    if(err != 0){
        //这肯定是有问题，要打印紧急日志
        ngx_log_stderr(err,"CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!",err);
        return;
    }

    //等待线程，让线程真返回
    for(auto& item:m_threadVector){
        pthread_join(item->_Handle, NULL);      //等待线程终止
    }

    //流程走到这里，线程肯定都返回了
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);

    //释放new出来的ThreadItem
    for(auto& item:m_threadVector){
        if(item){
            delete item;
        }
    }

    m_threadVector.clear();
    ngx_log_stderr(0,"CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");

    return;
}

//-------------------------------------------------------------------------------------------------------
//收到消息后，入消息队列，并触发线程池中的线程来处理
void CThreadPool::inMsgRecvQueueAndSignal(char *buf){
    //互斥
    int err = pthread_mutex_lock(&m_pthreadMutex);
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!",err);
    }

    m_MsgRecvQueue.push_back(buf);
    ++m_iRecvMsgQueueCount;

    //取消互斥
    err = pthread_mutex_unlock(&m_pthreadMutex);   
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
    }

    Call();
    return;
}

//来任务了
void CThreadPool::Call(){
    int err = pthread_cond_signal(&m_pthreadCond);      //唤醒一个等待该条件的线程
    if(err != 0 )
    {
        //这是有问题啊，要打印日志啊
        ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!",err);
    }


    //如果当前的工作线程都忙，则要报警
    if(m_iThreadNum == m_iRuningThreadNum){
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10){
            //两次报告间隔超过10秒
            m_iLastEmgTime = currtime;
            ngx_log_stderr(0,"CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    }
    return;
}

//唤醒丢失问题，sem_t sem_write;
//参考信号量解决方案：https://blog.csdn.net/yusiguyuan/article/details/20215591  linux多线程编程--信号量和条件变量 唤醒丢失事件
