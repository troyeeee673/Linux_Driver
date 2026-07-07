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
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/platform_device.h>

/* 地址映射后的虚拟地址指针 */
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

#define LED_OFF 0 /* 关闭 */
#define LED_ON 1  /* 打开 */

#define PLATFORM_NAME "platled"
#define PLATFORM_COUNT 1

/* LED设备结构体 */
struct newchrled_dev
{
    struct cdev cdev;      /* 字符设备 */
    dev_t devid;           /* 设备号 */
    struct class *class;   /* 类 */
    struct device *device; /* 设备 */
    int major;             /* 主设备号 */
    int minor;             /* 次设备号 */
};

struct newchrled_dev newchrled; /* led设备 */


static int newchrled_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &newchrled;
    return 0;
}

static int newchrled_release(struct inode *inode, struct file *filp)
{
    /* 如果不需要使用dev，可以去掉这行，或者添加 (void)dev; 来避免警告 */
    return 0;
}

// led 开关
static void led_switch(u8 state)
{
    u32 val = 0;
    if (state == LED_ON)
    {
        val = readl(GPIO1_DR);
        val &= ~(1 << 3); // 清零bit3, 打开led
        writel(val, GPIO1_DR);
    }
    else if (state == LED_OFF)
    {
        val = readl(GPIO1_DR);
        val |= (1 << 3);
        writel(val, GPIO1_DR);
    }
}

static ssize_t newchrled_write(struct file *filp, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    unsigned char databuf[1];
    unsigned long ret = 0;
    ret = copy_from_user(databuf, buf, count);
    if (ret < 0)
    {
        printk("kernel copy_from_user failed");
        return -EFAULT;
    }
    /*判断开灯、关灯*/
    led_switch(databuf[0]);
    return 0;
}


static const struct file_operations fops =
{
        .owner = THIS_MODULE,
        .write = newchrled_write,
        .open = newchrled_open,
        .release = newchrled_release,
};

static int led_probe(struct platform_device *dev)
{
    int ret = 0;
    unsigned int val = 0;
    int i;  /* 在函数开头声明循环变量 */
    struct resource *led_res[5]; /* 在函数开头声明所有变量 */

    printk("led driver probe\r\n");

    /*初始化led、字符设备驱动*/
    // 1. 从设备中获取资源
    
    for (i = 0; i < 5; i++)
    {
        led_res[i] = platform_get_resource(dev, IORESOURCE_MEM, i);
        if (led_res[i] == NULL)
            return -EINVAL;
    }
    // 计算内存长度
    //  (led_res[0]->end) - (led_res[0]->start) + 1;
    /* 内存映射 */
    /* 1,初始化LED灯,地址映射 */
    IMX6U_CCM_CCGR1 = ioremap(led_res[0]->start, resource_size(led_res[0]));
    SW_MUX_GPIO1_IO03 = ioremap(led_res[1]->start, resource_size(led_res[1]));
    SW_PAD_GPIO1_IO03 = ioremap(led_res[2]->start, resource_size(led_res[2]));
    GPIO1_DR = ioremap(led_res[3]->start, resource_size(led_res[3]));
    GPIO1_GDIR = ioremap(led_res[4]->start, resource_size(led_res[4]));

    /* 2,初始化 */
    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3 << 26); /* 先清除以前的配置bit26,27 */
    val |= 3 << 26;    /* bit26,27置1 */
    writel(val, IMX6U_CCM_CCGR1);

    writel(0x5, SW_MUX_GPIO1_IO03);    /* 设置复用 */
    writel(0X10B0, SW_PAD_GPIO1_IO03); /* 设置电气属性 */

    /*4. 设置GPIO*/
    val = readl(GPIO1_GDIR); // 读出寄存器中的值
    val |= 1 << 3;           // bit3置一， 设置GPIO1_IO03引脚为输出
    writel(val, GPIO1_GDIR); // 写回寄存器

    /*5. 设置DR寄存器，决定LED亮灭*/
    val = readl(GPIO1_DR);
    val &= ~(1 << 3); // 清零bit3, 打开led
    writel(val, GPIO1_DR);
    /*2. 注册设备*/
    newchrled.major = 0;//手动清零
    if (newchrled.major)
    {
        newchrled.devid = MKDEV(newchrled.major, 0);
        ret = register_chrdev_region(newchrled.devid, PLATFORM_COUNT, PLATFORM_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&newchrled.devid, 0, PLATFORM_COUNT, PLATFORM_NAME);
        newchrled.major = MAJOR(newchrled.devid);
        newchrled.minor = MINOR(newchrled.devid);
    }
    if (ret < 0)
    {
        printk("chrdev_region error\n");
        return -EFAULT;
    }
    printk("major: %d\n", newchrled.major);
    printk("minor: %d\n", newchrled.minor);

    newchrled.cdev.owner = THIS_MODULE;
    cdev_init(&newchrled.cdev, &fops);
    ret = cdev_add(&newchrled.cdev, newchrled.devid, PLATFORM_COUNT);

    /*自动创建字符设备*/
    newchrled.class = class_create(THIS_MODULE, PLATFORM_NAME);
    if (IS_ERR(newchrled.class))
    {
        return PTR_ERR(newchrled.class);
    }
    newchrled.device = device_create(newchrled.class, NULL, newchrled.devid, NULL, PLATFORM_NAME);
    if (IS_ERR(newchrled.device))
    {
        return PTR_ERR(newchrled.device);
    }

    return 0;
}

static int led_remove(struct platform_device *dev)
{
    unsigned int val;  /* 变量声明移到函数开头 */
    
    printk("led driver remove\r\n");
    /*卸载驱动时关闭*/
    val = readl(GPIO1_DR);
    val |= (1 << 3); // 置一bit3, 关闭led
    writel(val, GPIO1_DR);

    /*取消地址映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /*删除设备*/
    cdev_del(&newchrled.cdev);
    /*卸载设备*/
    unregister_chrdev_region(newchrled.devid, PLATFORM_COUNT);

    /*销毁设备*/
    device_destroy(newchrled.class, newchrled.devid);
    /*销毁类*/
    class_destroy(newchrled.class);
    return 0;
}

static struct platform_driver led_driver = {
    .driver = {
        .name = "imx6ull-led",
    },
    .probe = led_probe,
    .remove = led_remove,
};

/*驱动加载*/
static int __init led_driver_init(void)
{
    // 向总线注册驱动
    return platform_driver_register(&led_driver);
}

/*驱动卸载*/
static void __exit led_driver_exit(void)
{
    // 撤销驱动注册
    platform_driver_unregister(&led_driver);
}

module_init(led_driver_init);
module_exit(led_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vicky");