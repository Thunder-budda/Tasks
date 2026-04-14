#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/kvmalloc_driver"
#define TEST_SIZE (1024 * 1024)
#define FIRST_MARK 0x5a
#define LAST_MARK 0xa5

int main(void)
{
    int fd;
    ssize_t written;
    ssize_t total_read;
    ssize_t current_read;
    unsigned char *write_buf;
    unsigned char *read_buf;
    size_t index;

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    write_buf = malloc(TEST_SIZE);
    read_buf = malloc(TEST_SIZE);
    if (!write_buf || !read_buf) {
        perror("malloc");
        close(fd);
        free(write_buf);
        free(read_buf);
        return 1;
    }

    for (index = 0; index < TEST_SIZE; index++)
        write_buf[index] = (unsigned char)(index & 0xff);

    write_buf[0] = FIRST_MARK;
    write_buf[TEST_SIZE - 1] = LAST_MARK;
    memset(read_buf, 0, TEST_SIZE);

    written = write(fd, write_buf, TEST_SIZE);
    if (written != TEST_SIZE) {
        if (written < 0)
            perror("write");
        else
            fprintf(stderr, "write: expected %d bytes, got %zd\n", TEST_SIZE, written);
        close(fd);
        free(write_buf);
        free(read_buf);
        return 1;
    }

    total_read = 0;
    while (total_read < TEST_SIZE) {
        current_read = read(fd, read_buf + total_read, TEST_SIZE - total_read);
        if (current_read < 0) {
            perror("read");
            close(fd);
            free(write_buf);
            free(read_buf);
            return 1;
        }
        if (current_read == 0)
            break;
        total_read += current_read;
    }

    printf("write first=0x%02x last=0x%02x\n", write_buf[0], write_buf[TEST_SIZE - 1]);
    if (total_read > 0)
        printf("read  first=0x%02x last=0x%02x\n", read_buf[0], read_buf[total_read - 1]);
    printf("read bytes=%zd\n", total_read);

    if (total_read != TEST_SIZE) {
        fprintf(stderr, "read: expected %d bytes, got %zd\n", TEST_SIZE, total_read);
        close(fd);
        free(write_buf);
        free(read_buf);
        return 1;
    }

    if (memcmp(write_buf, read_buf, TEST_SIZE) != 0) {
        fprintf(stderr, "verify: payload mismatch\n");
        close(fd);
        free(write_buf);
        free(read_buf);
        return 1;
    }

    printf("verify: userspace readback matches written data\n");
    printf("check dmesg for both kmalloc/vmalloc boundary bytes logged by the driver\n");

    close(fd);
    free(write_buf);
    free(read_buf);
    return 0;
}