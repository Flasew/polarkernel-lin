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

MODULE_LICENSE("Dual BSD/GPL");

#define TRUE 1
#define FALSE 0

#define DEBUG 1

/* device names */
#define GIH_DEV     "gih"           /* device that accepts user input */
#define LOG_DEV     "gihlog"        /* logging device for interrupt happen */

/* minor number for all logging devices */
#define INTR_LOG_MINOR 0
#define WQ_N_LOG_MINOR 1
#define WQ_X_LOG_MINOR 2

/* gih ioctl */
#define GIH_IOC 'G'
/*
 * ioctl operations are for gih configuration.
 * Current implementation only allows configuration on driver load 
 * i.e. can't change it at run time
 *
 * This could be changed later by manipulating open and close functions,
 * (It's safer, though, to keep it in this way and modify the userland app.)
 */
#define GIH_IOC_CONFIG_IRQ      _IOW(GIH_IOC, 1, int) 
#define GIH_IOC_CONFIG_SLEEP_T  _IOW(GIH_IOC, 2, unsigned int) 
#define GIH_IOC_CONFIG_WRT_SZ   _IOW(GIH_IOC, 3, size_t) 
#define GIH_IOC_CONFIG_PATH     _IOW(GIH_IOC, 4, const char *)
#define GIH_IOC_CONFIG_FINISH   _IO (GIH_IOC, 5)

/* individual log, contains a timespec and a irq indntifier */
struct log {
    ssize_t byte_sent;              /* number of bytes sent this time
                                       only set by wqlogx device */
    unsigned long irq_count;        /* irq identifier */
    struct timeval time;            /* time of the log */
};

/* FIFO buffer for logging devices */
#define LOG_FIFO_SZ 4096                        /* buffer size of FIFO */
#define LOG_STR_BUF_SZ 256                      /* max len for log string */
DEFINE_KFIFO(ilog_buf, struct log, LOG_FIFO_SZ);    
DEFINE_KFIFO(wq_n_buf, struct log, LOG_FIFO_SZ);
DEFINE_KFIFO(wq_x_buf, struct log, LOG_FIFO_SZ);

/* log device structure */
typedef struct log_dev {
    unsigned long irq_count;        /* total number of irq caught; in N & X
                                       devices they reperesent number of 
                                       not missed irq */
    dev_t dev_num;                  /* device number */
    struct mutex dev_open;          /* device can only open once a time*/
    struct kfifo buffer;            /* FIFO buffer */
} log_dev;

/* gih device structure */
#define DATA_FIFO_SZ (1<<20)        /* 1MB */
DECLARE_KFIFO(data_buf, unsigned char, DATA_FIFO_SZ);

#define IRQ_NAME "gih irq handler"
#define IRQ_WQ_NAME "irq work queue"
#define PATH_MAX_LEN 128            /* Just a file name... should be enough */

typedef struct gih_dev {
    int irq;                                /* irq line to be catched */
    unsigned int sleep_msec;                /* time to sleep */
    size_t write_size;                      /* how much to write each time */
    bool configured;                        /* device conf status */
    atomic64_t data_wait;                   /* number of data on wait */
    dev_t dev_num;                          /* device number */
    struct workqueue_struct * irq_wq;       /* work queue */
    struct file * dest_filp;                /* destination file pointer */
    struct work_struct work;                /* work to be put in the queue */
    struct mutex dev_open;                  /* dev can only be opening once */
    struct mutex wrt_lock;                  /* mutex to protect write to file */
    struct cdev gih_cdev;                   /* gih char device */
    struct cdev log_cdev;                   /* log char device */
    struct kfifo data_buf;                  /* buffer of data */
    char path[PATH_MAX_LEN];                /* destination file path */
} gih_dev;

#endif
