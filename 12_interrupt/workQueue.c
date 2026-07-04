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
#include <linux/string.h>
#include <linux/interrupt.h> // 添加中断头文件

/* cdev 系列操作是"内核注册"，后面做的 class 和 device 是"应用层可见性创建"。内核注册让驱动能用，可见性创建让应用程序能找到它*/

#define IMX6UIRQ_CNT 1
#define IMX6UIRQ_NAME "imx6uirq"

#define KEY_NUM 1 // 按键数量

// 键值
#define KEY0VALUE 0X01
#define INVAKEY 0XFF

/*key中断描述结构体*/
struct irq_keydesc
{
    int gpio;            // io编号
    int irqnum;          // 中断号
    unsigned char value; // 键值
    char name[10];       // 按键名
    // 中断处理函数
    irqreturn_t (*handler)(int, void *);
};

/*设备结构体*/
struct imx6uirq_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    struct irq_keydesc irqkey[KEY_NUM];
    struct timer_list timer;

    int major;
    int minor;
    int ledgpio;
    atomic_long_t key_value; // 按键值
    atomic_long_t release_key;
    struct work_struct work;
};

struct imx6uirq_dev imx6uirqdev;

static int imx6uirq_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &imx6uirqdev;
    return 0;
}

static int imx6uirq_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t imx6uirq_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    return 0;
}

static ssize_t imx6uirq_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    unsigned long keyvalue;
    unsigned long releasekey;
    struct imx6uirq_dev *dev = filp->private_data;

    keyvalue = atomic_long_read(&dev->key_value);
    releasekey = atomic_long_read(&dev->release_key);

    if (releasekey) /*有效按键*/
    {
        if (keyvalue & 0x80) // 按键按下并完成释放
        {
            keyvalue &= ~0x80;
            ret = copy_to_user(buf, &keyvalue, sizeof(keyvalue));
        }
        else
        {
            goto data_error;
        }
        atomic_long_set(&dev->release_key, 0); // 按下标志清零
    }
    else
    {
        goto data_error;
    }
    return ret;
data_error:
    return -EINVAL;
}

/*操作集*/
static const struct file_operations imx6uirq_fops = {
    .owner = THIS_MODULE,
    .write = imx6uirq_write,
    .read = imx6uirq_read,
    .open = imx6uirq_open,
    .release = imx6uirq_release,
};

/*中断处理函数*/
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct imx6uirq_dev *dev = dev_id;

    // 放到定时器处理函数中，进行消抖
#if 0
    value = gpio_get_value(dev->irqkey[0].gpio);
    if (value == 0) /* 按下 */
    {
        printk("KEY0 Push!\r\n");
    }
    else if (value == 1)
    { /* 释放 */
        printk("KEY0 release!\r\n");
    }
#endif

// 耗时的部分放到下半部实现
#if 0
    dev->timer.data = (volatile unsigned long)dev_id;
    // 进行10ms延时
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
#endif
    schedule_work(&dev->work);
    return IRQ_HANDLED;
}

/*tasklet处理函数*/
static void key_work(struct work_struct *work)
{
    // imx6uirqdev.timer.data = (unsigned long)(&imx6uirqdev);
    struct imx6uirq_dev *dev = container_of(work, struct imx6uirq_dev, work);
    dev->timer.data = (unsigned long)dev;
    // 进行10ms延时
    mod_timer(&imx6uirqdev.timer, jiffies + msecs_to_jiffies(10));
}

/* 定时器处理函数 */
static void timer_func(unsigned long arg)
{
    int value = 0;
    struct imx6uirq_dev *dev = (struct imx6uirq_dev *)arg;

    value = gpio_get_value(dev->irqkey[0].gpio);
    if (value == 0) /* 按下 */
    {
        atomic_long_set(&dev->key_value, dev->irqkey[0].value);
        printk("KEY0 Push!\r\n");
    }
    else if (value == 1) /* 释放 */
    {
        atomic_long_set(&dev->key_value, dev->irqkey[0].value | 0x80); // 最高位置一，代表释放
        atomic_long_set(&dev->release_key, 1);
        printk("KEY0 release!\r\n");
    }
}

/*按键初始化*/
static int keyio_init(struct imx6uirq_dev *dev)
{
    int i = 0;
    int ret = 0;

    /*按键初始化*/
    /*1. 获取节点*/
    dev->nd = of_find_node_by_path("/key");
    if (dev->nd == NULL)
    {
        ret = -EFAULT;
        goto fail_nd;
    }

    /*2. 提取gpio对应的编号*/
    for (i = 0; i < KEY_NUM; i++)
    {
        dev->irqkey[i].gpio = of_get_named_gpio(dev->nd, "key-gpios", i); // 注意这里改为 i
    }

    /*3. 初始化io和中断*/
    for (i = 0; i < KEY_NUM; i++)
    {
        memset(dev->irqkey[i].name, 0, sizeof(dev->irqkey[i].name));
        sprintf(dev->irqkey[i].name, "KEY%d", i);

        /*请求io*/
        ret = gpio_request(dev->irqkey[i].gpio, dev->irqkey[i].name);
        if (ret)
        {
            printk("gpio_request failed for KEY%d\n", i);
            goto fail_irq;
        }

        /*设置gpio方向*/
        gpio_direction_input(dev->irqkey[i].gpio);

        /*获取中断号*/
        dev->irqkey[i].irqnum = gpio_to_irq(dev->irqkey[i].gpio);
        // dev->irqkey[i].irqnum = irq_of_parse_and_map(dev->nd, i);
    }

    /*设置中断处理函数和键值*/
    dev->irqkey[0].handler = key0_handler;
    dev->irqkey[0].value = KEY0VALUE;
    // dev->irqkey[0].tasklet = key_tasklet;

    /*按键中断初始化*/
    for (i = 0; i < KEY_NUM; i++)
    {
        // 修正：第一个参数应该是中断号，不是gpio编号
        ret = request_irq(dev->irqkey[i].irqnum,                      // 中断号
                          dev->irqkey[i].handler,                     // 中断处理函数
                          IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, // 触发方式
                          dev->irqkey[i].name,                        // 中断名称
                          &imx6uirqdev);                              // 设备ID
        if (ret)
        {
            printk("request_irq failed for %s, ret=%d\n", dev->irqkey[i].name, ret);
            goto fail_irq;
        }
    }
    INIT_WORK(&dev->work, key_work);
    return 0;

fail_irq:
    // 释放已申请的中断和GPIO
    for (i = 0; i < KEY_NUM; i++)
    {
        if (dev->irqkey[i].irqnum)
        {
            free_irq(dev->irqkey[i].irqnum, &imx6uirqdev);
        }
        if (dev->irqkey[i].gpio >= 0)
        {
            gpio_free(dev->irqkey[i].gpio); // 修正：传递gpio编号，不是结构体指针
        }
    }
fail_nd:
    return ret;
}

/*入口*/
static int __init imx6uirq_init(void)
{
    int ret = 0;
    /*注册字符驱动*/
    imx6uirqdev.major = 0;
    if (imx6uirqdev.major)
    {
        /*1. 创建设备号*/
        imx6uirqdev.devid = MKDEV(imx6uirqdev.major, 0);
        /*2. 注册设备*/
        register_chrdev_region(imx6uirqdev.devid, IMX6UIRQ_CNT, IMX6UIRQ_NAME);
    }
    else
    {
        alloc_chrdev_region(&imx6uirqdev.devid, 0, IMX6UIRQ_CNT, IMX6UIRQ_NAME);
        imx6uirqdev.major = MAJOR(imx6uirqdev.devid);
        imx6uirqdev.minor = MINOR(imx6uirqdev.devid);
    }

    /*初始化cdev*/
    imx6uirqdev.cdev.owner = THIS_MODULE;
    cdev_init(&imx6uirqdev.cdev, &imx6uirq_fops);

    /*添加cdev*/
    cdev_add(&imx6uirqdev.cdev, imx6uirqdev.devid, IMX6UIRQ_CNT);

    /*创建类*/
    imx6uirqdev.class = class_create(THIS_MODULE, IMX6UIRQ_NAME);
    if (IS_ERR(imx6uirqdev.class))
    {
        ret = PTR_ERR(imx6uirqdev.class);
        goto fail_class;
    }

    /*创建设备*/
    imx6uirqdev.device = device_create(imx6uirqdev.class, NULL, imx6uirqdev.devid, NULL, IMX6UIRQ_NAME);
    if (IS_ERR(imx6uirqdev.device))
    {
        ret = PTR_ERR(imx6uirqdev.device);
        goto fail_device;
    }

    /*初始化keyio*/
    ret = keyio_init(&imx6uirqdev);
    if (ret < 0)
        goto fail_keyio;

    // 初始化定时器
    init_timer(&imx6uirqdev.timer);
    imx6uirqdev.timer.function = timer_func;

    /*按键值初始化*/
    atomic_long_set(&imx6uirqdev.key_value, INVAKEY);
    atomic_long_set(&imx6uirqdev.release_key, 0);

    return 0;

fail_keyio:
    device_destroy(imx6uirqdev.class, imx6uirqdev.devid);
fail_device:
    class_destroy(imx6uirqdev.class);
fail_class:
    return ret;
}

/*出口*/
static void __exit imx6uirq_exit(void)
{
    int i = 0;

    /*释放中断*/
    for (i = 0; i < KEY_NUM; i++)
    {
        free_irq(imx6uirqdev.irqkey[i].irqnum, &imx6uirqdev);
    }

    /*释放gpio*/
    for (i = 0; i < KEY_NUM; i++)
    {
        gpio_free(imx6uirqdev.irqkey[i].gpio); // 修正：传递gpio编号
    }

    /*删除定时器*/
    del_timer_sync(&imx6uirqdev.timer);
    /* 注销字符设备驱动 */
    cdev_del(&imx6uirqdev.cdev);
    unregister_chrdev_region(imx6uirqdev.devid, IMX6UIRQ_CNT);

    device_destroy(imx6uirqdev.class, imx6uirqdev.devid);
    class_destroy(imx6uirqdev.class);
}

module_init(imx6uirq_init);
module_exit(imx6uirq_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
MODULE_DESCRIPTION("imx6uirq driver for key interrupt");