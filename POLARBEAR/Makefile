SHELL := /bin/bash

MODNAME=gih
SRC=src
BUILD=build
PYSETUP=setup.py
PYCMOD=$(MODNAME)_configure.c
PYCTL=$(MODNAME).py
PWD  := $(shell pwd)

all: kernMod pyconf

kernMod: buildDir
	$(MAKE) -C $(PWD)/$(SRC) M=$(PWD)/$(SRC) 
	mv $(PWD)/$(SRC)/$(MODNAME).ko $(PWD)/$(BUILD)

buildDir: 
	@if [ ! -d $(PWD)/$(BUILD) ]; then mkdir $(PWD)/$(BUILD); fi;  

pyconf: $(SRC)/$(PYSETUP) $(SRC)/$(PYCMOD) buildDir
	$(MAKE) -C $(SRC) M=$(PWD)/$(SRC) pyconf
	cp $(PWD)/$(SRC)/$(PYCTL) $(PWD)/$(BUILD)

clean:
	$(MAKE) -C $(PWD)/$(SRC) M=$(PWD)/$(SRC) clean
	rm -rf build .*.cmd .tmp_versions

new: clean all

.PHONY: clean all
