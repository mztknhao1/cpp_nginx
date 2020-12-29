#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"


// 从连接池中获取一个空闲连接，当一个客户端连接TCP进入，我希望把这个连接和我的连接池中的一个对象绑定在一起，后续我可以通过这个连接把这个对象拿到】
lpngx_connection_t  CSocket::ngx_get_connection(int isock){
    lpngx_connection_t c = m_pfree_connections;

    if(c == nullptr){
        //系统应该控制连接数量，防止空闲连接被耗尽，能走到这里，都不正常
        ngx_log_stderr(0,"CSocekt::ngx_get_connection()中空闲链表为空,这不应该!");
        return NULL;
    }


    m_pfree_connections = c->data;              //空闲连接池指向下一个
    m_free_connection_n --;                     //空闲连接少1


    //(1) 先把c指向的对象中有用的东西搞出来保存成变量，因为这些数据可能有用？（按道理之前都已经空闲了就应该释放了呀）
    uintptr_t   instance = c->instance;
    uint64_t    iCurrsequence = c->iCurrsequence;
    //...

    //(2) 把以往有用的数据取出后，清空并给适当值
    memset(c, 0, sizeof(ngx_connection_t));
    c->fd = isock;

    //(3) 这个值有用，所以在上边(1)中被保留，没有被清空，现在又把这个值赋回来
    c->instance = !instance;    // 最初这个值默认为1，取反为0
    // c->iCurrsequence = (iCurrsequence + 1); //为啥nginx写的很奇怪
    c->iCurrsequence = iCurrsequence;++c->iCurrsequence;

    return c;
}

void CSocket::ngx_free_connection(lpngx_connection_t c){
    // 释放连接对象
    
    c->data = m_pfree_connections;
    ++c->iCurrsequence;

    m_pfree_connections = c;
    ++m_free_connection_n;

    return; 
}