# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
MODNAME=gih
PYSETUP=setup.py
BUILD=build
SRC=src

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODNAME).o
	# Otherwise we were called directly from the command
	# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD  := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

	
pyconf:
	python $(PWD)/$(PYSETUP) build
	python3 $(PWD)/$(PYSETUP) build
	cd ..
	cp $(PWD)/$(BUILD)/*/*.so $(PWD)/../$(BUILD)

clean:
	rm -rf *.o *.ko *.so *.pyc .*.cmd .tmp_versions \
		modules.order Module.symvers *.mod.* build __pycache__ 
	
.PHONY: clean
