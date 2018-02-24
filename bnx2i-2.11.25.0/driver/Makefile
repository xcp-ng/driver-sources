#!/usr/bin/make
# Makefile for building Linux QLogic Gigabit L5 driver as a module.
# $id$
KVER=
ifeq ($(KVER),)
  KVER=$(shell uname -r)
endif

KERNVER := $(shell echo $(KVER) | sed -e 's/\([0-9]*\).*/\1/')
KMAJVER := $(shell echo $(KVER) | sed -e 's/[0-9]*\.\([0-9]*\).*/\1/')
KMINVER := $(shell echo $(KVER) | sed -e 's/[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/')

# PREFIX may be set by the RPM build to set the effective root.
PREFIX=
ifeq ($(shell ls /lib/modules/$(KVER)/build > /dev/null 2>&1 && echo build),)
# SuSE source RPMs
  _KVER=$(shell echo $(KVER) | cut -d "-" -f1,2)
  _KFLA=$(shell echo $(KVER) | cut -d "-" -f3)
  _ARCH=$(shell file -b /lib/modules/$(shell uname -r)/build | cut -d "/" -f5)
  ifeq ($(_ARCH),)
    _ARCH=$(shell uname -m)
  endif
  ifeq ($(shell ls /usr/src/linux-$(_KVER)-obj > /dev/null 2>&1 && echo linux),)
    ifeq ($(shell ls /usr/src/kernels/$(KVER)-$(shell uname -m) > /dev/null 2>&1 && echo linux),)
      LINUX=
    else
      LINUX=/usr/src/kernels/$(KVER)-$(shell uname -m)
      LINUXSRC=$(LINUX)
    endif
  else
    LINUX=/usr/src/linux-$(_KVER)-obj/$(_ARCH)/$(_KFLA)
    LINUXSRC=/usr/src/linux-$(_KVER)
  endif
else
  LINUX=/lib/modules/$(KVER)/build
  ifeq ($(shell ls /lib/modules/$(KVER)/source > /dev/null 2>&1 && echo source),)
    LINUXSRC=$(LINUX)
  else
    LINUXSRC=/lib/modules/$(KVER)/source
  endif
endif

# Make sure only one bnx2? DKMS RPM is installed.
#ifneq ($(shell rpm -qa | grep -v "bnx2i-*" | grep "netxtreme2-.*dkms" | wc -l), 1)
    #$(error More than one netxtreme2 DKMS RPM installed!!!  Failing build.)
#endif

ifeq ($(BNX2_CNIC_INC),)
  # Obtain the current working directory.  $(PWD) doesn't work because this
  # makefile cannot override the $(PWD) definition of the parent makefile.
  BCMPWD = $(shell pwd)
  ifeq ($(shell test -e $(BCMPWD)/../../bnx2 > /dev/null 2>&1 || echo notfound),)
    BNX2_CNIC_INC := $(BCMPWD)/../../bnx2/src
  else
    $(error bnx2/cnic package not found. $(BCMPWD))
  endif
  export BNX2_CNIC_INC
endif

ifeq ($(BCMMODDIR),)
ifeq ($(shell ls /lib/modules/$(KVER)/updates > /dev/null 2>&1 && echo 1),1)
    BCMMODDIR=/lib/modules/$(KVER)/updates
  else
    ifeq ($(shell grep -q "search.*[[:space:]]updates" /etc/depmod.conf > /dev/null 2>&1 && echo 1),1)
      BCMMODDIR=/lib/modules/$(KVER)/updates
    else
      ifeq ($(shell grep -q "search.*[[:space:]]updates" /etc/depmod.d/* > /dev/null 2>&1 && echo 1),1)
        BCMMODDIR=/lib/modules/$(KVER)/updates
      else
        BCMMODDIR=/lib/modules/$(KVER)/kernel/drivers/scsi
      endif
    endif
  endif
endif

# iSCSI offload is only supported for RHEL5.4+, RHEL6.0+, SLES11sp1+,
# and 2.6 upstream kernels

# Check for the existence of version.h
ifneq ($(shell ls $(LINUXSRC)/include/linux/version.h > /dev/null 2>&1 && echo version),)
  ifneq ($(shell grep "RHEL" $(LINUXSRC)/include/linux/version.h > /dev/null 2>&1 && echo rhel),)
    MAJVER := $(shell grep "MAJOR" $(LINUXSRC)/include/linux/version.h | sed -e 's/.*MAJOR \([0-9]\)/\1/')
    MINVER := $(shell grep "MINOR" $(LINUXSRC)/include/linux/version.h | sed -e 's/.*MINOR \([0-9]\)/\1/')
    DISTRO="RHEL"
  else
    # Can be upstream or SLES11 kernel
    ifeq ($(shell test -f /etc/SuSE-release > /dev/null 2>&1 || echo notfound),)
      VERSION := $(shell grep VERSION /etc/SuSE-release | sed -e 's/.*= //')
      PATCHLEVEL := $(shell grep PATCHLEVEL /etc/SuSE-release | sed -e 's/.*= //')
      DISTRO="SLES"
    else
      # Upstream kernel detected, allow compilation
      ifeq ($(shell [ "$(KERNVER)" = "2" ] || echo notfound),)
        ifeq ($(shell [ $(KMAJVER) -ge "6" -a $(KMINVER) -gt "30" ] || echo notfound),)
          BCM_ISCSI_BUILD=iscsibuild
          BCM_ISCSI_INSTALL=iscsiinstall
          BCM_ISCSI_CLEAN=iscsiclean
        endif
      else
        ifeq ($(shell [ $(KMAJVER) -gt "2" ] || echo notfound),)
          BCM_ISCSI_BUILD=iscsibuild
          BCM_ISCSI_INSTALL=iscsiinstall
          BCM_ISCSI_CLEAN=iscsiclean
        endif
      endif
    endif
  endif
else
  #Only use the /etc/redhat or SuSE -release if the version.h doesn't exist
  ifeq ($(shell test -f /etc/redhat-release > /dev/null 2>&1 || echo notfound),)
    MAJVER := $(shell sed -e 's/.*release \([0-9]\).*/\1/' /etc/redhat-release)
    MINVER := $(shell sed -e 's/.*\.\([0-9]\)*.*/\1/' /etc/redhat-release)
    DISTRO="RHEL"
  endif
  ifeq ($(shell test -f /etc/SuSE-release > /dev/null 2>&1 || echo notfound),)
    VERSION := $(shell grep VERSION /etc/SuSE-release | sed -e 's/.*= //')
    PATCHLEVEL := $(shell grep PATCHLEVEL /etc/SuSE-release | sed -e 's/.*= //')
    DISTRO="SLES"
  endif
  ifeq ($(shell test -f /etc/os-release > /dev/null 2>&1 || echo notfound),)
    UDISTRO := $(shell grep -o -m 1 Ubuntu /etc/os-release)
    ifeq ($(shell [ "$(UDISTRO)" = "Ubuntu" ] && [ "$(KERNVER)" -ge "4" ] || echo notfound),)
      ifeq ($(shell [ "$(KMAJVER)" -ge "15" ] || echo notfound),)
        BRCM_FLAGS += -DUBUNTU_DEBUGFS
      endif
    endif
    SLES_DISTRO := $(shell grep -o -m 1 SLES /etc/os-release)
    ifeq ($(shell [ "$(SLES_DISTRO)" = "SLES" ] && [ "$(KERNVER)" -ge "5" ] || echo notfound),)
        DISTRO="SLES"
        BRCM_FLAGS += -DSLES15_DEBUGFS
    endif
  endif
  ifeq ($(shell [ $(KERNVER) -gt "2" ] || echo notfound),)
    BCM_ISCSI_BUILD=iscsibuild
    BCM_ISCSI_INSTALL=iscsiinstall
    BCM_ISCSI_CLEAN=iscsiclean
  endif
endif

ifeq ($(DISTRO), "RHEL")
  # Normalized the RHEL version string
  ifeq ($(shell [ "$(MAJVER)" -lt "10" ] || echo notfound),)
    ifeq ($(shell [ "$(MINVER)" -lt "10" ] || echo notfound),)
      RHEL_DISTRO_VER = "0x0$(MAJVER)0$(MINVER)"
    else
      RHEL_DISTRO_VER = "0x0$(MAJVER)$(MINVER)"
    endif
  else
    ifeq ($(shell [ "$(MINVER)" -lt "10" ] || echo notfound),)
      RHEL_DISTRO_VER = "0x$(MAJVER)0$(MINVER)"
    else
      RHEL_DISTRO_VER = "0x$(MAJVER)$(MINVER)"
    endif
  endif

  # Check for specific RHEL versions for iSCSI and dual iSCSI stack support
  ifeq ($(shell [ "$(MAJVER)" = "5" ] || echo notfound),)
    ifeq ($(shell [ "$(MINVER)" -gt "3" ] || echo notfound),)
      BCM_ISCSI_BUILD=iscsibuild
      BCM_ISCSI_INSTALL=iscsiinstall
      BCM_ISCSI_CLEAN=iscsiclean
      BRCM_FLAGS += -D__RHEL_DISTRO__=$(RHEL_DISTRO_VER)
    endif
  else
    ifeq ($(shell [ "$(MAJVER)" -ge "6" ] || echo notfound),)
      BCM_ISCSI_BUILD=iscsibuild
      BCM_ISCSI_INSTALL=iscsiinstall
      BCM_ISCSI_CLEAN=iscsiclean
      BRCM_FLAGS += -D__RHEL_DISTRO__=$(RHEL_DISTRO_VER)
    endif
  endif
endif

ifeq ($(DISTRO), "SLES")
  # Check for specific SLES versions for iSCSI support
  SLES_DISTRO_VER = "0x$(VERSION)0$(PATCHLEVEL)"
  ifeq ($(shell [ "$(VERSION)" = "11" ] || echo notfound),)
    ifeq ($(shell [ "$(PATCHLEVEL)" -gt "0" ] || echo notfound),)
      BCM_ISCSI_BUILD=iscsibuild
      BCM_ISCSI_INSTALL=iscsiinstall
      BCM_ISCSI_CLEAN=iscsiclean
      BRCM_FLAGS += -D__SLES_DISTRO__=$(SLES_DISTRO_VER)
    endif
  else
    ifeq ($(shell [ "$(VERSION)" = "12" ] || echo notfound),)
      BCM_ISCSI_BUILD=iscsibuild
      BCM_ISCSI_INSTALL=iscsiinstall
      BCM_ISCSI_CLEAN=iscsiclean
      BRCM_FLAGS += -D__SLES_DISTRO__=$(SLES_DISTRO_VER)
    endif
  endif
endif

ifeq ($(shell awk '/(ep_connect)/,/;/ {printf $$0; next}' $(LINUXSRC)/include/scsi/scsi_transport_iscsi.h | awk -F ',' '{print NF}'),4)
  BRCM_FLAGS += -D_DEFINE_EP_CONNECT_IFACE_NUM_
endif

ifneq ($(shell grep get_ep_param $(LINUXSRC)/include/scsi/scsi_transport_iscsi.h > /dev/null 2>&1 && echo ep_param),)
  BRCM_FLAGS += -D_DEFINE_GET_EP_PARAM_
endif

ifneq ($(shell grep attr_is_visible $(LINUXSRC)/include/scsi/scsi_transport_iscsi.h > /dev/null 2>&1 && echo attr),)
  BRCM_FLAGS += -D_DEFINE_ATTR_IS_VISIBLE_
endif
ifneq ($(shell awk '/(attr_is_visible)/,/;/ {printf $$0; next}' $(LINUXSRC)/include/scsi/scsi_transport_iscsi.h | grep umode_t > /dev/null 2>&1 && echo umode),)
  BRCM_FLAGS += -D_DEFINE_ATTR_IS_VISIBLE_UMODE_
endif

ifneq ($(shell grep iscsi_scsi_req $(LINUXSRC)/include/scsi/iscsi_proto.h > /dev/null 2>&1 && echo scsi_req),)
  BRCM_FLAGS += -D_DEFINE_USE_SCSI_REQ_
endif

ifneq ($(shell grep frwd_lock $(LINUXSRC)/include/scsi/libiscsi.h > /dev/null 2>&1 && echo frwd_lock),)
  BRCM_FLAGS += -D_DEFINE_REDUCE_LOCK_CONTENTION_
endif

ifneq ($(shell grep cpuhp_setup_state $(LINUXSRC)/include/linux/cpuhotplug.h > /dev/null 2>&1 && echo cpuhp_setup_state),)
  BRCM_FLAGS += -D_DEFINE_USE_CPU_HP_
endif

ifneq ($(shell grep signal_pending $(LINUXSRC)/include/linux/sched/signal.h > /dev/null 2>&1 && echo signal_pending),)
  BRCM_FLAGS += -D_DEFINE_SCHED_HEADER_
endif

ifneq ($(shell grep "timer_setup" $(LINUXSRC)/include/linux/timer.h > /dev/null 2>&1 && echo timer_setup),)
  BRCM_FLAGS += -DHAVE_TIMER_SETUP
endif

ifneq ($(shell grep "mmiowb_spin_lock" $(LINUXSRC)/include/asm-generic/mmiowb.h > /dev/null 2>&1 && echo mmiowb_spin_lock),)
  BRCM_FLAGS += -D_DEFINE_MMIOWB_SPIN_LOCK_
endif

ifneq ($(shell grep "use_clustering" $(LINUXSRC)/include/scsi/scsi_host.h > /dev/null 2>&1 && echo use_clustering),)
  BRCM_FLAGS += -D_DEFINE_USE_CLUSTERING_
endif

ifneq ($(shell grep "ISCSI_PARAM_BOOT_ROOT" $(LINUXSRC)/include/scsi/iscsi_if.h > /dev/null 2>&1 && echo ISCSI_PARAM_BOOT_ROOT),)
  BRCM_FLAGS += -D_DEFINE_BOOT_PARAM_
endif

# Checks to see if we should use thunk-extern
ifeq ($(RHEL_DISTRO_VER), "0x0610")
  USE_THUNK_EXTERN = 1
endif

ifeq ($(DISTRO), "RHEL")
  ifeq ($(MAJVER), 7)
    ifneq ($(shell test $(MINVER) -gt 4 && echo thunk_extern),)
      USE_THUNK_EXTERN = 1
    endif
  endif
endif

ifeq ($(DISTRO), "RHEL")
  ifeq ($(MAJVER), 8)
      USE_THUNK_EXTERN = 1
   BRCM_FLAGS += -DRHEL8_DEBUGFS
  endif
endif

SUSE_BRAND = $(shell cat /etc/SUSE-brand 2>/dev/null | grep VERSION | sed 's/VERSION = //')
SUSE_PATCHLEVEL = $(shell cat /etc/SuSE-release 2>/dev/null | grep PATCHLEVEL | sed 's/PATCHLEVEL = //')
ifeq ($(SUSE_BRAND), 12)
  ifneq ($(shell test $(SUSE_PATCHLEVEL) -gt 3 && echo thunk_extern),)
    USE_THUNK_EXTERN = 1
  endif
endif
ifeq ($(SUSE_BRAND), 15)
  USE_THUNK_EXTERN = 1
endif

ifeq ($(USE_THUNK_EXTERN),1)
  THUNK_OPTIONS = -mindirect-branch=thunk-extern -mindirect-branch-register
else
  THUNK_OPTIONS = -mindirect-branch=thunk-inline -mindirect-branch-register -DTHUNK_INLINE
endif

# Check if compiler supports thunk options
ifneq ($(shell echo 'int main(){return 0;}' | gcc -x c $(THUNK_OPTIONS) - 2>/dev/null && echo thunk),)
  BRCM_FLAGS += $(THUNK_OPTIONS)
endif

default: build 

# check if 2.6 or newer kernel 

ifeq ($(shell [ "$(KERNVER)" = "2" -a $(KMAJVER) -ge 6 ] || echo notfound),)
BCM_DRV = bnx2i.ko
endif
ifeq ($(shell [ $(KERNVER) -gt 2 ] || echo notfound),)
BCM_DRV = bnx2i.ko
endif

ifneq ($(BCM_DRV),)
ifneq ($(KERNELRELEASE),)
bnx2i-objs := bnx2i_iscsi.o bnx2i_hwi.o bnx2i_init.o bnx2i_sysfs.o bnx2i_debugfs.o
obj-m += bnx2i.o
EXTRA_CFLAGS += -I${BNX2_CNIC_INC} -D_SYSFS_INCL_ ${BRCM_FLAGS}
else # ($(KERNELRELEASE),)
iscsibuild:
	make -C $(LINUX) SUBDIRS=$(shell pwd) modules
endif

else # BCM_DRV

# 2.4 kernel not supported
$(error iSCSI Offload not supported on 2.4 kernel)

endif # BCM_DRV

iscsiinstall:
	make -C $(LINUX) SUBDIRS=$(shell pwd) modules
	mkdir -p $(PREFIX)/$(BCMMODDIR)
	install -m 444 $(BCM_DRV) $(PREFIX)/$(BCMMODDIR)
	@if [ "$(PREFIX)" = "" ]; then /sbin/depmod -a $(KVER);\
	else echo " *** Run '/sbin/depmod -a' to update the module database.";\
	fi
	@if [ -f /boot/initramfs-$(KVER).img ] && [ "`lsinitrd /boot/initramfs-$(KVER).img | grep bnx2i.ko`" ] ; then\
		echo "back up initrd to /boot/initramfs-$(KVER).img.bak" ;\
		cp /boot/initramfs-$(KVER).img /boot/initramfs-$(KVER).img.bak ;\
		echo "add driver bnx2i to initrd" ;\
		dracut -f --add-drivers bnx2i ;\
	fi

iscsiclean:
	rm -f bnx2i.o bnx2i.mod.[co] bnx2i_iscsi.o bnx2i_iscsi.mod.[co]
	rm -rf bnx2i_init.o bnx2i_init.mod.[co]
	rm -rf .bnx2i*cmd bnx2i.ko .tmp_versions
	rm -rf .*.swp *.symvers *.order
	rm -rf bnx2i_hwi.o bnx2i_sysfs.o bnx2i_hwi.mod.[co] bnx2i_swi.mod.[co]
	rm -f bnx2i_debugfs.o bnx2i_debugfs.mod.[co]
	rm -rf bnx2i.sdtinfo.c  bnx2i.sdtstub.[oS] .bnx2i*.d .ctf

build:	$(BCM_ISCSI_BUILD)

install: $(BCM_ISCSI_INSTALL) 

print-%:
	@echo '$*=$($*)'

.PHONEY: all clean install

clean: $(BCM_ISCSI_CLEAN)

cscope:
	find . -name "*.[ch]" > cscope.files
	cscope -bp3

tags:
	ctags -R
