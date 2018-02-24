KNAME ?= $(shell uname -r)
KERNEL_VERSION := $(shell uname -r)
KDIR := /lib/modules/${KERNEL_VERSION}/build
DRV_SRC := $(shell basename `pwd`)
DRV_VER := ENIC_WHOLE_VERSION
SU=/usr/bin/sudo
OS_REL_VER := $(shell lsb_release -r -s)
OS_CODENAME := $(shell lsb_release -c -s)
OS_DISTRIBUTION := $(shell lsb_release -i -s)
IMGLOOP ?= $(shell sudo losetup -f)
HOST_ARCH := $(shell dpkg-architecture -qDEB_HOST_ARCH)
PACKNAME := kmod-enic-${KNAME}-${DRV_VER}_${HOST_ARCH}

.PHONY: deb
deb:
	dpkg-buildpackage -b -us -uc -tc;
	mv ../kmod-enic_${DRV_VER}_${HOST_ARCH}.deb ./${PACKNAME}.deb

clean:
	rm *.iso *.deb &> /dev/null

.PHONY: default
default: all

.PHONY: all
all: deb
	$(SU) rm -rf disk images iso_image
	echo $(OS_CODENAME) $(OS_REL_VER) $(OS_DISTRIBUTION)
	mkdir -p disk/driver-injection-disk/$(OS_DISTRIBUTION)-drivers/$(OS_CODENAME)/
	mkdir -p disk/ubuntu-drivers/$(OS_CODENAME)/
	cp ${PACKNAME}.deb disk/driver-injection-disk/$(OS_DISTRIBUTION)-drivers/$(OS_CODENAME)/
	cp ${PACKNAME}.deb disk/ubuntu-drivers/$(OS_CODENAME)/
	mkisofs -V kmod-enic_${HOST_ARCH} -R -o ${PACKNAME}.iso disk
	mkdir -p images
	dd if=/dev/zero of=images/dd.img bs=1024 count=64k
	mkdir -p images/tmp
	$(SU) losetup ${IMGLOOP} images/dd.img
	echo "y" | $(SU) mkfs -t ext3 -L OEMDRV ${IMGLOOP}
	$(SU) mount ${IMGLOOP} images/tmp
	$(SU) rm -fr images/tmp/lost+found
	$(SU) cp -r disk/* images/tmp/
	$(SU) umount images/tmp
	$(SU) losetup -d ${IMGLOOP}
	mv images/dd.img ${PACKNAME}.img

