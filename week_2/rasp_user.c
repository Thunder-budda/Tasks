#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#define IO_MAGIC 'R'
#define SET_BUF_SIZE _IOW(IO_MAGIC, 1, int)

int main() {
    int fd;
    int new_size;
    size_t size;
    char *u_buf;
    char *r_buf;

    fd = open("/dev/rasp_driver", O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // 1. 通过ioctl动态调整内核缓冲区大小
    printf("请输入新的内核缓冲区大小(字节): ");
    scanf("%d", &new_size);

    if (ioctl(fd, SET_BUF_SIZE, &new_size) < 0) {
        perror("ioctl SET_BUF_SIZE");
        close(fd);
        return -1;
    }
    printf("内核缓冲区已调整为 %d 字节\n", new_size);

    // 2. 写入数据
    printf("请输入你想发送给内核的数据长度: ");
    scanf("%zu", &size);

    u_buf = (char *)malloc(size + 1);
    if (!u_buf) {
        perror("malloc");
        close(fd);
        return -1;
    }

    printf("输入具体内容: ");
    scanf("%s", u_buf);

    ssize_t written = write(fd, u_buf, strlen(u_buf) + 1);
    if (written < 0) {
        perror("write");
    } else {
        printf("成功写入 %zd 字节\n", written);
    }

    // 3. 读回数据验证
    r_buf = (char *)malloc(size + 1);
    if (!r_buf) {
        perror("malloc");
        free(u_buf);
        close(fd);
        return -1;
    }
    memset(r_buf, 0, size + 1);

    ssize_t rd = read(fd, r_buf, size);
    if (rd < 0) {
        perror("read");
    } else {
        printf("从内核读回 %zd 字节: %s\n", rd, r_buf);
    }

    free(u_buf);
    free(r_buf);
    close(fd);
    return 0;
}
