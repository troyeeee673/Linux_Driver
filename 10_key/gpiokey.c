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

#define GPIOKEY_CNT 1
#define GPIOKEY_NAME "gpiokey"

#define KEY0Value 0xF0
#define INVALKEY  0x00

/*设备结构体*/
struct gpiokey_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node* nd;
    int major;
    int minor;
    int key_gpio;
    atomic_long_t keyvalue;

};

struct gpiokey_dev gpiokey;

static int key_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &gpiokey;
    return 0;
}

static int key_release(struct inode *inode, struct file *filp)
{
    struct gpiokey_dev *dev = (struct gpiokey_dev *)filp->private_data;
    return 0;
}


static ssize_t key_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    unsigned char databuf[1];
    unsigned long ret = 0;
    struct gpiokey_dev *dev = filp->private_data;
    ret = copy_from_user(databuf, buf, count);
    if (ret < 0)
    {
        printk("kernel copy_from_user faikey");
        return -EFAULT;
    }

    return 0;
}

static ssize_t key_read(struct filp *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int value;
    struct gpiokey_dev *dev = filp->private_data;
    int ret = 0;
    if(gpio_get_value(dev->key_gpio) == 0)/*按键按下*/
    {
        while(gpio_get_value(dev->key_gpio));//等待按键释放
        atomic_long_set(&dev->keyvalue, KEY0Value);//设置按键值

    }
    else//案件未按下
    {
        atomic_long_set(&dev->keyvalue, INVALKEY);

    }
    value = atomic_long_read(&dev->keyvalue);

    ret = copy_to_user(buf, &value, sizeof(value));//想应用发送数据
    return 0;
}
/*操作集*/
static const struct file_operations key_fops = {
    .owner      = THIS_MODULE,
    .write      = key_write,
    .read       = key_read,
    .open       = key_open,
    .release    = key_release,
};


/*按键初始化函数*/
int keyio_init(struct gpiokey_dev *dev)
{
    int ret = 0 ;
    /*初始化keyvalue*/
    atomic_long_set(&gpiokey.keyvalue, INVALKEY);
    dev->nd = of_find_node_by_path("/key");
    if(dev->nd == NULL)
    {
        ret = -EINVAL;
        goto fail_nd;
    }

    /*获取设备io*/
    dev->key_gpio = of_get_named_gpio(dev->nd, "key-gpios", 0);
    if(dev->key_gpio < 0)
    {
        return -EINVAL;
        goto fail_nd;
    }

    /*请求gpio*/
    ret = gpio_request(dev->key_gpio, "key");
    if(ret)
    {
        ret = -EBUSY;
        printk("keydev io%d cannot be requested \n", dev->key_gpio);
        goto fail_nd;
    }
    /*配置gpio*/
    ret = gpio_direction_input(dev->key_gpio);
    if(ret < 0)
    {
        ret = -EINVAL;
        goto fial_input;
    }


    return 0;
fial_input:
    /*释放io*/
    gpio_free(dev->key_gpio);
fail_nd:
    return ret;
}

/*入口*/
static int __init key_init(void)
{
    int ret = 0;
    /*注册字符驱动*/
    gpiokey.major = 0;
    if (gpiokey.major)
    {
        /*1. 创建设备号*/
        gpiokey.devid = MKDEV(gpiokey.major, 0);
        /*2. 注册设备*/
        register_chrdev_region(gpiokey.devid, GPIOKEY_CNT, GPIOKEY_NAME);
    }
    else
    {
        alloc_chrdev_region(&gpiokey.devid, 0, GPIOKEY_CNT, GPIOKEY_NAME);
        gpiokey.major = MAJOR(gpiokey.devid);
        gpiokey.minor = MINOR(gpiokey.devid);
    }
    /*初始化cdev*/
    gpiokey.cdev.owner = THIS_MODULE;
    cdev_init(&gpiokey.cdev, &key_fops);

    /*添加cdev*/
    cdev_add(&gpiokey.cdev, gpiokey.devid, GPIOKEY_CNT);

    /*创建类*/
    gpiokey.class = class_create(THIS_MODULE, GPIOKEY_NAME);
    if (IS_ERR(gpiokey.class))
    {
        return PTR_ERR(gpiokey.class);
    }

    /*创建设备*/
    gpiokey.device = device_create(gpiokey.class, NULL, gpiokey.devid, NULL, GPIOKEY_NAME);
    if (IS_ERR(gpiokey.device))
    {
        return PTR_ERR(gpiokey.device);
    }

    /*初始化io*/
    ret = keyio_init(&gpiokey);
    if(ret < 0)
    {
        return -EINVAL;
    }
    return ret;
}
/*出口*/

static void __exit key_exit(void)
{
    /* 注销字符设备驱动 */
    cdev_del(&gpiokey.cdev);
    unregister_chrdev_region(gpiokey.devid, GPIOKEY_CNT);

    device_destroy(gpiokey.class, gpiokey.devid);
    class_destroy(gpiokey.class);

    gpio_free(gpiokey.key_gpio);

}

module_init(key_init);
module_exit(key_exit);
MODULE_LICENSE("GPL");