#ifndef _EPOLL_SERVER_H
#define _EPOLL_SERVER_H

// 通过文件名获取文件的类型
const char *get_file_type(const char *name);

//获取一行 \r\n 结尾的数据
int get_line(int cfd, char *buf, int size);

//初始化监听套接字
int init_listen_fd(int port,int epfd);

//监听cfd事件
void do_accept(int lfd,int epfd);

//断开连接
void disconnect(int cfd,int epfd);

//回发http协议应答
void send_respond( int cfd,int no,const char *dcpt,const char *type, int len);

//回发文件内容
void send_file(int cfd,const char *file);

//回发目录内容
void send_dir(int cfd,const char *dirname);

void encode_str(char* to, int tosize, const char* from);

void decode_str(char *to, char *from);

int hexit(char c);

//处理http请求
void http_request(int cfd,const char *file);

//处理cfd业务
void do_read(int cfd,int epfd);

//启动epoll监听
void epoll_run(int port);

//发送404html页面
void send_error(int cfd, int status, char *title, char *text);


#endif