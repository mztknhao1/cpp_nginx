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
#include <pthread.h>   //多线程
#include <sstream>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
//#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  


using handler = bool(CLogicSocket::*)(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, 
                                        char *pPkgBody, unsigned short iBodyLength);

//用来保存 成员函数指针
// static const handler


//构造函数
CLogicSocket::CLogicSocket()
{

}

//析构函数
CLogicSocket::~CLogicSocket()
{

}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CLogicSocket::Initialize()
{
    //做一些和本类相关的初始化工作
    //....日后根据需要扩展
        
    bool bParentInit = CSocket::Initialize();  //调用父类的同名函数
    return bParentInit;
}


//处理收到的数据包
//pMsgBuf: 消息头+包头+包体
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf){
    lpmsg_header_t pMsgHeader = (lpmsg_header_t)pMsgBuf;
    lpcomm_pkg_header_t pPkgHeader = (lpcomm_pkg_header_t)(pMsgBuf+m_iLenMsgHeader);
    void    *pPkgBody = NULL;

    unsigned short pkgLen = ntohs(pPkgHeader->pkgLen);                                  //包头+包体的宽度

    if(m_iLenMsgHeader == pkgLen){
        //只有包头，没有包体
        if(pPkgHeader->crc32 != 0){
            return;
        }
        pPkgBody = NULL;
    }else{
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);                                   //ntohl 针对4字节，网络序转主机序
        pPkgBody = (void*)(pMsgBuf+m_iLenMsgHeader+m_iLenPkgHeader);                    //跳过消息头、包头

        //计算crc32完整性
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody, pkgLen-m_iLenPkgHeader);        //计算纯包体的crc值
        if(calccrc != pPkgHeader->crc32){
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!"); 
            return;
        }
    }

    //拿出消息代码来
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode);
    lpngx_connection_t pConn = pMsgHeader->pConn;

    //做一些判断
    //（1）如果从收到客户端发来的数据包，到释放一个线程池中的线程处理该包的过程中，客户端断开了
    if(pConn->iCurrsequence != pMsgHeader->iCurrsequence){
        return;
    }

    //判断消息代码是正确的，防止客户端恶意侵害我们的服务器
    std::stringstream ss;
    ss << imsgCode;
    IRequestHandler* handler = g_handlerFactory.getHandler(ss.str());
    if(handler == nullptr){
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;  //没有相关的处理函数
    }

    handler->doHandler(pConn, pMsgHeader, (char *)pPkgBody, pkgLen-m_iLenPkgHeader);

    return;
}

void CLogicSocket::SendNoBodyPkgToClient(lpmsg_header_t pMsgHeader, unsigned short iMsgCode){
    CMemory *pMemory = CMemory::GetInstance();

    char    *pSendBuf = (char *)pMemory->AllocMemory(MSG_HEADER_LEN+PKG_HEADER_LEN, false);
    char    *pTmpBuf  = pSendBuf;

    memcpy(pTmpBuf, pMsgHeader, MSG_HEADER_LEN);
    pTmpBuf += MSG_HEADER_LEN;

    lpcomm_pkg_header_t pPkgHeader = (lpcomm_pkg_header_t)(pTmpBuf);
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen  = htons(PKG_HEADER_LEN);
    pPkgHeader->crc32   = 0;
    g_socket.msgSend(pSendBuf);

    return;
}

/**
 * @description: 心跳包检测时间到，该去检测心跳包是否超时 
 * @param {*}
 * @return {*}
 */
void CLogicSocket::procPingTimeOutChecking(lpmsg_header_t tmpmsg, time_t cur_time){
    CMemory *pMemory = CMemory::GetInstance();

    //检测连接是否断开，下面是没断开的情况
    if(tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence){
        lpngx_connection_t pConn = tmpmsg->pConn;

        if(m_ifTimeOutKick == 1){
            //时间一到，直接踢出去
            zdClosesocketProc(pConn);
        }
        else if((cur_time - pConn->lastPingTime)>(m_iWaitTime*3+10)){
            //超时踢得判断标准是每次检查隔3*m_iWaitTime+10
            ngx_log_stderr(0, "时间到了，踢人了");
            zdClosesocketProc(pConn);
        }
        //内存要释放
        pMemory->FreeMemory(tmpmsg);    
    }
    else{
        //此时连接断开了
        pMemory->FreeMemory(tmpmsg);
    }
    return;
}