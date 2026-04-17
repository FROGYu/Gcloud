#pragma once

/*
    这里声明服务端启动入口函数。总入口 main 后面根据命令行参数判断要启动 server
    还是 client 时，会转发到这个函数继续执行服务端自己的初始化和事件循环。
*/
int RunServerMain(int argc, char* argv[]);
