# If KERNELRELEASE is defined, we've been invoked from the
#      # kernel build system and can use its language.
MODNAME=gih
SRC=src
BUILD=build
PYSETUP=setup.py

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODNAME).o
	# Otherwise we were called directly from the command
	# line; invoke the kernel build system.
	# 
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD  := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/$(SRC) modules
	mv $(PWD)/src/$(MODNAME).ko $(PWD)/$(BUILD)

endif

.PHONY: clean

pyconf: setup.py gih_config.c
	python $(PYSETUP) build
	python3 $(PYSETUP) build
	cp $(BUILD)/*/*.so $(BUILD)

clean:
	cd $(SRC) 
	rm -rf *.o *.ko *.so *.pyc modules.order Module.symvers *.mod.* \
	build __pycache__
	cd ..
	cd $(BUILD)
	rm -rf *.o *.ko *.so *.pyc modules.order Module.symvers *.mod.* \
	build __pycache__
	cd ..

new: clean default
