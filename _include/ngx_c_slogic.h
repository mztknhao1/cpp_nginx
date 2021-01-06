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
    //处理各种业务逻辑相关的函数
    bool _HandleRegister(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength);
    bool _HandleLogIn(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength);

public:
    virtual void threadRecvProcFunc(char *pmsgBuf);

};


#endif