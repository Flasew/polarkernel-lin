"""Filename: gih.py
Author: Weiyang Wang
Description: User-land utility program for the gih module. This file creates
             an abstract gih class providing the interface for user to
             interact and control the gih device.

             Notice that this script currently REQUIRES ROOT PERMISSION.
Date: July 31, 2017
"""

from __future__ import print_function
import sys
from sys import stderr
from sys import stdout
import os
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
        __setup {boolean} -- if the device has been setup (is running)
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
    __setup    = False
    __gihFile  = None
    __modPath  = ''
    __fd       = -1

    __GIH_DEVICE = '/dev/gih'
    __INTR_LOG   = '/dev/gihlog0'
    __WQ_N_LOG   = '/dev/gihlog1'
    __WQ_X_LOG   = '/dev/gihlog2'
    __MAX_ALL_LOG_SIZE = 256 * 8192



    def __init__(self,
                 irq = -1,
                 delayTime = -1,
                 wrtSize = -1,
                 keepMissed = -1,
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
            keepMissed {number} -- behavior on missed data, -1 for not set
                                    (default: {-1})
            gihPath {str} -- path of the kernel module
        """
        if not Gih.__isLoaded:
            if not Gih.load(gihPath):
                return

        if not Gih.__isOpened:
            if not Gih.open():
                return

        Gih.__setup = False

        self.irq = irq
        if irq != -1:
            self.configureIRQ(irq)

        self.delayTime = delayTime
        if delayTime != -1:
            self.configureDelayTime(delayTime)

        self.wrtSize = wrtSize
        if wrtSize != -1:
            self.configureWrtSize(wrtSize)

        self.keepMissed = keepMissed
        if keepMissed != -1:
            self.configureMissed(keepMissed)

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
        if not Gih.__isLoaded:
            print('Error: module is not loaded.', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if Gih.__setup:
            print('Error: device is running.', file = stderr)
            return -1

        if type(irq) != int or irq <= 0:
            print('Error: irq needs to be a positive integer.', file = stderr)
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
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if Gih.__setup:
            print('Error: device is running.', file = stderr)
            return -1

        if type(delayTime) != int or delayTime < 0:
            print('Error: delay time needs to be a non-negative integer '
                'in milliseconds.', file = stderr)
            return -1

        if gih_config.configure_delay_t(Gih.__fd, delayTime) == delayTime:
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
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if Gih.__setup:
            print('Error: device is running.', file = stderr)
            return -1

        if type(wrtSize) != int or wrtSize <= 0:
            print('Error: write size needs to be a positive integer in byte.',
                    file = stderr)
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
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if Gih.__setup:
            print('Error: device is running.', file = stderr)
            return -1

        if not os.path.exists(path) or os.path.isdir(path):
            print('Error: {:s} does not exist or is a directory.'.format(path),
                    file = stderr)
            return -1

        if gih_config.configure_path(Gih.__fd, path) == len(path):
            self.path = path
            return True
        else:
            self.path = ''
            return False



    def configureMissed(self, keepMissed):
        """Set the behavior when missed data happens.

        Missed data happens when the user tries to write to a non-empty buffer.
        If keepMissed is set to false, each write will clear the data buffer
        prior to write new data into the buffer;
        otherwise, the data is appended to the old data

        Arguments:
            keepMissed {number} -- if missed data should be kept or not,
                                    any non-zero number is True and 0 is false.

        Returns:
            number -- on success, returns 0 if set to false, 1 if set to true;
                      on failure returns -1
        """
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to configuration.',\
                file = stderr)
            return -1

        if Gih.__setup:
            print('Error: device is running.', file = stderr)
            return -1

        res = gih_config.configure_missed(Gih.__fd, keepMissed)

        if res == 0 or res == 1:
            self.keepMissed = res
        else:
            self.keepMissed = -1

        return self.keepMissed


    def start(self):
        """Finish configuration of the gih device. Checks error.

        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return False

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to start running.',
                    file = stderr)
            return False

        if Gih.__setup:
            print('Error: device already running.', file = stderr)
            return False

        unset = False

        if self.irq == -1:
            print('IRQ not set!', file = stderr)
            unset = True

        if self.delayTime == -1:
            print('Delay time not set!', file = stderr)
            unset = True

        if self.wrtSize == -1:
            print('Write size not set!', file = stderr)
            unset = True

        if self.keepMissed == -1:
            print('Missed data behavior not set!', file = stderr)
            unset = True

        if self.path == '':
            print('Output path not set!', file = stderr)
            unset = True

        if unset:
            return False

        if gih_config.configure_start(Gih.__fd) == 0:
            Gih.__setup = True
            return True


    def stop(self):
        """Stops gih device and allow re-configuration.

        Returns:
            bool -- True on success, False otherwise
        """
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device needs to be opened prior to stop running.',
                    file = stderr)
            return -1

        if not Gih.__setup:
            print('Error: device not running.', file = stderr)
            return -1

        Gih.__setup = False

        return (gih_config.configure_stop(Gih.__fd) == 0)



    # TODO: potential unicodeError?
    def write(self, dataStr, block = False):
        """Write data to the gih device. Gih will resent these data out
        on interrupt happening.

        Arguments:
            dataStr {str} -- string to be send out to the device
            block {bool} -- should the write call block if the device is full

        Returns:
            number -- number of bytes written to gih on success, -1 otherwise
        """
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print("Error: device needs to be opened to be written to.")
            return -1

        if not Gih.__setup:
            print("Error: device needs to be started prior to writing.")
            return -1

        try:
            if block:
                outByte = Gih.__gihFile.write(dataStr)
                Gih.__gihFile.flush()
            else:
                outByte = os.write(Gih.__fd, dataStr.encode('ascii'))
            return outByte

        except PermissionError:
            print('Error: writing to gih device file failed, ' +
                   'permission denied. ' +
                   '(root privilege required for kernel operations).',\
                  file = stderr)
            return -1



    def __str__(self):
        """Creates a formatted string of the gih object with its status.

        Returns:
            str - nicely formatted string about the gih device.
        """

        return \
        "========gih device========\n" +\
        "- Module is {:s}loaded\n".format("" if Gih.__isLoaded else "un") +\
        "- Device is {:s}.\n"\
            .format("opened" if Gih.__isOpened else "closed") +\
        "- Device is currently{:s} running.\n\n"\
            .format("" if Gih.__setup else " not") +\
        "- Configuration status (-1 being not configured):\n" +\
        "     IRQ: {:d}\n".format(self.irq) +\
        "     Delay Time: {:d} millisecond(s)\n".format(self.delayTime) +\
        "     Write Size: {:d} byte(s) on interrupt\n".format(self.wrtSize) +\
        "     Keep Missed Data: {0}\n"\
            .format("-1" if self.keepMissed == -1 else\
                   (True if self.keepMissed else False)) +\
        "     Destination File Path: {:s}\n"\
            .format("-1" if self.path == "" else self.path)



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
        However, simply opening the device will not run it, start() needs to be
        called.

        This method will set the __gihfile and __fd variables on success

        Returns:
            bool -- True on success or already opened,
            False with printing error message otherwise
        """

        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if Gih.__isOpened:
            print('Error: device already opened.', file = stderr)
            return True

        try:
            print('Opening gih device...', file = stderr)
            Gih.__fd  = os.open(Gih.__GIH_DEVICE, os.O_NONBLOCK | os.O_WRONLY)
            Gih.__gihFile  = os.fdopen(Gih.__fd, 'w')
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
        This will also stop the gih device from catching interrupts.

        This method will also reset the file to None and __fd to -1 on success

        Returns:
            bool -- True on success or not opened, False otherwise
        """
        if not Gih.__isLoaded:
            print('Error: module is not loaded', file = stderr)
            return -1

        if not Gih.__isOpened:
            print('Error: device is not opened.', file = stderr)
            return True

        try:
            print('Closing gih device...', file = stderr)
            Gih.__gihFile.close()
            Gih.__gihFile  = None
            Gih.__fd       = -1
            Gih.__setup    = False
            Gih.__isOpened = False
            return True

        except:
            print('Error: close gih device file failed.', file = stderr)
            return False



    @staticmethod
    def remove():
        """Shutdown the gih device, wrapper method for both close and unload.

        Returns:
            bool -- True on success, False otherwise
        """
        if Gih.close():
            Gih.unload()
            return True
        return False



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
            return sorted(allLogs, key = lambda x: int(x.split()[3]))

        # default case of unsupported key, return by type.
        else:
            print('Unsupported sorting key! Return value sorted by type.')
            return allLogs


