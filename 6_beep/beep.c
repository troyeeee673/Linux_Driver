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

#define beep_CNT 1
#define beep_NAME "beep"

#define beep_OFF 0
#define beep_ON 1

/*设备结构体*/
struct beep_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    int major;
    int minor;
    int beep_gpio;
};

struct beep_dev beep;

static int beep_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &beep;
    return 0;
}

static int beep_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t beep_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos)
{

    int ret;

    unsigned char databuf[1];
    struct beep_dev *dev = filp->private_data;

    ret = copy_from_user(databuf, buf, count);
    if (ret < 0)
    {
        return -EFAULT;
    }

    if (databuf[0] == beep_ON)
        gpio_set_value(dev->beep_gpio, 0);
    else if(databuf[0] == beep_OFF)
        gpio_set_value(dev->beep_gpio, 1);

        return 0;
}
/*操作集*/
static const struct file_operations beep_fops = {
    .owner = THIS_MODULE,
    .write = beep_write,
    .open = beep_open,
    .release = beep_release,
};

/*入口*/
static int __init beep_init(void)
{
    int ret = 0;
    /*注册字符驱动*/
    beep.major = 0;
    if (beep.major)
    {
        /*1. 创建设备号*/
        beep.devid = MKDEV(beep.major, 0);
        /*2. 注册设备*/
        ret = register_chrdev_region(beep.devid, beep_CNT, beep_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&beep.devid, 0, beep_CNT, beep_NAME);
        beep.major = MAJOR(beep.devid);
        beep.minor = MINOR(beep.devid);
    }
    if (ret < 0)
    {
        goto fail_devid;
    }
    /*初始化cdev*/
    beep.cdev.owner = THIS_MODULE;
    cdev_init(&beep.cdev, &beep_fops);

    /*添加cdev*/
    ret = cdev_add(&beep.cdev, beep.devid, beep_CNT);
    if (ret)
    {
        goto fail_cdev_add;
    }

    /*创建类*/
    beep.class = class_create(THIS_MODULE, beep_NAME);
    if (IS_ERR(beep.class))
    {
        ret = PTR_ERR(beep.class);
        goto fial_class;
    }

    /*创建设备*/
    beep.device = device_create(beep.class, NULL, beep.devid, NULL, beep_NAME);
    if (IS_ERR(beep.device))
    {
        ret = PTR_ERR(beep.device);
        goto fail_device;
    }

    /*初始化beep*/
    /*获取结点*/
    beep.nd = of_find_node_by_path("/beep");
    if (beep.nd == NULL)
    {
        ret = -EINVAL;
        goto fail_nd;
    }

    /*获取gpio编号*/
    beep.beep_gpio = of_get_named_gpio(beep.nd, "beep-gpios", 0);
    if (beep.beep_gpio < 0)
    {
        ret = -EINVAL;
        goto fail_nd;
    }

    /*申请io*/
    ret = gpio_request(beep.beep_gpio, "beep-gpio"); // 申请一个io beep.gpio, 取一个名字“beep-gpio”
    if (ret)
    {
        printk("cannot request beep-gpio\r\n");
        goto fail_nd;
    }

    /*设置io方向*/
    ret = gpio_direction_output(beep.beep_gpio, 0);
    if (ret < 0)
    {
        goto fail_set;
    }

    /*设置默认输出电平*/
    gpio_set_value(beep.beep_gpio, 0);
    return 0;

fail_set:
    gpio_free(beep.beep_gpio);
fail_nd:
    device_destroy(beep.class, beep.devid);
fail_device:
    class_destroy(beep.class);
fial_class:
    cdev_del(&beep.cdev);
fail_cdev_add:
    unregister_chrdev_region(beep.devid, beep_CNT);
fail_devid:
    return ret;
}
/*出口*/

static void __exit beep_exit(void)
{

    /*关闭蜂鸣器*/
    gpio_set_value(beep.beep_gpio, 1);
    /*注销设备*/
    cdev_del(&beep.cdev);
    unregister_chrdev_region(beep.devid, beep_CNT);
    /*销毁设备*/
    device_destroy(beep.class, beep.devid);
    /*销毁类*/
    class_destroy(beep.class);
    /*释放gpio*/
    gpio_free(beep.beep_gpio);
}

module_init(beep_init);
module_exit(beep_exit);
MODULE_LICENSE("GPL");