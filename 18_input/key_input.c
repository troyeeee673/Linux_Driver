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
#include <linux/input.h>

/* cdev 系列操作是"内核注册"，后面做的 class 和 device 是"应用层可见性创建"。内核注册让驱动能用，可见性创建让应用程序能找到它*/

#define KEYINPUT_CNT 1
#define KEYINPUT_NAME "keyinput"

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
struct keyinput_dev
{
    struct device_node *nd;
    struct irq_keydesc irqkey[KEY_NUM];
    struct timer_list timer;
    struct input_dev *input_dev; /*输入设备*/
};

struct keyinput_dev keyinputdev;

/*中断处理函数*/
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct keyinput_dev *dev = dev_id;

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
    dev->timer.data = (volatile unsigned long)dev_id;
    // 进行10ms延时
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));

    return IRQ_HANDLED;
}

/* 定时器处理函数 */
static void timer_func(unsigned long arg)
{
    int value = 0;
    struct keyinput_dev *dev = (struct keyinput_dev *)arg;

    value = gpio_get_value(dev->irqkey[0].gpio);
    if (value == 0) /* 按下 */
    {
        /*上报按键值*/
        input_event(dev->input_dev, EV_KEY, KEY_0, 1);
        input_sync(dev->input_dev);
        printk("KEY0 Push!\r\n");
    }
    else if (value == 1) /* 释放 */
    {
        /*上报*/
        input_event(dev->input_dev, EV_KEY, KEY_0, 0);
        input_sync(dev->input_dev);
        printk("KEY0 release!\r\n");
    }
}

/*按键初始化*/
static int keyio_init(struct keyinput_dev *dev)
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

    /*按键中断初始化*/
    for (i = 0; i < KEY_NUM; i++)
    {
        // 修正：第一个参数应该是中断号，不是gpio编号
        ret = request_irq(dev->irqkey[i].irqnum,                      // 中断号
                          dev->irqkey[i].handler,                     // 中断处理函数
                          IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, // 触发方式
                          dev->irqkey[i].name,                        // 中断名称
                          &keyinputdev);                              // 设备ID
        if (ret)
        {
            printk("request_irq failed for %s, ret=%d\n", dev->irqkey[i].name, ret);
            goto fail_irq;
        }
    }

    return 0;

fail_irq:
    // 释放已申请的中断和GPIO
    for (i = 0; i < KEY_NUM; i++)
    {
        if (dev->irqkey[i].irqnum)
        {
            free_irq(dev->irqkey[i].irqnum, &keyinputdev);
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
static int __init keyinput_init(void)
{
    int ret = 0;

    /*初始化keyio*/
    ret = keyio_init(&keyinputdev);
    if (ret < 0)
        goto fail_keyio;
    // 初始化定时器
    init_timer(&keyinputdev.timer);
    keyinputdev.timer.function = timer_func;

   
    /*注册input_dev*/ 
    /*申请设备*/
    keyinputdev.input_dev = input_allocate_device();
    if(keyinputdev.input_dev == NULL)
    {
        ret = -EINVAL;
        goto fail_keyio;
    }
    //设置按键名字
    keyinputdev.input_dev->name = KEYINPUT_NAME;
    /*设置事件和键值*/
    __set_bit(EV_KEY, keyinputdev.input_dev->evbit);/*按键事件*/
    __set_bit(EV_REP, keyinputdev.input_dev->evbit);/*重复事件*/
    __set_bit(KEY_0, keyinputdev.input_dev->keybit);/*按键值（使用key0）*/

    //注册
    ret = input_register_device(keyinputdev.input_dev);
    if(ret)
    {
        ret = -EINVAL;
        goto fail_input_register;
    }

    return 0;
fail_input_register:
    input_free_device(keyinputdev.input_dev);
fail_keyio:
    return ret;
}

/*出口*/
static void __exit keyinput_exit(void)
{
    int i = 0;

    /*释放中断*/
    for (i = 0; i < KEY_NUM; i++)
    {
        free_irq(keyinputdev.irqkey[i].irqnum, &keyinputdev);
    }

    /*释放gpio*/
    for (i = 0; i < KEY_NUM; i++)
    {
        gpio_free(keyinputdev.irqkey[i].gpio); // 修正：传递gpio编号
    }

    /*删除定时器*/
    del_timer_sync(&keyinputdev.timer);

    /*注销input_dev*/
    input_unregister_device(keyinputdev.input_dev);
    input_free_device(keyinputdev.input_dev);
}

module_init(keyinput_init);
module_exit(keyinput_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
MODULE_DESCRIPTION("imx6uirq driver for key interrupt");