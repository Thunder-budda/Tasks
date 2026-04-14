KERNEL_PATH=/home/kamchio/libs/linux-5.4.31
ARCH=arm
CROSS_COMPILE=arm-linux-gnueabihf-
CC=$(CROSS_COMPILE)gcc
USER_TEST=kvmalloc_user_test

# 树莓派本机编译时使用本地内核头文件
RPI_KERNEL_PATH=/lib/modules/$(shell uname -r)/build

ifeq ($(KERNELRELEASE),) 

all:
	#make -C 进入指定目录进行编译（KERNEL_PATH）, M=$(PWD) 指定编译当前目录
	make -C $(KERNEL_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules 

user:
	$(CC) -Wall -Wextra -O2 -o $(USER_TEST) kvmalloc_user_test.c

# 在树莓派4b上本机编译，无需交叉编译器
rpi:
	make -C $(RPI_KERNEL_PATH) M=$(PWD) modules

clean:
	make -C $(KERNEL_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean
	rm -f $(USER_TEST)

rpi-clean:
	make -C $(RPI_KERNEL_PATH) M=$(PWD) clean

else

obj-m += kvmalloc_driver.o #要编译的内核模块

endif
