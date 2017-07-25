#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/cdev.h> 
#include <linux/fs.h>
#include <linux/kfifo.h>

#include <asm/uaccess.h>
#include <asm/io.h>


DEFINE_KFIFO(echo_buf, unsigned char, 1024);

static int echo_open(struct inode *, struct file *);
static int echo_close(struct inode *, struct file *);
static ssize_t echo_read(struct file *, char *, size_t, loff_t *); 
static ssize_t echo_write(struct file *, const char __user *, size_t, loff_t *);

static struct file_operations echo_fops = {
    .owner   = THIS_MODULE,
    .open    = echo_open,
    .write   = echo_write,
    .read    = echo_read,
    .release = echo_close
};

typedef struct echo_dev {
    size_t message_length;
    dev_t dev;
    struct kfifo buffer;
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
    int cped;
    int last;
    size_t diff;

    diff = echo.message_length - *offset;
    length = min(len, diff);
    kfifo_to_user(&echo.buffer, buffer, length, &cped);
    if (cped != length) {
        printk(KERN_ALERT "partial reading from echo\n");
        *offset += cped; 
        return cped;
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
    int cped;
    size_t max = 1023;

    *offset = 0;
    length = min(len, max);
    printk(KERN_ALERT "Write length %d\n",length);
    kfifo_from_user(&echo.buffer, buffer, length, &cped);
    if (length != cped) {
        printk(KERN_ALERT "part writing to echo\n");
        return cped;
    }

    printk(KERN_ALERT "by return... %d\n",length);
    kfifo_put(&echo.buffer, '\0');
    echo.message_length = length;
    return length;
}

static int __init echo_init(void) {

    int error = 0;
    static struct cdev cdev;

    echo.cdev = &cdev;
    echo.buffer = echo_buf;

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

    printk(KERN_ALERT "Echo driver unloaded\n");

}

module_init(echo_init);
module_exit(echo_exit);
