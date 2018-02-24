#!/usr/bin/make
# Makefile for building Linux QLogic Gigabit L5 driver as a module.
# $id$
KVER=
ifeq ($(KVER),)
  KVER=$(shell uname -r)
endif

# PREFIX may be set by the RPM build to set the effective root.
PREFIX=

ifeq ($(FC2_INCLUDE),)
  # Obtain the current working directory.  $(PWD) doesn't work because this
  # makefile cannot override the $(PWD) definition of the parent makefile.
  BCMPWD = $(shell pwd)
  ifeq ($(shell test -e $(BCMPWD)/../../fclibs/include > /dev/null 2>&1 || echo notfound),)
    FC2_INCLUDE := $(BCMPWD)/../../fclibs/include
  else
      $(error fclibs package not found. $(BCMPWD))
  endif
  export FC2_INCLUDE
endif

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

ifeq ($(shell grep scsi_get_resid $(LINUXSRC)/include/scsi/*.h > /dev/null 2>&1 && echo resid_defined),)
  BRCM_FLAGS += -D_DEFINE_SCSI_GET_RESID
endif

ifeq ($(shell grep scsi_set_resid $(LINUXSRC)/include/scsi/*.h > /dev/null 2>&1 && echo resid_defined),)
  BRCM_FLAGS += -D_DEFINE_SCSI_SET_RESID
endif

# check if 2.4 kernel or 2.5+ kernel
BCM_KVER:=$(shell echo $(KVER) | cut -c1-3 | sed 's/2\.[56]/2\.6/')

KBUILD_EXTRA_SYMBOLS := $(BNX2_CNIC_INC)/Module.symvers

ifeq ($(shell [ "$(BCM_KVER)" = "2.6" ] || [ "$(BCM_KVER)" = "3.0" ]),)
BCM_DRV = bnx2fc.ko


# Check distro/kernel compatibility
ifeq ($(INCDIR),)
    INCDIR:=$(CURDIR)
endif

include $(INCDIR)/version.mk
DISTRO_COMPATIBLE="no"

# If we are RHEL or UEK set RHEL_COMPAT as yes
RHEL_COMPAT="no"
ifeq ($(DISTRO), "RHEL")
  RHEL_COMPAT="yes"
endif
ifeq ($(DISTRO), "UEK")
  RHEL_COMPAT="yes"
endif
ifeq ($(DISTRO), "Citrix")
  RHEL_COMPAT="yes"
endif

ifeq ($(RHEL_COMPAT), "yes")
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

    ifeq ($(shell [ "$(MAJVER)" = "6" ] || echo notfound),)
        ifeq ($(shell [ "$(MINVER)" = "1" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_RHEL__=$(RHEL_DISTRO_VER)
            export BNX2FC_SUP
        endif
        ifeq ($(shell [ "$(MINVER)" -ge "2" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_RHEL__=$(RHEL_DISTRO_VER)
            export BNX2FC_SUP
        endif
    else
        ifeq ($(shell [ "$(MAJVER)" -ge "7" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_RHEL__=$(RHEL_DISTRO_VER)
            export BNX2FC_SUP
        endif
    endif

    ifeq ($(shell [ "$(MAJVER)" = "8" ] || echo notfound),)
        ifeq ($(shell [ "$(MINVER)" -ge "2" ] || echo notfound),)
            BRCM_FLAGS += -D_RHEL82
        endif
    endif
endif

ifeq ($(DISTRO), "UEK")
  DISTRO_COMPATIBLE="yes"
  BNX2FC_SUP = -D__BNX2FC_RHEL__=$(RHEL_DISTRO_VER) -D__BNX2FC_UEK__
  export BNX2FC_SUP
endif

ifeq ($(DISTRO), "Citrix")
  DISTRO_COMPATIBLE="yes"
  BNX2FC_SUP = -D__BNX2FC_RHEL__=$(RHEL_DISTRO_VER) -D__BNX2FC_CITRIX__
  export BNX2FC_SUP
endif

ifeq ($(DISTRO), "Ubuntu")
  DISTRO_COMPATIBLE="yes"
  BNX2FC_SUP = -D__BNX2FC_UBUNTU__
  export BNX2FC_SUP
endif

ifeq ($(DISTRO), "SLES")
    SLES_DISTRO_VER = "0x$(MAJVER)0$(MINVER)"
    ifeq ($(shell [ "$(MAJVER)" = "11" ] || echo notfound),)
        ifeq ($(shell [ "$(MINVER)" = "1" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_SLES__=$(SLES_DISTRO_VER) -I$(FC2_INCLUDE)
            export BNX2FC_SUP
        endif
        ifeq ($(shell [ "$(MINVER)" -ge "2" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_SLES__=$(SLES_DISTRO_VER)
            export BNX2FC_SUP
        endif
    else
        ifeq ($(shell [ "$(MAJVER)" -ge "12" ] || echo notfound),)
            DISTRO_COMPATIBLE="yes"
            BNX2FC_SUP = -D__BNX2FC_SLES__=$(SLES_DISTRO_VER)
            export BNX2FC_SUP
        endif
    endif
    ifeq ($(shell [ "$(MAJVER)" = "15" ] || echo notfound),)
            ifeq ($(shell [ "$(MINVER)" = "0" ] || echo notfound),)
                SLES_VER := $(shell grep -o -m 1 15-SP2 /etc/os-release | cut -d "=" -f2)
                ifeq ($(shell [ "$(SLES_VER)" == "15-SP2" ] || echo notfound),)
                    BRCM_FLAGS += -D_SLES15_SP2
                endif
            endif
    endif
endif

# For upstream there is no distro info. So RHEL or SLES is not detected. Set it
# based on the environment variable.  Run "BNX2FC_KERNEL_OVERRIDE=1 make"
ifeq ($(shell [ "$(DISTRO_COMPATIBLE)" = "no" ] || echo notfound),)
    ifeq ($(shell [ "$(BNX2FC_KERNEL_OVERRIDE)" = "1" ] || echo notfound),)
        DISTRO_COMPATIBLE="yes"
    else
        $(warning "Cannot compile bnx2fc on $(DISTRO) $(KVER)")
    endif
endif

ifneq ($(shell ls $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo libfc),)
  ifneq ($(shell grep fcoe_dev_stats $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo fcoe_dev_stats),)
    BRCM_FLAGS += -D_DEFINE_FCOE_DEV_STATS_
  endif
  ifneq ($(shell grep "fc_disc_config" $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo fc_disc_config),)
    BRCM_FLAGS += -D_DEFINE_FC_DISC_CONFIG_
  endif
endif

ifneq ($(shell ls $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo libfcoe),)
  ifneq ($(shell grep "*alloc" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo fcoe_transport_alloc),)
    BRCM_FLAGS += -D_DEFINE_FCOE_TRANSPORT_ALLOC_
  endif
  ifneq ($(shell grep "get_netdev" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo get_netdev),)
    BRCM_FLAGS += -D_DEFINE_PORT_GET_NETDEV_
  endif
  ifneq ($(shell grep "*cdev" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo fcoe_ctlr_cdev),)
    BRCM_FLAGS += -D_DEFINE_FCOE_CTLR_CDEV_
  endif
endif

ifneq ($(shell ls $(LINUXSRC)/include/scsi/fcoe_sysfs.h > /dev/null 2>&1 && echo fcoe_sysfs),)
  ifneq ($(shell grep fcoe_sysfs_function_template $(LINUXSRC)/include/scsi/fcoe_sysfs.h > /dev/null 2>&1 && echo fcoe_sysfs_func),)
    BRCM_FLAGS += -D_DEFINE_FCOE_SYSFS_
  endif
  ifneq ($(shell grep set_fcoe_ctlr_enabled $(LINUXSRC)/include/scsi/fcoe_sysfs.h > /dev/null 2>&1 && echo set_fcoe_ctlr_enabled),)
    BRCM_FLAGS += -D_DEFINE_USE_FCOE_SYSFS_CTLR_IF_
  endif
endif

ifneq ($(shell grep "fc_lun\[8\]" $(LINUXSRC)/include/scsi/fc/fc_fcp.h > /dev/null 2>&1 && echo fc_lun),)
  BRCM_FLAGS += -D_DEFINE_FCP_LUN_ARRAY_
endif

ifeq ($(shell awk '/(*kmap_atomic)/,/;/ {printf $$0; next}' $(LINUXSRC)/include/linux/highmem.h | awk -F ',' '{print NF}'),1)
  BRCM_FLAGS += -D_DEFINE_KMAP_ATOMIC_
endif

ifneq ($(shell grep "__ethtool_get_settings" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo ethtool),)
  BRCM_FLAGS += -D_DEFINE_ETHTOOL_GET_
endif

ifneq ($(shell grep "lockless" $(LINUXSRC)/include/scsi/scsi_host.h > /dev/null 2>&1 && echo lockless),)
  BRCM_FLAGS += -D_DEFINE_SHOST_LOCKLESS_
endif

ifneq ($(shell grep "dev_get_stats" $(LINUXSRC)/include/linux/netdevice.h | head -n 1 | grep "," > /dev/null 2>&1 && echo dev),)
  BRCM_FLAGS += -D_DEFINE_DEV_GET_STATS_
endif

ifneq ($(shell grep "fcoe_link_speed_update" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo fcoe_link_speed_update),)
  BRCM_FLAGS += -D_DEFINE_FCOE_LINK_SPEED_UPDATE_
endif

ifneq ($(shell grep "enum fip_mode" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo fip_mode),)
  BRCM_FLAGS += -D_FIP_MODE_ENUM_
endif

ifneq ($(shell grep "fc_rport_init" $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo fc_rport_init),)
  BRCM_FLAGS += -D_DEFINE_FC_RPORT_INIT_
endif

ifneq ($(shell grep "fc_rport_login" $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo fc_rport_login),)
  BRCM_FLAGS += -D_DEFINE_FC_RPORT_LOGIN_
endif

ifneq ($(shell grep "fc_rport_logoff" $(LINUXSRC)/include/scsi/libfc.h > /dev/null 2>&1 && echo fc_rport_logoff),)
  BRCM_FLAGS += -D_DEFINE_FC_RPORT_LOGOFF_
endif

# Only occurance of "kthread" in libfcoe.h is for our per cpu I/O thread; otherwise the name is just "thread"
ifneq ($(shell grep "kthread" $(LINUXSRC)/include/scsi/libfcoe.h > /dev/null 2>&1 && echo fc_rport_init),)
  BRCM_FLAGS += -D_DEFINE_FC_PERCPU_KTHREAD_
endif

ifneq ($(shell grep "cpuhp_setup_state" $(LINUXSRC)/include/linux/cpuhotplug.h > /dev/null 2>&1 && echo cpuhp_setup_state),)
  BRCM_FLAGS += -D_DEFINE_CPUHP_SETUP_STATE_
endif

ifneq ($(shell grep "timer_setup" $(LINUXSRC)/include/linux/timer.h > /dev/null 2>&1 && echo timer_setup),)
  BRCM_FLAGS += -D_DEFINE_TIMER_SETUP_
endif

ifneq ($(shell grep "mmiowb_spin_lock" $(LINUXSRC)/include/asm-generic/mmiowb.h > /dev/null 2>&1 && echo mmiowb_spin_lock),)
  BRCM_FLAGS += -D_DEFINE_MMIOWB_SPIN_LOCK_
endif

ifneq ($(shell grep "use_clustering" $(LINUXSRC)/include/scsi/scsi_host.h > /dev/null 2>&1 && echo use_clustering),)
  BRCM_FLAGS += -D_DEFINE_USE_CLUSTERING_
endif

ifneq ($(shell grep "skb_frag_struct" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_frag_struct),)
  BRCM_FLAGS += -D_DEFINE_SKB_FRAG_
endif

ifneq ($(shell grep "alloc_workqueue" $(LINUXSRC)/include/linux/workqueue.h > /dev/null 2>&1 && echo alloc_workqueue),)
  BRCM_FLAGS += -D_WORKQUEUE_ENABLED
endif

ifneq ($(shell grep "*scsi_done" $(LINUXSRC)/include/scsi/scsi_cmnd.h > /dev/null 2>&1 && echo *scsi_done),)
  BRCM_FLAGS += -D_CMD_SCSI_DONE_
endif

ifneq ($(shell grep "shost_attrs" $(LINUXSRC)/include/scsi/scsi_host.h > /dev/null 2>&1 && echo shost_attrs),)
  BRCM_FLAGS += -D_SHOST_ATTRS_
endif

ifneq ($(shell grep "cmd_size" $(LINUXSRC)/include/scsi/scsi_host.h > /dev/null 2>&1 && echo cmd_size),)
  BRCM_FLAGS += -D_CMD_SIZE_
endif

ifeq ($(wildcard $(LINUXSRC)/include/scsi/fc_encode.h),)
  BRCM_FLAGS += -D_INCLUDE_ENCODE_
endif

ifeq ($(DISTRO_COMPATIBLE)$(KERNEL_COMPATIBLE), "yes""yes")
    BCM_BNX2FC_PFC=bnx2fc_pfc
    BCM_BNX2FC_BUILD=bnx2fc_build
    BCM_BNX2FC_CLEAN=bnx2fc_clean
    BCM_BNX2FC_INSTALL=bnx2fc_install
endif

ifneq ($(shell grep "kref_read" $(LINUXSRC)/include/linux/kref.h > /dev/null 2>&1 && echo kref_read),)
  BRCM_FLAGS += -D_KREF_READ_
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
  endif
endif

ifeq ($(DISTRO), "RHEL")
  ifeq ($(MAJVER), 9)
      USE_THUNK_EXTERN = 1
  endif
endif

SUSE_BRAND = $(shell cat /etc/SUSE-brand 2>/dev/null | grep VERSION | sed 's/VERSION = //')
ifeq ($(SUSE_BRAND), 12)
  ifneq ($(shell test $(MINVER) -gt 3 && echo thunk_extern),)
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

ifneq ($(KERNELRELEASE),)
bnx2fc-objs := bnx2fc_fcoe.o bnx2fc_hwi.o bnx2fc_io.o bnx2fc_tgt.o bnx2fc_els.o bnx2fc_debugfs.o
obj-m += bnx2fc.o
EXTRA_CFLAGS += ${BRCM_FLAGS} ${BNX2FC_SUP} -g
else # ($(KERNELRELEASE),)
bnx2fc_build:
	make -C $(LINUX) M=$(shell pwd) modules INCDIR=$(INCDIR)
endif

else # ifeq ($(BCM_KVER),2.6)
  $(error bnx2fc is not supported on 2.4 kernel)
endif # ifeq ($(BCM_KVER),2.6)

bnx2fc_pfc:
	$(MAKE) PFC=1

bnx2fc_install:
	mkdir -p $(PREFIX)/lib/modules/$(KVER)/kernel/drivers/scsi;
	mkdir -p $(PREFIX)/lib/modules/$(KVER)/override;
	install -m 444 $(BCM_DRV) $(PREFIX)/lib/modules/$(KVER)/override;
	@if [ "$(PREFIX)" = "" ]; then /sbin/depmod -a ;\
	else echo " *** Run '/sbin/depmod -a' to update the module database.";\
	fi
	@if [ -f /boot/initramfs-$(KVER).img ] && [ "`lsinitrd /boot/initramfs-$(KVER).img | grep bnx2fc.ko`" ] ; then\
		echo "back up initrd to /boot/initramfs-$(KVER).img.bak" ;\
		cp /boot/initramfs-$(KVER).img /boot/initramfs-$(KVER).img.bak ;\
		echo "add driver bnx2fc to initrd" ;\
		dracut -f --add-drivers bnx2fc ;\
	fi

.PHONY: all clean install

bnx2fc_clean:
	rm -f bnx2fc.o bnx2fc.mod.[co] bnx2fc_fcoe.o bnx2fc_fcoe.mod.[co]
	rm -rf .*.swp
	rm -rf .ctf
	rm -rf bnx2fc_hwi.o bnx2fc_hwi.mod.[co] 
	rm -rf bnx2fc_els.o bnx2fc_els.mod.[co] 
	rm -rf bnx2fc_tgt.o bnx2fc_tgt.mod.[co] 
	rm -rf bnx2fc_io.o bnx2fc_io.mod.[co] 
	rm -f *.ko .*.cmd *sdtstub* .*sdtstub* *sdtinfo*

pfc: $(BCM_BNX2FC_PFC)
build: $(BCM_BNX2FC_BUILD)
clean: $(BCM_BNX2FC_CLEAN)
install: build $(BCM_BNX2FC_INSTALL)


cscope:
	find . -name "*.[ch]" > cscope.files
	cscope -bp3

tags:
	ctags -R
