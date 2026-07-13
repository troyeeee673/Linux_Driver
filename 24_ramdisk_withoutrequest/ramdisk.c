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
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>

// 定义磁盘大小，使用内存模拟
#define RAMDISK_SIZE (2 * 1024 * 1024) /* 大小2MB */
#define RAMDISK_NAME "ramdisk"         /* 名字 */
#define RAMDISK_MINOR 3                /* 表示3个分区 */

/* ramdisk设备结构体 */
struct ramdisk_dev
{
    int major;                 /* 主设备号 */
    unsigned char *ramdiskbuf; /* ramdisk的内存空间，模拟磁盘的大小 */
    struct gendisk *gendisk;
    struct request_queue *queue;
    spinlock_t lock;
};

struct ramdisk_dev ramdisk;

/* 制造请求函数 */
static void ramdisk_make_request(struct request_queue *queue, struct bio *bio)
{
    int offset;
    struct bio_vec bvec;
    struct bvec_iter iter;

    offset = bio->bi_iter.bi_sector << 9; /* 要操作的磁盘起始扇区偏移,改为字节地址 */

    /* 循环处理每个段 */
    bio_for_each_segment(bvec, bio, iter)
    {
        /* 获取bio里面的缓冲区：
         * 如果是读：从磁盘里面读取到的数据保存在此缓冲区里面
         * 如果是写：此缓冲区保存着要写入到磁盘里面的数据
         */
        char *ptr = page_address(bvec.bv_page) + bvec.bv_offset;
        int len = bvec.bv_len; /* 长度 */

        if (bio_data_dir(bio) == READ) /* 读操作 */
            memcpy(ptr, ramdisk.ramdiskbuf + offset, len);
        else /* 写操作 */
            memcpy(ramdisk.ramdiskbuf + offset, ptr, len);
        offset += len;
    }

    set_bit(BIO_UPTODATE, &bio->bi_flags);
    bio_endio(bio, 0);
}

static int ramdisk_open(struct block_device *bdev, fmode_t mode)
{
    printk(KERN_INFO "ramdisk: device opened\n");
    return 0;
}

static void ramdisk_release(struct gendisk *disk, fmode_t mode)
{
    printk(KERN_INFO "ramdisk: device released\n");
}

static int ramdisk_getgeo(struct block_device *dev, struct hd_geometry *geo)
{
    /* 设置磁盘几何参数 */
    geo->heads = 2;
    geo->sectors = 32;
    geo->cylinders = get_capacity(dev->bd_disk) / (2 * 32);
    return 0;
}

/* 块设备操作集 */
static const struct block_device_operations ramdisk_fops =
{
    .owner = THIS_MODULE,
    .open = ramdisk_open,
    .release = ramdisk_release,
    .getgeo = ramdisk_getgeo,
};

/* 入口函数 */
static int __init ramdisk_init(void)
{
    int ret = 0;

    printk(KERN_INFO "ramdisk: initializing...\n");

    // 1. 申请内存
    ramdisk.ramdiskbuf = kzalloc(RAMDISK_SIZE, GFP_KERNEL);
    if (ramdisk.ramdiskbuf == NULL)
    {
        printk(KERN_ERR "ramdisk: failed to allocate memory\n");
        ret = -ENOMEM;
        goto fail_kzalloc;
    }

    // 2. 注册块设备
    ramdisk.major = register_blkdev(0, RAMDISK_NAME);
    if (ramdisk.major < 0)
    {
        printk(KERN_ERR "ramdisk: failed to register block device\n");
        ret = ramdisk.major;
        goto fail_register;
    }
    printk(KERN_INFO "ramdisk: registered with major number %d\n", ramdisk.major);

    // 3. 申请gendisk
    ramdisk.gendisk = alloc_disk(RAMDISK_MINOR);
    if (ramdisk.gendisk == NULL)
    {
        printk(KERN_ERR "ramdisk: failed to allocate gendisk\n");
        ret = -ENOMEM;
        goto fail_alloc_disk;
    }

    // 4. 初始化自旋锁
    spin_lock_init(&ramdisk.lock);

    // 5. 申请请求队列
    ramdisk.queue = blk_alloc_queue(GFP_KERNEL);
    if (ramdisk.queue == NULL)
    {
        printk(KERN_ERR "ramdisk: failed to initialize request queue\n");
        ret = -ENOMEM;
        goto fail_init_queue;
    }
    ramdisk.queue->queuedata = &ramdisk;

    // 绑定制造请求函数
    blk_queue_make_request(ramdisk.queue, ramdisk_make_request);

    // 6. 初始化gendisk
    ramdisk.gendisk->major = ramdisk.major;
    ramdisk.gendisk->first_minor = 0;
    ramdisk.gendisk->fops = &ramdisk_fops;
    sprintf(ramdisk.gendisk->disk_name, "%s", RAMDISK_NAME);
    ramdisk.gendisk->private_data = &ramdisk;
    ramdisk.gendisk->queue = ramdisk.queue;
    set_capacity(ramdisk.gendisk, RAMDISK_SIZE / 512);

    // 7. 添加磁盘设备
    add_disk(ramdisk.gendisk);
    printk(KERN_INFO "ramdisk: initialized successfully\n");

    return 0;

fail_init_queue:
    put_disk(ramdisk.gendisk);
fail_alloc_disk:
    unregister_blkdev(ramdisk.major, RAMDISK_NAME);
fail_register:
    kfree(ramdisk.ramdiskbuf);
fail_kzalloc:
    return ret;
}

/* 出口函数 */
static void __exit ramdisk_exit(void)
{
    printk(KERN_INFO "ramdisk: exiting...\n");

    del_gendisk(ramdisk.gendisk);
    put_disk(ramdisk.gendisk);
    blk_cleanup_queue(ramdisk.queue);
    unregister_blkdev(ramdisk.major, RAMDISK_NAME);
    kfree(ramdisk.ramdiskbuf);

    printk(KERN_INFO "ramdisk: exited successfully\n");
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vicky");
MODULE_DESCRIPTION("Simple RAM Disk Driver with Request Queue");
MODULE_VERSION("1.0");