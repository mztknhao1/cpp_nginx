/*
 * @Author: your name
 * @Date: 2021-01-09 15:36:41
 * @LastEditTime: 2021-01-12 08:49:46
 * @LastEditors: Please set LastEditors
 * @Description: 一些数据结构（和客户端协商好的）
 * @FilePath: /mztkn_study_nginx/nginx/_include/ngx_logic_comm.h
 */

#ifndef __NGX_LOGICCOMM_H__
#define __NGX_LOGICCOMM_H__


//收发包宏定义-------------------------------------

#define _CMD_START              0
#define _CMD_PING               _CMD_START + 0      //ping命令【心跳包】
#define _CMD_REGISTER           _CMD_START + 5      //注册
#define _CMD_LOGIN              _CMD_START + 6      //登录


//结构体定义----------------------------------------
#pragma pack (1)

typedef struct _register_s   _register_t, *_lpregister_t;
typedef struct _login_s      _login_t,    *_lplogin_t;


struct _register_s{
    int         iType;          //类型
    char        username[56];   //用户名
    char        passward[40];   //密码
};

struct _login_s{
    char        username[56];
    char        password[40];
};

#pragma pack()

#endif