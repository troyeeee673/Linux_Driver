#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>      // 提供 struct file_operations, struct inode 等
#include <linux/uaccess.h> // 提供 copy_to_user, copy_from_user
#include <linux/io.h>
#include <linux/slab.h>

#define LED_MAJOR 200
#define LED_NAME "led"

/* 寄存器物理地址 */
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)

#define LED_OFF 0
#define LED_ON 1

/* 地址映射后的虚拟地址指针 */
// 这里的__iomem是remap返回值属性
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

int led_open(struct inode *inode, struct file *file)
{
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

ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
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

int led_release(struct inode *inode, struct file *file)
{
    /*不能该函数内添加下面的代码，因为每一次应用执行close函数，都会调用到这些代码，如果命令是开灯，当执行到close时，马上又会将灯关闭*/
    // unsigned int val = readl(GPIO1_DR);
    // val |= (1 << 3); // 置一bit3, 关闭led
    // writel(val, GPIO1_DR);
    return 0;
}

/*操作集合*/
static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .write = led_write,
    .open = led_open,
    .release = led_release};

/*入口*/
static int __init led_init(void)
{
    unsigned int val = 0;
    /*1. 进行地址映射*/
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

    /*3. 注册字符设备*/
    if (register_chrdev(LED_MAJOR, LED_NAME, &led_fops) < 0)
    {
        printk("register failed");
        return -EIO;
    }
    printk("led_init");
    return 0;
}

/*出口*/
static void __exit led_exit(void)
{
    /*卸载驱动时关闭*/
    unsigned int val = readl(GPIO1_DR);
    val |= (1 << 3); // 置一bit3, 关闭led
    writel(val, GPIO1_DR);

    /*1. 取消地址映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /*2. 注销字符设备*/
    unregister_chrdev(LED_MAJOR, LED_NAME);
    printk("led_exit");
}

/*注册驱动的入口与出口*/
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");