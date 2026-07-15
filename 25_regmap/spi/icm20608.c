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
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/unaligned/be_byteshift.h>
#include "icm20608.h"

#define ICM20608_DEV_CNT 1
#define ICM20608_DEV_NAME "icm20608"

/*
 * ICM20608的扫描元素，3轴加速度计、
 * 3轴陀螺仪、1路温度传感器，1路时间戳
 */
enum inv_icm20608_scan
{
    INV_ICM20608_SCAN_ACCL_X,
    INV_ICM20608_SCAN_ACCL_Y,
    INV_ICM20608_SCAN_ACCL_Z,
    INV_ICM20608_SCAN_TEMP,
    INV_ICM20608_SCAN_GYRO_X,
    INV_ICM20608_SCAN_GYRO_Y,
    INV_ICM20608_SCAN_GYRO_Z,
    INV_ICM20608_SCAN_TIMESTAMP,
};

#define ICM20608_CHAN(_type, _channel2, _index) {         \
    .type = _type,                                        \
    .modified = 1,                                        \
    .channel2 = _channel2,                                \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |        \
                          BIT(IIO_CHAN_INFO_CALIBBIAS),   \
    .scan_index = _index,                                 \
    .scan_type = {                                        \
        .sign = 's',                                      \
        .realbits = 16,                                   \
        .storagebits = 16,                                \
        .shift = 0,                                       \
        .endianness = IIO_BE,                             \
    },                                                    \
}

/* ICM20608 通道 */
static const struct iio_chan_spec icm20608_channels[] = {
    /* 温度通道 */
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = INV_ICM20608_SCAN_TEMP,
        .scan_type = {
            .sign = 's',
            .realbits = 16,
            .storagebits = 16,
            .shift = 0,
            .endianness = IIO_BE,
        },
    },

    /* 加速度X，Y，Z三个通道 */
    ICM20608_CHAN(IIO_ACCEL, IIO_MOD_X, INV_ICM20608_SCAN_ACCL_X), /* 加速度X轴 */
    ICM20608_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_ICM20608_SCAN_ACCL_Y), /* 加速度Y轴 */
    ICM20608_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_ICM20608_SCAN_ACCL_Z), /* 加速度Z轴 */

    ICM20608_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_ICM20608_SCAN_GYRO_X), /* 陀螺仪X轴 */
    ICM20608_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_ICM20608_SCAN_GYRO_Y), /* 陀螺仪Y轴 */
    ICM20608_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_ICM20608_SCAN_GYRO_Z), /* 陀螺仪Z轴 */
};

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
    struct spi_device *spi;
    struct regmap *regmap;
    struct regmap_config regmap_config;
    struct mutex mutex;
};

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
    regmap_write(dev->regmap, reg, data);
}

static const struct iio_info icm20608_info = {
    .driver_module = THIS_MODULE,
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

static int icm20608_read_channel_data(struct iio_dev *indio_dev,
                                      struct iio_chan_spec const *chan, int *val)
{
    int ret = 0;
    struct icm20608_dev *dev = iio_priv(indio_dev);

    switch (chan->type)
    {
    case IIO_ACCEL:
        printk("IIO_ACCEL\r\n");
        switch (chan->channel2)
        {
        case IIO_MOD_X:
            printk("IIO_MOD_X\r\n");
            break;
        case IIO_MOD_Y:
            printk("IIO_MOD_Y\r\n");
            break;
        case IIO_MOD_Z:
            printk("IIO_MOD_Z\r\n");
            break;
        default:
            break;
        }
        break;
    case IIO_ANGL_VEL:
        printk("IIO_ANGL_VEL\r\n");
        break;
    case IIO_TEMP:
        printk("IIO_TEMP\r\n");
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}

static int icm20608_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan, int *val, int *val2,
                             long mask)
{
    int ret = 0;
    struct icm20608_dev *dev = iio_priv(indio_dev);

    /* 区分读取的数据类型，是RAW? SCALE? 还是OFFSET等 */
    switch (mask)
    {
    case IIO_CHAN_INFO_RAW:
        printk("IIO_CHAN_INFO_RAW\r\n");
        mutex_lock(&dev->mutex);
        icm20608_read_channel_data(indio_dev, chan, val);
        mutex_unlock(&dev->mutex);
        return ret;
    case IIO_CHAN_INFO_SCALE:
        printk("IIO_CHAN_INFO_SCALE\r\n");
        return ret;
    case IIO_CHAN_INFO_OFFSET:
        printk("IIO_CHAN_INFO_OFFSET\r\n");
        return ret;
    case IIO_CHAN_INFO_CALIBBIAS:
        printk("IIO_CHAN_INFO_CALIBBIAS\r\n");
        return ret;
    default:
        return -EINVAL;
    }

    return 0;
}
static int icm20608_write_raw(struct iio_dev *indio_dev,
                              struct iio_chan_spec const *chan, int val, int val2, long mask)
{
    printk("icm20608_write_raw\r\n");
    return 0;
}

static int icm20608_write_raw_get_fmt(struct iio_dev *indio_dev,
                                      struct iio_chan_spec const *chan,
                                      long mask)
{
    printk("icm20608_write_raw_get_fmt\r\n");
    return 0;
}

/* iio_info */
static const struct iio_info icm20608_info = {
    .driver_module = THIS_MODULE,
    .read_raw = icm20608_read_raw,
    .write_raw = icm20608_write_raw,
    .write_raw_get_fmt = icm20608_write_raw_get_fmt,
};

static int icm20608_probe(struct spi_device *spi)
{
    int ret = 0;
    /*申请iio_dev和icm20608_dev*/
    struct icm20608_dev *dev;
    struct iio_dev *indio_dev;

    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*dev));
    if (indio_dev)
    {
        ret = -EINVAL;
        goto fail_iio_alloc;
    }

    dev = iio_priv(indio_dev);

    /*初始化dev*/
    dev->spi = spi;
    spi_set_drvdata(spi, indio_dev);
    mutex_init(&dev->mutex);

    // 初始化iio_dev
    indio_dev->dev.parent = &spi->dev;
    indio_dev->channels = icm20608_channels;
    indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);
    indio_dev->name = ICM20608_DEV_NAME;
    indio_dev->modes = INDIO_DIRECT_MODE; /* 直接模式，提供sysfs接口 */
    indio_dev->info = &icm20608_info;

    /* 将iio_dev注册到内核 */
    ret = iio_device_register(indio_dev);
    if (ret < 0)
    {
        dev_err(&spi->dev, "unable to register iio device\n");
        goto fail_iio_register;
    }

    /*申请并初始化regmap*/
    dev->regmap_config.reg_bits = 8;
    dev->regmap_config.val_bits = 8;
    dev->regmap_config.read_flag_mask = 0x80;

    dev->regmap = regmap_init_spi(spi, &(dev->regmap_config));
    /*初始化spi_device*/
    spi->mode = SPI_MODE_0;
    spi_setup(spi);

    // 初始化icm20608
    icm20608_chip_init(dev);

    return 0;
fail_iio_register:
fail_iio_alloc:
    return ret;
}

static int icm20608_remove(struct spi_device *spi)
{
    int ret = 0;

    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct icm20608_dev *dev;

    dev = iio_priv(indio_dev);

    /* 1、注销iio_dev */
    iio_device_unregister(indio_dev);

    /* 2、删除regmap */
    regmap_exit(dev->regmap);
    return ret;
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
