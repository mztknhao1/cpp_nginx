/*
 * @Author: your name
 * @Date: 2021-01-12 08:50:35
 * @LastEditTime: 2021-01-12 08:51:40
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /nginx/_include/PingHandler.h
 */
#ifndef __PINGHANDLER_H__
#define __PINGHANDLER_H__


#include "IRequestHandler.h"
class PingHandler:public IRequestHandler{

    virtual bool doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBuf, unsigned short iBodyLenght) override;
};







#endif