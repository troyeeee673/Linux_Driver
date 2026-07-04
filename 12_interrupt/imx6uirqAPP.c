#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>


int main(int argc, char **argv)
{
    unsigned char data;
    if (argc < 2)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }
    int fd;
    char *filename = argv[1];
    int ret;

    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }

    /*循环读取按键值*/
    while (1)
    {
        ret = read(fd, &data, sizeof(data));
        if(ret < 0)
        {
            // printf("read error\r\n");
        }
        else{
            if(data)
                printf("key value = %#x\r\n", data);
        }
    }
    close(fd);
    exit(0);
}