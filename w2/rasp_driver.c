#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>


#define DEVICE_NAME "rasp_driver"
#define CLASS_NAME "rasp_class"
#define IO_MAGIC 'R'
#define SET_BUF_SIZE _IOW(IO_MAGIC, 1, int)
#define DEFAULT_BUF_SIZE 1024 //默认缓冲区大小


static size_t curr_size = 0;
static size_t data_len = 0; //实际数据长度
static char* kernel_buffer = NULL;//内核缓冲区指针
static DEFINE_MUTEX(rasp_mutex);

static dev_t dev;
static struct cdev* rasp_cdev;

static struct class* rasp_class = NULL;
static struct device* rasp_device = NULL;

MODULE_LICENSE("GPL");


static int rasp_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "Raspberry 4b Device opened\n");
    return 0;
}

static int rasp_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "Raspberry 4b Device closed\n");
    return 0;
}


static ssize_t rasp_write(struct file *filp, const char __user *buf, size_t len, loff_t *offt)
{
    mutex_lock(&rasp_mutex);

    if (!kernel_buffer || curr_size == 0) {
        mutex_unlock(&rasp_mutex);
        return -ENOMEM;
    }

    //限制写入长度不超过缓冲区大小
    if (len > curr_size)
        len = curr_size;

    if (copy_from_user(kernel_buffer, buf, len)) {//从用户空间复制数据到内核空间
        mutex_unlock(&rasp_mutex);
        return -EFAULT;
    }
    data_len = len; //记录实际数据长度

    printk(KERN_INFO "Data written to Raspberry 4b Device: %zu bytes\n", len);

    mutex_unlock(&rasp_mutex);
    return len;
}

static ssize_t rasp_read(struct file *filp, char __user *buf, size_t len, loff_t *offt)
{
    size_t to_read;

    mutex_lock(&rasp_mutex);

    if (!kernel_buffer || data_len == 0 || *offt >= data_len) {
        mutex_unlock(&rasp_mutex);
        return 0; //无数据或已读完，返回EOF
    }

    to_read = data_len - *offt;
    if (to_read > len)
        to_read = len;

    if (copy_to_user(buf, kernel_buffer + *offt, to_read)) {
        mutex_unlock(&rasp_mutex);
        return -EFAULT;
    }

    *offt += to_read; //更新偏移量
    printk(KERN_INFO "Data read from Raspberry 4b Device: %zu bytes\n", to_read);

    mutex_unlock(&rasp_mutex);
    return to_read;
}

static long rasp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int new_size;
    char* new_ptr;

    switch(cmd){
        case SET_BUF_SIZE:
            if (copy_from_user(&new_size, (int __user *)arg, sizeof(int))) {
                return -EFAULT;
            }
            if (new_size <= 0) {
                return -EINVAL;
            }

            mutex_lock(&rasp_mutex);
            //动态调整内核缓冲区大小
            new_ptr = krealloc(kernel_buffer, new_size, GFP_KERNEL);
            if(!new_ptr) {
                mutex_unlock(&rasp_mutex);
                return -ENOMEM;
            }
            kernel_buffer = new_ptr;
            curr_size = new_size;
            mutex_unlock(&rasp_mutex);
            printk(KERN_INFO "Kernel buffer resized to: %d bytes\n", new_size);

            break;

        default:
            return -EINVAL;
    }
    return 0;
}

//操作函数集合
struct file_operations rasp_fops = {
    .owner = THIS_MODULE, //THIS_MODULE宏定义为指向当前模块的指针
    .release = rasp_release, //关闭设备函数
    .open = rasp_open,     //打开设备函数
    .write = rasp_write, //写设备函数
    .read = rasp_read,   //读设备函数
    .unlocked_ioctl = rasp_ioctl, //ioctl函数指针
};



static int __init rasp_buffer_manage_init(void)
{
    printk(KERN_INFO "Real LED Driver Initialized\n");

    //分配默认缓冲区
    kernel_buffer = kmalloc(DEFAULT_BUF_SIZE, GFP_KERNEL);
    if (!kernel_buffer) {
        printk(KERN_ERR "Failed to allocate kernel buffer\n");
        return -ENOMEM;
    }
    curr_size = DEFAULT_BUF_SIZE;

    //定义（申请cdev变量）
    rasp_cdev = cdev_alloc();
    //动态分配设备号
    if(alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0){
        kfree(kernel_buffer);
        kernel_buffer = NULL;
        printk(KERN_INFO "Failed to allocate char device region\n");
        return -EINVAL;
    }

     //创建设备类
    rasp_class = class_create(CLASS_NAME);
    if (IS_ERR(rasp_class)) {
        unregister_chrdev_region(dev, 1);
        kfree(kernel_buffer);
        kernel_buffer = NULL;
        printk(KERN_ERR "Failed to create device class\n");
        return PTR_ERR(rasp_class);
    }
    //创建设备节点
    rasp_device = device_create(rasp_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(rasp_device)) {
        class_destroy(rasp_class);
        unregister_chrdev_region(dev, 1);
        kfree(kernel_buffer);
        kernel_buffer = NULL;
        printk(KERN_ERR "Failed to create device node\n");
        return PTR_ERR(rasp_device);
    }

    //初始化cdev变量
    cdev_init(rasp_cdev, &rasp_fops);
    //注册cdev变量
    cdev_add(rasp_cdev, dev, 1);
  
    return 0;
}

static void __exit rasp_buffer_manage_exit(void)
{
    //释放内核缓冲区
    kfree(kernel_buffer);
    kernel_buffer = NULL;
    //注销cdev变量 释放cdev变量空间
    cdev_del(rasp_cdev);
    unregister_chrdev_region(dev, 1);
    //销毁设备节点和设备类
    device_destroy(rasp_class, dev);
    class_destroy(rasp_class);

    printk(KERN_INFO "Raspberry 4b Driver Exited\n");
}

module_init(rasp_buffer_manage_init);
module_exit(rasp_buffer_manage_exit);
