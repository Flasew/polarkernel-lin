/*
 * Filename: gih.h
 * Author: Weiyang Wang
 * Description: header file for the generic interrupt handler kernel module. 
 *              Declares several structures, functions, and constants that will
 *              be used in the module.
 * Date: Jul 20, 2017
 */

#ifndef _GIH_H
#define _GIH_H

#define TRUE 1
#define FALSE 0

/* device names */
#define HANDLER_DEV "gih"           /* device that accepts user input */
#define INTR_LOG    "ilog"          /* logging device for interrupt happen */
#define WQ_ENTR_LOG "wqlogn"        /* logging device for entering work queue */
#define WQ_EXIT_LOG "wqlogx"        /* logging device for exiting work queue */

/* minor number for all logging devices */
#define INTR_LOG_MINOR 0
#define WQ_N_LOG_MINOR 1
#define WQ_X_LOG_MINOR 2

/* gih ioctl */
/* structure for configurating the gih device, passed in by GIH */
struct gih_conf {
    int irq;                    /* irq number */
    size_t write_size           /* how much to write each time */
    char * dest;                /* destination path of file */
};

#define GIH_IOC 'G'
#define GIH_IOC_CONFIG     _IOW(GIH_IOC, 1, struct gih_conf *) /* conf. */
#define GIH_IOC_SET_WRT_SZ _IOW(GIH_IOC, 2, int) /* change write size */

/* individual log, contains a timespec and a irq indntifier */
struct log {
    ssize_t byte_sent;              /* number of bytes sent this time
                                       only set by wqlogx device */
    unsigned long irq_id;           /* irq identifier */
    struct timeval time;            /* time of the log */
};

/* FIFO buffer for logging devices */
#define LOG_FIFO_SZ 4096                        /* buffer size of FIFO */
DECLARE_KFIFO(ilog_buf, struct log, FIFO_SZ);    
DECLARE_KFIFO(wq_n_buf, struct log, FIFO_SZ);
DECLARE_KFIFO(wq_x_buf, struct log, FIFO_SZ);

/* log device structure */
typedef struct log_dev {
    unsigned long irq_id;           /* total number of irq caught,
                                       counted differently for workqueue's and
                                       intr's */
    struct kfifo buffer;            /* FIFO buffer */
    struct cdev cdev;               /* char device */
} log_dev;

/* gih device structure */
#define DATA_FIFO_SZ 65536
DECLARE_KFIFO(data_buf, unsigned char, DATA_FIFO_SZ);

#define IRQ_NAME "gih irq handler"
#define IRQ_WQ_NAME "irq work queue"

typedef struct gih_dev {
    bool configured;                        /* device conf status */
    int irq;                                /* irq line to be catched */
    size_t data_wait;                       /* number of data on wait */
    size_t write_size;                      /* how much to write each time */
    const char * path;                      /* destination file path */
    struct workqueue_struct * irq_wq;       /* work queue */
    struct work_struct * work;              /* work to be put in the queue */
    struct file * dest_filp;                /* destination file pointer */
    struct kfifo data_buf;                  /* buffer of data */
    struct cdev cdev;                       /* char device */
} gih_dev;

#endif
