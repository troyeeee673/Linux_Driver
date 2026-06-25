#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>      // 提供 struct file_operations, struct inode 等
#include <linux/uaccess.h> // 提供 copy_to_user, copy_from_user
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>

/* 寄存器物理地址 */
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)

#define LED_OFF 0
#define LED_ON 1

#define NEWCHRLED_NAME "newchrled"
#define NEWCHRLED_COUNT 1

/* 地址映射后的虚拟地址指针 */
// 这里的__iomem是remap返回值属性
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

static struct newchrled_dev newchrled;
/*led 设备结构体*/
struct newchrled_dev
{
    struct cdev cdev; // 字符设备结构体
    dev_t devid;      // 整体设备号
    struct class *class;
    struct device *device;
    int major; // 主设备号
    int minor; // 次设备号
};

static int newchrled_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &newchrled;
    return 0;
}

static int newchrled_release(struct inode *inode, struct file *filp)
{
    struct newchrled_dev *dev = (struct newchrled_dev*)filp->private_data;
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

/*入口*/
static int __init newchrled_init(void)
{
    int ret = 0;
    unsigned int val;
    /*1. 初始化led*/
    IMX6U_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE, 4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE, 4);

    /*2. 初始化时钟*/
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
    /*2. 注册设备*/
    newchrled.major = 0;//手动清零
    if (newchrled.major)
    {
        newchrled.devid = MKDEV(newchrled.major, 0);
        ret = register_chrdev_region(newchrled.devid, NEWCHRLED_COUNT, NEWCHRLED_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&newchrled.devid, 0, NEWCHRLED_COUNT, NEWCHRLED_NAME);
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
    ret = cdev_add(&newchrled.cdev, newchrled.devid, NEWCHRLED_COUNT);

    /*自动创建字符设备*/
    newchrled.class = class_create(THIS_MODULE, NEWCHRLED_NAME);
    if (IS_ERR(newchrled.class))
    {
        return PTR_ERR(newchrled.class);
    }
    newchrled.device = device_create(newchrled.class, NULL, newchrled.devid, NULL, NEWCHRLED_NAME);
    if (IS_ERR(newchrled.device))
    {
        return PTR_ERR(newchrled.device);
    }

    return 0;
}

/*出口*/
static void __exit newchrled_exit(void)
{
    
    /*卸载驱动时关闭*/
    unsigned int val = readl(GPIO1_DR);
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
    unregister_chrdev_region(newchrled.devid, NEWCHRLED_COUNT);


    /*销毁设备*/
    device_destroy(newchrled.class, newchrled.devid);
    /*销毁类*/
    class_destroy(newchrled.class);
}

/*注册和卸载模块*/
module_init(newchrled_init);
module_exit(newchrled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
