#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include "epollhttp.h"

#define MAXSIZE 2048

void send_error(int cfd, int status, char *title, char *text)
{
	char buf[4096] = {0};

	sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
	sprintf(buf+strlen(buf), "Content-Type:%s\r\n", "text/html");
	sprintf(buf+strlen(buf), "Content-Length:%d\r\n", -1);
	sprintf(buf+strlen(buf), "Connection: close\r\n");
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);

	memset(buf, 0, sizeof(buf));

	sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
	sprintf(buf+strlen(buf), "<body bgcolor=\"#cc99cc\"><h2 align=\"center\">%d %s</h4>\n", status, title);
	sprintf(buf+strlen(buf), "%s\n", text);
	sprintf(buf+strlen(buf), "<hr>\n</body>\n</html>\n");
	send(cfd, buf, strlen(buf), 0);
	
	return ;
}

// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');   
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav" ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

//获取一行 \r\n 结尾的数据
int get_line(int cfd, char *buf, int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while((i < size-1) && (c !='\n')){
		n = recv(cfd, &c, 1, 0);
		if(n>0){
			if( c == '\r')	{
				n = recv(cfd, &c, 1, MSG_PEEK);//拷贝读
				if((n>0) && (c=='\n')){
					recv(cfd, &c ,1 ,0);
				}else{
					c= '\r';
				}
			}
			buf[i] = c;
			i++;
		}else {
			c='\n';
		}
	}

	buf[i] = '\0';
	
	if(-1 == n)
		i=n;
	
	return i;	
}


//初始化监听套接字
int init_listen_fd(int port,int epfd)
{
	//创建监听套接字
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if(-1==lfd){
		perror("socket error");
		exit(1);
	}

	//创建服务器地址结构 IP+port
	struct sockaddr_in srv_addr;

	bzero(&srv_addr,sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port);
	srv_addr.sin_addr.s_addr = htons(INADDR_ANY);

	//端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt,sizeof(opt));

	//给lfd绑定地址结构
	int ret = bind(lfd,(struct sockaddr*)&srv_addr,sizeof(srv_addr));
	if(-1 == ret){
		perror("bind error");
		exit(1);
	}

	//设置监听上限
	ret = listen(lfd,128);
	if(-1 == ret){
		perror("listen error");
		exit(1);
	}

	//将lfd上监听树
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd; 
	ret = epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if(-1 == ret){
		perror("epoll_ctl add lfd error");
		exit(1);
	}

	return lfd;	
}

//监听cfd事件
void do_accept(int lfd,int epfd)
{
	struct sockaddr_in clt_addr;
	socklen_t clt_addr_len = sizeof(clt_addr);

	int cfd = accept(lfd, (struct sockaddr*)&clt_addr, &clt_addr_len);	
	if(-1 == cfd){
		perror("accept error");
		exit(1);
	}
	
	//打印客户端地址结构
	char client_ip[64] = {0};
	printf("New Client IP: %s ,Port: %d, cfd = %d\n",
		inet_ntop(AF_INET, &clt_addr.sin_addr.s_addr, client_ip, sizeof(client_ip)),
		ntohs(clt_addr.sin_port), cfd);

	//设置cfd非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd,F_SETFL, flag);

	//cfd上树
	struct epoll_event ev;
	ev.data.fd = cfd;

	//ET
	ev.events = EPOLLIN | EPOLLET;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd ,&ev);
	if(-1 == ret){
		perror("epoll_ctl add cfd error");
		exit(1);
	}
}

//
void disconnect(int cfd,int epfd)
{
	int ret = epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,NULL);
	if(0 != ret){
		perror("epoll_ctl del cfd error");
		exit(1);
	}
	close(cfd);
}

//回发http协议应答
void send_respond( int cfd,int no,const char *dcpt,const char *type, int len)
{
	char buf[1024] = {0};
	sprintf(buf,"HTTP/1.1 %d %s\r\n",no,dcpt);
	sprintf(buf+strlen(buf),"%s\r\n",type);
	sprintf(buf+strlen(buf),"Content-Length:%d\r\n",len);
	sprintf(buf+strlen(buf),"\r\n");
	send(cfd,buf,strlen(buf),0);

}

//回发文件内容
void send_file(int cfd,const char *file)
{
	int n=0,ret;
	char buf[1024];

	int fd=open(file, O_RDONLY);
	if(fd ==-1){
		//404
		send_error(cfd,404,"Not Found","No such file or direntry");
		perror("open error");
		exit(1);
	}
	while((n=read(fd,buf,sizeof(buf)))>0){
		ret = send(cfd,buf,n,0);
		if(ret==-1){
			printf("errno = %d\n",errno);
			if(errno == EAGAIN){
				printf("EAGAIN\n");
				continue;
			}else if(errno == EINTR){
				printf("EINTR");
				continue;
			}else{
				perror("send error");
				exit(1);
			}
		}
	}

	close(fd);
}

void send_dir(int cfd,const char *dirname)
{
	int i, ret;

    // 拼一个html页面<table></table>
    char buf[4094] = {0};

    sprintf(buf, "<html><head><title>dir: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>dirname: %s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    
    // 目录项二级指针
    struct dirent** ptr;
    int num = scandir(dirname, &ptr, NULL, alphasort);
    
    // 遍历
    for(i = 0; i < num; ++i) {
    
        char* name = ptr[i]->d_name;

        // 拼接文件的完整路径
        sprintf(path, "%s/%s", dirname, name);
        printf("path = %s ===================\n", path);
        struct stat st;
        stat(path, &st);

		// 编码生成 %E5 %A7 之类的东西
        encode_str(enstr, sizeof(enstr), name);
        
        // 如果是文件
        if(S_ISREG(st.st_mode)) {       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        } else if(S_ISDIR(st.st_mode)) {		// 如果是目录       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                perror("send error:");
                continue;
            } else if (errno == EINTR) {
                perror("send error:");
                continue;
            } else {
                perror("send error:");
                exit(1);
            }
        }
        memset(buf, 0, sizeof(buf));
        // 字符串拼接
    }

    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
}

//处理http请求
void http_request(int cfd,const char *request)
{
		// 拆分http请求行
    char method[12], path[1024], protocol[12];
    sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
    printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);

    // 转码 将不能识别的中文乱码 -> 中文
    // 解码 %23 %34 %5f
    decode_str(path, path);
        
    char* file = path+1; // 去掉path中的/ 获取访问文件名
    
    // 如果没有指定访问的资源, 默认显示资源目录中的内容
    if(strcmp(path, "/") == 0) {    
        // file的值, 资源目录的当前位置
        file = "./";
    }

    // 获取文件属性
    struct stat st;
    int ret = stat(file, &st);
    if(ret == -1) { 
        send_error(cfd, 404, "Not Found", "NO such file or direntry");     
        return;
    }

		//为普通文件
		if(S_ISREG(st.st_mode)){
			//回发http协议应答
			//send_respond(cfd,200,"OK"," Content-Type: text/plain; charset=iso-8859-1",sbuf.st_size);
			send_respond(cfd,200,"OK",get_file_type(file),st.st_size);

			//回发文件内容
			send_file(cfd,file);
		}else if(S_ISDIR(st.st_mode)){
			send_respond(cfd,200,"OK",get_file_type(".html"),-1);
			
			send_dir(cfd,file);
		}
		return;
	
}

void do_read(int cfd,int epfd)
{	
 // 将浏览器发过来的数据, 读到buf中 
    char line[1024] = {0};
    // 读请求行
    int len = get_line(cfd, line, sizeof(line));
    if(len == 0) {   
        printf("客户端断开了连接...\n");
        // 关闭套接字, cfd从epoll上del
        disconnect(cfd, epfd);         
    } else { 
    	printf("============= 请求头 ============\n");   
        printf("请求行数据: %s", line);
        // 还有数据没读完,继续读走
		while (1) {
			char buf[1024] = {0};
			len = get_line(cfd, buf, sizeof(buf));	
			if (buf[0] == '\n') {
				break;	
			} else if (len == -1)
				break;
		}
        printf("============= The End ============\n");
    }
    
    // 判断get请求
    if(strncasecmp("get", line, 3) == 0) { // 请求行: get /hello.c http/1.1   
        // 处理http请求
        http_request(cfd, line);
        
        // 关闭套接字, cfd从epoll上del
        disconnect(cfd, epfd);         
    }
}

void epoll_run(int port)
{
	int i=0;
	struct epoll_event all_events[MAXSIZE];

	//创建epoll监听树根
	int epfd = epoll_create(MAXSIZE);
	if(-1 == epfd){
		perror("epoll_create error");
		exit(1);
	}

	//创建lfd,添加至监听树
	int lfd = init_listen_fd(port,epfd);

	while(1){
		//监听节点对应事件
		int ret = epoll_wait (epfd, all_events,MAXSIZE,-1);
		if(-1 == ret){
			perror("epoll_wait error");
			exit(1);
		}

		for( i = 0; i<ret; ++i){
			//只处理读事件
			struct epoll_event *pev = &all_events[i];

			//不是读事件	
			if(!(pev->events & EPOLLIN)){
					continue;
					}

					if(pev->data.fd == lfd){
					do_accept(lfd,epfd);
					} else{
					do_read(pev->data.fd,epfd);
					}

		}
	}		

}

void encode_str(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {    
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {      
            *to = *from;
            ++to;
            ++tolen;
        } else {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}

void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from  ) {     
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {       
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;                      
        } else {
            *to = *from;
        }
    }
    *to = '\0';
}

int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

