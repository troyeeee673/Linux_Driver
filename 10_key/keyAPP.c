#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>



#define KEY0Value 0xF0
#define INVALKEY  0x00
/*
 *argc:应用程序参数个数
 *argv[]:具体的参数内容，字符串形式
 *./ledAPP  <filename>  <0:1> 0表示关灯，1表示开灯
 * */

int main(int argc, char **argv)
{
    int value;
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
    while(1)
    {
        read(fd, &value, sizeof(value));
        if(value == KEY0Value)
        {
            printf("key0 pressed");
        }
    }
    close(fd);
    exit(0);
}