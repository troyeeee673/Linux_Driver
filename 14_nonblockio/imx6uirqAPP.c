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

int main(int argc, char **argv)
{
    // fd_set readfds;
    struct pollfd fds;
    int timeout;
    unsigned char data;
    if (argc < 2)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }
    int fd;
    char *filename = argv[1];
    int ret;

    fd = open(filename, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }
#if 0
    /*循环读取按键值*/
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 5000000; /*500ms*/
        ret = select(fd + 1, &readfds, NULL, NULL, &timeout);

        switch (ret)
        {
        case 0: // 超时
            printf("超时\n");
            break;
        case -1: // 错误
            break;

        default:
            if (FD_ISSET(fd, &readfds))
            {
                ret = read(fd, &data, sizeof(data));
                if (ret < 0)
                {
                    // printf("read error\r\n");
                }
                else
                {
                    if (data)
                        printf("key value = %#x\r\n", data);
                }
            }
            break;
        }
    }
#endif

    /*循环读取按键值*/
    while (1)
    {
        fds.fd = fd;
        fds.events = POLLIN;
        timeout = 500;
        ret = poll(&fds, 1, timeout);
        if (ret == 0)
        {
            // 超时
            printf("超时\n");
        }
        else if (ret < 0)
        {
            // 错误
        }
        else
        {
            if (fds.revents & POLLIN)
            {
                ret = read(fd, &data, sizeof(data));
                if (ret < 0)
                {
                    // printf("read error\r\n");
                }
                else
                {
                    if (data)
                        printf("key value = %#x\r\n", data);
                }
            }
        }
    }
    close(fd);
    exit(0);
}