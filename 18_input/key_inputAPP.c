#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static struct input_event inputevent;

int main(int argc, char **argv)
{
    ssize_t err;
    int fd;
    char *filename = argv[1];

    if (argc < 2)
    {
        fprintf(stderr, "Usage...\n");
        exit(1);
    }

    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        perror("open()");
        exit(1);
    }

    while (1)
    {
        err = read(fd, &inputevent, sizeof(inputevent));
        if (err > 0)
        {
            switch (inputevent.type)
            {
            case EV_KEY:
                // printf("key wad pressed\r\n");
                /*判断是按键还是button*/
                if(inputevent.code < BTN_MISC)//key按下
                {
                    printf("key %d %s\n", inputevent.code, inputevent.value);
                }
                else{
                    printf("btn %d %s\n", inputevent.code, inputevent.value);
                }
                break;
            case EV_SYN:
                // printf("sync event\r\n");
                break;
            }
        }
        else
        {
            printf("read failed\r\n");
        }
    }

    close(fd);
    exit(0);
}