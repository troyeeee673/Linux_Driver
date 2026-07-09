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
#include <linux/i2c.h>
#include <linux/delay.h>
#include "ap3216c_reg.h"

#define AP3216C_DEV_CNT 1
#define AP3216C_DEV_NAME "ap3216c"

/*传统的匹配表*/
static struct i2c_device_id ap3216c_id[] = {
    {"asc, ap3216c", 0},
    {}

};

/*设备树匹配表*/
static struct of_device_id ap3216c_of_match[] = {
    {.compatible = "asc, ap3216c"},
    {}};

struct ap3216c_dev
{
    int major;
    int minor;
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    void *private_data;
    unsigned short ir, als, ps;
};
struct ap3216c_dev ap3216c_dev;

/*读取ap3216cN个寄存器数据*/
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *data, int n)
{
    int ret = 0;
    struct i2c_msg msg[2]; // 一个i2c时序主要发送两大部分数据，第一发送要读取的从机的地址，第二要发送要操作的寄存器
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    // 发送要读取的寄存器的首地址
    msg[0].addr = client->addr; /*从机地址*/
    msg[0].flags = 0;           // 表示发送数据
    msg[0].buf = &reg;          // 要发送的数据，这里是寄存器地址
    msg[0].len = 1;             // 寄存器地址8位，一个字节

    msg[1].addr = client->addr;
    msg[1].flags = 1;  // 要读数据
    msg[1].buf = data; // 保存读到的数据
    msg[1].len = n;

    ret = i2c_transfer(client->adapter, msg, 2);
    return ret;
}

/*写入ap3216c的N个寄存器*/
static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, u8 *buf, u8 len)
{
    int ret = 0;
    u8 b[256];
    struct i2c_msg msg[1]; // 写数据的时序是将地址、寄存器和数据一并发送
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    /*构建要发送的数据，也就是寄存器首地址+实际数据*/
    b[0] = reg;
    memcpy(&b[1], buf, len);

    // 发送
    msg[0].addr = client->addr; /*从机地址*/
    msg[0].flags = 0;           // 表示发送数据
    msg[0].buf = b;             // 要发送的数据，这里是寄存器地址
    msg[0].len = len + 1;       // 寄存器地址一个字节+实际数据

    ret = i2c_transfer(client->adapter, msg, 1);
    return ret;
}

/*读取一个寄存器*/
static unsigned char ap3216c_read_one_reg(struct ap3216c_dev *dev, u8 reg)
{
    u8 data;
    data = ap3216c_read_regs(dev, reg, &data, 1);
    return data;
}

/*写一个寄存器*/
static void ap3216c_write_one_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
    u8 buf = 0;
    buf = data;
    ap3216c_write_regs(dev, reg, &buf, 1);
}


void ap3216c_readdata(struct ap3216c_dev* dev)
{
    unsigned char buf[6];
    unsigned char i = 0;

    /* 循环的读取数据 */
    for(i = 0; i < 6; i++) {
        buf[i] = ap3216c_read_one_reg(dev, AP3216C_IRDATALOW + i);
    }

    if(buf[0] & 0x80) { /* 为真表示IR和PS数据无效 */
        dev->ir = 0;
        dev->ps = 0;
    } else {
        dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0x03);
        dev->ps = (((unsigned short)buf[5] & 0x3F) << 4) | (buf[4] & 0x0F);
    }

    dev->als = ((unsigned short)buf[3] << 8) | buf[2];
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    unsigned char value = 0;
    filp->private_data = &ap3216c_dev;

    printk("ap3216c_open\r\n");

    /* 初始化AP3216C */
    ap3216c_write_one_reg(&ap3216c_dev, AP3216C_SYSTEMCONG, 0X4); /* 复位 */
    mdelay(50);
    ap3216c_write_one_reg(&ap3216c_dev, AP3216C_SYSTEMCONG, 0X3); /* 复位 */

    value = ap3216c_read_one_reg(&ap3216c_dev, AP3216C_SYSTEMCONG);
    printk("AP3216C_SYSTEMCONG = %#x\r\n", value);
    return 0;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offf)
{
    long err = 0;
    short data[3];
    struct ap3216c_dev* dev = (struct ap3216c_dev*)filp->private_data;
    
    /*向应用返回原始数据*/
    /*读取数据*/
    ap3216c_readdata(dev);
    /*向应用上报数据*/
    data[0] = dev->ir;
    data[1] = dev->als;
    data[2] = dev->ps;

    err = copy_to_user(buf, data, sizeof(data));

    return err;
}
/*操作集*/
static const struct file_operations ap3216c_fops = {
    .owner = THIS_MODULE,
    .read = ap3216c_read,
    .open = ap3216c_open,
    .release = ap3216c_release,
};

static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    /*搭建字符设备框架，进行数据的读取*/
    /*注册字符驱动*/
    ap3216c_dev.major = 0;
    if (ap3216c_dev.major)
    {
        /*1. 创建设备号*/
        ap3216c_dev.devid = MKDEV(ap3216c_dev.major, 0);
        /*2. 注册设备*/
        ret = register_chrdev_region(ap3216c_dev.devid, AP3216C_DEV_CNT, AP3216C_DEV_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&ap3216c_dev.devid, 0, AP3216C_DEV_CNT, AP3216C_DEV_NAME);
        ap3216c_dev.major = MAJOR(ap3216c_dev.devid);
        ap3216c_dev.minor = MINOR(ap3216c_dev.devid);
    }
    if (ret < 0)
    {
        ret = -EFAULT;
        goto fail_devid;
    }
    /*初始化cdev*/
    ap3216c_dev.cdev.owner = THIS_MODULE;
    cdev_init(&ap3216c_dev.cdev, &ap3216c_fops);

    /*添加cdev*/
    ret = cdev_add(&ap3216c_dev.cdev, ap3216c_dev.devid, AP3216C_DEV_CNT);
    if (ret < 0)
    {
        ret = -EINVAL;
        goto fail_cdev;
    }

    /*创建类*/
    ap3216c_dev.class = class_create(THIS_MODULE, AP3216C_DEV_NAME);
    if (IS_ERR(ap3216c_dev.class))
    {
        ret = PTR_ERR(ap3216c_dev.class);
        goto fail_class;
    }

    /*创建设备*/
    ap3216c_dev.device = device_create(ap3216c_dev.class, NULL, ap3216c_dev.devid, NULL, AP3216C_DEV_NAME);
    if (IS_ERR(ap3216c_dev.device))
    {
        ret = PTR_ERR(ap3216c_dev.device);
        goto fail_device;
    }
    ap3216c_dev.private_data = client;

    return 0;

fail_device:
    class_destroy(ap3216c_dev.class);
fail_class:
    cdev_del(&ap3216c_dev.cdev);
fail_cdev:
    unregister_chrdev_region(ap3216c_dev.devid, AP3216C_DEV_CNT);
fail_devid:
    return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
    cdev_del(&ap3216c_dev.cdev);
    unregister_chrdev_region(ap3216c_dev.devid, AP3216C_DEV_CNT);
    device_destroy(ap3216c_dev.class, ap3216c_dev.devid);
    class_destroy(ap3216c_dev.class);
    return 0;
}
static struct i2c_driver ap3216c_driver = {
    .probe = ap3216c_probe,
    .remove = ap3216c_remove,
    .driver = {
        .name = "ap3216c",
        .owner = THIS_MODULE,
        .of_match_table = ap3216c_of_match,
    },
    /*如果不使用设备树，就要用id_table进行设备匹配*/
    .id_table = ap3216c_id,

};

/*入口*/
static int __init ap3216c_init(void)
{
    int ret = 0;
    /*注册驱动*/
    ret = i2c_add_driver(&ap3216c_driver);
    return ret;
}

/*出口*/
static void __exit ap3216c_exit(void)
{
    /*注销驱动*/
    i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
