/*
 * Filename: gih_configure.h
 * Author: Weiyang Wang
 * Description: User land configuration utility for the gih device,
 *              responsible for calling all the ioctl routines to set the
 *              irq number, delay time, write-file path and write size.
 *
 *              The function defined in this file is supposed to be called 
 *              by the python script that will be used to configure the file
 *              (That means try not to directly use this file...)
 *
 *              Currently only supports load-time configuration (can only
 *              configure the device once, but this can be changed by
 *              making the CONFIGURED field to be modifiable thus the device
 *              can be re-configured when closed. )
 * Date: Jul 26, 2017
 */

#include <Python.h>                 /* python module */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
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
#define GIH_IOC_CONFIG_DELAY_T  _IOW(GIH_IOC, 2, unsigned int) 
#define GIH_IOC_CONFIG_WRT_SZ   _IOW(GIH_IOC, 3, size_t) 
#define GIH_IOC_CONFIG_PATH     _IOW(GIH_IOC, 4, const char *)
#define GIH_IOC_CONFIG_START    _IO (GIH_IOC, 5)
#define GIH_IOC_CONFIG_STOP     _IO (GIH_IOC, 6)
#define GIH_IOC_CONFIG_MISS     _IOW(GIH_IOC, 7, int)


/* see the header comments for each function */
static PyObject * configure_irq     (PyObject *, PyObject *);
static PyObject * configure_delay_t (PyObject *, PyObject *);
static PyObject * configure_wrt_sz  (PyObject *, PyObject *);
static PyObject * configure_path    (PyObject *, PyObject *);
static PyObject * configure_missed  (PyObject *, PyObject *);
static PyObject * configure_start   (PyObject *, PyObject *);
static PyObject * configure_stop    (PyObject *, PyObject *);


/* register functions */
static PyMethodDef config_methods[] = {

    { "configure_irq", configure_irq, 
        METH_VARARGS, "configure irq number" },

    { "configure_delay_t", configure_delay_t,
        METH_VARARGS, "configure delay time in ms" },

    { "configure_wrt_sz", configure_wrt_sz,
        METH_VARARGS, "configure write size in byte" },

    { "configure_path", configure_path, 
        METH_VARARGS, "configure path of output" },

    { "configure_missed", configure_missed, 
        METH_VARARGS, "configure if to keep missed data" },

    { "configure_start", configure_start, 
        METH_VARARGS, "start device" },

    { "configure_stop", configure_stop, 
        METH_VARARGS, "stop device" },

    { NULL, NULL, 0, NULL }
};

/* initialization. I tried to make it available for both py2 and 3 here.. */
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

    PyObject * m;

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3(MOD_NAME, config_methods, MOD_DOC);
#endif

    return (m != NULL) ? m : NULL;
}

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initgih_config(void) {
    moduleinit();
}
#else
PyMODINIT_FUNC PyInit_gih_config(void) {
    return moduleinit();
}
#endif /* PY_MAJOR_VERSION < 3 */

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
 *     @args: argument that wraps two integer value
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
    int fd;                 /* file descriptor (of gih device) */
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
 * Function name: configure_delay_t
 * 
 * Function prototype:
 *     static PyObject * 
 *     configure_delay_t(PyObject * self, PyObject * args);
 *     
 * Description: 
 *     Configure the delay time, in milliseconds, for the interrupt handler.
 *     The device will delay the specified time before it send out the data 
 *     when interrupt occurs.
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wraps two value
 *            arg1: int fd - file descriptor
 *            arg2: unsigned int time - delay time in ms
 *     
 * Side Effects:
 *     On success, set the delay time of the device.
 *     
 * Error Condition: 
 *     Argument is invalid if isn't a positive integer. 
 *     Because of the preemptive feature of the kernel, 1 or 0 ms delay time
 *     may not work really well.
 *     
 * Return: 
 *     On success, returns the sleeping time configured to the device
 *     Otherwise returns NULL
 */
static PyObject * configure_delay_t(PyObject * self, PyObject * args) {

    unsigned int time;          /* sleeting time */
    int fd;                     /* file descriptor */
    errno = 0;                  /* error indicator */

    /* parse the input number 
     * Notice that no overflow check will be performed, better check in 
     * the python script. 
     */
    if (!PyArg_ParseTuple(args, "iI:sleep_time", &fd, &time))   return NULL;

    /* call the ioctl to set sleep time */
    if (ioctl(fd, GIH_IOC_CONFIG_DELAY_T, time) < 0) {
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
 *     Configure the write size, in bytes, for each write operation when an 
 *     interrupt occurs. Don't make this too big though...
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wraps two value
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
 *     Return the set value upon success, 
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
 *     project, this is supposed to be a char device under /dev mount point,
 *     but it should also be possible to write to any existing file. Notice
 *     that by implementation, the gih device will not be able to create a file
 *     if it's not-existing. The python module should check if the file exists.
 *     
 * Arguments:
 *     @self:
 *     @args: argument that wraps two value
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
 * Function name: configure_missed
 * 
 * Function prototype:
 *     static PyObject * configure_missed(PyObject * self, PyObject * args);
 *     
 * Description: 
 *     Sets the behavior when "missed data" happens. Missed data happens when
 *     the user tries to write to a non-empty buffer. If keep_missed is set
 *     to false, each write will clear the data buffer prior to write new
 *     data into the buffer; otherwise, the data is appended to the old data.
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wraps one value
 *            arg1: int fd - file descriptor
 *            arg2: int keep_missed - boolean value indicating keep the 
 *                  missed data or not.
 *     
 * Side Effects:
 *     On success, set the behavior on data missed
 *     
 * Error Condition: 
 *     Argument should be invalid if its not 0 or 1, but the method
 *     treats every non-zero value as true.
 *     
 * Return: 
 *     return 0 if set to false, 1 if set to true, NULL on failure.
 */
static PyObject * configure_missed(PyObject * self, PyObject * args) {

    int keep_missed;            /* keep missed data or not 
                                   despite it's an int, only 0 and 1 should 
                                   be passed in. */
    int fd;                     /* file descriptor */
    errno = 0;                  /* error indicator */

    /* parse the input number 
     * Notice that no overflow check will be performed, better check in 
     * the python script. 
     */
    if (!PyArg_ParseTuple(args, "iI:write_size", &fd, &keep_missed))   
        return NULL;

    /* call the ioctl to set sleep time */
    if (ioctl(fd, GIH_IOC_CONFIG_MISS, keep_missed) < 0) {
        err(1, "ioctl(gih): missed data behavior configuration");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): missed data behavior configuration failed, "
            "error code %s", strerror(errno));
    }

    return Py_BuildValue("I", (keep_missed == 0) ? 0 : 1);
}

/*
 * Function name: configure_start 
 * 
 * Function prototype:
 *     static PyObject * configure_start(PyObject * self, PyObject * args)
 *     
 * Description: 
 *     This function is called after configuration is finished. Currently, 
 *     it is only possible configure the device once, so make sure only call
 *     this routine AFTER all the configuration work is finished.
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wraps one value
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
static PyObject * configure_start(PyObject * self, PyObject * args) {

    int fd;                 /* file descriptor */
    errno = 0;              /* error code */

    /* parse the input argument */
    if (!PyArg_ParseTuple(args, "i:start", &fd))     return NULL;

    /* call the ioctl to finish construction */
    if (ioctl(fd, GIH_IOC_CONFIG_START, NULL) < 0) {
        err(1, "ioctl(gih): start device");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): start device failed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("i", 0);
}


/*
 * Function name: configure_stop 
 * 
 * Function prototype:
 *     static PyObject * configure_stop(PyObject * self, PyObject * args)
 *     
 * Description: 
 *     Stops the device running, and allows re-configuration. 
 *     
 * Arguments:
 *     @self: the calling object
 *     @args: argument that wraps one value
 *            arg1: int fd - file descriptor
 *     
 * Side Effects:
 *     On success, set the device status to not-setup
 *     
 * Error Condition: 
 *     The call will fail if the device is not configured.
 *     
 * Return: 
 *     return 0 upon success, NULL otherwise.
 */
static PyObject * configure_stop(PyObject * self, PyObject * args) {

    int fd;                 /* file descriptor */
    errno = 0;              /* error code */

    /* parse the input argument */
    if (!PyArg_ParseTuple(args, "i:stop", &fd))     return NULL;

    /* call the ioctl to finish construction */
    if (ioctl(fd, GIH_IOC_CONFIG_STOP, NULL) < 0) {
        err(1, "ioctl(gih): device stop");
        return PyErr_Format(PyExc_Exception, 
            "ioctl(gih): stop device ailed, error code %s", 
            strerror(errno));
    }

    return Py_BuildValue("i", 0);
}

