/*
 * @Author: your name
 * @Date: 2020-12-13 14:58:07
 * @LastEditTime: 2021-01-08 10:40:55
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /mztkn_study_nginx/nginx/_include/ngx_global.h
 */
#ifndef __NGX_GBLDEF_H__
#define __NGX_GBLDEF_H__

#include "ngx_c_socket.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_slogic.h"
#include "HandlerFactory.h"

typedef struct
{
	char ItemName[50];
	char ItemContent[500];
}CConfItem, *LPCConfItem;

//和运行日志相关
typedef struct{
	int		log_level;								//日志级别
	int		fd;										//日志文件描述符
}ngx_log_t;


/***********************外部全局变量声明*******************************/
extern int 				g_environlen;
extern size_t 			g_envneddmem;
extern size_t 			g_argvneedmem;
extern int    			g_os_argc;
extern char* 			gp_envmem;
extern char** 			g_os_argv;

extern pid_t 			ngx_pid;
extern pid_t 			ngx_parent;
extern ngx_log_t 		ngx_log;


extern int   			g_daemonized;				// 守护进程标记	
extern int 	 			ngx_process;
extern int   			ngx_reap;
extern int	 			g_stopEvent;

extern CLogicSocket 	g_socket;
extern CThreadPool 		g_threadpool;

extern HandlerFactory 	g_handlerFactory;


#endif