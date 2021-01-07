#ifndef __LOGINHANDLER_H__
#define __LOGINHANDLER_H__


#include "IRequestHandler.h"
class LoginHandler:public IRequestHandler{

    virtual bool doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBuf, unsigned short iBodyLenght) override;
};


#endif