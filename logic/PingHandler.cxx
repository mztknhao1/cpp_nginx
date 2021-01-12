/*
 * @Author: your name
 * @Date: 2021-01-12 08:50:48
 * @LastEditTime: 2021-01-12 18:48:03
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /nginx/logic/PingHandler.cxx
 */
#include "IRequestHandler.h"
#include "PingHandler.h"
#include "ngx_func.h"
#include "ngx_c_lockmutex.h"
#include "ngx_logic_comm.h"
#include "ngx_c_slogic.h"

bool PingHandler::doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBuf, unsigned short iBodyLength){
    
    if(iBodyLength != 0){
        return false;
    }
    CLock lock(&pConn->logicProcMutex);
    pConn->lastPingTime = time(NULL);

    //服务器也发送一个只有包头的数据给客户端，作为返回数据
    CLogicSocket::SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);
    
    // ngx_log_stderr(0, "成功收到了心跳包!");

    return true;
}