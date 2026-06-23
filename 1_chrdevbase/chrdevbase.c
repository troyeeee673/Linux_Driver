#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>          // 提供 struct file_operations, struct inode 等
#include <linux/uaccess.h>     // 提供 copy_to_user, copy_from_user

#define CHRDEVBASE_MAJOR    200
#define CHRDEVBASE_NAME     "chrdevbase"

char readbuf[100], writebuf[100];
static char kerneldata[] = {"kernel data"};

static int chrdevbase_open(struct inode *inode, struct file *filp)
{
    // printk("chrdevbase_open\r\n");
    return 0;
}

static int chrdevbase_release(struct inode *inode, struct file *filp)
{
    // printk("chrdevbase_release\r\n");
    return 0;
}

static ssize_t chrdevbase_read(struct file *filp, __user char *buf, size_t count, loff_t *ppos)
{
    // 将内核数据拷贝到 readbuf
    memcpy(readbuf, kerneldata, sizeof(kerneldata));
    
    // 将 readbuf 拷贝到用户空间 buf
    if(copy_to_user(buf, readbuf, count) != 0)
    {
        printk("copy_to_user() failed\r\n");
        return -EFAULT;
    }
    return 0;
}

static ssize_t chrdevbase_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    // 从用户空间 buf 拷贝到 writebuf
    if(copy_from_user(writebuf, buf, count) != 0)
    {
        printk("copy_from_user() failed\r\n");
        return -EFAULT;
    }
    printk("kernel recevdata:%s\r\n", writebuf);
    return 0;
}

/*字符设备的操作集合*/
static struct file_operations chrdevbase_fops={
    .owner = THIS_MODULE,
    .open = chrdevbase_open,
    .release = chrdevbase_release,
    .read = chrdevbase_read,
    .write = chrdevbase_write
};

static int __init chrdevbase_init(void)
{
    int ret = 0;
    printk(KERN_INFO "chrdevbase init\n");

    /*注册设备*/
    ret = register_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME, &chrdevbase_fops );
    if(ret < 0)
    {
        printk("chrdevbase init failed!\r\n");
        return ret;
    }

    return 0;
}

static void __exit chrdevbase_exit(void)
{
    printk(KERN_INFO "chrdevbase exit\n");
    /* 注销字符设备 */
    unregister_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME);
}

/*模块的入口与出口*/
module_init(chrdevbase_init);
module_exit(chrdevbase_exit);

MODULE_LICENSE("GPL");