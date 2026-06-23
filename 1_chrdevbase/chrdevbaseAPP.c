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
 *./chrdevbaseAPP  <filename>  <1:2> 1表示读，2表示写
 * ./chrdevbaseAPP /dev/chrdevbase 1      表示从驱动里面读数据
 * ./chrdevbaseAPP /dev/chrdevbase 2      表示向驱动里面写数据
 * */

int main(int argc, char **argv)
{
    char readbuf[100], writebuf[100];
    static char usrdata[] = "user data";
    int fd;
    if (argc != 3)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }

    char *filename = argv[1];
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }

    if (atoi(argv[2]) == 1)
    {
        /*read*/
        int len = read(fd, readbuf, 50);
        if (len < 0)
        {
            perror("read()");
            exit(1);
        }
        else
        {
            printf("APP read data:%s\r\n", readbuf);
        }
    }

    else if (atoi(argv[2]) == 2)
    {
        /*write*/
        memcpy(writebuf, usrdata, sizeof(usrdata));
        if (write(fd, writebuf, sizeof(usrdata)) < 0)
        {
            perror("write()");
            exit(1);
        }

    }

    /*close*/
    close(fd);

    exit(0);
}