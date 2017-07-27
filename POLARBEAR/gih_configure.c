/*
 * Filename: gih_configure.h
 * Author: Weiyang Wang
 * Description: User land configuration utility for the gih device,
 *              respoinsible for calling all the ioctl routines to set the
 *              irq number, delay time, write-file path and write size.
 *
 *              The function defined in this file is supposed to be called 
 *              by the python script that will be used to configure the file
 *              (That means try not to directly use this file...)
 *
 *              Currently only supports load-time configuration (can only
 *              confifure the device once, but this can be changed by
 *              making the CONFIGURED field to be modufiable thus the device
 *              can be re-configured when closed. )
 * Date: Jul 26, 2017
 */

#include <Python.h>                 /* python module */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MOD_NAME "gih_config"
#define MOD_DOC  "gih ioctl configuration routines"

/* gih ioctl */
#define GIH_IOC 'G'

#define GIH_IOC_CONFIG_IRQ      _IOW(GIH_IOC, 1, int) 
#define GIH_IOC_CONFIG_SLEEP_T  _IOW(GIH_IOC, 2, unsigned int) 
#define GIH_IOC_CONFIG_WRT_SZ   _IOW(GIH_IOC, 3, size_t) 
#define GIH_IOC_CONFIG_PATH     _IOW(GIH_IOC, 4, const char *)
#define GIH_IOC_CONFIG_FINISH   _IO (GIH_IOC, 5)

errno = 0;

/* see the header comments for each function */
static PyObject * configure_irq     (PyObject *, PyObject *);
static PyObject * configure_sleep_t (PyObject *, PyObject *);
static PyObject * configure_wrt_sz  (PyObject *, PyObject *);
static PyObject * configure_path    (PyObject *, PyObject *);
static PyObject * configure_finish  (PyObject *, PyObject *);

/* register funcitons */
static PyMethodDef config_methods[] = {

    { "configure_irq", configure_irq, 
        METH_VARARGS, "configure irq number" },

    { "configure_sleep_t", configure_sleep_t,
        METH_VARARGS, "configure sleep time in ms" },

    { "configure_wrt_sz", configure_wrt_sz,
        METH_VARARGS, "configure write size in byte" },

    { "configure_path", configure_path, 
        METH_VARARGS, "configure path of output" },

    { "configure_finish", configure_finish, 
        METH_VARARGS, "configuration finished" },

    { NULL, NULL, 0, NULL }
};

/* initiallization. I tried to make it avaliable for both py2 and 3 here.. */
#if PY_MAJOR_VERSION >= 3
  static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    MOD_NAME,           /* m_name */
    MOD_DOC,            /* m_doc */
    -1,                 /* m_size */
    config_methods,     /* m_methods */
    NULL,               /* m_reload */
    NULL,               /* m_traverse */
    NULL,               /* m_clear */
    NULL,               /* m_free */
  };
#endif /* PY_MAJOR_VERSION >= 3 */

static PyObject * moduleinit(void) {

    PyObject *m;

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3(MOD_NAME, config_methods, MOD_DOC);
#endif

    return (m == NULL) ? m : NULL;
}

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initgih_config(void) {
    moduleinit();
}
#else
PyMODINIT_FUNC PyInit_gih_config(void) {
    return moduleinit();
}
#endif /* PY_MAJOR_VERSION >= 3 */

/*
 * Function name: configure_irq
 * 
 * Function prototype:
 *     static PyObject * configure_irq(PyObject * self, PyObject * args);
 *     
 * Description: 
 *     Configure the irq number of the gih device. The device will be catching
 *     all interrupts from the specified irq line as long as the device is 
 *     opened. 
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wrapps two integer value
 *            arg1: int fd - file descriptor
 *            arg2: int irq - irq number
 *     
 * Side Effects:
 *     On success, the irq number associated with the gih device is set to 
 *     the number specified by args.
 *     
 * Error Condition: 
 *     Argument is invalid if isn't a positive integer. 
 *     This function will NOT check if this irq is assigned to any device 
 *     as long as it's a positive integer!
 *     
 * Return: 
 *     On success, returns the irq number configured to the device
 *     Otherwise returns NULL
 */
static PyObject * configure_irq(PyObject * self, PyObject * args) {

    int irq;                /* irq number */
    int fd;                 /* file descripter (of gih device) */
    errno = 0;              /* error indicator */

    /* parse the input number and check validity */
    if (!PyArg_ParseTuple(args, "ii:IRQ", &fd, &irq))   return NULL;
    if (irq < 0)                                        return NULL;

    /* call the ioctl to set irq */
    if (ioctl(fd, GIH_IOC_CONFIG_IRQ, irq) < 0) {
        err(1, "ioctl(gih): irq configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): irq configuration failed, error code %s", 
            strerror(errno));
    }
    
    return Py_BuildValue("i", irq);
}

/*
 * Function name: configure_sleep_t
 * 
 * Function prototype:
 *     static PyObject * 
 *     configure_sleep_t(PyObject * self, PyObject * args);
 *     
 * Description: 
 *     Configure the delay time, in milliseconds, for the interrupt handler.
 *     The device will delay the specified time before it send out the data 
 *     when interrupt occurs.
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wrapps two value
 *            arg1: int fd - file descriptor
 *            arg2: unsigned int time - delay time in ms
 *     
 * Side Effects:
 *     On success, set the delay time of the device.
 *     
 * Error Condition: 
 *     Argument is invalid if isn't a positive integer. 
 *     Because of the preemtible feature of the kernel, 1 or 0ms delay time
 *     may not work really well.
 *     
 * Return: 
 *     On success, returns the sleeping time configured to the device
 *     Otherwise returns NULL
 */
static PyObject * configure_sleep_t(PyObject * self, PyObject * args) {

    unsigned int time;          /* sleeing time */
    int fd;                     /* file descriptor */
    errno = 0;                  /* error indicator */

    /* parse the input number 
     * Notice that no overflow check will be performed, better check in 
     * the python script. 
     */
    if (!PyArg_ParseTuple(args, "iI:sleep_time", &fd, &time))   return NULL;

    /* call the ioctl to set sleep time */
    if (ioctl(fd, GIH_IOC_CONFIG_SLEEP_T, time) < 0) {
        err(1, "ioctl(gih): sleep time configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): sleep time configuration failed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("I", time);
}

/*
 * Function name: configure_wrt_sz
 * 
 * Function prototype:
 *     static PyObject * configure_wrt_sz(PyObject * self, PyObject * args);
 *     
 * Description: 
 *     Configure the write size, in bytes, for each write opeartion when an 
 *     interrupt occurs. Don't make this too big though...
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wrapps two value
 *            arg1: int fd - file descriptor
 *            arg2: unsigned int wrt_sz - num of bytes to write upon 
 *                  receive interruption
 *     
 * Side Effects:
 *     On success, set the write size of the device.
 *     
 * Error Condition: 
 *     Argument is invalid if isn't a positive integer. 
 *     Notice that too big of a write size may cause performance suffer.
 *     
 * Return: 
 *     Return the seted value upon success, 
 *     NULL other wise.
 */
static PyObject * configure_wrt_sz(PyObject * self, PyObject * args) {

    unsigned int wrt_sz;        /* writing size */
    int fd;                     /* file descriptor */
    errno = 0;                  /* error indicator */

    /* parse the input number 
     * Notice that no overflow check will be performed, better check in 
     * the python script. 
     */
    if (!PyArg_ParseTuple(args, "iI:write_size", &fd, &wrt_sz))   return NULL;

    /* call the ioctl to set sleep time */
    if (ioctl(fd, GIH_IOC_CONFIG_WRT_SZ, wrt_sz) < 0) {
        err(1, "ioctl(gih): write size configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): write size configuration failed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("I", wrt_sz);
}

/*
 * Function name: configure_path
 * 
 * Function prototype:
 *     static PyObject * configure_path(PyObject * self, PyObject * args)
 *     
 * Description: 
 *     Set the path of the output destination of gih device. For the specific
 *     project, this is supposed to be a char deivce under /dev mount point,
 *     but it should also be possible to write to any existing file. Notice
 *     that by implementation, the gih device will not be able to create a file
 *     if it's not-existing. The python module should check if the file exists.
 *     
 * Arguments:
 *     @self:
 *     @args: argument that wrapps two value
 *            arg1: int fd - file descriptor
 *            arg2: const char * path - path of the output destination.
 *     
 * Side Effects:
 *     On success, set the writing destination to path.
 *     
 * Error Condition: 
 *     Argument is invalid if isn't a string. 
 *     The string should not be longer then PATH_MAX_LEN set by the device.
 *     
 * Return: 
 *     On success, return the length of the pathname.
 *     Otherwise return NULL
 */
static PyObject * configure_path(PyObject * self, PyObject * args) {

    const char * path;          /* path of the output file */
    int fd;                     /* file descriptor */
    errno = 0;                  /* error indicator */

    /* parse the input argument */
    if (!PyArg_ParseTuple(args, "is:path", &fd, &path))   return NULL;

    /* call the ioctl to set sleep time */
    if (ioctl(fd, GIH_IOC_CONFIG_PATH, path) < 0) {
        err(1, "ioctl(gih): path configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): path configuration failed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("i", strlen(path));
}

/*
 * Function name: configure_finish 
 * 
 * Function prototype:
 *     static PyObject * configure_finish(PyObject * self, PyObject * args)
 *     
 * Description: 
 *     This function is called after configuration is finished. Currently, 
 *     it is only possible configure the device once, so make sure only call
 *     this routine AFTER all the configuration work is finished.
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wrapps one value
 *            arg1: int fd - file descriptor
 *     
 * Side Effects:
 *     On success, set the device status to configured
 *     
 * Error Condition: 
 *     The call will fail if the device is already configured.
 *     
 * Return: 
 *     return 0 upon success, NULL otherwise.
 */
static PyObject * configure_finish(PyObject * self, PyObject * args) {

    int fd;                 /* file descriptor */
    errno = 0;              /* error code */

    /* parse the input argument */
    if (!PyArg_ParseTuple(args, "i:finished", &fd))     return NULL;

    /* call the ioctl to finish construction */
    if (ioctl(fd, GIH_IOC_CONFIG_FINISH, NULL) < 0) {
        err(1, "ioctl(gih): finish configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): finish configuration failed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("i", 0);
}


