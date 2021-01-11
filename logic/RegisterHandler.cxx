/*
 * @Author: your name
 * @Date: 2021-01-09 13:01:29
 * @LastEditTime: 2021-01-11 09:56:09
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /mztkn_study_nginx/nginx/logic/RegisterHandler.cxx
 */

#include <arpa/inet.h>

#include "IRequestHandler.h"
#include "RegisterHandler.h"
#include "ngx_func.h"
#include "ngx_logic_comm.h"
#include "ngx_c_lockmutex.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_global.h"


//TODO 还没写具体处理逻辑
bool RegisterHandler::doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength){
    
    ngx_log_stderr(0, "开始执行RegisterHandler函数()");
    // return false;
    //（1）首先判断包体的合法性
    if(pPkgBody == nullptr){
        return false;
    }

    int iRecvLen = sizeof(_register_t);
    if(iRecvLen != iBodyLength){
        return false;
    }

    //(2) 对于同一个用户，可能发送多个请求，造成多个线程同时为改用户服务，这里加互斥
    CLock lock(&pConn->logicProcMutex);

    //(3) 取得了整个发送过来的数据
    _lpregister_t    pRecvInfo = (_lpregister_t)(pPkgBody);

    //(4这里要根据业务逻辑进一步考虑合法性。。。

    //(5)给客户端返回数据，一般也是一个结构，这里以同样的register为例
    lpcomm_pkg_header_t pPkgHeader;
    CMemory *pMemory = CMemory::GetInstance();
    CCRC32 *pCrc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(_register_t);
    int iMsgHeadLen = sizeof(msg_header_t);
    int iPkgHeadLen = sizeof(comm_pkg_header_t);
    //a) 要分配发出去的包的内存

iSendLen = 60000;
    char *pSendBuf = (char *)pMemory->AllocMemory(iSendLen+iMsgHeadLen+iPkgHeadLen, false);
    //b) 填充消息头
    memcpy(pSendBuf, pMsgHeader, iMsgHeadLen);
    //c) 填充包头
    pPkgHeader = (lpcomm_pkg_header_t)(pSendBuf + iMsgHeadLen);
    pPkgHeader->msgCode = _CMD_REGISTER;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen = htons(iPkgHeadLen + iSendLen);
    //d) 填充包体
    _lpregister_t pSendInfo = (_lpregister_t)(pSendBuf + iMsgHeadLen + iPkgHeadLen);
    //。。。

    //e)计算crc
    pPkgHeader->crc32 = pCrc32->Get_CRC((unsigned char *)pSendInfo,iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);


    //f) 发送数据包
    g_socket.msgSend(pSendBuf);

    return true;
}