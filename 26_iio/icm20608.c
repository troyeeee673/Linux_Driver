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
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include "icm20608.h"

#define ICM20608_DEV_CNT 1
#define ICM20608_DEV_NAME "icm20608"

/*传统的匹配表*/
static struct spi_device_id icm20608_id[] = {
    {"alientek,icm20608", 0},
    {}

};

/*设备树匹配表*/
static struct of_device_id icm20608_of_match[] = {
    {.compatible = "alientek,icm20608"},
    {}};

// 设备结构体
struct icm20608_dev
{
    int major;
    int minor;
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    void *pribate_data;
    struct device_node *nd;
    // int cs_gpio;            // 片选信号
    signed int gyro_x_adc;  /* 陀螺仪X轴原始值 */
    signed int gyro_y_adc;  /* 陀螺仪Y轴原始值 */
    signed int gyro_z_adc;  /* 陀螺仪Z轴原始值 */
    signed int accel_x_adc; /* 加速度计X轴原始值 */
    signed int accel_y_adc; /* 加速度计Y轴原始值 */
    signed int accel_z_adc; /* 加速度计Z轴原始值 */
    signed int temp_adc;    /* 温度原始值 */

    struct regmap *regmap;
    struct regmap_config regmap_config;
};
struct icm20608_dev icm20608_dev;



/*读取一个寄存器*/
static unsigned char icm20608_read_one_reg(struct icm20608_dev *dev, u8 reg)
{
    u8 ret = 0;
    unsigned int data;
    ret = regmap_read(dev->regmap, reg, &data);
    return (u8)data;
}

/*写一个寄存器*/
static void icm20608_write_one_reg(struct icm20608_dev *dev, u8 reg, u8 data)
{
    u8 buf = 0;
    buf = data;
    regmap_write(dev->regmap,reg, data);
}

void icm20608_readdata(struct icm20608_dev *dev)
{
    unsigned char data[14];

    regmap_bulk_read(dev->regmap, ICM20_ACCEL_XOUT_H, data, 14);

    dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]);
    dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]);
    dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]);
    dev->temp_adc = (signed short)((data[6] << 8) | data[7]);
    dev->gyro_x_adc = (signed short)((data[8] << 8) | data[9]);
    dev->gyro_y_adc = (signed short)((data[10] << 8) | data[11]);
    dev->gyro_z_adc = (signed short)((data[12] << 8) | data[13]);
}

static int icm20608_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &icm20608_dev;
    return 0;
}

static int icm20608_release(struct inode *inode, struct file *filp)
{
    return 0;
}

ssize_t icm20608_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offf)
{
    signed int data[7];
    long err = 0;
    struct icm20608_dev *dev = (struct icm20608_dev *)filp->private_data;

    icm20608_readdata(dev);
    data[0] = dev->gyro_x_adc;
    data[1] = dev->gyro_y_adc;
    data[2] = dev->gyro_z_adc;
    data[3] = dev->accel_x_adc;
    data[4] = dev->accel_y_adc;
    data[5] = dev->accel_z_adc;
    data[6] = dev->temp_adc;
    err = copy_to_user(buf, data, sizeof(data));
    return err;
}
/*操作集*/
static const struct file_operations icm20608_fops = {
    .owner = THIS_MODULE,
    .read = icm20608_read,
    .open = icm20608_open,
    .release = icm20608_release,
};

/*icm20608芯片初始化*/
void icm20608_chip_init(struct icm20608_dev *dev)
{
    // u8 value = 0;

    icm20608_write_one_reg(dev, ICM20_PWR_MGMT_1, 0x80); /* 复位，复位后为0x40,睡眠模式 */
    mdelay(50);
    icm20608_write_one_reg(dev, ICM20_PWR_MGMT_1, 0x01); /* 关闭睡眠，自动选择时钟 */
    mdelay(50);

    // 测试数据
    //  value = icm20608_read_one_reg(dev, ICM20_WHO_AM_I);
    //  printk("ICM20608 ID = %#X\r\n", value);

    // value = icm20608_read_one_reg(dev, ICM20_PWR_MGMT_1);
    // printk("ICM20_PWR_MGMT_1 = %#X\r\n", value);

    icm20608_write_one_reg(dev, ICM20_SMPLRT_DIV, 0x00);    /* 输出速率是内部采样率 */
    icm20608_write_one_reg(dev, ICM20_GYRO_CONFIG, 0x18);   /* 陀螺仪±2000dps量程 */
    icm20608_write_one_reg(dev, ICM20_ACCEL_CONFIG, 0x18);  /* 加速度计±16G量程 */
    icm20608_write_one_reg(dev, ICM20_CONFIG, 0x04);        /* 陀螺仪低通滤波BW=20Hz */
    icm20608_write_one_reg(dev, ICM20_ACCEL_CONFIG2, 0x04); /* 加速度计低通滤波BW=21.2Hz */
    icm20608_write_one_reg(dev, ICM20_PWR_MGMT_2, 0x00);    /* 打开加速度计和陀螺仪所有轴 */
    icm20608_write_one_reg(dev, ICM20_LP_MODE_CFG, 0x00);   /* 关闭低功耗 */
    icm20608_write_one_reg(dev, ICM20_FIFO_EN, 0x00);       /* 关闭FIFO */
}

static int icm20608_probe(struct spi_device *spi)
{
    int ret = 0;
    /*搭建字符设备框架，进行数据的读取*/
    /*注册字符设备*/
    icm20608_dev.major = 0;
    if (icm20608_dev.major)
    {
        /*1. 创建设备号*/
        icm20608_dev.devid = MKDEV(icm20608_dev.major, 0);
        /*2. 注册设备*/
        ret = register_chrdev_region(icm20608_dev.devid, ICM20608_DEV_CNT, ICM20608_DEV_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&icm20608_dev.devid, 0, ICM20608_DEV_CNT, ICM20608_DEV_NAME);
        icm20608_dev.major = MAJOR(icm20608_dev.devid);
        icm20608_dev.minor = MINOR(icm20608_dev.devid);
    }
    if (ret < 0)
    {
        ret = -EFAULT;
        goto fail_devid;
    }
    /*初始化cdev*/
    icm20608_dev.cdev.owner = THIS_MODULE;
    cdev_init(&icm20608_dev.cdev, &icm20608_fops);

    /*添加cdev*/
    ret = cdev_add(&icm20608_dev.cdev, icm20608_dev.devid, ICM20608_DEV_CNT);
    if (ret < 0)
    {
        ret = -EINVAL;
        goto fail_cdev;
    }

    /*创建类*/
    icm20608_dev.class = class_create(THIS_MODULE, ICM20608_DEV_NAME);
    if (IS_ERR(icm20608_dev.class))
    {
        ret = PTR_ERR(icm20608_dev.class);
        goto fail_class;
    }

    /*创建设备*/
    icm20608_dev.device = device_create(icm20608_dev.class, NULL, icm20608_dev.devid, NULL, ICM20608_DEV_NAME);
    if (IS_ERR(icm20608_dev.device))
    {
        ret = PTR_ERR(icm20608_dev.device);
        goto fail_device;
    }

    
    /*申请并初始化regmap*/
    icm20608_dev.regmap_config.reg_bits = 8;
    icm20608_dev.regmap_config.val_bits = 8;
    icm20608_dev.regmap_config.read_flag_mask = 0x80;

    icm20608_dev.regmap = regmap_init_spi(spi, &icm20608_dev.regmap_config);
    /*初始化spi_device*/
    spi->mode = SPI_MODE_0;
    spi_setup(spi);
    /*设置私有数据*/
    icm20608_dev.pribate_data = spi;
    /*初始化icm20608*/
    icm20608_chip_init(&icm20608_dev);

    return 0;
fail_device:
    class_destroy(icm20608_dev.class);
fail_class:
    cdev_del(&icm20608_dev.cdev);
fail_cdev:
    unregister_chrdev_region(icm20608_dev.devid, ICM20608_DEV_CNT);
fail_devid:
    return ret;
}

static int icm20608_remove(struct spi_device *spi)
{
    cdev_del(&icm20608_dev.cdev);
    unregister_chrdev_region(icm20608_dev.devid, ICM20608_DEV_CNT);
    device_destroy(icm20608_dev.class, icm20608_dev.devid);
    class_destroy(icm20608_dev.class);
    regmap_exit(icm20608_dev.regmap);
    return 0;
}
// spi_driver
static struct spi_driver icm20608_driver = {
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .driver = {
        .name = "icm20608",
        .owner = THIS_MODULE,
        .of_match_table = icm20608_of_match,
    },
    /*如果不使用设备树，就要用id_table进行设备匹配*/
    .id_table = icm20608_id,

};

/*入口*/
static int __init icm20608_init(void)
{
    int ret = 0;
    /*注册驱动*/
    ret = spi_register_driver(&icm20608_driver);
    return ret;
}

/*出口*/
static void __exit icm20608_exit(void)
{
    /*注销驱动*/
    spi_unregister_driver(&icm20608_driver);
}

module_init(icm20608_init);
module_exit(icm20608_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
