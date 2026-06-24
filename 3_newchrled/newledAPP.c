#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 *argc:应用程序参数个数
 *argv[]:具体的参数内容，字符串形式
 *./ledAPP  <filename>  <0:1> 0表示关灯，1表示开灯
 * */

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }
    int fd;
    char *filename = argv[1];
    unsigned char databuf[1];
    int ret;

    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }

    databuf[1] = (unsigned char)atoi(argv[2]);
    ret = write(fd, databuf, 1);
    if (ret < 0)
    {
        perror("write()");
        close(fd);
        exit(1);
    }

    close(fd);
    exit(0);
}