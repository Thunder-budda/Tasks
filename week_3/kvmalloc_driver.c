#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>


#define DEVICE_NAME "kvmalloc_driver"
#define CLASS_NAME "kvmalloc_class"
#define DEFAULT_BUF_SIZE (1024 * 1024) //1MB缓冲区


static size_t data_len = 0; //实际数据长度
static char* kernel_buffer_kmalloc = NULL; //kmalloc缓冲区指针
static char* kernel_buffer_vmalloc = NULL; //vmalloc缓冲区指针
static char* kernel_buffer = NULL; //当前使用的缓冲区指针
static DEFINE_MUTEX(kvmalloc_mutex);

static dev_t dev;
static struct cdev* kvmalloc_cdev;

static struct class* kvmalloc_class = NULL;
static struct device* kvmalloc_device = NULL;

MODULE_LICENSE("GPL");


static void log_buffer_boundaries(const char *tag)
{
    unsigned char kmalloc_first;
    unsigned char kmalloc_last;
    unsigned char vmalloc_first;
    unsigned char vmalloc_last;

    if (!kernel_buffer_kmalloc || !kernel_buffer_vmalloc)
        return;

    kmalloc_first = kernel_buffer_kmalloc[0];
    kmalloc_last = kernel_buffer_kmalloc[DEFAULT_BUF_SIZE - 1];
    vmalloc_first = kernel_buffer_vmalloc[0];
    vmalloc_last = kernel_buffer_vmalloc[DEFAULT_BUF_SIZE - 1];

    printk(KERN_INFO "%s kmalloc[first=0x%02x last=0x%02x] vmalloc[first=0x%02x last=0x%02x]\n",
           tag, kmalloc_first, kmalloc_last, vmalloc_first, vmalloc_last);
}


static int kvmalloc_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "kvmalloc Device opened\n");
    return 0;
}

static int kvmalloc_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "kvmalloc Device closed\n");
    return 0;
}


static ssize_t kvmalloc_write(struct file *filp, const char __user *buf, size_t len, loff_t *offt)
{
    unsigned char first_byte;
    unsigned char last_byte;

    mutex_lock(&kvmalloc_mutex);

    if (!kernel_buffer_kmalloc || !kernel_buffer_vmalloc) {
        mutex_unlock(&kvmalloc_mutex);
        return -ENOMEM;
    }

    if (len == 0) {
        data_len = 0;
        mutex_unlock(&kvmalloc_mutex);
        return 0;
    }

    //限制写入长度不超过缓冲区大小
    if (len > DEFAULT_BUF_SIZE)
        len = DEFAULT_BUF_SIZE;

    if (copy_from_user(kernel_buffer_kmalloc, buf, len) ||
        copy_from_user(kernel_buffer_vmalloc, buf, len)) {
        mutex_unlock(&kvmalloc_mutex);
        return -EFAULT;
    }

    first_byte = kernel_buffer_kmalloc[0];
    last_byte = kernel_buffer_kmalloc[len - 1];
    kernel_buffer_kmalloc[0] = first_byte;
    kernel_buffer_kmalloc[DEFAULT_BUF_SIZE - 1] = last_byte;
    kernel_buffer_vmalloc[0] = first_byte;
    kernel_buffer_vmalloc[DEFAULT_BUF_SIZE - 1] = last_byte;
    kernel_buffer = kernel_buffer_kmalloc;
    data_len = len; //记录实际数据长度

    printk(KERN_INFO "Data written to kvmalloc Device: %zu bytes\n", len);
    log_buffer_boundaries("After write:");

    mutex_unlock(&kvmalloc_mutex);
    return len;
}

static ssize_t kvmalloc_read(struct file *filp, char __user *buf, size_t len, loff_t *offt)
{
    size_t to_read;

    mutex_lock(&kvmalloc_mutex);

    if (!kernel_buffer || data_len == 0 || *offt >= data_len) {
        mutex_unlock(&kvmalloc_mutex);
        return 0; //无数据或已读完，返回EOF
    }

    log_buffer_boundaries("Before read:");

    to_read = data_len - *offt;
    if (to_read > len)
        to_read = len;

    if (copy_to_user(buf, kernel_buffer + *offt, to_read)) {
        mutex_unlock(&kvmalloc_mutex);
        return -EFAULT;
    }

    *offt += to_read; //更新偏移量
    printk(KERN_INFO "Data read from kvmalloc Device: %zu bytes\n", to_read);

    mutex_unlock(&kvmalloc_mutex);
    return to_read;
}

//操作函数集合
struct file_operations kvmalloc_fops = {
    .owner = THIS_MODULE, //THIS_MODULE宏定义为指向当前模块的指针
    .release = kvmalloc_release, //关闭设备函数
    .open = kvmalloc_open,     //打开设备函数
    .write = kvmalloc_write, //写设备函数
    .read = kvmalloc_read,   //读设备函数
};



static int __init kvmalloc_buffer_manage_init(void)
{
    int ret = -ENOMEM;
    printk(KERN_INFO "kvmalloc Driver Initialized\n");

    //使用kmalloc分配1MB缓冲区
    kernel_buffer_kmalloc = kmalloc(DEFAULT_BUF_SIZE, GFP_KERNEL);
    if (!kernel_buffer_kmalloc) {
        printk(KERN_ERR "Failed to allocate kernel buffer with kmalloc\n");
        return -ENOMEM;
    }
    memset(kernel_buffer_kmalloc, 0, DEFAULT_BUF_SIZE);
    printk(KERN_INFO "Kernel buffer allocated by kmalloc at address: 0x%p\n", kernel_buffer_kmalloc);

    //使用vmalloc分配1MB虚拟缓冲区
    kernel_buffer_vmalloc = vmalloc(DEFAULT_BUF_SIZE);
    if (!kernel_buffer_vmalloc) {
        printk(KERN_ERR "Failed to allocate kernel buffer with vmalloc\n");
        goto cleanup_kmalloc;
    }
    memset(kernel_buffer_vmalloc, 0, DEFAULT_BUF_SIZE);
    printk(KERN_INFO "Kernel buffer allocated by vmalloc at address: 0x%p\n", kernel_buffer_vmalloc);

    kernel_buffer = kernel_buffer_kmalloc; //默认使用kmalloc分配的缓冲区

    //定义（申请cdev变量）
    kvmalloc_cdev = cdev_alloc();
    if (!kvmalloc_cdev)
        goto cleanup_buffers;

    //动态分配设备号
    if(alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0){
        ret = -EINVAL;
        printk(KERN_INFO "Failed to allocate char device region\n");
        goto cleanup_buffers;
    }

    //创建设备类
    kvmalloc_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(kvmalloc_class)) {
        printk(KERN_ERR "Failed to create device class\n");
        ret = PTR_ERR(kvmalloc_class);
        goto cleanup_chrdev;
    }

    //创建设备节点
    kvmalloc_device = device_create(kvmalloc_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(kvmalloc_device)) {
        printk(KERN_ERR "Failed to create device node\n");
        ret = PTR_ERR(kvmalloc_device);
        goto cleanup_class;
    }

    //初始化cdev变量
    cdev_init(kvmalloc_cdev, &kvmalloc_fops);
    //注册cdev变量
    cdev_add(kvmalloc_cdev, dev, 1);
  
    return 0;

cleanup_class:
    class_destroy(kvmalloc_class);
cleanup_chrdev:
    unregister_chrdev_region(dev, 1);
cleanup_buffers:
    vfree(kernel_buffer_vmalloc);
    kernel_buffer_vmalloc = NULL;
cleanup_kmalloc:
    kfree(kernel_buffer_kmalloc);
    kernel_buffer_kmalloc = NULL;
    kernel_buffer = NULL;
    return ret ? ret : -ENOMEM;
}

static void __exit kvmalloc_buffer_manage_exit(void)
{
    //释放kmalloc分配的缓冲区
    kfree(kernel_buffer_kmalloc);
    kernel_buffer_kmalloc = NULL;
    //释放vmalloc分配的虚拟缓冲区
    vfree(kernel_buffer_vmalloc);
    kernel_buffer_vmalloc = NULL;
    kernel_buffer = NULL;
    //注销cdev变量 释放cdev变量空间
    cdev_del(kvmalloc_cdev);
    unregister_chrdev_region(dev, 1);
    //销毁设备节点和设备类
    device_destroy(kvmalloc_class, dev);
    class_destroy(kvmalloc_class);

    printk(KERN_INFO "kvmalloc Driver Exited\n");
}

module_init(kvmalloc_buffer_manage_init);
module_exit(kvmalloc_buffer_manage_exit);
