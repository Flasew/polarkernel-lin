#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>

MODULE_LICENSE("Dual BSD/GPL");

static irqreturn_t
detected(int irq, void* dev_id)
{
    printk(KERN_ALERT "TYPE\n");
    return IRQ_HANDLED;
}

static int 
grabKBD(void)
{
    int err;
    free_irq(1, NULL);
    err = request_irq(1, detected, IRQF_SHARED, "kbd", (void*)detected);
    if (err != 0) 
        printk(KERN_ALERT "ERROR: %d\n", err);
    return 0;
}

static void 
ungrabKBD(void)
{
    free_irq(1, NULL);
}
module_init(grabKBD);
module_exit(ungrabKBD);
