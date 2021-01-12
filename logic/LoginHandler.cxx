/*
 * @Author: your name
 * @Date: 2021-01-07 09:33:15
 * @LastEditTime: 2021-01-12 19:00:53
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /nginx/logic/LoginHandler.cxx
 */
#include "IRequestHandler.h"
#include "LoginHandler.h"
#include "ngx_func.h"
#include "ngx_func.h"
#include "ngx_logic_comm.h"
#include "ngx_c_lockmutex.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_global.h"
#include <arpa/inet.h>

bool LoginHandler::doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength){
    
    if(pPkgBody == NULL)
    {        
        return false;
    }		    
    int iRecvLen = sizeof(_login_t); 
    if(iRecvLen != iBodyLength) 
    {     
        return false; 
    }
    CLock lock(&pConn->logicProcMutex);
        
    _lplogin_t p_RecvInfo = (_lplogin_t)pPkgBody;     
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;

	lpcomm_pkg_header_t pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();

    int iSendLen = sizeof(_login_t);  
    char *p_sendbuf = (char *)p_memory->AllocMemory(MSG_HEADER_LEN+PKG_HEADER_LEN+iSendLen,false);    
    memcpy(p_sendbuf,pMsgHeader,MSG_HEADER_LEN);    
    pPkgHeader = (lpcomm_pkg_header_t)(p_sendbuf+MSG_HEADER_LEN);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(PKG_HEADER_LEN + iSendLen);    
    _lplogin_t p_sendInfo = (_lplogin_t)(p_sendbuf+MSG_HEADER_LEN+PKG_HEADER_LEN);
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		   
    //ngx_log_stderr(0,"成功收到了登录并返回结果！");
    g_socket.msgSend(p_sendbuf);
    return true;
    return false;
}