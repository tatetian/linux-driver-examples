#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <new>
#include <stdlib.h>
#define CMD_SESSION_KEY      _IOW('f', 20, unsigned long)

#define BUF_SIZE 512

int normal_read(int argc, char** argv) {
    if(argc < 2) return -1;
    int fd = open(argv[1], O_RDONLY | O_DIRECT);
    ioctl(fd, CMD_SESSION_KEY, 1);
    
    ///char buf[BUF_SIZE] = {0};
    void* mem;
    char* buf;
    posix_memalign(&mem, 512, BUF_SIZE);
    buf = new(mem) char[BUF_SIZE];

    int cnt = read(fd, buf, BUF_SIZE);
    if(cnt < 0) {
	    printf("failed to read!\n");
	    return 1;
    }
    printf("before while: cnt = %d\n", cnt);
	    
    while(cnt)
    {
	buf[cnt] = 0;
        printf("%s", buf);
	cnt = read(fd, buf, BUF_SIZE);
    }
    return 0;
}

int user1_write(int argc, char** argv) {
    if(argc < 3) return -1;

    int fd = open(argv[1], O_DIRECT | O_RDWR | O_CREAT);
    ioctl(fd, CMD_SESSION_KEY, 1);
   
    void *mem;
    char* buf;
    posix_memalign(&mem, 512, BUF_SIZE);
    buf = new(mem) char[BUF_SIZE];
    strcpy(buf, argv[2]);
    int cnt = write(fd, buf, strlen(argv[2]));
    if(cnt > 0) return 0;

    return -1;
}

int main(int argc, char **argv)
{
	//return normal_read(argc, argv);
	return user1_write(argc, argv);
}
