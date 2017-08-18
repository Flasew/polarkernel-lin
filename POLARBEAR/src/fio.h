/*
 * Filename: fio.h
 * Author: Alex Zahn (modified by Weiyang Wang)
 * Description: routines that allows in-kernel file I/O. Although it's not 
 *              recommended to do so, this is the fastest way to guarantee
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
 * Function name: file_open
 * 
 * Function prototype:
 *     static struct file * file_open(const char * path, int flags, int rights);
 *     
 * Description: 
 *     Opens a file in kernel by its path. Wraps the build-in filp_open() call.
 *     
 * Arguments:
 *     @path: path of the file. Please always use absolute path. 
 *     @flags: opening flag for the file, see <linux/fs.h> for more details.
 *     @rights: opening rights for the path, see <linux/fs.h> for more details.
 *     
 * Side Effects:
 *     Specified file is opened.
 *     
 * Error Condition: 
 *     When filp_open() returns error.
 *     
 * Return: 
 *     Pointer to opened file on success, NULL otherwise.
 */
static struct file * file_open(const char * path, int flags, int rights) {
    struct file * filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());

    printk(KERN_ALERT "[fio] Before filp open...\n");
    filp = filp_open(path, flags, rights);
    printk(KERN_ALERT "[fio] After filp open...\n");

    set_fs(oldfs);

    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        printk(KERN_ALERT "[fio] FILE OPENING FAILED, err code %d\n", err);
        return NULL;
    }
    return filp;
}

/*
 * Function name: file_close
 * 
 * Function prototype:
 *     static inline void file_close(struct file * filp);
 *     
 * Description: 
 *     Close a opened file. Wrapper function of the built-in filp_close() call.
 *     
 * Arguments:
 *     @filp: pointer to the file to be closed
 *     
 * Side Effects:
 *     On success, the opened file is closed.
 *     
 * Error Condition: 
 *     When filp_close() produce an error.
 *     
 * Return: 
 *     None.
 */
static inline void file_close(struct file * filp) {
    filp_close(filp, NULL);
}

/*
 * Function name: file_write
 * 
 * Function prototype:
 *     static int file_write(struct file * filp, 
 *                           unsigned char * data, 
 *                           size_t size)
 *     
 * Description: 
 *     Write @size amount of @data into the file specified by @filp. Mostly a 
 *     wrapper of vfs_write(), but does some trick to allow in-kernel writing.
 *     
 * Arguments:
 *     @filp: pointer to file to be written to 
 *     @data: data to be written to the file
 *     @size: amount of data to write
 *     
 * Side Effects:
 *     On success, @size amount of data is written into the file.
 *     
 * Error Condition: 
 *     When vfs_write() returns error.
 *     
 * Return: 
 *     Number of bytes written on success, -ERRORCODE otherwise.
 */
static int file_write(struct file * filp, 
                      unsigned char * data, 
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
 * Function name: file_write_kfifo
 * 
 * Function prototype:
 *     static inline int file_write_kfifo(struct file * filp, 
 *                                        struct kfifo * kfifo_buf,  
 *                                        size_t size)
 *     
 * Description: 
 *     Write @size amount of data from @kfifo_buf to @filp. Creates a buffer to
 *     hold data from @kfifo_buf, than calls file_write() to finish the writing.
 *     
 * Arguments:
 *     @filp: pointer to file to be written to 
 *     @kfifo_buf: pointer to a kfifo struct that holds data.
 *     @size: amount of data to write
 *     
 * Side Effects:
 *     On success, @size amount of data is written into the file.
 *     
 * Error Condition: 
 *     This function does not check if @kfifo_buf even has @size amount of 
 *     data in it, therefore does the checking before calling. 
 *     Error when file_write() (vfs_write()) returns error.
 *     
 * Return: 
 *     Returns the result of file_write(filp, data, size)
 */
static inline int file_write_kfifo(struct file * filp, 
                                   struct kfifo * kfifo_buf,  
                                   size_t size) {
    size_t put;
    unsigned char data[size];
    
    for (put = 0; put < size; put++)
        kfifo_get(kfifo_buf, &data[put]);

    return file_write(filp, data, size);
}

/*
 * Function name: file_sync
 * 
 * Function prototype:
 *     static inline int file_sync(struct file * filp);
 *     
 * Description: 
 *     Sync the file specified by @filp. Wrapper function of vfs_fsync().
 *     
 * Arguments:
 *     @filp: pointer to the file to be synchronized.
 *     
 * Side Effects:
 *     On success, synchronize the file.
 *     
 * Error Condition: 
 *     Currently this function does no error checking. I might look into the
 *     source code of vfs_fsync() and add that. 
 *     
 * Return: 
 *     0
 */
static inline int file_sync(struct file * filp) {
    vfs_fsync(filp, 0);
    return 0;
}

#endif
