#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__

#define NGX_MAX_ERROR_STR   2048

//简单功能函数----------------------------
#define ngx_cpymem(dst, src, n) (((u_char *) memcpy(dst, src, n)) + (n))


//日志相关---------------------------------
//日志等级共9个，级别从高到低，数字最小的级别最高
#define NGX_LOG_STDERR          0       //控制台错误
#define NGX_LOG_EMERG           1       //紧急
#define NGX_LOG_ALERT           2       //严重
#define NGX_LOG_CRIT            3       //错误
#define NGX_LOG_ERR             4       //错误
#define NGX_LOG_WARN            5       //警告
#define NGX_LOG_NOTICE          6       //注意
#define NGX_LOG_INFO            7       //信息
#define NGX_LOG_DEBUG           8       //调试


#define NGX_ERROR_LOG_PATH      "logs/error1.log"

#endif