#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>     //uintptr_t
#include <stdarg.h>     //va_start....
#include <unistd.h>     //STDERR_RILENO
#include <sys/time.h>   //gettimeofday
#include <fcntl.h>      //open
#include <errno.h>      //errno
#include <string.h>


#include "ngx_global.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"


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

    
}