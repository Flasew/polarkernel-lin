#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/cdev.h> 
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#define BUF_SIZE 512

#define ECHO_IOC 'E'
#define ECHO_IOC_SET_BUF_SZ     _IOW(ECHO_IOC, 1, int)
#define ECHO_IOC_CLR_BUF        _IO(ECHO_IOC, 2)

static int echo_open(struct inode *, struct file *);
static int echo_close(struct inode *, struct file *);
static long echo_ioctl(struct file *, unsigned int, unsigned long);
static ssize_t echo_read(struct file *, char *, size_t, loff_t *); 
static ssize_t echo_write(struct file *, const char __user *, size_t, loff_t *);

static int resize_buffer(size_t);

static struct file_operations echo_fops = {
    .owner   = THIS_MODULE,
    .open    = echo_open,
    .write   = echo_write,
    .read    = echo_read,
    .unlocked_ioctl   = echo_ioctl, 
    .release = echo_close
};

typedef struct echo_dev {
    size_t buffer_size;
    size_t message_length;
    dev_t dev;
    char * buffer;
    struct cdev * cdev;
} echo_dev;

static echo_dev echo;

static int echo_open(struct inode * inode, struct file * filp) {

    printk(KERN_ALERT "[echo] Opening device...\n");
    return 0;
}

static int echo_close(struct inode * inode, struct file * filp) {

    printk(KERN_ALERT "[echo] Closing echo device...\n");
    return 0;
}

static ssize_t echo_read(struct file * filp, 
                         char * buffer, 
                         size_t len, 
                         loff_t * offset) {

    int length;
    int uncped;
    int last;
    size_t diff;

    diff = echo.message_length - *offset;
    length = min(len, diff);
    uncped = copy_to_user(buffer, echo.buffer, length);
    if (uncped != 0) {
        printk(KERN_ALERT "partial reading from echo\n");
        *offset += length - uncped; 
        return length - uncped;
    }
    else { 
        *offset += length;
        last = min(len, echo.message_length);
        buffer[last] = '\0';
        printk(KERN_ALERT "length = %d\n", length);
        return length;
    }
}

static ssize_t echo_write(struct file * filp, 
                          const char __user * buffer, 
                          size_t len, 
                          loff_t * offset) {

    int length;
    int uncped;

    *offset = 0;
    length = min(len, echo.buffer_size - 1);
    printk(KERN_ALERT "Write length %d\n",length);
    uncped = copy_from_user(echo.buffer, buffer, length);
    if (uncped != 0) {
        printk(KERN_ALERT "ERROR writing to echo\n");
        return length - uncped;
    }

    printk(KERN_ALERT "by return... %d\n",length);
    echo.buffer[length] = '\0';
    echo.message_length = length;
    return length;
}

static long echo_ioctl(struct file * filep, 
                       unsigned int cmd, 
                       unsigned long arg) {

    int error = 0;

    switch (cmd) {

        case ECHO_IOC_SET_BUF_SZ:
            printk(KERN_ALERT "Resizing bufsiz to %lu...\n", arg);
            if (arg >= 128 && arg <= 8192) {
                error = resize_buffer(arg);
                if (error != 0) {
                    error = -ENOMEM;
                    printk(KERN_ALERT "ERROR changing buffer size:"
                            " not enough memory\n");
                }
            } 
            else {
                printk(KERN_ALERT "ERROR changing buffer size:"
                    " invalid argument\n");
                error = -EINVAL;
            }
            break;

        case ECHO_IOC_CLR_BUF:
            memset(echo.buffer, 0, echo.buffer_size);
            echo.message_length = 0;
            break;

        default: 
            error = -EINVAL;
            break;
    }
    return error;
}

static int resize_buffer(size_t new_size) {

    char * temp;
    temp = krealloc(echo.buffer, new_size, GFP_KERNEL);
    if (!echo.buffer) {
        printk(KERN_ALERT "ERROR: reallcate buffer failed\n");
        return -ENOMEM;
    }
    echo.buffer = temp;
    return 0;
}


static int __init echo_init(void) {

    int error = 0;
    static struct cdev cdev;

    echo.cdev = &cdev;

    echo.buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
    if(!echo.buffer) {
        printk(KERN_ALERT "ERROR: allcate buffer failed\n");
        return -ENOMEM;
    }

    echo.buffer_size = BUF_SIZE;

    error = alloc_chrdev_region(&echo.dev, 0, 1, "echo");
    if (error) {
        printk(KERN_ALERT "ERROR: allcate dev num failed\n");
        return error;
    }

    cdev_init(echo.cdev, &echo_fops);
    error = cdev_add(echo.cdev, echo.dev, 1);
    if (error) {
        printk(KERN_ALERT "ERROR: add cdev failed\n");
        return error;
    }

    printk(KERN_ALERT "Echo driver loaded, Maj %d: Min %d\n", 
            MAJOR(echo.dev), MINOR(echo.dev));
    return error;

}

static void __exit echo_exit(void) {

    unregister_chrdev_region(echo.dev, 1);
    cdev_del(echo.cdev);

    kfree(echo.buffer);
    printk(KERN_ALERT "Echo driver unloaded\n");

}

module_init(echo_init);
module_exit(echo_exit);
