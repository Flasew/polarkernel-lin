/*
 * Filename: fio.h
 * Author: Alex Zahn (modified by Weiyang Wang)
 * Description: routines that allows in-kernel file I/O. Although it's not 
 *              recommanded to do so, this is the fastest way to guarentee
 *              performance for this particular task.
 * Date: July 25, 2017
 * Ref: https://stackoverflow.com/questions/1184274/        \
 *      how-to-read-write-files-within-a-linux-kernel-module
 *      
 *      https://github.com/Flasew/linux3_tests/blob/master/vfs/vfs.c
 */

#ifndef _FIO_H
#define _FIO_H

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
static struct file* file_open(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());

    filp = filp_open(path, flags, rights);

    set_fs(oldfs);

    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        printk(KERN_ALERT "[fio] FILE OPENING FAILED, err code %d\n", err);
        return NULL;
    }
    return filp;
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
static inline void file_close(struct file* filp) {
    filp_close(filp, NULL);
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
static int file_write(struct file* filp, 
                      unsigned char* data, 
                      size_t size) {

    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(filp, data, size, &filp->f_pos);

    set_fs(oldfs);
    return ret;
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
static inline int file_write_kfifo(struct file* filp, 
                                   struct kfifo * kfifo_buf,  
                                   size_t size) {
    size_t put;
    unsigned char data[size];
    
    for (put = 0; put < size; put++)
        kfifo_get(kfifo_buf, &data[put]);

    return file_write(filp, data, size);
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
static inline int file_sync(struct file* filp) {
    vfs_fsync(filp, 0);
    return 0;
}

#endif
