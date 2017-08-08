/*
 * Filename: gih.c
 * Author: Weiyang Wang
 * Description: Body of the generic interrupt handler module.
 *              On load, this module will create four devices under /dev, 
 *              each for different purposes. User can write to the gih device
 *              and allow their data to be delayed upon interrupt happens. 
 *              Interrupts to catch is specified by the irq number.
 *              See README for more details.
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
 * Function name: gih_open
 * 
 * Function prototype:
 *     static int gih_open(struct inode * inode, struct file * filp)； 
 *     
 * Description: 
 *     Open the gih device. The gih device only catches interrupts while being
 *     opened. The open function sets up several fields of the gih_device 
 *     structure; 
 *     In the first opening, the open function expects user to configure the
 *     device by the ioctl methods therefore will not open the destination file
 *     not set up irq. 
 *
 *     NEW: now open call will not register irq not open file, ioctl's 
 *     start will do it. 
 *
 *     NEW in kmalloc version: works are allocated and initialized dynamically.
 *     
 * Arguments:
 *     @inode: inode pointer of the gih char device 
 *     @filp:  file pointer of the gih char device 
 *     
 * Side Effects:
 *     On all opening, sets up the data_wait, irq_wq, data_buf and work fields 
 *     of the gih device
 *     On non-initial opening, sets up the irq line and opens the destination 
 *     file. Locks the dev_open mutex.
 *     
 * Error Condition: 
 *     Opening for more than once will return -EBUSY.
 *     Opening with invalid fields in gih will cause undefined behaviors,
 *     probably something serious.
 *     
 * Return: 
 *     0 on success, -ERRORCODE on failure.
 */
static int gih_open(struct inode * inode, struct file * filp) {

    /* lock the gih device, it can only be opened once */
    if (!mutex_trylock(&gih.dev_open)) {return -EBUSY;}
    
    printk(KERN_ALERT "[gih] Opening gih device...\n");

    /* set up necessary fields */
    atomic_set(&gih.data_wait, 0);
    gih.irq_wq = create_workqueue(IRQ_WQ_NAME);
    kfifo_reset(&gih.data_buf);
    // INIT_WORK(&gih.work, gih_do_work);

    printk(KERN_ALERT "[gih] Remember to start the device with ioctl after "
        "configuration.\n");

    /* XXX: this part is moved to ioctl's GIH_IOC_CONFIG_START */

    // /* otherwise, set up the dest. file and irq */
    // else {
    //     error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
    //         IRQ_NAME, (void*)&gih);
    //     if (error < 0) {
    //         printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", error);
    //         return error;
    //     }
    //
    //     gih.dest_filp = file_open(gih.path, O_WRONLY, S_IRWXUGO);
    // }
    // 
    return 0;
}

/*
 * Function name: gih_close
 * 
 * Function prototype:
 *     static int gih_close(struct inode * inode, struct file * filp);
 *     
 * Description: 
 *     Close the gih device. If the device is running (i.e. setup is true),
 *     gih_close() will release the irq line, close the dest. file, and 
 *     flushes/destroys the workqueue. If there're still data left in the 
 *     gih device (data_wait != 0), depending on keep_missed, gih_close() will
 *     either discard all the data or dump them all at once
 *     
 * Arguments:
 *     @inode: inode pointer of the gih char device 
 *     @filp:  file pointer of the gih char device 
 *     
 * Side Effects:
 *     Frees the registered irq, flushes then destroys the workqueue, and
 *     write all the data left into the destination file and close it
 *     if gih is running or discard them. Print a message if it's not running.
 *     Unlock the opening lock.
 *     
 * Error Condition: 
 *     The "dump all data left" approach currently is not safe if the dest. file
 *     cannot accept huge amount of data.
 *     
 * Return: 
 *     0 on success, -ERRORCODE on failure
 */
static int gih_close(struct inode * inode, struct file * filp) {

    int copied = 0;
    size_t dwait;

    printk(KERN_ALERT "[gih] Releasing gih device...\n");

    /* if the device is not functioning, print the necessary message */
    if (!gih.setup) {
        printk(KERN_ALERT "[gih] Device hasn't been setup.\n");
        destroy_workqueue(gih.irq_wq);
        mutex_unlock(&gih.dev_open);
        return 0;
    }

    /* otherwise, release whatever should be released */
    if (gih.setup) {
        free_irq(gih.irq, (void*)&gih);
        flush_workqueue(gih.irq_wq);
        gih.setup = FALSE;      
    }
    destroy_workqueue(gih.irq_wq);


    mutex_lock(&gih.wrt_lock);

    /* if we should remove all missed data, reset kfifo */
    if (!gih.keep_missed) {
        kfifo_reset(&gih.data_buf);
        atomic_set(&gih.data_wait, 0);
    }

    else {
        /* this would result as dumping all unsent data, skipping the intr */

        dwait = atomic_read(&gih.data_wait);
        copied = file_write_kfifo(gih.dest_filp, &gih.data_buf, dwait);

        if  (copied < 0) {
            printk(KERN_ALERT "[gih] ERROR writing the rest of data\n");
        } 

        else if (copied != dwait) {
            printk(KERN_ALERT 
                "[gih] WARNING: data lose occurred, %zu bytes lost\n", 
                dwait - copied);
            copied = 0;
        }
    }

    mutex_unlock(&gih.wrt_lock);

    file_close(gih.dest_filp);
    gih.dest_filp = NULL;

    mutex_unlock(&gih.dev_open);
    return copied;
}

/*
 * Function name: gih_write
 * 
 * Function prototype:
 *     static ssize_t gih_write(struct file * filp, 
 *                              const char __user * buffer, 
 *                              size_t len, 
 *                              loff_t * offset)
 *     
 * Description: 
 *     Write data to the gih device. gih will hold these data until an interrupt
 *     occurs, and send the specified amount of data to the destination file 
 *     after a specified delay time. 
 *     
 * Arguments:
 *     @filp:   file pointer of the gih char device 
 *     @buffer: incoming data buffer from user space
 *     @len   : length of the incoming data
 *     @offset: offset into the gih device
 *     
 * Side Effects:
 *     Locks the writing lock on buffer while executing.
 *     Data will be copied to the data_buf in gih_device (implemented by kfifo).
 *     
 * Error Condition: 
 *     If kfifo is full / have less space then len, only part of the incoming
 *     data will be accepted into the buffer.
 *     
 * Return: 
 *     number of bytes copied to data_buf on success,
 *     -ERRORCODE on failure.
 */
static ssize_t gih_write(struct file * filp, 
                         const char __user * buffer, 
                         size_t len, 
                         loff_t * offset) {

    int copied;
    size_t length;
    size_t avail;

    if (DEBUG) printk(KERN_ALERT "[gih] Entering write function...\n");

    mutex_lock(&gih.wrt_lock);

    /* if we should remove all missed data, reset kfifo */
    if (!gih.keep_missed) {
        kfifo_reset(&gih.data_buf);
        atomic_set(&gih.data_wait, 0);
    }

    /* check how much space is still left */
    if ((avail = kfifo_avail(&gih.data_buf)) < len - 1) 
        printk(KERN_ALERT "[gih] WARNING: gih buffer is full, "
            "%zu byte not written in this call.\n", len - avail);

    length = min(len, avail);
    
    kfifo_from_user(&gih.data_buf, buffer, length, &copied);

    /* TODO: is atomic really more efficient here??? */
    *offset = atomic_add_return(copied, &gih.data_wait);

    mutex_unlock(&gih.wrt_lock);

    if (DEBUG) {
        printk(KERN_ALERT "[gih] %d bytes written to gih.\n", copied);
        printk(KERN_ALERT "[gih] data_buf kfifo length is %d", 
            kfifo_len(&gih.data_buf));
        printk(KERN_ALERT "[gih] data_wait is %lld", *offset);
    }

    return copied;
}

/*
 * Function name: gih_ioctl 
 * 
 * Function prototype:
 *     static long gih_ioctl(struct file * filp, 
 *                           unsigned int cmd, 
 *                           unsigned long arg)
 *     
 * Description: 
 *     All ioctl commands are used to configure the gih device on its first 
 *     opening. There're four fields that user NEEDS TO specify: 
 *         -irq number to be registered
 *         -delay time before data is send out to destination file
 *         -amount of data to send out upon receive each interrupt
 *         -path of the destination file
 *     AFTER all these fields are set, call the last GIH_IOC_CONFIG_START case
 *     to finish configuration and start the gih device. GIH_IOC_CONFIG_START
 *     will register irq and open the destination file, which the gih_open() 
 *     function will not do on first opening.
 *     If wanted to reconfigure the device, call GIH_IOC_CONFIG_STOP to unset
 *     the device.
 *     
 * Arguments:
 *     @filp: file pointer of the gih char device
 *     @cmd:  ioctl command to be performed
 *     @arg:  argument passed to a specified ioctl (might be a pointer)
 *     
 * Side Effects:
 *     For each entry, set the specified field to arg on success.
 *     Starts the gih device if is GIH_IOC_CONFIG_START,
 *     Stpps the device if if is GIH_IOC_CONFIG_STOP.
 *     
 * Error Condition: 
 *     Trying to configure the device while it's running will result in -EBUSY.
 *     Invalid arguments to ioctl will result in -EINVAL.
 *     Specific errors will result in its -ERRORCODE.
 *
 * Return: 
 *     0 on success, -ERRORCODE on failure.
 */
static long gih_ioctl(struct file * filp, 
                      unsigned int cmd, 
                      unsigned long arg) {

    int length;
    int error = 0;

    switch (cmd) {

        /* irq */
        case GIH_IOC_CONFIG_IRQ:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR setting IRQ: device running.\n");
                error = -EBUSY;
            }

            else {
                if ((int)arg < 0) {
                    printk(KERN_ALERT "[gih] ERROR: IRQ "
                            "needs to be positive.\n");
                    error = -EINVAL;
                    break;
                }

                error = 0;
                gih.irq = (int)arg;

                if (DEBUG) 
                    printk(KERN_ALERT "[gih] irq configured to %d\n", gih.irq);
            }
            break;


        /* delay time in milliseconds */
        case GIH_IOC_CONFIG_DELAY_T:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR setting delay time: "
                    "device running.\n");
                error = -EBUSY;
            }

            else {
                if ((int)arg < 0) {
                    printk(KERN_ALERT "[gih] ERROR: delay time "
                            "needs to be non-negative.\n");
                    error = -EINVAL;
                    break;
                }

                error = 0;
                gih.sleep_msec = (unsigned int)arg;

                if (DEBUG) 
                    printk(KERN_ALERT "[gih] delay time configured to %u\n", 
                        gih.sleep_msec);
            }
            
            break;


        /* amount of data to send on each interrupt */
        case GIH_IOC_CONFIG_WRT_SZ:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR setting write size: "
                    "device running.\n");
                error = -EBUSY;
            }

            else {
                if ((int)arg <= 0) {
                    printk(KERN_ALERT "[gih] ERROR: writing size "
                            "needs to be positive.\n");
                    error = -EINVAL;
                    break;
                }

                error = 0;
                gih.write_size = (size_t)arg;

                if (DEBUG) 
                    printk(KERN_ALERT "[gih] write size configured to %zu\n",
                        gih.write_size);
            }
            
            break;


        /* path of the destination file */
        case GIH_IOC_CONFIG_PATH:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR setting destination path: "
                    "device running.\n");
                error = -EBUSY;
            }
            
            else {
                error = 0;
                length = strlen((const char *)arg);

                if (length > PATH_MAX_LEN - 1)
                    return -EINVAL;

                strncpy(gih.path, (const char *)arg, length);
                gih.path[length] = '\0';

                if (DEBUG) 
                    printk(KERN_ALERT "[gih] Destination path configured "
                            "to %s\n", gih.path);

            }

            break;


        /* make sure to only call this after configuration */
        case GIH_IOC_CONFIG_START:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR: device already running.\n");
                error = -EBUSY;
            }
            else {
                error = 0;

                if (DEBUG) printk(KERN_ALERT "[gih] Finishing configuration\n");

                /* set the irq */
                error = request_irq(gih.irq, gih_intr, IRQF_SHARED,
                    IRQ_NAME, (void*)&gih);

                if (error < 0) {
                    printk(KERN_ALERT "[gih] IRQ REQUEST ERROR: %d\n", error);
                    return error;
                }
            
                gih.dest_filp = file_open(gih.path, O_WRONLY, S_IRWXUGO);

                if (!gih.dest_filp) {
                    printk(KERN_ALERT "[gih] ERROR setting destination path: "
                            "file openeing failed.\n");
                    error = -EBADF;
                    break;
                }
                gih.setup = TRUE;
                printk(KERN_ALERT "[gih] Configuration finished, "
                        "device started.\n");

            }
            
            break;


        /* this allows reconfiguration. Release irq. */
        case GIH_IOC_CONFIG_STOP:
            if (!gih.setup) {
                printk(KERN_ALERT "[gih] ERROR: device is not running.\n");
                error = -EBUSY;
            }

            else {
                error = 0;
                
                free_irq(gih.irq, (void*)&gih);
                flush_workqueue(gih.irq_wq);

                file_close(gih.dest_filp);
                gih.dest_filp = NULL;

                gih.setup = FALSE;
                printk(KERN_ALERT "[gih] Device stopped running, "
                    "reconfiguration available.\n");
            }
            
            break;


        /* keep missed data or not */
        case GIH_IOC_CONFIG_MISS:
            if (gih.setup) {
                printk(KERN_ALERT "[gih] ERROR setting missed data behavior: "
                    "device running.\n");
                error = -EBUSY;
            }

            else {

                error = 0;
                gih.keep_missed = ((int)arg == 0) ? FALSE : TRUE;

                if (DEBUG) 
                    printk(KERN_ALERT "[gih] keep missed data: %d\n",
                        gih.keep_missed);
            }
            
            break;


        default:
            return -EINVAL;
    }

    return error;
}

/*
 * Function name: gih_do_work
 * 
 * Function prototype:
 *     static void gih_do_work(struct work_struct * work);
 *     
 * Description: 
 *     Work function to execute for the work queue, which does the work of 
 *     sending data that was buffered in the gih device to the destination file
 *     This function also generates two log, one on entering the wq, the other
 *     on exiting the wq, and are stored in the gihlog1 and gihlog2 device. 
 *     When debug is turned on, user can examine the performance of the gih 
 *     device from the debug output generated in this function.
 *     
 * Arguments:
 *     @work: work structure that is put on the work queue.
 *     
 * Side Effects:
 *     Output at least the specified amount of data (write_size) to the 
 *     destination file. Write two logs to the wq_n_log and wq_x_log device.
 *     
 * Error Condition: 
 *     If wrt_lock is contently locked, this function will be blocked.
 *     
 * Return: 
 *     No return value.
 */
static void gih_do_work(struct work_struct * work) {

    size_t n_out_byte;            /* number of byte to output */
    size_t out = 0;               /* number of byte actually outputted */
    struct log exit;
    struct log entry;

    if (DEBUG) printk(KERN_ALERT "[gih] Entering work queue function...\n");

    do_gettimeofday(&entry.time);

    mutex_lock(&gih.wrt_lock);

    n_out_byte = min((size_t)kfifo_len(&gih.data_buf), gih.write_size);

    usleep_range(gih.sleep_msec * 1000 - 2 * 2 * TIME_DELTA, 
            gih.sleep_msec * 1000 - TIME_DELTA);

    if (DEBUG) printk(KERN_ALERT "[gih] calling write\n");
    out = file_write_kfifo(gih.dest_filp, &gih.data_buf, n_out_byte);
    if (DEBUG) printk(KERN_ALERT "[gih] finished write\n");

    atomic_sub(out, &gih.data_wait);

    if (DEBUG) {
        printk(KERN_ALERT "[gih] %zu bytes read from gih.\n", out);
        printk(KERN_ALERT "[gih] data_buf kfifo length is %d", 
            kfifo_len(&gih.data_buf));
        printk(KERN_ALERT "[gih] data_wait is %d", atomic_read(&gih.data_wait));
    }

    file_sync(gih.dest_filp);

    mutex_unlock(&gih.wrt_lock);

    if (DEBUG) 
        printk(KERN_ALERT "[gih] %zu bytes written out to dest file.\n", out);

    entry.byte_sent = -1,
    entry.irq_count = log_devices[WQ_N_LOG_MINOR].irq_count++;
    kfifo_in(&wq_n_buf, &entry, 1);

    if (DEBUG) printk(KERN_ALERT "[log] WQN element num %u\n", 
        (unsigned int)kfifo_len(&wq_n_buf));

    exit.byte_sent = out;
    exit.irq_count = log_devices[WQ_X_LOG_MINOR].irq_count++;
    
    do_gettimeofday(&exit.time);
    kfifo_in(&wq_x_buf, &exit, 1);

    kfree(work);

    if (DEBUG) printk(KERN_ALERT "[log] WQX element num %u\n", 
        (unsigned int)kfifo_len(&wq_x_buf));

    if (DEBUG) printk(KERN_ALERT "[gih] Exiting work queue function...\n");
}

/*
 * Function name: gih_intr
 * 
 * Function prototype:
 *     static irqreturn_t gih_intr(int irq, void * data);
 *     
 * Description: 
 *     Interrupt handler of the gih device. Top half will record a log of when 
 *     interrupt had happened; bottom half will queue the work of sending output
 *     data on the workqueue.
 *     
 * Arguments:
 *     @irq
 *     @data
 *     Unused.
 *     
 * Side Effects:
 *     Write a log to the intr_log device.
 *     Allocate a work structure on the heap to pass to gih_do_work().
 *     
 * Error Condition: 
 *     Not really, if any happened there would be undefined behavior.
 *     
 * Return: 
 *     IRQ_HANDLED on success.
 */
static irqreturn_t gih_intr(int irq, void * data) {
    /* enqueue work, write log */
    struct log intr_log; 
    struct work_struct * work;    

    if (DEBUG) printk(KERN_ALERT "[gih] INTERRUPT CAUGHT.\n");

    do_gettimeofday(&intr_log.time);

    work = kmalloc(sizeof(struct work_struct), GFP_ATOMIC);
    INIT_WORK(work, gih_do_work);
    queue_work(gih.irq_wq, work);

    intr_log.byte_sent = -1; 
    intr_log.irq_count = log_devices[INTR_LOG_MINOR].irq_count++;

    kfifo_in(&ilog_buf, &intr_log, 1);

    if (DEBUG) printk(KERN_ALERT "[log] Falling out: INT element num %u\n", 
        (unsigned int)kfifo_len(&ilog_buf));

    /* perhaps also try kernel thread, given the work function in this way */

    return IRQ_HANDLED;
}


/* log device function definitions */

/*
 * Function name: log_open
 * 
 * Function prototype:
 *     static int log_open(struct inode * inode, struct file * filp);
 *     
 * Description: 
 *     Open the log device mounted at /dev, for reading logs. Retrieves the 
 *     minor number to identify which log device have been opened.
 *     
 * Arguments:
 *     @inode: inode pointer of the log char device 
 *     @filp:  file pointer of the log char device 
 *     
 * Side Effects:
 *     Locks the opening lock of the opened device;
 *     sets the private_data field of @filp to the log device structure;
 *     reset the offset into the file.
 *     
 * Error Condition: 
 *     If the device is already opened will return -EBUSY
 *     
 * Return: 
 *     0 on success, -EBUSY if the device have been opened. 
 */
static int log_open(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);

    if (!mutex_trylock(&log_devices[minor].dev_open)) {return -EBUSY;}

    filp->private_data = &log_devices[minor];
    filp->f_pos = 0;

    if (DEBUG) printk(KERN_ALERT "[log] Log device %u opened\n", minor);

    return 0;
}

/*
 * Function name: log_close
 * 
 * Function prototype:
 *     static int log_close(struct inode * inode, struct file * filp);
 *     
 * Description: 
 *     Close the opened log device.
 *     
 * Arguments:
 *     @inode: inode pointer of the log char device 
 *     @filp:  file pointer of the log char device 
 *     
 * Side Effects:
 *     Unlocks the opening lock of the device, 
 *     clears the private_data filed of @filp.
 *     
 * Error Condition: 
 *     None, if any happens will result in undefined behavior.
 *     
 * Return: 
 *     0 on success
 */
static int log_close(struct inode * inode, struct file * filp) {

    unsigned int minor = iminor(inode);
    mutex_unlock(&log_devices[minor].dev_open);

    filp->private_data = NULL;

    if (DEBUG) printk(KERN_ALERT "[log] Log device %u released\n", minor);
    return 0;
}

/*
 * Function name: log_read
 * 
 * Function prototype:
 *     static ssize_t log_read(struct file * filp, 
 *                             char __user * buf, 
 *                             size_t len, 
 *                             loff_t * offset);
 *     
 * Description: 
 *     Read the logs stored in the log device. Since the logs are stored in 
 *     kfifo structure, reading will dequeue the stored entry. Each read will 
 *     extract all the logs stored in the device. 
 *     
 * Arguments:
 *     @filp: file pointer to the log device
 *     @buf:  output buffer to write to 
 *     @len:  length of the data to read. Should be set to the maximum possible
 *            amount of logs stored.
 *     @offset: offset into the log device, used to mark reading finish. 
 *     
 * Side Effects:
 *     Reading will clear the log entries, by design. 
 *     
 * Error Condition: 
 *     If buf is somehow not big enough, partial reading will occur, and may 
 *     result in log information disappearing. Make sure the buf (len) is big 
 *     enough for reading from log.
 *     (Recommended set this value to LOG_STR_BUF_SZ * LOG_FIFO_SZ, see gih.h)
 *     
 * Return: number of bytes outputted from log device.
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
       try to make enough read size in user-land 
       otherwise data may be truncated */

    for (finished_log = 0; 
         finished_log < amount_log && len > 0; 
         finished_log++) {

        kfifo_out(device.buffer, &log, 1);

        // if (DEBUG) {
        //     printk(KERN_ALERT "[log] out: bsent %zd, "
        //         "ict %lu, time s %ld, time ms %ld\n",
        //         *(ssize_t*)&log,
        //         *(unsigned long*)((void*)&log+sizeof(ssize_t)),
        //         *(long*)(&log+sizeof(ssize_t)+sizeof(unsigned long)),
        //         *(long*)(&log+sizeof(ssize_t)+sizeof(unsigned long)+ 
        //             sizeof(long)));
        // }

        log_len = snprintf(buf, len - 1, 
            "[%010ld.%06ld] interrupt count: %lu | write size: %zd\n", 
            log.time.tv_sec, log.time.tv_usec,
            log.irq_count, log.byte_sent);

        if (log_len < 0) return log_len;
        if (log_len >= len) break;      /* for safety... */

        len -= log_len;
        *offset += log_len;
        buf += log_len;
    }
    buf[*offset] = '\0';
    return *offset;
}

/*
 * Function name: gih_init
 * 
 * Function prototype:
 *     static int __init gih_init(void);
 *     
 * Description: 
 *     Initializer of the gih module. Sets up the char device number region,
 *     initializes the necessary char devices, and mount them under /dev.
 *     Sets up the device structure of gih and all 3 log devices.
 *     Initializes all the locks involved in the module
 *     
 * Arguments:
 *     None.
 *     
 * Side Effects:
 *     On success, the following fields are set to their correct value:
 *         In gih_dev gih: 
 *              dev_t dev_num;
 *              struct class * gih_class;               
 *              struct device * gih_device;
 *              struct mutex dev_open;          
 *              struct mutex wrt_lock;          
 *              struct cdev gih_cdev;          
 *              struct cdev log_cdev;         
 *              struct kfifo data_buf;        
 *         In each log_dev[i]:
 *              dev_t dev_num;              
 *              struct kfifo * buffer;      
 *              struct class * log_class;    
 *              struct device * log_device; 
 *              struct mutex dev_open;     
 *     
 * Error Condition: 
 *     If any allocation fails, will return the appropriate error code and
 *     terminate the program.
 *     
 * Return: 
 *     0 on success, -ERRORCODE otherwise
 */
static int __init gih_init(void) {

    int error;
    int gih_major;
    int log_major;

    /* data buffer of gih */
    INIT_KFIFO(data_buf);
    gih.data_buf = *(struct kfifo *)&data_buf;

    /* I tried to do initialize these 3 kfifos here, but the program won't 
       compile... They're initialized at the beginning of this program. */
    log_devices[INTR_LOG_MINOR].buffer = (struct kfifo *)&ilog_buf;
    log_devices[WQ_N_LOG_MINOR].buffer = (struct kfifo *)&wq_n_buf;
    log_devices[WQ_X_LOG_MINOR].buffer = (struct kfifo *)&wq_x_buf;

    /* allocate Maj/Min for gih */
    error = alloc_chrdev_region(&gih.dev_num, 0, 1, GIH_DEV);
    if (error) {
        printk(KERN_ALERT "[gih] ERROR: allocate dev num failed\n");
        return error;
    }
    gih_major = MAJOR(gih.dev_num);

    /* initialize/add the cdev of gih */
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
        printk(KERN_ALERT "[log] ERROR: allocate dev num failed\n");
        return error;
    }
    log_major = MAJOR(log_devices[INTR_LOG_MINOR].dev_num);

    log_devices[WQ_N_LOG_MINOR].dev_num = MKDEV(log_major, WQ_N_LOG_MINOR);
    log_devices[WQ_X_LOG_MINOR].dev_num = MKDEV(log_major, WQ_X_LOG_MINOR);

    /* initialize/add the cdev of gih */
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

    /* initialize the mutexs */
    mutex_init(&gih.dev_open);
    mutex_init(&gih.wrt_lock);
    mutex_init(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_init(&log_devices[WQ_X_LOG_MINOR].dev_open);

    printk(KERN_ALERT "[gih] [log] gih module loaded.\n");

    if (DEBUG) {
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
 * Function name: gih_exit
 * 
 * Function prototype:
 *     static void __exit gih_exit(void);
 *     
 * Description: 
 *     Exit function of the gih module. Destroys and deallocates whatever 
 *     needs to be. 
 *     
 * Arguments:
 *     None
 *     
 * Side Effects:
 *     All fields deallocated, allowing a safe exit of the gih module. 
 *     
 * Error Condition: 
 *     Not callable if the device is still open. This may induce unsafe behavior
 *     if the device is "dead-opened". 
 *     
 * Return: 
 *     None
 */
static void __exit gih_exit(void) {

    /* destroy the device nodes */
    device_destroy(log_devices[INTR_LOG_MINOR].log_class,
            log_devices[INTR_LOG_MINOR].dev_num);
    device_destroy(log_devices[WQ_N_LOG_MINOR].log_class,
            log_devices[WQ_N_LOG_MINOR].dev_num);
    device_destroy(log_devices[WQ_X_LOG_MINOR].log_class,
            log_devices[WQ_X_LOG_MINOR].dev_num);
    class_destroy(log_devices[INTR_LOG_MINOR].log_class);

    device_destroy(gih.gih_class, gih.dev_num);
    class_destroy(gih.gih_class);

    /* release the registered device region */
    unregister_chrdev_region(gih.dev_num, 1);
    unregister_chrdev_region(log_devices[INTR_LOG_MINOR].dev_num, 3);

    cdev_del(&gih.gih_cdev);
    cdev_del(&gih.log_cdev);

    /* destroy the mutexs */
    mutex_destroy(&gih.dev_open);
    mutex_destroy(&gih.wrt_lock);
    mutex_destroy(&log_devices[INTR_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_N_LOG_MINOR].dev_open);
    mutex_destroy(&log_devices[WQ_X_LOG_MINOR].dev_open);

    printk(KERN_ALERT "[gih] [log] gih module unloaded.\n");
}

module_init(gih_init);
module_exit(gih_exit);
