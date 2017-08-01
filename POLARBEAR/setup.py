
from distutils.core import setup, Extension

module1 = Extension('gih_config', sources = ['gih_configure.c'])

setup (name = 'gih_config',
        version = '1.0',
        description = 'GIH Ioctl routines',
        ext_modules = [module1])
