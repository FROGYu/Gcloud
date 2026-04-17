#pragma once

/*
    这里声明客户端启动入口函数。总入口 main 后面根据命令行参数判断要启动 server
    还是 client 时，会转发到这个函数继续执行客户端自己的扫描和同步逻辑。
*/
int RunClientMain(int argc, char* argv[]);
