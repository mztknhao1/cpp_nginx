#ifndef __IREQUESTHANDLER_H__
#define __IREQUESTHANDLER_H__

#include "ngx_c_socket.h"


// 抽象基类
class IRequestHandler{

public:
    virtual ~IRequestHandler(){}

    virtual bool doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char* pPkgBuf, unsigned short iBodyLength) = 0;
};


#endif