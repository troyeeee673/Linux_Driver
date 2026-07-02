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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>


/* cdev 系列操作是"内核注册"，后面做的 class 和 device 是"应用层可见性创建"。内核注册让驱动能用，可见性创建让应用程序能找到它*/

#define TIMER_CNT 1
#define TIMER_NAME "timer"

#define CLOSE_CMD       _IO(0xEF, 1)           // 关闭命令
#define OPEN_CMD        _IO(0xEF, 2)            // 打开命令
#define SETPERIOD_CMD   _IOW(0xEF, 3, int)     // 设置周期

/*设备结构体*/
struct timer_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    struct timer_list timer;
    int major;
    int minor;
    int ledgpio;
    atomic_long_t timeperiod;  // 定时周期
};

struct timer_dev timerdev;

static int timer_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &timerdev;
    return 0;
}

static int timer_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t timer_write(struct file *filp, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    return 0;
}

static ssize_t timer_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    return 0;
}

static long timer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int value = 0;
    struct timer_dev *dev = filp->private_data;
    long period;

    switch (cmd)
    {
    case CLOSE_CMD:
        // 关闭定时器
        del_timer_sync(&dev->timer);
        break;
        
    case OPEN_CMD:
        // 启动定时器
        period = atomic_long_read(&dev->timeperiod);
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(period));
        break;
        
    case SETPERIOD_CMD:
        // 从用户空间获取新的周期值
        ret = copy_from_user(&value, (int __user *)arg, sizeof(int));
        if (ret < 0) {
            return -EFAULT;
        }
        // 原子操作设置新的周期值
        atomic_long_set(&dev->timeperiod, value);
        // 立即生效新的周期
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(value));
        break;
        
    default:
        return -ENOTTY;
    }

    return ret;
}

/*操作集*/
static const struct file_operations timer_fops = {
    .owner = THIS_MODULE,
    .write = timer_write,
    .read = timer_read,
    .open = timer_open,
    .release = timer_release,
    .unlocked_ioctl = timer_ioctl,
};

/* 定时器处理函数 */
static void timer_func(unsigned long arg)
{
    struct timer_dev *dev = (struct timer_dev *)arg;
    static int state = 1;
    long period;
    
    state = !state;
    gpio_set_value(dev->ledgpio, state); // 设置led灯翻转

    /* 使用原子操作读取最新周期值 */
    period = atomic_long_read(&dev->timeperiod);
    
    /* 在定时器处理函数中调用开启定时器，这样才能保证定时器被周期调用 */
    mod_timer(&(dev->timer), jiffies + msecs_to_jiffies(period));
}

/*初始化LED*/
static int led_init(struct timer_dev *dev)
{
    int ret = 0;
    /*获取设备节点*/
    dev->nd = of_find_node_by_path("/gpioled");
    if (dev->nd == NULL)
    {
        ret = -EINVAL;
        goto fail_nd;
    }

    /*获取gpio*/
    dev->ledgpio = of_get_named_gpio(dev->nd, "led-gpios", 0);
    if (dev->ledgpio < 0)
    {
        ret = -EINVAL;
        goto fail_nd;
    }

    /*请求IO*/
    ret = gpio_request(dev->ledgpio, "led");
    if (ret)
    {
        ret = -EBUSY;
        printk("failed request io %d", dev->ledgpio);
        goto fail_request;
    }

    /*配置gpio方向*/
    ret = gpio_direction_output(dev->ledgpio, 1); // 设置为输出，默认高电平
    if (ret < 0)
    {
        ret = -EINVAL;
        goto fail_gpioset;
    }
    return 0;
    
fail_gpioset:
    gpio_free(dev->ledgpio);
fail_request:
fail_nd:
    return ret;
}

/*入口*/
static int __init timer_init(void)
{
    int ret = 0;
    long period;
    
    /*注册字符驱动*/
    timerdev.major = 0;
    if (timerdev.major)
    {
        /*1. 创建设备号*/
        timerdev.devid = MKDEV(timerdev.major, 0);
        /*2. 注册设备*/
        register_chrdev_region(timerdev.devid, TIMER_CNT, TIMER_NAME);
    }
    else
    {
        alloc_chrdev_region(&timerdev.devid, 0, TIMER_CNT, TIMER_NAME);
        timerdev.major = MAJOR(timerdev.devid);
        timerdev.minor = MINOR(timerdev.devid);
    }
    
    /*初始化cdev*/
    timerdev.cdev.owner = THIS_MODULE;
    cdev_init(&timerdev.cdev, &timer_fops);

    /*添加cdev*/
    cdev_add(&timerdev.cdev, timerdev.devid, TIMER_CNT);

    /*创建类*/
    timerdev.class = class_create(THIS_MODULE, TIMER_NAME);
    if (IS_ERR(timerdev.class))
    {
        ret = PTR_ERR(timerdev.class);
        goto fail_class;
    }

    /*创建设备*/
    timerdev.device = device_create(timerdev.class, NULL, timerdev.devid, NULL, TIMER_NAME);
    if (IS_ERR(timerdev.device))
    {
        ret = PTR_ERR(timerdev.device);
        goto fail_device;
    }

    /*初始化led*/
    ret = led_init(&timerdev);
    if (ret < 0)
    {
        goto fail_ledinit;
    }
    
    /*初始化定时器*/
    init_timer(&timerdev.timer);

    /* 初始化原子变量为默认周期 500ms */
    atomic_long_set(&timerdev.timeperiod, 500);
    
    /*配置定时器周期和处理函数*/
    period = atomic_long_read(&timerdev.timeperiod);
    timerdev.timer.expires = jiffies + msecs_to_jiffies(period);
    timerdev.timer.function = timer_func;
    timerdev.timer.data = (unsigned long)(&timerdev); // data会传递给定时器的function，作为函数参数
    
    /*添加定时器到系统,添加到系统后，定时器会立马执行，开始计时*/
    add_timer(&timerdev.timer);

    return 0;
    
fail_ledinit:
    device_destroy(timerdev.class, timerdev.devid);
fail_device:
    class_destroy(timerdev.class);
fail_class:
    return ret;
}

/*出口*/
static void __exit timer_exit(void)
{
    /*关灯*/
    gpio_set_value(timerdev.ledgpio, 1);
    /*删除定时器*/
    del_timer(&timerdev.timer);
    /* 注销字符设备驱动 */
    cdev_del(&timerdev.cdev);
    unregister_chrdev_region(timerdev.devid, TIMER_CNT);

    device_destroy(timerdev.class, timerdev.devid);
    class_destroy(timerdev.class);

    /*释放GPIO*/
    if (timerdev.ledgpio >= 0)
    {
        gpio_free(timerdev.ledgpio);
    }
}

module_init(timer_init);
module_exit(timer_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
MODULE_DESCRIPTION("Timer driver for LED blinking");