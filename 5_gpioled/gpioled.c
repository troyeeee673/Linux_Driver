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
    struct device_node* nd;
    int major;
    int minor;
    int led_gpio;

};

struct gpioled_dev gpioled;

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &gpioled;
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    struct gpioled_dev *dev = (struct led_dev *)filp->private_data;
    return 0;
}


static ssize_t led_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    unsigned char databuf[1];
    unsigned long ret = 0;
    struct gpioled_dev *dev = filp->private_data;
    ret = copy_from_user(databuf, buf, count);
    if (ret < 0)
    {
        printk("kernel copy_from_user failed");
        return -EFAULT;
    }
    /*判断开灯、关灯*/
    if(databuf[0] == LED_ON)
    {
        gpio_set_value(dev->led_gpio, 0);
    }
    else if(databuf[0] == LED_OFF)
    {
        gpio_set_value(dev->led_gpio, 1);
    }
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
    int ret = 0;
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

    /*获取设备结点*/
    gpioled.nd = of_find_node_by_path("/gpioled");
    if(gpioled.nd == NULL)
    {
        ret = -EINVAL;
        goto fail_findnode;
    }

    /*获取led对应的GPIO*/
    gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpios", 0);
    if(gpioled.led_gpio < 0)
    {
        printk("cannot find led_gpio\r\n");
        ret = -EINVAL;
        goto fail_findgpio;
    }
    /*申请gpio*/
    ret = gpio_request(gpioled.led_gpio,  "led_gpio");
    if(ret)
    {
        printk("failed led gpio request\r\n");
        ret = -EINVAL;
        goto fail_gpiorequest;
    }
    /*使用GPIO, 设置为输出*/
    ret = gpio_direction_output(gpioled.led_gpio, 1);
    if(ret)
    {
        goto fail_setoutput;
    }

    /*设置输出低电平, 开灯*/
    gpio_set_value(gpioled.led_gpio, 0);

    return 0;
fail_setoutput:
    gpio_free(gpioled.led_gpio);
fail_gpiorequest:
fail_findgpio:
fail_findnode:
    return ret;
}
/*出口*/

static void __exit led_exit(void)
{
    /*关灯*/
    gpio_set_value(gpioled.led_gpio, 1);
    /*注销设备*/
    cdev_del(&gpioled.cdev);
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
    /*销毁设备*/
    device_destroy(gpioled.class, gpioled.devid);

    /*销毁类*/
    class_destroy(gpioled.class);

    /*释放gpio*/
    gpio_free(gpioled.led_gpio);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");