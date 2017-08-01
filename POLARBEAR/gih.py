# Filename: gih.py
# Author: Weiyang Wang
# Description: Userland utility program for the gih module. This file creates
#              an abstract gih class providing the interface for user to
#              interact and control the gih device.
#              TODO: This program also includes an test program that could allow
#              user to test with any interrupt line, despite its intended to be
#              imported as a python module in other control scripts.
#
#              Notice that this script currently REQUIRES ROOT PERMISSION.
# Date: July 31, 2017

from __future__ import print_function
import sys
from sys import stderr
import subprocess
import gih_config

class Gih(object):
    """User land gih device control interface.

    Attributes:
        irq {number} -- irq number that the gih device is capturing.
        delayTime {number} -- delay time before send data upon receive interrupt
        wrtSize {number} -- size of data to send out on each interrupt
        outputPath {number} -- path of the output file
        configured {boolean} -- if the device is configured

    Variables:
        __isLoaded {boolean} -- if the module was loaded.
        __isOpened {boolean} -- if the gih device file is opened
        __gihFile {file} -- gih device file
        __modPath {str} -- gih module path
        __fd {number} -- file descriptor of the gih device

    Constants:
        __GIH_DEVICE {str} -- [device node of gih device]
        __INTR_LOG {str} -- [device node of "interrupt happened" log]
        __WQ_N_LOG {str} -- [device node of "entering workqueue" log]
        __WQ_X_LOG {str} -- [device node of "exiting workqueue" log]
    """

    __isLoaded = False
    __isOpened = False
    __gihFile  = None
    __modPath  = ''
    __fd       = -1

    __GIH_DEVICE = '/dev/gih'
    __INTR_LOG   = '/dev/gihlog0'
    __WQ_N_LOG   = '/dev/gihlog1'
    __WQ_X_LOG   = '/dev/gihlog2'


    @staticmethod
    def loadMod(modPath = 'gih.ko'):
        """Load the module if it's not loaded. Need root privilage, and

        Arguments:
            modPath {str} -- path of the module. defaulted to a relative path.

        Returns:
            bool -- True on success loading, False if loaded or failed loading.
        """
        if Gih.__isLoaded:
            print('Error: module already loaded.', file = stderr)
            return False

        # personally I'd change this one to subprocess.run, which was added in
        # python 3.5. To maintain compability, I'll use call here
        cmd = 'insmod {:s}'.format(modPath)
        print('Running shell command: \"{:s}\" ...'.format(cmd))

        retcode = subprocess.call(cmd, shell=True)
        if retcode == 0:
            Gih.__isLoaded = True
            Gih.__modPath  = modPath
            return True
        else:
            print('Error: module loading failed, shell returned {:d}'.\
                format(retcode), file = stderr)
            return False



    @staticmethod
    def unloadMod():
        """Unload the module. Need root privilage

        Returns:
            bool -- True on success unloading, False otherwise or not loaded.
        """

        if not Gih.__isLoaded:
            print('Error: module is not loaded.', file = stderr)
            return False

        cmd = 'rmmod gih'
        print('Running shell command: \"{:s}\" ...'.format(cmd))

        retcode = subprocess.call(cmd, shell=True)
        if retcode == 0:
            Gih.__isLoaded = False
            return True
        else:
            print('Error: module unloading failed, shell returned {:d}'.\
                format(retcode), file = stderr)
            return False



    @staticmethod
    def reloadMod():
        """Reload the module in order to reset parameters.
        Need root privilage

        This method requires that the mode is already loaded.

        Returns:
            bool -- True on success reloading, False otherwise.
        """

        if not Gih.__isLoaded:
            print('Error: module is not loaded. Call load().', file = stderr)
            return False

        Gih.unloadMod()
        Gih.loadMod(Gih.__modPath)



    @staticmethod
    def openDevice():
        """Open the gih device file.
        This device file NEEDS TO BE OPENED while operating.

        This method will set the gihfile and fd variables on success

        Returns:
            bool -- True on success or already opened,
            False with printing error message otherwise
        """

        if Gih.__isOpened:
            print('Error: device already opened.', file = stderr)
            return True

        try:
            print('Opening gih device...', file = stderr)
            Gih.__gihFile  = open(Gih.__GIH_DEVICE, "w")
            Gih.__fd       = Gih.__gihFile.fileno()
            Gih.__isOpened = True
            return True

        except IOError:
            print('Error: open gih device file failed, file does not exist.',
                file = stderr)
            return False

        except PermissionError:
            print('Error: open gih device file failed, permission denied \
                (root privilage required for kernel operations).',
                file = stderr)
            return False



    @staticmethod
    def closeDevice():
        """Closed the device file.
        This will stop the gih device from catching interrupts.

        This method will also reset the file to None and fd to -1 on success

        Returns:
            bool -- True on success or not opened, False otherwise
        """
        if not Gih.__isOpened:
            print('Error: device is not opened.', file = stderr)
            return True

        try:
            print('Closing gih device...', file = stderr)
            Gih.__gihFile.close()
            Gih.__gihFile  = None
            Gih.__fd       = -1
            Gih.__isOpened = False
            return True

        except:
            print('Error: close gih device file failed.', file = stderr)
            return False



    def __init__(self, irq = -1, delayTime = -1, wrtSize = -1, path = ''):
        """Create a new gih object

        Creates a new gih object with specified parameters. If the gih module
        is not loaded, will load the module.
        Attributes are set to -1 or empty to denote "unset", if not specified.
        This function will not finish configuration regardless of
        if all parameters are set.

        Keyword Arguments:
            irq {number} -- [irq number to catch] (default: {-1})
            delayTime {number} -- [sleep time before send data] (default: {-1})
            wrtSize {number} -- [size of data to send] (default: {-1})
            path {str} -- [path of output file] (default: {''})
        """
        if not Gih.__isLoaded:
            if not Gih.loadMod():
                return

        if not Gih.__isOpened:
            if not Gih.openDevice():
                return

        self.configured = False

        self.irq = irq
        if irq != -1:
            self.configureIRQ(irq)

        self.delayTime = delayTime
        if delayTime != -1:
            self.configureDelayTime(delayTime)

        self.wrtSize = wrtSize
        if wrtSize != -1:
            self.configureWrtSize(wrtSize)

        self.path = path
        if path != '':
            self.configurePath(path)



    def configureIRQ(self, irq):
        """Set the irq number for gih to capture.

        Arguments:
            irq {number} -- [irq number for gih to capture]

        Returns:
            number -- on success, return the set irq number; otherwise -1
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened prior to configuration.")
            return -1

        if gih_config.configure_irq(Gih.__fd, irq) == irq:
            self.irq = irq
        else:
            self.irq = -1

        return self.irq



    def configureDelayTime(self, delayTime):
        """Set the delayTime before gih send out data

        Arguments:
            delayTime {number} -- [delayTime in microsecond]

        Returns:
            number -- on success, return the set delayTime; otherwise -1
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened prior to configuration.")
            return -1

        if gih_config.configure_sleep_t(Gih.__fd, delayTime) == delayTime:
            self.delayTime = delayTime
        else:
            self.delayTime = -1

        return self.delayTime



    def configureWrtSize(self, wrtSize):
        """Set the size of gih output on each interrupt

        Arguments:
            wrtSize {number} -- [output data size in byte]

        Returns:
            number -- on success, return the set wrtSize; otherwise -1
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened prior to configuration.")
            return -1

        if gih_config.configure_wrt_sz(Gih.__fd, wrtSize) == wrtSize:
            self.wrtSize = wrtSize
        else:
            self.wrtSize = -1

        return self.wrtSize



    def configureWrtSize(self, wrtSize):
        """Set the size of gih output on each interrupt

        Arguments:
            wrtSize {number} -- [output data size in byte]

        Returns:
            number -- on success, return the set wrtSize; otherwise -1
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened prior to configuration.")
            return -1

        if gih_config.configure_wrt_sz(Gih.__fd, wrtSize) == wrtSize:
            self.wrtSize = wrtSize
        else:
            self.wrtSize = -1

        return self.wrtSize



    def configurePath(self, path):
        """Set the path of gih output on each interrupt

        Arguments:
            path {str} -- [path of the output file]

        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened prior to configuration.")
            return -1

        if gih_config.configure_path(Gih.__fd, path) == len(path):
            self.path = path
            return True
        else:
            self.path = ''
            return False



    def configureFinish(self):
        """Finish configuration of the gih device. Checks error.

        Raises:
            ValueError -- if any value is left unset, will raise a ValueError.

        Returns:
            bool -- True on success, False otherwise
        """
        if self.irq == -1:
            raise ValueError('IRQ not set!')

        if self.delayTime == -1:
            raise ValueError('Delay time not set!')

        if self.wrtSize == -1:
            raise ValueError('Write size not set!')

        if self.path == '':
            raise ValueError('Output path not set!')

        return (gih_config.configure_finish(Gih.__fd) == 0)



