#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>     //uintptr_t
#include <stdarg.h>     //va_start....
#include <unistd.h>     //STDERR_RILENO
#include <sys/time.h>   //gettimeofday
#include <fcntl.h>      //open
#include <errno.h>      //errno
#include <string.h>
#include <time.h>


#include "ngx_global.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"

//全局量--------------------------------------------------------------
//错误等级
static u_char err_levels[][20] = {
    {"stderr"},             //控制台错误
    {"emerg"},
    {"alert"},
    {"crit"},
    {"error"},
    {"warn"},
    {"notice"},
    {"info"},
    {"debug"}
};

ngx_log_t   ngx_log;



/*********************************************************************
往标准错误上打印一条错误信息，通过可变参数组合输出字符串【支持...省略号形参】，
自动往字符串最末端增加换行符【调用者不用加\n】
示例代码：ngx_log_stderr(0, "Invalid Info %d", 10);
err : 错误代码
**********************************************************************/
void ngx_log_stderr(int err, const char *fmt, ...){
    va_list args;                           //创建一个val_list类型变量
    u_char  errstr[NGX_MAX_ERROR_STR+1];    //2048
    u_char  *p, *last;


    memset(errstr, 0, sizeof(errstr));

    last = errstr + NGX_MAX_ERROR_STR;      //last指向整个buffer最后

    p = ngx_cpymem(errstr, "nginx: ", 7);   //p指向"nginx: "之后

    va_start(args, fmt);                    //使args指向起始的参数
    p = ngx_vslprintf(p, last, fmt, args);
    va_end(args);

    // 如果错误代码不是0，表示有错误发生
    if(err){
        p = ngx_log_errno(p, last, err);
    }

    //若位置不够， 那换行也要应插入到末尾，哪怕覆盖到其他内容
    if(p >= (last - 1)){
        p = (last-1) - 1;
    }
    *p++ = '\n';
    
    write(STDERR_FILENO, errstr, p-errstr);  //往标准错误上写
    
    if(ngx_log.fd > STDERR_FILENO){
        //如果是一个有效的日志文件，本条件成立，往日志文件写信息
        ngx_log_error_core(NGX_LOG_STDERR, err, (const char *)errstr);
    }

    return;
}

//---------------------------------------------------------------------------------------------
//给一段内存，一个错误编号，组合一个字符串
//buf: 内存
//last: 放的数据不超过此处
//err: 错误编号
u_char *ngx_log_errno(u_char *buf, u_char *last, int err){
    
    char *perrorinfo = strerror(err);   //根据errno返回一段错误信息
    size_t len = strlen(perrorinfo);

    //插入一些字符串
    char leftstr[10] = {0};
    sprintf(leftstr, " (%d: ", err);
    size_t leftlen = strlen(leftstr);

    char rightstr[] = ") ";
    size_t rightlen = strlen(rightstr);

    size_t extralen = leftlen + rightlen;  //左右额外宽度
    if((buf + len + extralen)<last){
        buf = ngx_cpymem(buf, leftstr, leftlen);
        buf = ngx_cpymem(buf, perrorinfo, len);
        buf = ngx_cpymem(buf, rightstr, rightlen);
    }
    return buf;
}


//------------------------------------------------------------------------------------------
// 往日志文件中写日志，代码中有自动加换行，如果定向位标准错误，则直接往屏幕上写日志【比如日志文件
// 打不开，那么就会直接定位为屏幕】
// level: 一个等级数字，日志的级别
// err: 是一个错误代码： 如果不是0，就应该转换成显示对应的错误信息，一起写到日志文件中
// 示例 ngx_log_error_core(5, 8, "这个XXX工作有问题，显示的结果是=%s", "YYYY");
void ngx_log_error_core(int level, int err, const char *fmt, ...){
    u_char *last;
    u_char errstr[NGX_MAX_ERROR_STR+1];

    memset(errstr,0,sizeof(errstr));
    last = errstr + NGX_MAX_ERROR_STR;

    struct timeval tv;
    struct tm      tm;
    time_t         sec;
    u_char         *p;    //指向要拷贝数据到其中的内存位置
    va_list        args;

    memset(&tv,0,sizeof(struct timeval));
    memset(&tm,0,sizeof(struct tm));

    gettimeofday(&tv, NULL);       //获取当前时间，返回自1970-01-01 00:00:00 到现在经历的秒数

    sec = tv.tv_sec;
    localtime_r(&sec, &tm);        //把参数1的time_t转换为本地时间，保存到参数2中，带_r是线程安全的版本
    tm.tm_mon++;                   //月份调整正常
    tm.tm_year += 1900;            //年份调整正常

    u_char strcurrtime[40] = {0};   //组合出当前时间字符串，形式如2020/12/14 19:57:11
    ngx_slprintf(strcurrtime,
                 (u_char*)-1,
                 "%4d/%02d/%02d %02d:%02d:%02d",
                 tm.tm_year, tm.tm_mon,
                 tm.tm_mday, tm.tm_hour,
                 tm.tm_min, tm.tm_sec);

    p = ngx_cpymem(errstr, strcurrtime, strlen((const char*)strcurrtime));
    p = ngx_slprintf(p, last, " [%s] ", err_levels[level]);
    p = ngx_slprintf(p, last, "%P: ", ngx_pid);

    va_start(args, fmt);
    p = ngx_vslprintf(p, last, fmt, args);
    va_end(args);

    if(err){
        p = ngx_log_errno(p, last, err);
    }

    if(p>=(last-1)){
        p = (last-1) - 1;
    }
    *p++ = '\n';

    ssize_t n;
    while(1){
        if(level > ngx_log.log_level){
            break;
        }

        //TODO: 磁盘是否满了的判断

        n = write(ngx_log.fd, errstr, p- errstr);
        if(n == -1){
            if(errno == ENOSPC){
                //TODO 磁盘没空间了
            }
            else{
                if(ngx_log.fd != STDERR_FILENO){
                    n = write(STDERR_FILENO, errstr, p - errstr);
                }
            }
        }
        break;
    }
    return ;
}

//-----------------------------------------------------------------------------------
//日志初始化，即把文件打开
void ngx_log_init(){
    u_char *plogname = NULL;
    size_t nlen;

    // 从配置文件中读取和日志相关的配置信息
    CConfig *p_config = CConfig::GetInstance();
    plogname = (u_char *)p_config->GetString("Log");
    if(plogname == NULL){
        //没读到，使用缺省
        plogname = (u_char *) NGX_ERROR_LOG_PATH;
    }
    ngx_log.log_level = p_config->GetIntDefault("LogLevel", NGX_LOG_NOTICE);

    //只写打开|追加到文件莫问|文件不存在则创建，第三个参数需要指定文件访问权限
    //mode = 0644： 文件权限 6：110， 4：100
    ngx_log.fd = open((const char*)plogname, O_WRONLY|O_APPEND|O_CREAT,0644);
    if(ngx_log.fd == -1){
        ngx_log_stderr(errno, "[alert] could not open err log file: open() \"%s\" failed", plogname);
        ngx_log.fd = STDERR_FILENO;
    }
    return;
}
