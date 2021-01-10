/*
 * @Author: your name
 * @Date: 2021-01-09 13:02:08
 * @LastEditTime: 2021-01-09 13:05:39
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /mztkn_study_nginx/nginx/_include/RegisterHandler.h
 */

#ifndef __REGISTERHANDLER_H__
#define __REGISTERHANDLER_H__


#include "IRequestHandler.h"
class RegisterHandler:public IRequestHandler{

    virtual bool doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBuf, unsigned short iBodyLenght) override;
};


#endif
