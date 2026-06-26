#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/types.h>

/* cdev 系列操作是“内核注册”，后面做的 class 和 device 是“应用层可见性创建”。内核注册让驱动能用，可见性创建让应用程序能找到它*/

#define GPIOLED_CNT 1
#define GPIOLED_NAME "gpioled"

#define LED_OFF 0
#define LED_ON 1

/*设备结构体*/
struct gpioled_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
};

struct gpioled_dev gpioled;

static int led_open(struct inode *inode, struct file *filp)
{
    // filp->private_data = &led;
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    // struct led_dev *dev = (struct led_dev *)filp->private_data;
    return 0;
}

// led 开关
static void led_switch(u8 state)
{
    // u32 val = 0;
    // if (state == LED_ON)
    // {
    //     val = readl(GPIO1_DR);
    //     val &= ~(1 << 3); // 清零bit3, 打开led
    //     writel(val, GPIO1_DR);
    // }
    // else if (state == LED_OFF)
    // {
    //     val = readl(GPIO1_DR);
    //     val |= (1 << 3);
    //     writel(val, GPIO1_DR);
    // }
}

static ssize_t led_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    // unsigned char databuf[1];
    // unsigned long ret = 0;
    // ret = copy_from_user(databuf, buf, count);
    // if (ret < 0)
    // {
    //     printk("kernel copy_from_user failed");
    //     return -EFAULT;
    // }
    // /*判断开灯、关灯*/
    // led_switch(databuf[0]);
    return 0;
}
/*操作集*/
static const struct file_operations led_fops = {
    .owner      = THIS_MODULE,
    .write      = led_write,
    .open       = led_open,
    .release    = led_release,
};

/*入口*/
static int __init led_init(void)
{
    /*注册字符驱动*/
    gpioled.major = 0;
    if (gpioled.major)
    {
        /*1. 创建设备号*/
        gpioled.devid = MKDEV(gpioled.major, 0);
        /*2. 注册设备*/
        register_chrdev_region(gpioled.devid, GPIOLED_CNT, GPIOLED_NAME);
    }
    else
    {
        alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME);
        gpioled.major = MAJOR(gpioled.devid);
        gpioled.minor = MINOR(gpioled.devid);
    }
    /*初始化cdev*/
    gpioled.cdev.owner = THIS_MODULE;
    cdev_init(&gpioled.cdev, &led_fops);

    /*添加cdev*/
    cdev_add(&gpioled.cdev, gpioled.devid, GPIOLED_CNT);

    /*创建类*/
    gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioled.class))
    {
        return PTR_ERR(gpioled.class);
    }

    /*创建设备*/
    gpioled.device = device_create(gpioled.class, NULL, gpioled.devid, NULL, GPIOLED_NAME);
    if (IS_ERR(gpioled.device))
    {
        return PTR_ERR(gpioled.device);

        
    }
    return 0;
}
/*出口*/

static void __exit led_exit(void)
{
    /*注销设备*/
    cdev_del(&gpioled.cdev);
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
    /*销毁设备*/
    device_destroy(gpioled.class, gpioled.devid);

    /*销毁类*/
    class_destroy(gpioled.class);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");