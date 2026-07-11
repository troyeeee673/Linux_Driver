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
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/types.h>

#define FT5426_DEV_CNT 1
#define FT5426_DEV_NAME "ft5426"

#define MAX_SUPPORT_POINTS 5      /* 5点触摸 */
#define TOUCH_EVENT_DOWN 0x00     /* 按下 */
#define TOUCH_EVENT_UP 0x01       /* 抬起 */
#define TOUCH_EVENT_ON 0x02       /* 接触 */
#define TOUCH_EVENT_RESERVED 0x03 /* 保留 */

/* FT5X06寄存器相关宏定义 */
#define FT5X06_TD_STATUS_REG 0X02   /* 状态寄存器地址 */
#define FT5x06_DEVICE_MODE_REG 0X00 /* 模式寄存器 */
#define FT5426_IDG_MODE_REG 0XA4    /* 中断模式 */
#define FT5X06_READLEN 29           /* 要读取的寄存器个数 */

struct ft5426_dev
{
    int irq_pin, reset_pin;
    int irq_num;
    struct device_node *nd;
    struct i2c_client *client;
    void *private_data;
    struct input_dev *input;
};

static struct ft5426_dev ft5426_dev;

/*传统的匹配表*/
static struct i2c_device_id ft5426_id[] = {
    {"edt,edt-ft5426", 0},
    {}

};

/*设备树匹配表*/
static struct of_device_id ft5426_of_match[] = {
    {.compatible = "edt,edt-ft5426"},
    {}};

/*读取ft5426N个寄存器数据*/
static int ft5426_read_regs(struct ft5426_dev *dev, u8 reg, void *data, int n)
{
    int ret = 0;
    struct i2c_msg msg[2]; // 一个i2c时序主要发送两大部分数据，第一发送要读取的从机的地址，第二要发送要操作的寄存器
    struct i2c_client *client = (struct i2c_client *)dev->client;

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

/*写入ft5426的N个寄存器*/
static int ft5426_write_regs(struct ft5426_dev *dev, u8 reg, u8 *buf, u8 len)
{
    int ret = 0;
    u8 b[256];
    struct i2c_msg msg[1]; // 写数据的时序是将地址、寄存器和数据一并发送
    struct i2c_client *client = (struct i2c_client *)dev->client;

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

/*写一个寄存器*/
static void ft5426_write_one_reg(struct ft5426_dev *dev, u8 reg, u8 data)
{
    u8 buf = 0;
    buf = data;
    ft5426_write_regs(dev, reg, &buf, 1);
}

/*读取一个寄存器*/
static unsigned char ft5426_read_one_reg(struct ft5426_dev *dev, u8 reg)
{
    u8 data;
    data = ft5426_read_regs(dev, reg, &data, 1);
    return data;
}

/*中断处理函数*/
static irqreturn_t ft5426_handler(int irq, void *dev_id)
{
    struct ft5426_dev *multidata = dev_id;
    u8 rdbuf[29];
    int i, type, x, y, id;
    int offset, tplen;
    int ret;
    bool down;

    offset = 1; /* 偏移1，也就是0X02+1=0x03,从0X03开始是触摸值 */
    tplen = 6;  /* 一个触摸点有6个寄存器来保存触摸值 */

    memset(rdbuf, 0, sizeof(rdbuf)); /* 清除 */

    /* 从FT5X06芯片读取触摸点信息 */
    ft5426_read_regs(multidata, FT5X06_TD_STATUS_REG, rdbuf, FT5X06_READLEN);
    for (i = 0; i < MAX_SUPPORT_POINTS; i++)
    {
        /* 提取出来每个触摸点的坐标 */
        u8 *buf = &rdbuf[i * tplen + offset];
        type = buf[0] >> 6; /* 获取触摸类型 */
        if (type == TOUCH_EVENT_RESERVED)
            continue;

        /* 我们所使用的触摸屏和FT5X06是反过来的 */
        x = ((buf[2] << 8) | buf[3]) & 0x0fff;
        y = ((buf[0] << 8) | buf[1]) & 0x0fff;

        id = (buf[2] >> 4) & 0x0f;
        down = type != TOUCH_EVENT_UP;

        /*上报数据*/
        /* 上报数据 */
        input_mt_slot(multidata->input, id);                                /* ABS_MT_SLOT */
        input_mt_report_slot_state(multidata->input, MT_TOOL_FINGER, down); /* ABS_MT_TRACKING_ID */

        if (!down)
        {
            continue;
        }

        input_report_abs(multidata->input, ABS_MT_POSITION_X, x);
        input_report_abs(multidata->input, ABS_MT_POSITION_Y, y);
    }
    input_mt_report_pointer_emulation(multidata->input, true);
    input_sync(multidata->input); /* SYN_REPORT */
    return IRQ_HANDLED;
}
/*中断初始化 */
static int ft5426_ts_irq(struct i2c_client *client, struct ft5426_dev *dev)
{
    int ret = 0;

    /* 1,申请中断GPIO */
    if (gpio_is_valid(dev->irq_pin))
    {
        ret = devm_gpio_request_one(&client->dev, dev->irq_pin,
                                    GPIOF_IN, "edt-ft5x06 irq");
        if (ret)
        {
            dev_err(&client->dev,
                    "Failed to request GPIO %d, error %d\n",
                    dev->irq_pin, ret);
            return ret;
        }
    }

    /* 2，申请中断,client->irq就是IO中断， */
    ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
                                    ft5426_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                    client->name, &ft5426_dev);
    if (ret)
    {
        dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
        return ret;
    }

    return 0;
}
/*复位ft5426*/
static int ft5426_reset(struct i2c_client *client, struct ft5426_dev *dev)
{
    int ret = 0;

    if (gpio_is_valid(dev->reset_pin))
    { /* 检查IO是否有效 */
        /* 申请复位IO，并且默认输出低电平 */
        ret = devm_gpio_request_one(&client->dev,
                                    dev->reset_pin, GPIOF_OUT_INIT_LOW,
                                    "edt-ft5x06 reset");
        if (ret)
        {
            return ret;
        }
        msleep(5);
        gpio_set_value(dev->reset_pin, 1); /* 输出高电平，停止复位 */
        msleep(300);
    }

    return 0;
}

static int ft5426_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;

    ft5426_dev.client = client;
    /*获取irq和reset引脚*/
    ft5426_dev.irq_pin = of_get_named_gpio(client->dev.of_node, "intterupt-gpio", 0);
    ft5426_dev.reset_pin = of_get_named_gpio(client->dev.of_node, "reset-gpio", 0);

    /*复位*/
    ft5426_reset(client, &ft5426_dev);
    /*初始化中断*/
    ft5426_ts_irq(client, &ft5426_dev);

    /*初始化ft5426*/
    /* 初始化FT5426 */
    ft5426_write_one_reg(&ft5426_dev, FT5x06_DEVICE_MODE_REG, 0); /* 进入正常模式 */
    ft5426_write_one_reg(&ft5426_dev, FT5426_IDG_MODE_REG, 1);    /* FT5426中断模式 */

    /*input子系统框架*/
    ft5426_dev.input = devm_input_allocate_device(&client->dev);

    ft5426_dev.input->name = client->name;

    ft5426_dev.input->id.bustype = BUS_I2C;
    ft5426_dev.input->dev.parent = &client->dev;

    __set_bit(EV_SYN, ft5426_dev.input->evbit);
    __set_bit(EV_KEY, ft5426_dev.input->evbit);
    __set_bit(EV_ABS, ft5426_dev.input->evbit);
    __set_bit(BTN_TOUCH, ft5426_dev.input->keybit);

    /* Single touch */
    input_set_abs_params(ft5426_dev.input, ABS_X, 0, 1024, 0, 0);
    input_set_abs_params(ft5426_dev.input, ABS_Y, 0, 600, 0, 0);

    /* Multi touch */
    input_mt_init_slots(ft5426_dev.input, MAX_SUPPORT_POINTS, 0);
    input_set_abs_params(ft5426_dev.input, ABS_MT_POSITION_X, 0, 1024, 0, 0);
    input_set_abs_params(ft5426_dev.input, ABS_MT_POSITION_Y, 0, 600, 0, 0);

    /*注册设备*/
    input_register_device(ft5426_dev.input);
    return ret;
}

static int ft5426_remove(struct i2c_client *client)
{
    input_unregister_device(ft5426_dev.input);
    return 0;
}
static struct i2c_driver ft5426_driver = {
    .probe = ft5426_probe,
    .remove = ft5426_remove,
    .driver = {
        .name = "edt-ft5426",
        .owner = THIS_MODULE,
        .of_match_table = ft5426_of_match,
    },
    /*如果不使用设备树，就要用id_table进行设备匹配*/
    .id_table = ft5426_id,

};

/*入口*/
static int __init ft5426_init(void)
{
    int ret = 0;
    /*注册驱动*/
    ret = i2c_add_driver(&ft5426_driver);
    return ret;
}

/*出口*/
static void __exit ft5426_exit(void)
{
    /*注销驱动*/
    i2c_del_driver(&ft5426_driver);
}

module_init(ft5426_init);
module_exit(ft5426_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
