"""Filename: gih.py
Author: Weiyang Wang
Description: User-land utility program for the gih module. This file creates
             an abstract gih class providing the interface for user to
             interact and control the gih device.
             TODO: This program also includes an test program that could allow
             user to test with any interrupt line, despite its intended to be
             imported as a python module in other control scripts.

             Notice that this script currently REQUIRES ROOT PERMISSION.
Date: July 31, 2017
"""


from __future__ import print_function
import sys
from sys import stderr
from sys import stdout
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
        __GIH_DEVICE {str} -- device node of gih device
        __INTR_LOG {str} -- device node of "interrupt happened" log
        __WQ_N_LOG {str} -- device node of "entering workqueue" log
        __WQ_X_LOG {str} -- device node of "exiting workqueue" log
        __MAX_ALL_LOG_SIZE {number} -- max read size of all logs
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
    __MAX_ALL_LOG_SIZE = 256 * 4096


    @staticmethod
    def load(modPath = 'gih.ko'):
        """Load the module if it's not loaded. Need root privilege, and

        Arguments:
            modPath {str} -- path of the module. defaulted to a relative path.

        Returns:
            bool -- True on success loading, False if loaded or failed loading.
        """
        if Gih.__isLoaded:
            print('Error: module already loaded.', file = stderr)
            return False

        # personally I'd change this one to subprocess.run, which was added in
        # python 3.5. To maintain compatibility, I'll use call here
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
    def unload():
        """Unload the module. Need root privilege.

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
    def reload():
        """Reload the module in order to reset parameters.
        Need root privilege

        This method requires that the mode is already loaded and device closed.

        Returns:
            bool -- True on success reloading, False otherwise.
        """

        if not Gih.__isLoaded:
            print('Error: module is not loaded. Call load().', file = stderr)
            return False

        Gih.unload()
        Gih.load(Gih.__modPath)



    @staticmethod
    def open():
        """Open the gih device file.
        This device file NEEDS TO BE OPENED while operating.

        This method will set the __gihfile and __fd variables on success

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
                (root privilege required for kernel operations).',
                file = stderr)
            return False



    @staticmethod
    def close():
        """Closed the device file.
        This will stop the gih device from catching interrupts.

        This method will also reset the file to None and __fd to -1 on success

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



    def __init__(self,
                 irq = -1,
                 delayTime = -1,
                 wrtSize = -1,
                 path = '',
                 gihPath = 'gih.ko'):
        """Create a new gih object

        Creates a new gih object with specified parameters. If the gih module
        is not loaded, will load the module.
        Attributes are set to -1 or empty to denote "unset", if not specified.
        This function will not finish configuration regardless of
        if all parameters are set.

        Keyword Arguments:
            irq {number} -- irq number to catch (default: {-1})
            delayTime {number} -- sleep time before send data (default: {-1})
            wrtSize {number} -- size of data to send (default: {-1})
            path {str} -- path of output file (default: {''})
            gihPath {str} -- path of the kernel module
        """
        if not Gih.__isLoaded:
            if not Gih.load(gihPath):
                return

        if not Gih.__isOpened:
            if not Gih.open():
                return

        self.setup = False

        self.irq = irq
        if irq != -1:
            self.configureIRQ(irq)

        self.delayTime = delayTime
        if delayTime != -1:
            self.setupelayTime(delayTime)

        self.wrtSize = wrtSize
        if wrtSize != -1:
            self.configureWrtSize(wrtSize)

        self.path = path
        if path != '':
            self.configurePath(path)



    def configureIRQ(self, irq):
        """Set the irq number for gih to capture.

        Arguments:
            irq {number} -- irq number for gih to capture

        Returns:
            number -- on success, return the set irq number; otherwise -1
        """
        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if self.setup:
            print('Error: device running.', file = stderr)
            return -1

        if gih_config.configure_irq(Gih.__fd, irq) == irq:
            self.irq = irq
        else:
            self.irq = -1

        return self.irq



    def configureDelayTime(self, delayTime):
        """Set the delayTime before gih send out data

        Arguments:
            delayTime {number} -- delayTime in microsecond

        Returns:
            number -- on success, return the set delayTime; otherwise -1
        """
        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if self.setup:
            print('Error: device running.', file = stderr)
            return -1

        if gih_config.configure_sleep_t(Gih.__fd, delayTime) == delayTime:
            self.delayTime = delayTime
        else:
            self.delayTime = -1

        return self.delayTime



    def configureWrtSize(self, wrtSize):
        """Set the size of gih output on each interrupt

        Arguments:
            wrtSize {number} -- output data size in byte

        Returns:
            number -- on success, return the set wrtSize; otherwise -1
        """
        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if self.setup:
            print('Error: device running.', file = stderr)
            return -1

        if gih_config.configure_wrt_sz(Gih.__fd, wrtSize) == wrtSize:
            self.wrtSize = wrtSize
        else:
            self.wrtSize = -1

        return self.wrtSize




    def configurePath(self, path):
        """Set the path of gih output on each interrupt

        Arguments:
            path {str} -- path of the output file

        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if self.setup:
            print('Error: device running.', file = stderr)
            return -1

        if gih_config.configure_path(Gih.__fd, path) == len(path):
            self.path = path
            return True
        else:
            self.path = ''
            return False



    def start(self):
        """Finish configuration of the gih device. Checks error.

        Raises:
            ValueError -- if any value is left unset, or the device is not
                          opened / is already running, will raise a ValueError.

        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isOpened:
            raise ValueError('Error: device needs \
                to be opened prior to start running.')
                
        if self.setup:
            raise ValueError('Error: device already running.')

        if self.irq == -1:
            raise ValueError('IRQ not set!')

        if self.delayTime == -1:
            raise ValueError('Delay time not set!')

        if self.wrtSize == -1:
            raise ValueError('Write size not set!')

        if self.path == '':
            raise ValueError('Output path not set!')

        self.setup = True

        return (gih_config.configure_start(Gih.__fd) == 0)


    def stop(self):
        """Stops gih device and allow re-configuration.

        Raises:
            ValueError -- if device is not opened / is not running
        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isOpened:
            raise ValueError('Error: device needs \
                to be opened prior to stop running.')
                
        if not self.setup:
            raise ValueError('Error: device not running.')

        self.setup = False

        return (gih_config.configure_stop(Gih.__fd) == 0)



    @staticmethod
    def readIntrLogs():
        """Read logs recorded at interrupt happening from the gihlog0 device

        Returns:
            list -- list of all logs currently in the file (as strings)
        """
        try:
            with open(Gih.__INTR_LOG, 'r') as logdev:
                allContent = logdev.read(Gih.__MAX_ALL_LOG_SIZE)
                logLines = [s + ' at interrupt happening' \
                            for s in allContent.split('\n')]
                return logLines[:-1]

        except IOError:
            print('Error: open gihlog device file failed, file does not exist.',
                file = stderr)
            return False

        except PermissionError:
            print('Error: open gihlog device file failed, permission denied.',
                file = stderr)
            return False



    @staticmethod
    def readWQNLogs():
        """Read logs recorded at entering workqueue from the gihlog1 device

        Returns:
            list -- list of all logs currently in the file (as strings)
        """
        try:
            with open(Gih.__WQ_N_LOG, 'r') as logdev:
                allContent = logdev.read(Gih.__MAX_ALL_LOG_SIZE)
                logLines = [s + ' at entering workqueue' \
                            for s in allContent.split('\n')]
                return logLines[:-1]

        except IOError:
            print('Error: open gihlog device file failed, file does not exist.',
                file = stderr)
            return False

        except PermissionError:
            print('Error: open gihlog device file failed, permission denied.',
                file = stderr)
            return False




    @staticmethod
    def readWQXLogs():
        """Read logs recorded at existing workqueue from the gihlog2 device

        Returns:
            list -- list of all logs currently in the file (as strings)
        """
        try:
            with open(Gih.__WQ_X_LOG, 'r') as logdev:
                allContent = logdev.read(Gih.__MAX_ALL_LOG_SIZE)
                logLines = [s + ' at exiting workqueue' \
                            for s in allContent.split('\n')]
                return logLines[:-1]

        except IOError:
            print('Error: open gihlog device file failed, file does not exist.',
                file = stderr)
            return False

        except PermissionError:
            print('Error: open gihlog device file failed, permission denied.',
                file = stderr)
            return False



    @staticmethod
    def readAllLogs(sortKey = 'type'):
        """Read all logs from all three devices, and return a sorted list of
        all the logs.

        Keyword Arguments:
            sortKey {str} -- sorting key for the logs (default: {'type'})
                             Acceptable values include:
                             'type': sort by which log device logs were from
                             'time': sort by when the logs were recorded
                             'count': sort by log count

        Returns:
            list -- sorted list of logs

        Raises:
            ValueError -- if the sorting key is not supported
        """
        # performance issues here when interrupts are frequent.
        allLogs = Gih.readIntrLogs()
        allLogs.extend(Gih.readWQNLogs())
        allLogs.extend(Gih.readWQXLogs())

        # okay, python don't have a switch statement...
        # type: sort by which log device were they from
        if sortKey == 'type':
            return allLogs

        # time: sort according to time. Actually a fixed length ASCII.
        elif sortKey == 'time':
            return sorted(allLogs, key = lambda x: x.split(']')[0])

        # sort according to interrupt count. Notice that with current
        # implementation, all 3 log devices have their own interrupt count
        # therefore this is NOT in the actual order if there's interrupt
        # missed. This should be stable w/ respect to type
        # this is also slow.
        elif sortKey == 'count':
            return sorted(allLogs, key = lambda x: float(x.split()[3]))

        # default case of unsupported key
        else:
            raise ValueError('Unsupported sorting key!')


    # TODO: potential unicodeError?
    def write(self, dataStr):
        """Write data to the gih device. Gih will resent these data out
        on interrupt happening.

        Arguments:
            dataStr {str} -- string to be send out to the device

        Returns:
            number -- number of bytes written to gih on success, -1 otherwise
        """
        if not Gih.__isOpened:
            print("Error: device needs to be opened to be written to.")
            return -1

        if not self.setup:
            print("Error: device needs to be configured prior to writing.")
            return -1

        try:
            outByte = Gih.__gihFile.write(dataStr)
            Gih.__gihFile.flush()
            return outByte

        except PermissionError:
            print('Error: writing to gih device file failed, \
                   permission denied. \
                   (root privilege required for kernel operations).',\
                  file = stderr)
            return -1



    @staticmethod
    def shutdown():
        """Shutdown the gih device, wrapper method for both close and unload.

        Returns:
            bool -- True on success, False otherwise
        """
        if Gih.close():
            Gih.unload()
            return True
        return False

