#ifndef __NGX_COMM_H__
#define __NGX_COMM_H__

//宏定义------------------------------------
#define _PKG_MAX_LENGTH     30000  //每个包的最大长度【包头+包体】不超过这个数字，为了留出一些空间，实际上编码是，包头+包体长度必须不大于 这个值-1000【29000】

//通信 收包状态定义
#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态
//#define _PKG_RV_FINISHED     4  //完整包收完，这个状态似乎没什么 用处

#define _DATA_BUFSIZE_       20  //因为我要先收包头，我希望定义一个固定大小的数组专门用来收包头，这个数字大小一定要 >sizeof(COMM_PKG_HEADER) ,所以我这里定义为20绰绰有余；
                                  //如果日后COMM_PKG_HEADER大小变动，这个数字也要做出调整以满足 >sizeof(COMM_PKG_HEADER) 的要求

//结构定义-----------------------------------------
#pragma pack (1)   //对齐方式，1字节对齐


typedef struct comm_pkg_header_s comm_pkg_header_t, *lpcomm_pkg_header_t;


//一些和网络通讯有关的结构
struct comm_pkg_header_s
{
    unsigned    short   pkgLen;     //报文总长度【包头+包体】，2字节，最大数字伟65535
    unsigned    short   msgCode;    //消息类型代码，2字节，用于区别每个不同的命令
    int                 crc32;      //crc校验---4字节，防止收发数据中出现收到内容和发送内容不一致情况
};

#pragma pack()      //取消只当对齐

#endif