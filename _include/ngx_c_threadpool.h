#ifndef __NGX_THREADPOOL_H__
#define __NGX_THREADPOOL_H__

#include <pthread.h>
#include <atomic>
#include <vector>
#include <list>
#include <memory>


//线程池管理类
class CThreadPool{
public:
    CThreadPool();
    ~CThreadPool();

public:
    bool Create(int threadNum);         //创建线程池
    void StopAll();                     //停止所有线程
    void inMsgRecvQueueAndSignal(char *buf);
    void Call();               //任务来了，调用一个线程池中的线程干活

private:
    static void *ThreadFunc(void *threadData);  //新线程的回调函数
    void   clearMsgRecvQueue();                 //清理消息队列

private:
    struct  ThreadItem
    {
        pthread_t   _Handle;                    //线程句柄
        CThreadPool *_pThis;                    //记录线程池指针
        bool        ifrunning;                  //标记是否正式启动

        ThreadItem(CThreadPool *pthis):_pThis(pthis), ifrunning(false){}
        ~ThreadItem(){}
    };

private:
    static pthread_mutex_t  m_pthreadMutex;     //线程同步互斥量
    static pthread_cond_t   m_pthreadCond;      //线程同步条件变量
    static bool             m_shutdown;         //线程退出标志

    int                     m_iThreadNum;       //要创建的线程数量
    std::atomic<int>        m_iRuningThreadNum; //运行中的线程数
    time_t                  m_iLastEmgTime;     //上一次线程不够用【紧急事件】的时间，防止日志报的太频繁

    std::vector<ThreadItem *> m_threadVector;   //线程容器，容器中就是各个线程了

    //接收消息队列相关
    std::list<char *>       m_MsgRecvQueue;     //接收消息队列（生产者）
    int                     m_iRecvMsgQueueCount;   //收消息队列大小
};




#endif