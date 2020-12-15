#ifndef __NGX_GBLDEF_H__
#define __NGX_GBLDEF_H__


typedef struct
{
	char ItemName[50];
	char ItemContent[500];
}CConfItem, *LPCConfItem;
//外部全局变量声明

//和运行日志相关
typedef struct{
	int		log_level;		//日志级别
	int		fd;				//日志文件描述符
}ngx_log_t;


extern int g_environlen;
extern char* gp_envmem;
extern char** g_os_argv;

extern pid_t ngx_pid;
extern ngx_log_t ngx_log;


#endif