#include "IRequestHandler.h"
#include "LoginHandler.h"
#include "ngx_func.h"

//TODO 还没写具体处理逻辑
bool LoginHandler::doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBuf, unsigned short iBodyLength){
    
    ngx_log_stderr(0, "执行了LoginHandler函数！");
    return false;
}