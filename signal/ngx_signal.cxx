#include <stdio.h>
#include <unistd.h>
#include "ngx_signal.h"

void mysignal(){
    printf("执行了mysignal()函数\n");
    return ;
}