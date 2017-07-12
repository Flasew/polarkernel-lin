#include <linux/init.h>
#include <linux/module.h>
#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

MODULE_LICENSE("Dual BSD/GPL");
static int hello_init(void)
{
    free_irq(1, NULL);
    printk(KERN_ALERT "Hello, world\n");
    return 0; 
}
static void hello_exit(void)
{
    printk(KERN_ALERT "Goodbye, cruel world\n");
}
module_init(hello_init);
module_exit(hello_exit);
