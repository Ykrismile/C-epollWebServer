#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "epollhttp.h"

int main(int argc, char *argv[])
{
	//判断命令行参数
	if(argc<3){
		printf("./a.out port path\n");
        exit(1);
	}	

	//获取端口
	int port = atoi(argv[1]);

	//改变进程工作目录
	int ret = chdir(argv[2]);
	if(-1 == ret){
		perror("chdir error");
		exit(1);
	}

	//启动epoll监听
	epoll_run(port);

	return 0;
}