/*
 * @Author: your name
 * @Date: 2021-01-12 10:19:26
 * @LastEditTime: 2021-01-12 12:27:23
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /nginx/net/ngx_c_socket_time.cxx
 */

#include <list>

#include "ngx_c_slogic.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"
#include "ngx_global.h"
#include "ngx_func.h"

/**
 * @description: 新来一个用户，将其加入到时间管理队列(multimap)中去,由三次握手连接进来之后调用 
 * @param {lpngx_connection_t pConn 连接信息}
 * @return {*}
 */
void CSocket::AddToTimerQueue(lpngx_connection_t pConn){
    CMemory *pMemory = CMemory::GetInstance();

    time_t futtime = time(NULL);
    futtime += m_iWaitTime;         //等待多少秒之后的时间

    CLock lock(&m_timequeueMutex);

    lpmsg_header_t  tmpMsgHeader = (lpmsg_header_t)pMemory->AllocMemory(m_iLenMsgHeader, false);
    tmpMsgHeader->pConn = pConn;
    tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
    m_timerQueuemap.insert(std::make_pair(futtime, tmpMsgHeader));      //按key自动排序 小->大
    m_cur_size_++;
    m_timer_value_ = GetEarliestTime();                                 //计时队列头部的值保存在m_timer_value_里面
    return;
}

/**
 * @description:从multimap中获得最早的时间回去 
 * @param {*}
 * @return {*}
 */
time_t CSocket::GetEarliestTime(){
    auto pos = m_timerQueuemap.begin();
    return pos->first;
}

/**
 * @description: 从m_timerQueuemap中移出最近的那个，然后返回最近那个的第二项 
 * @param {*}
 * @return {*}
 */
lpmsg_header_t CSocket::RemoveFirstTimer(){
    if(m_cur_size_ <= 0){
        return nullptr;
    }
    auto item = m_timerQueuemap.begin();
    lpmsg_header_t ptmp = item->second;
    m_timerQueuemap.erase(item);
    --m_cur_size_;
    return ptmp;
}

/**
 * @description: 获得超时的连接信息，没有超时则返回空，调用者负责互斥 
 * @param {*}
 * @return {*}
 */
lpmsg_header_t CSocket::GetOverTimeTimer(time_t cur_time){
    CMemory *pMemory = CMemory::GetInstance();
    lpmsg_header_t result = nullptr;

    if(m_cur_size_==0 || m_timerQueuemap.empty()){
        return result;
    }

    time_t earlistTime = GetEarliestTime();
    if(earlistTime <= cur_time){
        result = RemoveFirstTimer();                    //把这个超时节点从m_timerQueueMap删掉，并把这个节点的第2项返回

        if(m_ifTimeOutKick != 1){
            //如果不是超时就踢出，可以做一些其他事情
            time_t newinqueuetime = cur_time + m_iWaitTime;
            lpmsg_header_t tmpMsgHeader = (lpmsg_header_t)pMemory->AllocMemory(sizeof(msg_header_t),false);
			tmpMsgHeader->pConn = result->pConn;
			tmpMsgHeader->iCurrsequence = result->iCurrsequence;			
			m_timerQueuemap.insert(std::make_pair(newinqueuetime,tmpMsgHeader)); //自动排序 小->大			
			m_cur_size_++; 
        }

        if(m_cur_size_ > 0) //这个判断条件必要，因为以后我们可能在这里扩充别的代码
		{
			m_timer_value_ = GetEarliestTime(); //计时队列头部时间值保存到m_timer_value_里
		}

        return result;
    }

    return nullptr;

}

/**
 * @description:清理时间队列中所有内容 
 * @param {*}
 * @return {*}
 */
void CSocket::clearAllFromTimeQueue(){
    CMemory *pMemory = CMemory::GetInstance();
    for(auto& item:m_timerQueuemap){
        pMemory->FreeMemory(item.second);
        --m_cur_size_;
    }
    m_timerQueuemap.clear();
}

/**
 * @description: 把指定用户的tcp连接从timer表中移出 
 * @param {*}
 * @return {*}
 */
void CSocket::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
    std::multimap<time_t, lpmsg_header_t>::iterator pos,posend;
	CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_timequeueMutex);

    //因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for(; pos != posend; ++pos)	
	{
		if(pos->second->pConn == pConn)
		{			
			p_memory->FreeMemory(pos->second);  //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_; //减去一个元素，必然要把尺寸减少1个;								
			goto lblMTQM;
		}		
	}
	if(m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}
    return;    
}

/**
 * @description: 时间队列的监视线程，处理到期不发送心跳包的用户 
 * @param {*}
 * @return {*}
 */
void* CSocket::serverTimerQueueMonitorThread(void *threadData){
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CSocket* pSocketObj = pThread->_pThis;

    time_t absolute_time, cur_time;
    int err;

    //不退出
    while(g_stopEvent == 0){
        //队列不为空
        if(pSocketObj->m_cur_size_>0){
            //将最近发生事情的时间放到absolute_time中
            absolute_time = pSocketObj->m_timer_value_;
            cur_time = time(NULL);

            if(absolute_time < cur_time){
                //absolute_time小于cur_time表明有本应该隔了20秒发送心跳包的连接没发送
                std::list<lpmsg_header_t>   m_lsIdleList;         //保存要处理的内容
                lpmsg_header_t              result;

                //上锁
                err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
                if(err !=0){
                    ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);//有问题，要及时报告
                }
            
                //一次性把所有超时节点拿过来
                while((result = pSocketObj->GetOverTimeTimer(cur_time))!=NULL){
                    m_lsIdleList.push_back(result);
                }//end while

                //解锁
                err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex);
                if(err != 0){
                    ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);//有问题，要及时报告
                }

                //正式处理
                lpmsg_header_t tmpmsg;
                while(!m_lsIdleList.empty()){
                    tmpmsg = m_lsIdleList.front();
                    m_lsIdleList.pop_front();
                    pSocketObj->procPingTimeOutChecking(tmpmsg, cur_time);
                }//end while
            }
        }//end if(pSocketObj->m_cur_size_>0)
    
        usleep(500*1000);           //每次休息500毫秒
    }//end while

    return (void*)0;
}


/**
 * @description: 这里仅仅做了内存释放，没有处理具体业务，需要子类重写 
 * @param {*}
 * @return {*}
 */
void CSocket::procPingTimeOutChecking(lpmsg_header_t tmpmsg, time_t cur_time){
    CMemory *pMemory = CMemory::GetInstance();
    pMemory->FreeMemory(tmpmsg);
    return;
}