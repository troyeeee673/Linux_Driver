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

/* 寄存器物理地址 */
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)

#define DTSLED_CNT 1
#define DTSLED_NAME "dtsled"

#define LED_OFF 0
#define LED_ON 1

/* 地址映射后的虚拟地址指针 */
// 这里的__iomem是remap返回值属性
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

/*desled设备结构体*/
struct dtsled_dev
{
    dev_t devid;
    struct cdev cdev; /*字符设备*/
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
};

struct dtsled_dev dtsled; /*定义结构体变量*/

static int dtsled_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &dtsled;
    return 0;
}

static int dtsled_release(struct inode *inode, struct file *filp)
{
    struct dtsled_dev *dev = (struct dtsled_dev *)filp->private_data;
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

static ssize_t dtsled_write(struct file *filp, const char __user *buf,
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

static const struct file_operations dtsled_fops = {
    .owner = THIS_MODULE,
    .write = dtsled_write,
    .open = dtsled_open,
    .release = dtsled_release,
};

/*入口*/
static int __init dtsled_init(void)
{
    int ret = 0;
    const char *str;
    u32 regdata[10];
    int i;
    unsigned int val;
    /*注册字符设备*/
    /*申请字符设备号*/
    dtsled.major = 0;
    if (dtsled.major) // 定义了主设备号
    {
        dtsled.devid = MKDEV(dtsled.major, 0);
        ret = register_chrdev_region(dtsled.devid, DTSLED_CNT, DTSLED_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&dtsled.devid, 0, DTSLED_CNT, DTSLED_NAME);
        dtsled.major = MAJOR(dtsled.devid);
        dtsled.minor = MINOR(dtsled.devid);
    }
    if (ret < 0)
    {
        goto fail_devid;
    }

    /*2. 添加字符设备*/
    dtsled.cdev.owner = THIS_MODULE;
    cdev_init(&dtsled.cdev, &dtsled_fops);
    ret = cdev_add(&dtsled.cdev, dtsled.devid, DTSLED_CNT);
    if (ret < 0)
        goto fail_cdev;

    /*3.自动创建设备节点*/
    dtsled.class = class_create(THIS_MODULE, DTSLED_NAME);
    if (IS_ERR(dtsled.class))
    {
        ret = PTR_ERR(dtsled.class);
        goto fail_class;
    }

    dtsled.device = device_create(dtsled.class, NULL, dtsled.devid, NULL, DTSLED_NAME);
    if (IS_ERR(dtsled.device))
    {
        ret = PTR_ERR(dtsled.device);
        goto fail_device;
    }

    /*获取设备属性信息*/
    dtsled.nd = of_find_node_by_path("/alphaled");
    if (dtsled.nd == NULL)
    {
        ret = -EINVAL;
        goto fail_findnd;
    }
    ret = of_property_read_string(dtsled.nd, "status", &str);
    if (ret < 0)
    {
        goto fail_rs;
    }
    else
    {
        printk("status:%s\r\n", str);
    }
    ret = of_property_read_u32_array(dtsled.nd, "reg", regdata, 10);
    if (ret < 0)
    {
        goto fail_rs;
    }
    else
    {
        for (i = 0; i < 10; i++)
        {
            printk("reg[%d] = %d\t", i, regdata[i]);
        }
    }

    /*LED初始化*/
    /*1. 初始化LED灯，地址映射*/
    IMX6U_CCM_CCGR1 = ioremap(regdata[0], regdata[1]);
    SW_MUX_GPIO1_IO03 = ioremap(regdata[2], regdata[3]);
    SW_PAD_GPIO1_IO03 = ioremap(regdata[4], regdata[5]);
    GPIO1_DR = ioremap(regdata[6], regdata[7]);
    GPIO1_GDIR = ioremap(regdata[8], regdata[9]);

    /*2. 初始化io*/
    val = readl(IMX6U_CCM_CCGR1); // 读出寄存器中的值
    val &= ~(3 << 26);            // 清除bit27:26
    val |= 3 << 26;               // bit27:26置一， 使能GPIO1_IO03引脚的时钟
    writel(val, IMX6U_CCM_CCGR1); // 写回寄存器

    /*3. 配置GPIO复用和电气属性*/
    writel(0x05, SW_MUX_GPIO1_IO03);
    writel(0x10b0, SW_MUX_GPIO1_IO03);

    /*4. 设置GPIO*/
    val = readl(GPIO1_GDIR); // 读出寄存器中的值
    val |= 1 << 3;           // bit3置一， 设置GPIO1_IO03引脚为输出
    writel(val, GPIO1_GDIR); // 写回寄存器

    /*5. 设置DR寄存器，决定LED亮灭*/
    val = readl(GPIO1_DR);
    val &= ~(1 << 3); // 清零bit3, 打开led
    writel(val, GPIO1_DR);

    return 0;
fail_rs:

fail_findnd:
    device_destroy(dtsled.class, dtsled.devid);
fail_device:
    class_destroy(dtsled.class);
fail_class:
    cdev_del(&dtsled.cdev);
fail_cdev:
    unregister_chrdev_region(dtsled.devid, (unsigned int)DTSLED_NAME);
fail_devid:
    return ret;
}

/*出口*/
static void __exit dtsled_exit(void)
{
    unsigned int val = 0;

    val = readl(GPIO1_DR);
    val |= (1 << 3); /* bit3清零,打开LED灯 */
    writel(val, GPIO1_DR);
    /*取消地址映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /*删除设备*/
    cdev_del(&dtsled.cdev);

    /*释放设备号*/
    unregister_chrdev_region(dtsled.devid, (unsigned int)DTSLED_NAME);

    /*销毁设备*/
    device_destroy(dtsled.class, dtsled.devid);
    /*销毁类*/
    class_destroy(dtsled.class);
}

module_init(dtsled_init);
module_exit(dtsled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
