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
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/fcntl.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "gih.h"
#include "fio.h"

/* see each function's header for more detailed documentation */

/* gih device */
static int gih_open(struct inode *, struct file *);
static int gih_close(struct inode *, struct file *);
static long gih_ioctl(struct file *, unsigned int, unsigned long);
static ssize_t gih_write(struct file *, const char __user *, size_t, loff_t *);
static irqreturn_t gih_intr(int, void *);
static void gih_do_work(struct work_struct *);

struct file_operations gih_fops = {
    .owner              = THIS_MODULE,
    .write              = gih_write,
    .unlocked_ioctl     = gih_ioctl, 
    .open               = gih_open,
    .release            = gih_close
};

static gih_dev gih = { 0 };             /* gih device */

/* log devices */
static int log_open(struct inode *, struct file *);
static int log_close(struct inode *, struct file *);
static ssize_t log_read(struct file *, char *, size_t, loff_t *);

struct file_operations log_fops = {
    .owner   = THIS_MODULE,
    .read    = log_read,
    .open    = log_open,
    .release = log_close
};

static log_dev log_devices[3] = { 0 }; /* all the logging device, can be
                                          accessed by their minor number */

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static int 
gih_open(struct inode * inode, struct file * filp) 
{
    int error = 0;

    if (!mutex_trylock(&gih.dev_open)) {return -EBUSY;}
    
    printk(KERN_ALERT "[gih] Openining gih device...\n");

    atomic_set(&gih.data_wait, 0);
    gih.irq_wq = create_workqueue(IRQ_WQ_NAME);
    kfifo_reset(&gih.data_buf);
    INIT_WORK(&gih.work, gih_do_work);

    /* if not configurated, it's first time, conf. from ioctl needed */
    if (!gih.configured) {
        printk(KERN_ALERT "[gih] configuring with ioctl...\n");
        /* dump the rest of the work to ioctl */
        return 0;
    }

    else {
        error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
            IRQ_NAME, (void*)&gih);
        if (error < 0) {
            printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", error);
            return error;
        }

        gih.dest_filp = file_open(gih.path, O_WRONLY, S_IRWXUGO);
    }
    return error;
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static int 
gih_close(struct inode * inode, struct file * filp) 
{
    int copied = 0;
    size_t dwait;

    printk(KERN_ALERT "[gih] Releasing gih device...\n");

    if (!gih.configured) {
        printk(KERN_ALERT "[gih] Device still not configured.\n");
        destroy_workqueue(gih.irq_wq);
        mutex_unlock(&gih.dev_open);
        return 0;
    }

    free_irq(gih.irq, (void*)&gih);
    flush_workqueue(gih.irq_wq);
    destroy_workqueue(gih.irq_wq);
    /* this would result as dumping all unsent data, skipping the intr */
    dwait = atomic_read(&gih.data_wait);
    copied = file_write_kfifo(gih.dest_filp, &gih.data_buf, dwait);
    if  (copied != dwait) {
        if  (copied < 0) {
            printk(KERN_ALERT "[gih] ERROR writng the rest of data\n");
            return copied;
        } 
        else {
            printk(KERN_ALERT 
                "[gih] WARNING: data lose occured, %zu bytes lost\n", 
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

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static ssize_t 
gih_write(struct file * filp, 
          const char __user * buffer, 
          size_t len, 
          loff_t * offset) {

    int copied;
    size_t length;
    size_t avail;

    if (DEBUG) printk(KERN_ALERT "[gih] Entering write function...\n");

    mutex_lock(&gih.wrt_lock);
    if ((avail = kfifo_avail(&gih.data_buf)) < len - 1) 
        printk(KERN_ALERT "[gih] Warning: gih buffer is full, "
            "%zu byte loss occured.\n", len - avail);

    length = min(len, avail);
    
    kfifo_from_user(&gih.data_buf, buffer, length, &copied);

    *offset = atomic_read(&gih.data_wait);
    mutex_unlock(&gih.wrt_lock);

    if (DEBUG) printk(KERN_ALERT "[gih] %d bytes written to gih.\n", copied);
    if (DEBUG) printk(KERN_ALERT "[gih] data_buf kfifo length is %d", 
        kfifo_len(&gih.data_buf));
    if (DEBUG) printk(KERN_ALERT "[gih] data_wait is %lld", *offset);

    return copied;
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static long gih_ioctl(struct file * filep, 
                      unsigned int cmd, 
                      unsigned long arg) {

    int length;
    int error;

    if (gih.configured) {
        return -EINVAL;
    }

    switch (cmd) {

        case GIH_IOC_CONFIG_IRQ:
            gih.irq = (int)arg;
            if (DEBUG) printk(KERN_ALERT "[gih] irq configured to %d\n", 
                gih.irq);
            break;

        case GIH_IOC_CONFIG_SLEEP_T:
            gih.sleep_msec = (unsigned int)arg;
            if (DEBUG) printk(KERN_ALERT "[gih] sleep time configured to %u\n", 
                gih.sleep_msec);
            break;

        case GIH_IOC_CONFIG_WRT_SZ:
            gih.write_size = (size_t)arg;
            if (DEBUG) printk(KERN_ALERT "[gih] write size configured to %zu\n",
                gih.write_size);
            break;

        case GIH_IOC_CONFIG_PATH:
            length = strlen((const char *)arg);
            if (length > PATH_MAX_LEN - 1)
                return -EINVAL;
            strncpy(gih.path, (const char *)arg, length);
            gih.path[length] = '\0';
            if (DEBUG) printk(KERN_ALERT "[gih] path configured to %s\n", 
                gih.path);
            gih.dest_filp = file_open(gih.path, O_WRONLY, S_IRWXUGO);
            if (DEBUG) printk(KERN_ALERT "[gih] file opened successfully: %d", 
                gih.dest_filp != NULL);
            break;

        /* make sure to only call this after configuratoin */
        case GIH_IOC_CONFIG_FINISH:

            if (DEBUG) printk(KERN_ALERT "[gih] Finishing configuration\n");
            /* set the irq */
            error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
                IRQ_NAME, (void*)&gih);
            if (error < 0) {
                printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", error);
                return error;
            }
            
            gih.configured = TRUE;
            printk(KERN_ALERT "[gih] configuration finished.\n");
            break;

        default:
            return -EINVAL;
    }
    return 0;
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static void gih_do_work(struct work_struct * work) {

    size_t n_out_byte;            /* number of byte to output */
    size_t out = 0;               /* number of byte actually outputed */
    struct log exit;
    struct log entry;


    if (DEBUG) printk(KERN_ALERT "[gih] Entering work queue function...\n");

    do_gettimeofday(&entry.time);

    if (DEBUG) printk(KERN_ALERT "[log] WQN element num %u\n", 
        (unsigned int)kfifo_len(&wq_n_buf));

    mutex_lock(&gih.wrt_lock);

    n_out_byte = min((size_t)kfifo_len(&gih.data_buf), gih.write_size);


    if (DEBUG) printk(KERN_ALERT "[gih] calling write\n");
    out = file_write_kfifo(gih.dest_filp, &gih.data_buf, n_out_byte);
    if (DEBUG) printk(KERN_ALERT "[gih] finished write\n");

    atomic_sub(out, &gih.data_wait);

    if (DEBUG) printk(KERN_ALERT "[gih] %zu bytes read from gih.\n", out);

    if (DEBUG) printk(KERN_ALERT "[gih] data_buf kfifo length is %d", 
        kfifo_len(&gih.data_buf));

    if (DEBUG) printk(KERN_ALERT "[gih] data_wait is %d", 
            atomic_read(&gih.data_wait));

    file_sync(gih.dest_filp);

    mutex_unlock(&gih.wrt_lock);

    entry.byte_sent = -1,
    entry.irq_count = log_devices[WQ_N_LOG_MINOR].irq_count++;
    kfifo_in(&wq_n_buf, &entry, 1);

    exit.byte_sent = out;
    exit.irq_count = log_devices[WQ_X_LOG_MINOR].irq_count++;

    if (DEBUG) printk(KERN_ALERT "[gih] %zu bytes written out to dest file.\n", 
                out);

    do_gettimeofday(&exit.time);
    kfifo_in(&wq_x_buf, &exit, 1);

    if (DEBUG) printk(KERN_ALERT "[log] WQX element num %u\n", 
        (unsigned int)kfifo_len(&wq_x_buf));

    if (DEBUG)
        printk(KERN_ALERT "[gih] Exiting work queue function...\n");
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static irqreturn_t gih_intr(int irq, void * data) {
    /* enque work, write log */
    int error = 0;
    struct log intr_log; 

    intr_log.byte_sent = -1; 
    intr_log.irq_count = log_devices[INTR_LOG_MINOR].irq_count++;

    do_gettimeofday(&intr_log.time);
    kfifo_in(&ilog_buf, &intr_log, 1);

    if (DEBUG) printk(KERN_ALERT "[log] INT element num %u\n", 
        (unsigned int)kfifo_len(&ilog_buf));

    /* perhaps also try kernal thread, given the work function in this way */
    //INIT_WORK(&gih.work, gih_do_work);
    error = queue_work(gih.irq_wq, &gih.work);

    if (error) 
        printk(KERN_ALERT "[gih] WARNING: interrupt missed\n");

    return IRQ_HANDLED;
}

/* log device function definitions */

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static int log_open(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);

    if (!mutex_trylock(&log_devices[minor].dev_open)) {return -EBUSY;}

    filp->private_data = &log_devices[minor];
    filp->f_pos = 0;

    if (DEBUG)
        printk(KERN_ALERT "[log] Log device %u opened\n", minor);
    return 0;
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static int log_close(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);
    mutex_unlock(&log_devices[minor].dev_open);

    filp->private_data = NULL;

    if (DEBUG)
        printk(KERN_ALERT "[log] Log device %u released\n", minor);
    return 0;
}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static ssize_t log_read(struct file * filp, 
                        char __user * buf, 
                        size_t len, 
                        loff_t * offset) {

    size_t amount_log;
    size_t finished_log; 

    size_t log_len;

    log_dev device;
    struct log log;

    if (*offset != 0) {return 0;}

    device = *(log_dev*)filp->private_data;
    amount_log = kfifo_len(device.buffer);

    if (DEBUG) printk(KERN_ALERT "[log] Reading from log device %d, "
            "with %zu entries.\n", MINOR(device.dev_num), amount_log);

    /* this function doesn't do much of checking, 
       try to make enough read size in userland 
       otherwise data may be truncated */

    for (finished_log = 0; 
         finished_log < amount_log && len > 0; 
         finished_log++) {

        kfifo_out(device.buffer, &log, 1);

        if (DEBUG) {
            printk(KERN_ALERT "[log] out: bsent %zd, "
                "ict %lu, time s %ld, time ms %ld\n",
                *(ssize_t*)&log,
                *(unsigned long*)((void*)&log+sizeof(ssize_t)),
                *(long*)((void*)&log+sizeof(ssize_t)+sizeof(unsigned long)),
                *(long*)((void*)&log+sizeof(ssize_t)+sizeof(unsigned long)+
                    sizeof(long)));
        }

        log_len = snprintf(buf, len - 1, 
            "[ %ld.%ld], Intr cnt: %lu, w.sz: %zd\n", 
            log.time.tv_sec, log.time.tv_usec,
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

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static int __init gih_init(void) {

    int error;
    int gih_major;
    int log_major;

    INIT_KFIFO(data_buf);
    gih.data_buf = *(struct kfifo *)&data_buf;

    log_devices[INTR_LOG_MINOR].buffer = (struct kfifo *)&ilog_buf;
    log_devices[WQ_N_LOG_MINOR].buffer = (struct kfifo *)&wq_n_buf;
    log_devices[WQ_X_LOG_MINOR].buffer = (struct kfifo *)&wq_x_buf;

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

    /* create device nodes */
    gih.gih_class = class_create(THIS_MODULE, GIH_DEV);
    gih.gih_device = device_create(gih.gih_class, NULL, 
        gih.dev_num, &gih, GIH_DEV);

    log_devices[INTR_LOG_MINOR].log_class = class_create(THIS_MODULE, LOG_DEV);
    log_devices[WQ_N_LOG_MINOR].log_class = 
    log_devices[WQ_X_LOG_MINOR].log_class =
        log_devices[INTR_LOG_MINOR].log_class;

    log_devices[INTR_LOG_MINOR].log_device = 
        device_create(log_devices[INTR_LOG_MINOR].log_class, 
                gih.gih_device,
                log_devices[INTR_LOG_MINOR].dev_num, 
                &log_devices[INTR_LOG_MINOR],
                LOG_DEV_FMT, INTR_LOG_MINOR);

    log_devices[WQ_N_LOG_MINOR].log_device = 
        device_create(log_devices[WQ_N_LOG_MINOR].log_class, 
                gih.gih_device,
                log_devices[WQ_N_LOG_MINOR].dev_num, 
                &log_devices[WQ_N_LOG_MINOR],
                LOG_DEV_FMT, WQ_N_LOG_MINOR);

     log_devices[WQ_X_LOG_MINOR].log_device = 
        device_create(log_devices[WQ_X_LOG_MINOR].log_class, 
                gih.gih_device,
                log_devices[WQ_X_LOG_MINOR].dev_num, 
                &log_devices[WQ_X_LOG_MINOR],
                LOG_DEV_FMT, WQ_X_LOG_MINOR);

    mutex_init(&gih.dev_open);
    mutex_init(&gih.wrt_lock);
    mutex_init(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_X_LOG_MINOR].dev_open);

    if (DEBUG) {
        printk(KERN_ALERT "[gih] [log] gih module loaded.\n");

        printk(KERN_ALERT "[gih] GIH: Major: %d, Minor: %d\n",
                gih_major, MINOR(gih.dev_num));
        printk(KERN_ALERT "[log] Intr log: Major: %d, Minor: %d\n", 
                log_major, MINOR(log_devices[INTR_LOG_MINOR].dev_num));
        printk(KERN_ALERT "[log] WQ_N log: Major: %d, Minor: %d\n", 
                log_major, MINOR(log_devices[WQ_N_LOG_MINOR].dev_num));
        printk(KERN_ALERT "[log] WQ_X log: Major: %d, Minor: %d\n", 
                log_major, MINOR(log_devices[WQ_X_LOG_MINOR].dev_num));
    }

    return error;

}

/*
 * Function name: 
 * 
 * Function prototype:
 *     
 *     
 * Description: 
 *     
 *     
 * Arguments:
 *     @
 *     
 * Side Effects:
 *     
 *     
 * Error Condition: 
 *     
 *     
 * Return: 
 *     
 */
static void __exit gih_exit(void) {

    device_destroy(log_devices[INTR_LOG_MINOR].log_class,
            log_devices[INTR_LOG_MINOR].dev_num);
    device_destroy(log_devices[WQ_N_LOG_MINOR].log_class,
            log_devices[WQ_N_LOG_MINOR].dev_num);
    device_destroy(log_devices[WQ_X_LOG_MINOR].log_class,
            log_devices[WQ_X_LOG_MINOR].dev_num);
    class_destroy(log_devices[INTR_LOG_MINOR].log_class);

    device_destroy(gih.gih_class, gih.dev_num);
    class_destroy(gih.gih_class);

    unregister_chrdev_region(gih.dev_num, 1);
    unregister_chrdev_region(log_devices[INTR_LOG_MINOR].dev_num, 3);

    cdev_del(&gih.gih_cdev);
    cdev_del(&gih.log_cdev);

    mutex_destroy(&gih.dev_open);
    mutex_destroy(&gih.wrt_lock);
    mutex_destroy(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_X_LOG_MINOR].dev_open);

    if (DEBUG)
        printk(KERN_ALERT "[gih] [log] gih module unloaded.\n");
}

module_init(gih_init);
module_exit(gih_exit);
