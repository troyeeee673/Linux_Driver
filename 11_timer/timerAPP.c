#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#define CLOSE_CMD _IO(0XEF, 1)           // 关闭命令
#define OPEN_CMD _IO(0XEF, 2)            // 打开命令
#define SETPERIOD_CMD _IOW(0xEF, 3, int) // 设置周期

int main(int argc, char **argv)
{
    unsigned int cmd;
    unsigned int arg = 0;
    unsigned char str[100];
    if (argc < 2)
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

    /*循环读取按键值*/
    while (1)
    {
        printf("input cmd:");
        ret = scanf("%d", &cmd);
        if (ret != 1)
        {
            while (getchar() != '\n')
                ; // 清空缓冲区
            continue;
        }
        // 关闭
        if (cmd == 1)
        {
            ioctl(fd, CLOSE_CMD, &arg);
        }
        // 打开
        else if (cmd = 2)
        {
            ioctl(fd, OPEN_CMD, &arg);
        }
        // 设置周期
        else if (cmd == 3)
        {
            printf("input period:");
            ret = scanf("%d", &arg);
            if (ret != 1)
            {
                while (getchar() != '\n')
                    ; // 清空缓冲区
                continue;
            }
            ioctl(fd, SETPERIOD_CMD, &arg);
        }
    }
    close(fd);
    exit(0);
}