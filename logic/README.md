# 关于服务请求处理的一些想法

利用抽象类 class IRequestHandler{} 来定义处理函数的接口
其中有虚函数 bool handler(lpngx_connection_t, lpmsg_header_t, char* , unsigned short ) 