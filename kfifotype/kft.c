#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>

struct wrap {
    unsigned long aChar;
};

DEFINE_KFIFO(test, struct wrap, 512);

static int __init kft_init(void) {

    int j;
    int sz;
    struct wrap buf;

    struct wrap k = { .aChar = 'K' };
    struct wrap f = { .aChar = 'F' };
    struct wrap i = { .aChar = 'I' };
    struct wrap o = { .aChar = 'O' };

    kfifo_put(&test, k);
    kfifo_put(&test, f);
    kfifo_put(&test, i);
    kfifo_put(&test, f);
    kfifo_put(&test, o);

    sz = kfifo_len(&test);
    printk(KERN_ALERT "Size: %d\n", sz);
    
    printk(KERN_ALERT "Test result: \n");
    for (j = 0; j < sz; j++) {
        kfifo_get(&test, &buf);
        printk(KERN_ALERT "%c\n", (char)buf.aChar);
    }
   
    return 0;
}

static void __exit kft_exit(void) {
    return;
}

module_init(kft_init);
module_exit(kft_exit);
