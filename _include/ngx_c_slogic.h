/*
 * @Author: your name
 * @Date: 2021-01-06 19:21:16
 * @LastEditTime: 2021-01-12 13:52:32
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /nginx/_include/ngx_c_slogic.h
 */
#ifndef __NGX_C_SLOGIC_H__
#define __NGX_C_SLOGIC_H__

#include <sys/socket.h>
#include "ngx_c_socket.h"

//处理逻辑和通讯的子类
class CLogicSocket:public CSocket
{
public:
    CLogicSocket();
    virtual ~CLogicSocket();
    virtual bool Initialize();

public:
    //通用收发数据相关函数
    static void SendNoBodyPkgToClient(lpmsg_header_t pMsgHeader, unsigned short iMsgCode);


    // 处理各种业务逻辑相关的函数
    // bool _HandleRegister(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength);
    // bool _HandleLogIn(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength);

    /**
     * @description: 心跳包检测时间到，检测是否超时
     * @param {*}
     * @return {*}
     */
    virtual void procPingTimeOutChecking(lpmsg_header_t tmpmsg, time_t cur_time);

public:
    virtual void threadRecvProcFunc(char *pmsgBuf);

};


#endif