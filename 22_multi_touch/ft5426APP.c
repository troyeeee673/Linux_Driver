#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>

int main(int argc, char **argv)
{
    ssize_t err;
    int fd;
    char *filename = argv[1];
    int data[3];
    unsigned short ir, ps, als;

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
        err = read(fd, data, sizeof(data));
        if (err == 0)
        {
            ir = data[0];
            als = data[1];
            ps = data[2];
            printf("AP3216C ir = %d, als = %d, ps = %d\r\n", ir, als, ps);
        }
        usleep(200000);/*200ms*/
    }

    close(fd);
    exit(0);
}