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

#include <asm/irq.h>
#include <asm/io.h>

#include "gih.h"
#include "fio.h"

MODULE_LICENSE("Dual BSD/GPL");

/* gih device */
static int gih_open(struct inode *, struct file *);
static int gih_close(struct inode *, struct file *);
static int 
    gih_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static ssize_t gih_write(struct file *, const char __user *, size_t, loff_t *);
static irqreturn_t gih_intr(int, void *);
static void gih_do_work(void *);

struct file_operations gih_fops = {
    .owner   = THIS_MODULE,
    .write   = gih_write,
    .ioctl   = gih_ioctl, 
    .open    = gih_open,
    .release = gih_close,
};

gih_dev gih = 0;                    /* gih device */

/* log devices */
static int log_open(struct inode *, struct file *);
static int log_close(struct inode *, struct file *);
static ssize_t log_read(struct file *, char __user *, size_t, loff_t *);

struct file_operations log_fops = {
    .owner   = THIS_MODULE,
    .read    = log_read,
    .open    = log_read,
    .release = log_close,
};

log_dev log_devices[3] = { 0 };     /* all the logging device, can be accessed
                                       by their minor number */

INIT_KFIFO(ilog_buf);
INIT_KFIFO(wq_n_buf);
INIT_KFIFO(wq_x_buf);

log_devices[INTR_LOG_MINOR].buffer = ilog_buf;
log_devices[WQ_N_LOG_MINOR].buffer = wq_n_buf;
log_devices[WQ_X_LOG_MINOR].buffer = wq_x_buf;

static int 
gih_open(struct inode * inode, struct file * filp) 
{
    int error = 0;
    /* if not configurated, it's first time, conf. from ioctl needed */
    if (!gih.configurated) {
        printk(KERN_ALERT "[gio] configuring with ioctl...\n");
        /* dump the rest of the work to ioctl */
    }

    else {
        error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
            gih.irq_name, (void*)gih_intr);
        if (error < 0) {
            printk(KERN_ALERT "IRQ REQUEST ERROR: %d\n", err);
            return error;
        }
        gih.data_wait = 0;
        gih.irq_wq = create_singlethread_workqueue(IRQ_WQ_NAME);
        gih.dest_filp = open_file(gih.path);
        kfifo_reset(gih.data_buf);
        INIT_WORK(gih.work, gih_do_work, NULL);
    }
    return error;
}

static int 
gih_release(struct inode * inode, struct file * filp) 
{
    int error = 0;
    /* if not configurated, it's first time, after ioctl */
    if (!gih.configurated) {
        printk(KERN_ALERT "[gio] configuration finished...\n");
        gih.configurated = TRUE;
        return 0;
    }
    else {
        free_irq(gih.irq, (void*)gih_intr);
        flush_workqueue(gih.irq_wq);
        /* this would result as dumping all unsent data, skipping the intr */
        error = file_write(gih.dest_filp, gih.buffer, gih.data_wait);
        if (error != data_wait) {
            if (error < 0) {
                printk(KERN_ALERT "ERROR writng the rest of data\n");
                return error;
            } 
            else {
                printk(KERN_ALERT 
                    "WARNING: data lose occured, %d bytes lost\n", 
                    gih.data_wait - error);
            }
        } 
        else {
            error = 0;
        }
        file_close(gih.dest_filp);
        gih.dest_filp = NULL;
    }
    return error;
}

static ssize_t 
gih_write(struct file * filp, 
          char __user * buffer, 
          size_t len, 
          loff_t * offset) 
{
    unsigned char local_buf = 0;
    int i;
    int error = 0;

    for (i = 0; i < len; i++) {
        error = copy_from_user(&local_buf, buffer++, 1);
        if (error < 0) {
            printk(KERN_ALERT "ERROR writng to fifo buffer\n");
            return error;
        } 
        kfifo_put(gih.buffer, local_buf);
    }
    return i;
}

static irqreturn_t gih_intr(int irq, void* dev_id) {
    
}


