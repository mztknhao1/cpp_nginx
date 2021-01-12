/*
 * @Author: your name
 * @Date: 2021-01-09 13:01:29
 * @LastEditTime: 2021-01-12 20:40:11
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


bool RegisterHandler::doHandler(lpngx_connection_t pConn, lpmsg_header_t pMsgHeader, char *pPkgBody, unsigned short iBodyLength){
    
    //ngx_log_stderr(0,"执行了CLogicSocket::_HandleRegister()!");
    
    //(1)首先判断包体的合法性
    if(pPkgBody == NULL) //具体看客户端服务器约定，如果约定这个命令[msgCode]必须带包体，那么如果不带包体，就认为是恶意包，直接不处理    
    {        
        return false;
    }
		    
    int iRecvLen = sizeof(_register_t); 
    if(iRecvLen != iBodyLength) //发送过来的结构大小不对，认为是恶意包，直接不处理
    {     
        return false; 
    }

    //(2)对于同一个用户，可能同时发送来多个请求过来，造成多个线程同时为该 用户服务，比如以网游为例，用户要在商店中买A物品，又买B物品，而用户的钱 只够买A或者B，不够同时买A和B呢？
       //那如果用户发送购买命令过来买了一次A，又买了一次B，如果是两个线程来执行同一个用户的这两次不同的购买命令，很可能造成这个用户购买成功了 A，又购买成功了 B
       //所以，为了稳妥起见，针对某个用户的命令，我们一般都要互斥,我们需要增加临界的变量于ngx_connection_s结构中
    CLock lock(&pConn->logicProcMutex); //凡是和本用户有关的访问都互斥
    
    //(3)取得了整个发送过来的数据
    _lpregister_t p_RecvInfo = (_lpregister_t)pPkgBody; 
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);          //所有数值型,short,int,long,uint64_t,int64_t这种大家都不要忘记传输之前主机网络序，收到后网络转主机序
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 


    //(4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性，
       //当前该玩家的状态是否适合收到这个数据等等【比如如果用户没登陆，它就不适合购买物品等等】
        //这里大家自己发挥，自己根据业务需要来扩充代码，老师就不带着大家扩充了。。。。。。。。。。。。
    //。。。。。。。。

    //(5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，这里我们就以给客户端也返回同样的 STRUCT_REGISTER 结构来举例    
    //LPSTRUCT_REGISTER pFromPkgHeader =  (LPSTRUCT_REGISTER)(((char *)pMsgHeader)+m_iLenMsgHeader);	//指向收到的包的包头，其中数据后续可能要用到
	lpcomm_pkg_header_t pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(_register_t);  
    //a)分配要发送出去的包的内存

    //iSendLen = 65000; //unsigned最大也就是这个值
    char *p_sendbuf = (char *)p_memory->AllocMemory(MSG_HEADER_LEN+PKG_HEADER_LEN+iSendLen,false);//准备发送的格式，这里是 消息头+包头+包体
    //b)填充消息头
    memcpy(p_sendbuf,pMsgHeader,MSG_HEADER_LEN);                   //消息头直接拷贝到这里来
    //c)填充包头
    pPkgHeader = (lpcomm_pkg_header_t)(p_sendbuf+MSG_HEADER_LEN);    //指向包头
    pPkgHeader->msgCode = _CMD_REGISTER;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
    pPkgHeader->pkgLen  = htons(PKG_HEADER_LEN + iSendLen);        //整个包的尺寸【包头+包体尺寸】 
    //d)填充包体
    _lpregister_t p_sendInfo = (_lpregister_t)(p_sendbuf+MSG_HEADER_LEN+PKG_HEADER_LEN);	//跳过消息头，跳过包头，就是包体了
    //。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；
    
    //e)包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		

    //f)发送数据包
    g_socket.msgSend(p_sendbuf);

    return true;
}