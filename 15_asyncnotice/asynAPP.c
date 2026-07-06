#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/signal.h>
// #include <signal.h>

int fd;

static  void sigio_signal_func()
{
    int err;
    unsigned int keyvalue;
    err = read(fd, &keyvalue, sizeof(keyvalue));
    if(err < 0)
    {
        perror("read()");
        close(fd);
        exit(1);
    }
    printf("keyvalue = %d\n", keyvalue);


}
int main(int argc, char **argv)
{
    int flags = 0;
    unsigned char data;
    if (argc < 2)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }
    char *filename = argv[1];
    int ret;

    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }

    /*设置信号处理函数*/
    signal(SIGIO, sigio_signal_func);
    fcntl(fd, F_SETOWN, getpid());//设置当前进程的接收SIGIO信号
    //开启异步通知
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | FIOASYNC);

    while(1);
    close(fd);
    exit(0);
}