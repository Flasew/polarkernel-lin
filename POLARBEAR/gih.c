/*
 * Filename: gih.c
 * Author: Weiyang Wang
 * Descroption: Body of the generic interrupt handler module.
 *              On load, this module will create four devices under /dev, 
 *              each for different purposes. User can write to the gih device
 *              and allow their data to be delayed upon interrupt happens. 
 *              Interrupts to catch is specified by the irq number.
 *              See readme for more details.
 * Date: July 20, 2017
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/cdev.h> 
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/fcntl.h>
#include <linux/ktime.h>

#include <asm/atomic64_32.h>
#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "gih.h"
#include "fio.h"

/* gih device */
static int gih_open(struct inode *, struct file *);
static int gih_close(struct inode *, struct file *);
static long gih_ioctl(struct file *, unsigned int, unsigned long);
static ssize_t gih_write(struct file *, const char __user *, size_t, loff_t *);
static irqreturn_t gih_intr(int, void *);
static void gih_do_work(void *);

struct file_operations gih_fops = {
    .owner              = THIS_MODULE,
    .write              = gih_write,
    .unlocked_ioctl     = gih_ioctl, 
    .open               = gih_open,
    .release            = gih_close
};

static gih_dev gih = 0;             /* gih device */

/* log devices */
static int log_open(struct inode *, struct file *);
static int log_close(struct inode *, struct file *);
static ssize_t log_read(struct file *, char __user *, size_t, loff_t *);

struct file_operations log_fops = {
    .owner   = THIS_MODULE,
    .read    = log_read,
    .open    = log_read,
    .release = log_close
};

static log_dev log_devices[3] = { 0 }; /* all the logging device, can be
                                          accessed by their minor number */


static int 
gih_open(struct inode * inode, struct file * filp) 
{
    int error = 0;

    if (!mutex_trylock(&gih.dev_open)) {return -EBUSY;}
    
    printk(KERN_ALERT "[gih] Openining gih device...\n");

    atomic64_set(&gih.data_wait, 0);
    gih.irq_wq = create_singlethread_workqueue(IRQ_WQ_NAME);
    kfifo_reset(&gih.data_buf);
    INIT_WORK(&gih.work, gih_do_work, NULL);

    /* if not configurated, it's first time, conf. from ioctl needed */
    if (!gih.configurated) {
        printk(KERN_ALERT "[gih] configuring with ioctl...\n");
        /* dump the rest of the work to ioctl */
        return 0;
    }

    else {
        error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
            gih.irq_name, (void*)&gih);
        if (error < 0) {
            printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", err);
            return error;
        }

        gih.dest_filp = file_open(gih.path);
    }
    return error;
}

static int 
gih_close(struct inode * inode, struct file * filp) 
{
    int copied = 0;
    long long dwait;

    printk(KERN_ALERT "[gih] Releasing gih device...\n");

    free_irq(gih.irq, (void*)&gih);
    flush_workqueue(gih.irq_wq);
    destroy_workqueue(gih.irq_wq);
    /* this would result as dumping all unsent data, skipping the intr */
    dwait = atomic64_read(&gih.data_wait);
    copied = file_write_kfifo(gih.dest_filp, &gih.data_buf, dwait);
    if  (copied != dwait) {
        if  (copied < 0) {
            printk(KERN_ALERT "[gih] ERROR writng the rest of data\n");
            return copied;
        } 
        else {
            printk(KERN_ALERT 
                "[gih] WARNING: data lose occured, %d bytes lost\n", 
                dwait - copied);
        }
    } 
    else 
        copied = 0;

    file_close(gih.dest_filp);
    gih.dest_filp = NULL;

    mutex_unlock(&gih.dev_open);
    return copied;
}

static ssize_t 
gih_write(struct file * filp, 
          const char __user * buffer, 
          size_t len, 
          loff_t * offset) 
{

    unsigned char local_buf = 0;
    size_t copied;

    if (kfifo_is_full(&gih.data_buf)) {
        printk(KERN_ALERT "[gih] Warning: gih buffer is full, "
            "no data written\n");
        return 0;
    }


    *offset = 0;
    
    for (copied = 0; copied < len; copied++) {

        if (kfifo_is_full(&gih.data_buf)) {
            printk(KERN_ALERT "[gih] Warning: gih buffer is full, "
            "%d bytes of data lost during writing\n",
            len - copied);
            return copied;
        }

        get_user(local_buf, &buffer[copied]);
        kfifo_put(&gih.data_buf, local_buf);
    }

    atomic64_add(copied, &gih.data_wait);

    return copied;
}

static long gih_ioctl(struct file * filep, 
                      unsigned int cmd, 
                      unsigned long arg) {

    int length;
    int error;

    if (gih.configurated) {
        return -EINVAL;
    }

    switch (cmd) {

        case GIH_IOC_CONFIG_IRQ:
            gih.irq = (int)arg;
            error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
                gih.irq_name, (void*)&gih);
                if (error < 0) {
                    printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", err);
                    return error;
                }
            break;

        case GIH_IOC_CONFIG_SLEEP_T:
            gih.sleep_msec = (unsigned int)arg;
            break;

        case GIH_IOC_CONFIG_WRT_SZ:
            gih.write_size = (size_t)arg;
            break;

        case GIH_IOC_CONFIG_PATH:
            length = strlen((const char *)arg);
            if (length > PATH_MAX_LEN - 1)
                return -EINVAL;
            strncpy(gih.path, (const char *)arg, length);
            gih.path[length] = '\0';
            gih.dest_filp = open_file(gih.path);
            break;

        /* make sure to only call this after configuratoin */
        case GIH_IOC_CONFIG_FINISH:
            gih.configurated = TRUE;
            printk(KERN_ALERT "[gih] configuration finished.\n");
            break;

        default:
            return -EINVAL;
    }
    return 0;
}

static void 
gih_do_work(void * irq_count_in) {

    int irq_count;

    if (DEBUG)
        printk(KERN_ALERT "[gih] Entering work queue function...\n");

    irq_count = (unsigned long)irq_count_in;

    unsigned char output_chr = 0;
    size_t n_out_byte;            /* number of byte to output */
    size_t out = 0;               /* number of byte actually outputed */
    struct log exit;
    struct log entry = {
        -1,
        irq_count,
        0
    };

    log_devices[WQ_N_LOG_MINOR].irq_count++;

    do_gettimeofday(&entry.time);
    kfifo_put(&log_devices[WQ_N_LOG_MINOR].buffer, entry);

    mutex_lock(&gih.wrt_lock);

    n_out_byte = min(kfifo_len(&gih.data_buf), gih.write_size);

    msleep(gih.sleep_msec);

    while (out < n_out_byte) {
        out += file_write_kfifo(gih.dest_filp, gih.data_buf, 1);
        atomic64_dec(&gih.data_wait);
    }

    mutex_unlock(&gih.wrt_lock);

    log_devices[WQ_N_LOG_MINOR].irq_count++;

    exit = {
        out, 
        irq_count,
        0
    };
    do_gettimeofday(&exit.time);
    kfifo_put(&log_devices[WQ_X_LOG_MINOR].buffer, exit);

    if (DEBUG)
        printk(KERN_ALERT "[gih] Exiting work queue function...\n");
}

static irqreturn_t gih_intr(int irq, void * data) {
    /* enque work, write log */
    int error = 0;
    struct log intr_log = {
        -1, 
        log_devices[INTR_LOG_MINOR].irq_count++,
        0
    }
    do_gettimeofday(&intr_log.time);
    kfifo_put(&log_devices[INTR_LOG_MINOR].buffer, intr_log);

    /* perhaps also try kernal thread, given the work function in this way */
    PREPARE_WORK(&gih.work, gih_do_work, )
    error = queue_work(gih.irq_wq, gih.work,
        (void*)log_devices[INTR_LOG_MINOR].irq_count);

    if (error) 
        printk(KERN_ALERT "[gih] WARNING: interrupt missed\n");

    return IRQ_HANDLED;
}

/* log device function definitions */
static int log_open(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);

    if (!mutex_trylock(&log_devices[minor].dev_open)) {return -EBUSY;}

    filp->private_data = &log_devices[minor];
    filp->f_pos = 0;

    if (DEBUG)
        printk(KERN_ALERT "[log] Log device %u opened\n", minor);
    return 0;
}

static int log_close(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);
    mutex_unlock(&log_devices[minor].dev_open);

    filp->private_data = null;

    if (DEBUG)
        printk(KERN_ALERT "[log] Log device %u released\n", minor);
    return 0;
}

static ssize_t log_read(struct file * flip, 
                        char __user * buf, 
                        size_t len, 
                        loff_t * offset) {

    if (*offset != 0) {return 0;}

    size_t amount_log;
    size_t finished_log; 

    size_t log_len;

    log_dev device;
    struct log log;

    device = *filp->private_data;
    amount = kfifo_len(&device.buffer);

    /* this function doesn't do much of checking, 
       try to make enough read size in userland 
       otherwise data may be truncated */

    for (finished_log = 0; 
         finished_log < amount_log && len > 0; 
         finished_log++) {

        kfifo_get(&device.buffer, &log);

        log_len = snprintf(buf, len - 1, 
            "[ %ld.%ld], Intr cnt: %lu, w.sz: %d\n", 
            (long)log.time.tv_sec, (long)log.time.tv_usec,
            log.irq_count, log.byte_sent);

        if (log_len < 0) return log_len;
        if (log_len >= len) break;      /* for safty... */

        len -= log_len;
        *offset += log_len;
        buf += log_len;
    }
    buf[*offset] = '\0';
    return *offset;
}

static int __init gih_init(void) {

    int error;
    int gih_major;
    int log_major;

    INIT_KFIFO(data_buf);
    gih.data_buf = *(struct kfifo *)&data_buf;

    INIT_KFIFO(ilog_buf);
    INIT_KFIFO(wq_n_buf);
    INIT_KFIFO(wq_x_buf);

    log_devices[INTR_LOG_MINOR].buffer = *(struct kfifo *)&ilog_buf;
    log_devices[WQ_N_LOG_MINOR].buffer = *(struct kfifo *)&wq_n_buf;
    log_devices[WQ_X_LOG_MINOR].buffer = *(struct kfifo *)&wq_x_buf;

    /* allocate Maj/Min for gih */
    error = alloc_chrdev_region(&gih.dev_num, 0, 1, GIH_DEV);
    if (error) {
        printk(KERN_ALERT "[gih] ERROR: allcate dev num failed\n");
        return error;
    }
    gih_major = MAJOR(gih.dev_num);

    cdev_init(&gih.gih_cdev, &gih_fops);
    error = cdev_add(&gih.gih_cdev, gih.dev_num, 1);
    if (error) {
        printk(KERN_ALERT "[gih] ERROR: add cdev failed\n");
        return error;
    }

    /* allocate Maj/min for log */
    error = alloc_chrdev_region(&log_devices[INTR_LOG_MINOR].dev_num, 
        0, 3, LOG_DEV);
    if (error) {
        printk(KERN_ALERT "[log] ERROR: allcate dev num failed\n");
        return error;
    }
    log_major = MAJOR(log_devices[INTR_LOG_MINOR].dev_num);

    log_devices[WQ_N_LOG_MINOR].dev_num = MKDEV(log_major, WQ_N_LOG_MINOR);
    log_devices[WQ_X_LOG_MINOR].dev_num = MKDEV(log_major, WQ_X_LOG_MINOR);

    cdev_init(&gih.log_cdev, &log_fops);
    error = cdev_add(&gih.log_cdev, log_devices[INTR_LOG_MINOR].dev_num, 3);
    if (error) {
        printk(KERN_ALERT "[log] ERROR: add cdev failed\n");
        return error;
    }

    mutex_init(&gih.dev_open);
    mutex_init(&gih.wrt_lock);
    mutex_init(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_X_LOG_MINOR].dev_open);

    return error;

}

static void __exit gih_exit(void) {

    unregister_chrdev_region(gih.dev_num, 1);
    unregister_chrdev_region(log_devices[INTR_LOG_MINOR].dev_num, 3);

    cdev_del(&gih.gih_cdev);
    cdev_del(&gih.log_cdev);

    mutex_destroy(&gih.dev_open);
    mutex_destroy(&gih.wrt_lock);
    mutex_destroy(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_X_LOG_MINOR].dev_open);

}

module_init(gih_init);
module_exit(gih_exit);
