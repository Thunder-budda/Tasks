KERNEL_PATH=/home/kamchio/driver/linux-rpi-6.12.y  #raspberry pi宏定义内核地址（主机交叉编译用）
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

# 树莓派本机编译时使用本地内核头文件
RPI_KERNEL_PATH=/lib/modules/$(shell uname -r)/build

ifeq ($(KERNELRELEASE),) 

all:
	#make -C 进入指定目录进行编译（KERNEL_PATH）, M=$(PWD) 指定编译当前目录
	make -C $(KERNEL_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules 

# 在树莓派4b上本机编译，无需交叉编译器
rpi:
	make -C $(RPI_KERNEL_PATH) M=$(PWD) modules

clean:
	make -C $(KERNEL_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

rpi-clean:
	make -C $(RPI_KERNEL_PATH) M=$(PWD) clean

else

obj-m += kvmalloc_driver.o #要编译的内核模块

endif
