#
# Makefile for dell_rbu kernel module
#

TARGET := dell_rbu

KVER = "4.19.0+1"
CC = gcc

export KO_EXISTS=`cat /etc/modules 2>/dev/null | grep dell_rbu && echo 1 || echo 0`

ccflags-y := -Wall

obj-m:=$(TARGET).o

BUILD_DIR:=/lib/modules/$(KVER)/build

PWD:=$(shell pwd)

all:
	$(MAKE) -j4 CC=$(CC) -C $(BUILD_DIR) M=$(PWD) modules

clean:
	$(MAKE) -j4 -C $(BUILD_DIR) M=$(PWD) clean

install:
	@install -D -m 644 ${TARGET}.ko /lib/modules/$(shell uname -r)/updates/${TARGET}.ko
	@depmod -a $(shell uname -r)
	#dracut --force #if the module needs to be added in initrd
        
uninstall:
	@if [ "${KO_EXISTS}" != "0" ]; then sed -in '/${TARGET}/d' /etc/modules ; fi
	@rm -f /lib/modules/$(shell uname -r)/updates/${TARGET}.ko
	@depmod -a $(shell uname -r)
