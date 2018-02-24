#!/usr/bin/make
# Makefile for building Linux QLogic 10-Gigabit ethernet driver as a module.

KVER =
ifeq ($(KVER),)
    KVER = $(shell uname -r)
endif

__ARCH=$(shell uname -m)

# PREFIX may be set by the RPM build to set the effective root.
PREFIX =
ifeq ($(shell ls /lib/modules/$(KVER)/build > /dev/null 2>&1 && echo build),)
# SuSE source RPMs
    _KVER=$(shell echo $(KVER) | cut -d "-" -f1,2)
    _KFLA=$(shell echo $(KVER) | cut -d "-" -f3)
    _ARCH=$(shell file -b /lib/modules/$(shell uname -r)/build | cut -d "/" -f5)
    ifeq ($(_ARCH),)
        _ARCH=$(__ARCH)
    endif
    ifeq ($(shell ls /usr/src/linux-$(_KVER)-obj > /dev/null 2>&1 && echo linux),)
      ifeq ($(shell ls /usr/src/kernels/$(KVER)-$(__ARCH) > /dev/null 2>&1 && echo linux),)
        LINUX=
      else
        LINUX=/usr/src/kernels/$(KVER)-$(__ARCH)
        LINUXSRC=$(LINUX)
        ifeq ($(KERNEL_DIR),)
            KERNEL_DIR=$(LINUX)
        endif
      endif
    else
        ifeq ($(KERNEL_DIR),)
            KERNEL_DIR=/usr/src/linux-$(_KVER)-obj/$(_ARCH)/$(_KFLA)
        endif
        LINUXSRC=/usr/src/linux-$(_KVER)
    endif
else
    BCMPROC := $(__ARCH)
    BCMCFGS := /lib/modules/$(KVER)/build \
               /usr/src/kernels/$(KVER)-$(BCMPROC) \
               /usr/src/linux-$(shell echo $(KVER) | sed "s|\(.*\)-\([a-z]*\)|\1-obj/$(BCMPROC)/\2|") \
               /usr/src/linux

    # Delete any entries that do not exist.
    BCMTSTDIR = $(shell if ls $(DIR) > /dev/null 2>&1; then echo $(DIR); fi)
    BCMCFGS := $(foreach DIR, $(BCMCFGS), $(BCMTSTDIR))

    ifeq ($(BCMCFGS),)
            $(error Linux kernel config directory not found)
    else
        ifeq ($(KERNEL_DIR),)
            KERNEL_DIR := $(firstword $(BCMCFGS))
	    ifeq ($(LINUXSRC),)
	        ifneq ($(shell ls /lib/modules/$(KVER)/source > /dev/null 2>&1 && echo source),)
		    LINUXSRC=/lib/modules/$(KVER)/source
	        endif
	    endif
        endif
    endif
endif

ifeq ($(LINUXSRC),)
   LINUXSRC=$(KERNEL_DIR)
endif

# paths
sles_distro := $(wildcard /etc/SuSE-release)
rhel_distro := $(wildcard /etc/redhat-release)
ubuntu_distro := $(shell lsb_release -is 2> /dev/null | grep Ubuntu)
ifeq ($(sles_distro)$(ubuntu_distro),)
    DISTRO_DRV_DIR = extra
else # SuSE/Ubuntu
    DISTRO_DRV_DIR = updates
endif
ifeq ($(shell ls /lib/modules/$(KVER)/$(DISTRO_DRV_DIR) > /dev/null 2>&1 && echo 1),1)
    DRV_DIR = $(DISTRO_DRV_DIR)/netxtreme2
else
    ifeq ($(shell grep -q "search.*[[:space:]]$(DISTRO_DRV_DIR)" /etc/depmod.conf > /dev/null 2>&1 && echo 1),1)
        DRV_DIR = $(DISTRO_DRV_DIR)/netxtreme2
    else
        ifeq ($(shell grep -q "search.*[[:space:]]$(DISTRO_DRV_DIR)" /etc/depmod.d/* > /dev/null 2>&1 && echo 1),1)
            DRV_DIR = $(DISTRO_DRV_DIR)/netxtreme2
        else
            ifeq ($(shell if [ $(shell echo $(KVER) | cut -d. -f1) -ge 3 -a $(shell echo $(KVER) | cut -d. -f2) -ge 2 ]; then echo 1; fi),1)
                DRV_DIR = kernel/drivers/net/ethernet/broadcom
            else # KVER < 3.2.0
                DRV_DIR = kernel/drivers/net
            endif
        endif
    endif
endif

ifneq ($(rhel_distro),)
RHEL_MAJVER := $(shell grep "RHEL_MAJOR" /usr/include/linux/version.h | sed -e 's/.*MAJOR \([0-9]\)/\1/')
RHEL_MINVER := $(shell grep "MINOR" /usr/include/linux/version.h | sed -e 's/.*MINOR \([0-9]\)/\1/')
endif

MAN_DIR = $(PREFIX)/usr/share/man/man4

IDLE_CHK_C = bnx2x_self_test.c
REG_DUMP_H = bnx2x_dump.h
REG_HSI_H = bnx2x_reg.h bnx2x_fw_defs.h bnx2x_hsi.h bnx2x_mfw_req.h
CNIC_INC = cnic_if.h
INIT_OPS_H = bnx2x_init.h bnx2x_init_ops.h
SP_VERBS = bnx2x_sp.c bnx2x_sp.h
HW_CHANNEL_H = bnx2x_vfpf.h

SOURCES_PF = bnx2x_main.c bnx2x_cmn.[ch] bnx2x_link.c bnx2x.h bnx2x_link.h bnx2x_compat.h $(INIT_OPS_H) bnx2x_fw_file_hdr.h bnx2x_dcb.[ch] $(SP_VERBS) bnx2x_stats.[ch] bnx2x_ethtool.c $(IDLE_CHK_C) bnx2x_sriov.[ch] bnx2x_vfpf.c bnx2x_debugfs.[ch]
INIT_VAL_C = bnx2x_init_values_e1.c bnx2x_init_values_e1h.c bnx2x_init_values_e2.c

CHAR_FILES_C = bnx2x_char_dev.c
CHAR_FILES_H = bnx2x_char_dev.h bnx2x_user_iface.h
CHAR_FILES = $(CHAR_FILES_C) $(CHAR_FILES_H)

MODULE_NAME = bnx2x
ifeq ($(NPA),1)
	override EXTRA_CFLAGS += -D__VMKNPA__
endif

DISABLE_FUN_CAST = -Wno-cast-function-type
ifneq ($(shell echo 'int main(){return 0;}' | gcc -x c $(DISABLE_FUN_CAST) - 2>/dev/null && echo $(DISABLE_FUN_CAST)),)
        override EXTRA_CFLAGS += $(DISABLE_FUN_CAST)
endif

override EXTRA_CFLAGS += -DDRV_MODULE_NAME=\"$(MODULE_NAME)\"

obj-m +=  $(MODULE_NAME).o
bnx2x-objs = bnx2x_main.o bnx2x_link.o bnx2x_cmn.o $(INIT_VAL_C:%.c=%.o) bnx2x_dcb.o bnx2x_sp.o bnx2x_stats.o bnx2x_ethtool.o bnx2x_self_test.o bnx2x_sriov.o bnx2x_vfpf.o bnx2x_debugfs.o

ifeq ($(DISABLE_WERROR),)
        override EXTRA_CFLAGS += -Werror
endif

ifeq ($(NX2_BOOT_DISK),1)
	override EXTRA_CFLAGS += -DBNX2X_DRIVER_DISK
endif

ifneq ($(sles_distro),)
SLES_VERSION = $(shell cat /etc/SuSE-release | grep VERSION | grep -o -P [0-9]+)
SLES_PATCHLEVEL = $(shell cat /etc/SuSE-release | grep PATCHLEVEL | grep -o -P [0-9]+)
PADDED_PATCHLEVEL = $(shell if [ 10 -gt $(SLES_PATCHLEVEL) ]; then echo 0$(SLES_PATCHLEVEL); else echo $(SLES_PATCHLEVEL); fi)
SLES_DISTRO_VER = "0x$(SLES_VERSION)$(PADDED_PATCHLEVEL)"
override EXTRA_CFLAGS += -DSLES_DISTRO=$(SLES_DISTRO_VER)
endif

# Checks to see if we should use thunk-extern
ifneq ($(rhel_distro),)
  ifeq ($(RHEL_MAJVER), 6)
    ifneq ($(shell test $(RHEL_MINVER) -gt 9 && echo thunk_extern),)
      USE_THUNK_EXTERN = 1
    endif
  endif
  ifeq ($(RHEL_MAJVER), 7)
    ifneq ($(shell test $(RHEL_MINVER) -gt 4 && echo thunk_extern),)
      USE_THUNK_EXTERN = 1
    endif
  endif
  ifeq ($(RHEL_MAJVER), 8)
    USE_THUNK_EXTERN = 1
  endif
  ifeq ($(RHEL_MAJVER), 9)
    USE_THUNK_EXTERN = 1
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

ifneq ($(shell echo 'int main(){return 0;}' | gcc -x c $(THUNK_OPTIONS) - 2>/dev/null && echo thunk),)
	override EXTRA_CFLAGS += $(THUNK_OPTIONS)
endif

ifeq ($(shell grep eth_zero_addr $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_zero_addr),)
	override EXTRA_CFLAGS += -D_DEFINE_ETH_ZERO_ADDR_
endif

ifeq ($(shell grep pcie_capability_read_word $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pcie_capability_read_word),)
	override EXTRA_CFLAGS += -D_DEFINE_PCIE_CAPABILITY_READ_WORD_
endif

ifeq ($(shell grep tcp_v6_check $(LINUXSRC)/include/net/ip6_checksum.h > /dev/null 2>&1 && echo tcp_v6_check),)
	override EXTRA_CFLAGS += -D_DEFINE_TCP_V6_CHECK_
endif

ifeq ($(shell grep "prandom_bytes" $(LINUXSRC)/include/linux/random.h > /dev/null 2>&1 && echo prandom_bytes),)
  ifeq ($(shell grep "prandom_bytes" $(LINUXSRC)/include/linux/prandom.h > /dev/null 2>&1 && echo prandom_bytes),)
	override EXTRA_CFLAGS += -D_DEFINE_PRANDOM_BYTES_
  endif
endif

ifeq ($(shell grep "pci_vfs_assigned" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_vfs_assigned),)
	override EXTRA_CFLAGS += -D_DEFINE_PCI_VFS_ASSIGNED_
endif

ifeq ($(shell grep "pci_num_vf" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_num_vf),)
	override EXTRA_CFLAGS += -D_DEFINE_PCI_NUM_VF_
endif

ifneq ($(shell grep -A3 "pci_driver_rh" $(LINUXSRC)/include/linux/pci.h | grep "sriov_configure" > /dev/null 2>&1 && echo pci_driver_rh),)
	override EXTRA_CFLAGS += -DHAS_SRIOV_PCI_DRIVER_RH
endif

ifneq ($(shell grep "sriov_configure" $(LINUXSRC)/include/linux/pci.h  > /dev/null 2>&1 && echo sriov_configure),)
	override EXTRA_CFLAGS += -DHAS_SRIOV_CONFIGURE
endif

ifneq ($(shell grep "ndo_fix_features" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo pci_driver_rh),)
	override EXTRA_CFLAGS += -DHAS_NDO_FIX_FEATURES
endif

ifeq ($(shell grep "pcie_get_minimum_link" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pcie_get_minimum_link),)
	override EXTRA_CFLAGS += -D_DEFINE_PCIE_GET_MIN_LINK_
endif

ifeq ($(shell ls $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo aer),)
	override EXTRA_CFLAGS += -D_DEFINE_AER
endif

ifeq ($(shell ls $(LINUXSRC)/include/linux/netdev_features.h > /dev/null 2>&1 && echo netdev_features),)
	override EXTRA_CFLAGS += -D_DEFINE_NETDEV_FEATURES_T
endif

ifeq ($(shell grep "pci_wait_for_pending_transaction" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_wait_for_pending_transaction),)
	override EXTRA_CFLAGS += -D_DEFINE_PCIE_WAIT_PEND_TRANS_
endif

ifeq ($(shell grep "netdev_phys_port_id" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netdev_phys_port_id),)
	override EXTRA_CFLAGS += -D_DEFINE_PHYS_PORT_ID
endif
ifeq ($(shell grep "netdev_phys_item_id" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netdev_phys_port_id),)
	override EXTRA_CFLAGS += -D_DEFINE_PHYS_ITEM_ID
endif


ifeq ($(shell grep "netif_get_num_default_rss_queues" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netif_get_num_default_rss_queues),)
	override EXTRA_CFLAGS += -D_DEFINE_NETIF_GET_NUM_DEFAULT_RSS_QUEUES
endif

ifeq ($(shell grep "pci_enable_msix_range" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_enable_msix_range),)
	override EXTRA_CFLAGS += -D_DEFINE_PCI_ENABLE_MSIX_RANGE
endif

ifeq ($(shell grep "SET_ETHTOOL_OPS" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo SET_ETHTOOL_OPS),)
	override EXTRA_CFLAGS += -D_DEFINE_SET_ETHTOOL_OPS
endif

ifeq ($(shell grep "IFLA_VF_RATE" $(LINUXSRC)/include/uapi/linux/if_link.h > /dev/null 2>&1 && echo IFLA_VF_RATE),)
	override EXTRA_CFLAGS += -D_DEFINE_IFLA_VF_RATE
endif

ifeq ($(shell grep "EPROBE_DEFER" $(LINUXSRC)/include/linux/errno.h > /dev/null 2>&1 && echo EPROBE_DEFER),)
	override EXTRA_CFLAGS += -D_DEFINE_EPROBE_DEFER
endif

ifneq ($(shell grep "SKBTX_HW_TSTAMP" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo ptp_clock_kernel),)
	override EXTRA_CFLAGS += -D_HAS_HW_TSTAMP
endif

ifeq ($(shell grep "__smp_mb__before_atomic" $(LINUXSRC)/include/linux/bitops.h > /dev/null 2>&1 && echo __smp_mb__before_atomic),)
	override EXTRA_CFLAGS += -D_DEFINE_SMP_MB_BEFORE_ATOMIC
endif

ifeq ($(shell grep "smp_mb__before_atomic" $(LINUXSRC)/include/asm-generic/barrier.h > /dev/null 2>&1 && echo smp_mb__before_atomic),)
	override EXTRA_CFLAGS += -D_DEFINE_SMP_MB_BEFORE_ATOMIC_V2
endif


ifeq ($(shell grep "ether_addr_equal" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo ether_addr_equal),)
	override EXTRA_CFLAGS += -D_DEFINE_ETHER_ADDR_EQUAL
endif

ifeq ($(shell grep "skb_mark_napi_id" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_SKB_MARK_NAPI_ID
endif

ifeq ($(shell grep "skb_set_hash" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_set_hash),)
	override EXTRA_CFLAGS += -D_DEFINE_SKB_SET_HASH
endif

ifeq ($(shell grep "netdev_name" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netdev_name),)
	override EXTRA_CFLAGS += -D_DEFINE_NETDEV_NAME
endif

ifeq ($(shell grep "netdev_tx_completed_queue" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netdev_tx_completed_queue),)
	override EXTRA_CFLAGS += -D_DEFINE_NETDEV_TX_COMPLETED_QUEUE
endif

ifneq ($(shell grep -B3 accel_priv $(LINUXSRC)/include/linux/netdevice.h | grep ndo_select_queue > /dev/null 2>&1 && echo has_accel_priv),)
	override EXTRA_CFLAGS += -DBNX2X_SELECTQUEUE_HAS_ACCEL_PARAM
endif
ifneq ($(shell grep -B4 fallback $(LINUXSRC)/include/linux/netdevice.h | grep ndo_select_queue > /dev/null 2>&1 && echo has_accel_priv),)
	override EXTRA_CFLAGS += -DBNX2X_SELECTQUEUE_HAS_FALLBACK_PARAM
endif

ifneq ($(shell grep -B4 sb_dev $(LINUXSRC)/include/linux/netdevice.h | grep ndo_select_queue > /dev/null 2>&1 && echo has_sb_dev),)
ifneq ($(shell grep -B4 fallback $(LINUXSRC)/include/linux/netdevice.h | grep ndo_select_queue > /dev/null 2>&1 && echo has_sb_dev),)
	override EXTRA_CFLAGS += -DBNX2X_SELECTQUEUE_HAS_FALLBACK_SBDEV_PARAM
else
	override EXTRA_CFLAGS += -DBNX2X_SELECTQUEUE_HAS_SBDEV_PARAM
endif
endif
ifneq ($(shell grep "ethtool_ops_ext" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo ethtool_ops_ext),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_OPS_EXT
endif

ifneq ($(shell grep "new_ethtool_ops" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structnew_ethtool_ops > /dev/null 2>&1 && echo new_ethtool_ops),)
	override EXTRA_CFLAGS += -D_HAS_NEW_ETHTOOL_OPS
endif

ifneq ($(shell grep -B50 "\*get_rxfh_indir_size" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_rxfh_indir_size),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_RXF_INDIR_SIZE
endif

ifneq ($(shell grep -B50 "\*get_rxfh_indir" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_rxfh_indir),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_RXF_INDIR
endif

ifneq ($(shell grep -B50 "\*set_rxfh_indir" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo set_rxfh_indir),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_SET_RXF_INDIR
endif

ifneq ($(shell grep -B50 "\*get_channels" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_channels),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_CHANNELS
endif

ifneq ($(shell grep -B50 "\*set_channels" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo set_channels),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_SET_CHANNELS
endif

ifneq ($(shell grep -B50 "\*get_dump_flag" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_dump_flag),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_DUMP_FLAG
endif

ifneq ($(shell grep -B50 "\*get_dump_data" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_dump_data),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_DUMP_DATA
endif

ifneq ($(shell grep -B50 "\*set_dump" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo set_dump),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_SET_DUMP
endif

ifneq ($(shell grep -B50 "\*get_module_info" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_module_info),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_MODULE_INFO
endif

ifneq ($(shell grep -B50 "\*get_module_eeprom" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_module_eeprom),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_MODULE_EEPROM
endif

ifneq ($(shell grep -B50 "\*set_phys_id" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo set_phys_id),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_SET_PHYS_ID
endif

ifneq ($(shell grep -B50 "\*get_eee" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_eee),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_GET_EEE
endif

ifneq ($(shell grep -B50 "\*set_eee" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo set_eee),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_SET_EEE
endif

ifneq ($(shell grep -B50 "\*get_ts_info" $(LINUXSRC)/include/linux/ethtool.h | sed 's/[ ,\t]//gi' | grep structethtool_ops_ext > /dev/null 2>&1 && echo get_ts_info),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_EXT_TS_INFO
endif

ifneq ($(shell grep "ndo_set_vf_link_state" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_set_vf_link_state),)
	override EXTRA_CFLAGS += -D_HAS_SET_VF_LINK_STATE
endif

ifneq ($(shell grep "ndo_set_vf_spoofchk" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_set_vf_spoofchk),)
	override EXTRA_CFLAGS += -D_HAS_SET_VF_SPOOFCHK
endif

ifneq ($(shell grep "clamp" $(LINUXSRC)/include/linux/kernel.h > /dev/null 2>&1 && echo clamp),)
	override EXTRA_CFLAGS += -D_HAS_CLAMP
endif

ifneq ($(shell grep "clamp" $(LINUXSRC)/include/linux/minmax.h > /dev/null 2>&1 && echo clamp),)
	override EXTRA_CFLAGS += -D_HAS_CLAMP
endif

ifneq ($(shell ls $(LINUXSRC)/include/net/dcbnl.h  > /dev/null 2>&1 && echo getnumtcs),)
ifneq ($(shell grep "getnumtcs" $(LINUXSRC)/include/net/dcbnl.h | cut -d'*' -f1 | grep int > /dev/null 2>&1 && echo getnumtcs),)
	override EXTRA_CFLAGS += -D_GETNUMTCS_RETURNS_INT
endif
endif

ifneq ($(shell grep "vlan_rx_add_vid" $(LINUXSRC)/include/linux/netdevice.h | cut -d'*' -f1 | grep void > /dev/null 2>&1 && echo vlan_rx_add_vid),)
	override EXTRA_CFLAGS += -D_VLAN_RX_ADD_VID_RETURNS_VOID
endif

ifneq ($(shell grep "ndo_setup_tc_rh" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_setup_tc_rh),)
ifneq ($(shell grep -B50 "\*ndo_setup_tc_rh" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_ops_extended > /dev/null 2>&1 && echo ndo_setup_tc_rh),)
	override EXTRA_CFLAGS += -D_HAS_NDO_EXT_SETUP_TC
endif
endif

ifneq ($(shell grep -A1 "ndo_setup_tc" $(LINUXSRC)/include/linux/netdevice.h | grep handle > /dev/null 2>&1 && echo ndo_setup_tc),)
	override EXTRA_CFLAGS += -D_HAS_NDO_SETUP_TC_HANDLE
endif

ifneq ($(shell grep -A1 "enum tc_setup_type" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo tc_setup_type),)
	override EXTRA_CFLAGS += -D_HAS_TC_SETUP_TYPE
endif

ifeq ($(shell grep -A1 "vlan_rx_add_vid" $(LINUXSRC)/include/linux/netdevice.h | grep proto > /dev/null 2>&1 && echo vlan_rx_add_vid),)
	override EXTRA_CFLAGS += -D_VLAN_RX_ADD_VID_NO_PROTO
endif

ifneq ($(shell grep "ndo_fix_features" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_fix_features),)
	override EXTRA_CFLAGS += -DHAS_NDO_FIX_FEATURES
endif

ifneq ($(shell grep "ETH_SS_RSS_HASH_FUNCS" $(LINUXSRC)/include/uapi/linux/ethtool.h > /dev/null 2>&1 && echo RSS_HASH),)
	override EXTRA_CFLAGS += -D_HAS_RSS_HASH_FUNCS
endif

ifneq ($(shell grep "get_link_ksettings" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo get_link_ksettings),)
ifneq ($(shell grep "ethtool_convert_link_mode_to_legacy_u32" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo ethtool_convert_link_mode_to_legacy_u32),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_GET_LINK_KSETTINGS
endif
endif

ifneq ($(shell grep "supported_coalesce_params" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo supported_coalesce),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_SUPPORTED_COALESCE_PARAMS
endif

ifeq ($(shell grep "netdev_rss_key_fill" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo rss_key_fill),)
	override EXTRA_CFLAGS += -D_DEFINE_RSS_KEY_FILL
endif

ifeq ($(shell grep "napi_schedule_irqoff" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo irqoff),)
	override EXTRA_CFLAGS += -D_DEFINE_NAPI_SCHEDULE_IRQOFF
endif

ifneq ($(shell grep "ndo_features_check" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_gso_check),)
	override EXTRA_CFLAGS += -D_HAS_NDO_FEATURES_CHECK
endif
ifneq ($(shell grep "vlan_features_check" $(LINUXSRC)/include/linux/if_vlan.h > /dev/null 2>&1 && echo ndo_gso_check),)
	override EXTRA_CFLAGS += -D_HAS_NDO_FEATURES_VLAN_CHECK
endif
ifneq ($(shell grep "vxlan_features_check" $(LINUXSRC)/include/net/vxlan.h > /dev/null 2>&1 && echo ndo_gso_check),)
	override EXTRA_CFLAGS += -D_HAS_NDO_FEATURES_VXLAN_CHECK
endif

ifneq ($(shell grep "napi_alloc_skb" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo napi_alloc_skb),)
	override EXTRA_CFLAGS += -D_HAS_NAPI_ALLOC_SKB
endif
ifeq ($(shell grep "CYCLECOUNTER_MASK" $(LINUXSRC)/include/linux/timecounter.h > /dev/null 2>&1 && echo CYCLECOUNTER_MASK),)
	override EXTRA_CFLAGS += -D_DEFINE_CYCLECOUNTER_MASK
endif
ifneq ($(shell grep "timecounter_adjtime" $(LINUXSRC)/include/linux/timecounter.h > /dev/null 2>&1 && echo timecounter_adjtime),)
	override EXTRA_CFLAGS += -D_HAS_TIMECOUNTER_ADJTIME
endif
ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/time64.h > /dev/null 2>&1 && echo timespec64),)
	override EXTRA_CFLAGS += -D_HAS_TIMESPEC64
endif
ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/time.h > /dev/null 2>&1 && echo timespec64),)
# In RH6.8 there's no time64.h
	override EXTRA_CFLAGS += -D_HAS_TIMESPEC64
endif
ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/ptp_clock_kernel.h > /dev/null 2>&1 && echo timespec64),)
	override EXTRA_CFLAGS += -D_HAS_CALLBACK_TIMESPEC64
endif
ifeq ($(shell grep "skb_vlan_tag_present" $(LINUXSRC)/include/linux/if_vlan.h > /dev/null 2>&1 && echo skb_vlan_tag),)
	override EXTRA_CFLAGS += -D_DEFINE_SKB_VLAN_TAG
endif

ifneq ($(shell grep -A2 "ndo_bridge_getlink" $(LINUXSRC)/include/linux/netdevice.h | grep nlflags > /dev/null 2>&1 && echo bridge_get_link),)
	override EXTRA_CFLAGS += -D_HAS_BRIDGE_GETLINK_V3
else
ifneq ($(shell grep -A2 "ndo_bridge_getlink" $(LINUXSRC)/include/linux/netdevice.h | grep filter_mask > /dev/null 2>&1 && echo bridge_get_link),)
	override EXTRA_CFLAGS += -D_HAS_BRIDGE_GETLINK_V2
endif
endif

ifneq ($(shell grep -A3 "ndo_dflt_bridge_getlink" $(LINUXSRC)/include/linux/rtnetlink.h | grep filter_mask > /dev/null 2>&1 && echo dflt_bridge_get_link),)
	override EXTRA_CFLAGS += -D_HAS_DFLT_BRIDGE_GETLINK_V4
else
ifneq ($(shell grep -A2 "ndo_dflt_bridge_getlink" $(LINUXSRC)/include/linux/rtnetlink.h | grep nlflags > /dev/null 2>&1 && echo dflt_bridge_get_link),)
	override EXTRA_CFLAGS += -D_HAS_DFLT_BRIDGE_GETLINK_V3
else
	ifneq ($(shell grep -A2 "ndo_dflt_bridge_getlink" $(LINUXSRC)/include/linux/rtnetlink.h | grep mask > /dev/null 2>&1 && echo dflt_bridge_get_link),)
	override EXTRA_CFLAGS += -D_HAS_DFLT_BRIDGE_GETLINK_V2
endif
endif
endif

ifeq ($(shell grep "dma_zalloc_coherent" $(LINUXSRC)/include/linux/dma-mapping.h > /dev/null 2>&1 && echo dma_zalloc_coherent),)
	override EXTRA_CFLAGS += -D_DEFINE_DMA_ZALLOC_COHERENT
endif

ifeq ($(shell grep "inner_ipv6_hdr" $(LINUXSRC)/include/linux/ipv6.h > /dev/null 2>&1 && echo inner_ipv6_hdr),)
	override EXTRA_CFLAGS += -D_DEFINE_INNER_IPV6_HDR
endif

ifeq ($(shell grep "inner_ip_hdr" $(LINUXSRC)/include/linux/ip.h > /dev/null 2>&1 && echo inner_ip_hdr),)
	override EXTRA_CFLAGS += -D_DEFINE_INNER_IP_HDR
endif

ifeq ($(shell grep "inner_tcp_hdr" $(LINUXSRC)/include/linux/tcp.h > /dev/null 2>&1 && echo inner_tcp_hdr),)
	override EXTRA_CFLAGS += -D_DEFINE_INNER_TCP_HDR
endif

ifneq ($(shell grep "needs_freset" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo boo),)
	override EXTRA_CFLAGS += -D_HAS_PCI_FRESET
endif

ifeq ($(shell grep "READ_ONCE" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
ifeq ($(shell grep "READ_ONCE" $(LINUXSRC)/include/asm-generic/rwonce.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_READ_ONCE
endif
endif

ifeq ($(shell grep "WRITE_ONCE" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
ifeq ($(shell grep "WRITE_ONCE" $(LINUXSRC)/include/asm-generic/rwonce.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_WRITE_ONCE
endif
endif

ifeq ($(shell grep "data_access_exceeds_word_size" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_DATA_ACCESS_EXCEEDS_WORD_SIZE
endif

ifneq ($(shell grep "ktime_get_real_seconds" $(LINUXSRC)/include/linux/timekeeping.h > /dev/null 2>&1 && echo timekeeping),)
	override EXTRA_CFLAGS += -D_HAS_KTIME_GET_REAL_SECONDS
endif

ifeq ($(shell grep "dma_set_mask_and_coherent" $(LINUXSRC)/include/linux/dma-mapping.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_DMA_SET_MASK_AND_COHERENT
endif

ifeq ($(shell grep "skb_free_frag" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_SKB_FREE_FRAG
endif

ifeq ($(shell grep "gfpflags_allow_blocking" $(LINUXSRC)/include/linux/gfp.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_GFP_ALLOW_BLOCKING
endif

ifneq ($(shell grep -B2 "napi_hash_add" $(LINUXSRC)/include/linux/netdevice.h | grep RX_BUSY_POLL > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_HAS_NAPI_HASH_AUTO
endif

ifneq ($(shell grep "ktime_get_boottime" $(LINUXSRC)/include/linux/timekeeping.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_HAS_BOOTTIME
endif

ifeq ($(shell grep "ktime_ms_delta" $(LINUXSRC)/include/linux/ktime.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_KTIME_MS_DELTA
endif

ifeq ($(shell grep "pci_channel_offline" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo XXX),)
	override EXTRA_CFLAGS += -D_DEFINE_CHANNEL_OFFLINE
endif

ifeq ($(shell grep "typedef u64 cycle_t" $(LINUXSRC)/include/linux/types.h > /dev/null 2>&1 && echo cycle_t),)
        override EXTRA_CFLAGS += -D_HAS_REMOVED_CYCLE_T
endif

ifneq ($(shell grep -A1 "ndo_size" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_size),)
	override EXTRA_CFLAGS += -D_HAS_NDO_SIZE_EXT_OPS
endif

ifneq ($(shell grep "ndo_change_mtu" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_change_mtu),)
ifneq ($(shell grep -B50 "\*ndo_change_mtu" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_ops_extended > /dev/null 2>&1 && echo ndo_change_mtu),)
	override EXTRA_CFLAGS += -D_HAS_NDO_EXT_CHANGE_MTU
endif
endif

ifneq ($(shell grep "max_mtu" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo max_mtu),)
ifneq ($(shell grep -B50 "max_mtu" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_extended > /dev/null 2>&1 && echo max_mtu),)
	override EXTRA_CFLAGS += -D_HAS_EXT_MAX_MTU
else
	override EXTRA_CFLAGS += -D_HAS_MAX_MTU
endif
endif

ifneq ($(shell grep "timer_setup" $(LINUXSRC)/include/linux/timer.h > /dev/null 2>&1 && echo timer_setup),)
	override EXTRA_CFLAGS += -D_HAS_TIMER_SETUP
endif

ifeq ($(shell grep "TC_SETUP_QDISC_MQPRIO" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo TC_SETUP_QDISC_MQPRIO),)
	override EXTRA_CFLAGS += -D_DEFINE_TC_SETUP_QDISC_MQPRIO
endif

ifeq ($(shell grep "simple_open" $(LINUXSRC)/include/linux/fs.h > /dev/null 2>&1 && echo simple_open),)
	override EXTRA_CFLAGS += -D_DEFINE_SIMPLE_OPEN
endif

ifneq ($(shell grep "i_private" $(LINUXSRC)/include/linux/fs.h > /dev/null 2>&1 && echo boo),)
	override EXTRA_CFLAGS += -D_HAS_I_PRIVATE
endif

ifneq ($(shell grep "debugfs_remove_recursive" $(LINUXSRC)/include/linux/debugfs.h > /dev/null 2>&1 && echo boo),)
	override EXTRA_CFLAGS += -D_HAS_DEBUGFS_REMOVE_RECURSIVE
endif

ifneq ($(shell grep "eth_hw_addr_set" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_hw_addr_set),)
        override EXTRA_CFLAGS += -D_HAS_ETH_HW_ADDR_SET
endif

ifneq ($(shell grep "pci_is_pcie" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_is_pcie),)
        override EXTRA_CFLAGS += -D_HAS_PCI_IS_PCIE
endif

ifneq ($(shell grep  "netif_napi_add_weight" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo add_weight),)
        override EXTRA_CFLAGS += -D_HAS_NETIF_NAPI_ADD_WEIGHT
endif

ifneq ($(shell grep "bool" $(LINUXSRC)/include/linux/types.h > /dev/null 2>&1 && echo bool),)
        override EXTRA_CFLAGS += -D_HAS_BOOL
endif

ifneq ($(shell grep "kernel_ethtool_ringparam" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo kernel_ethtool_ringparam),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_KERNEL_ERING
endif

ifeq ($(shell grep "eth_random_addr" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 1>&1 && echo XXX),)
        override EXTRA_CFLAGS += -D_DEFINE_ETH_RANDOM_ADDR
endif
ifneq ($(shell grep "eth_random_addr" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_random_addr),)
	override EXTRA_CFLAGS += -D_HAS_ETH_RANDOM_ADDR
endif
ifneq ($(shell grep "get_random_bytes" $(LINUXSRC)/include/linux/random.h > /dev/null 2>&1 && echo get_random_bytes),)
	override EXTRA_CFLAGS += -D_HAS_ETH_RANDOM_BYTES
endif

ifeq ($(shell grep "pci_vpd_find_tag" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_vpd_find_tag),)
        override EXTRA_CFLAGS += -D_DEFINE_PCI_VPD_FIND_TAG
endif

ifneq ($(shell grep "pci_vpd_find_ro_info_keyword" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_vpd_find_ro_info_keyword),)
        override EXTRA_CFLAGS += -D_HAS_PCI_VPD_FIND_RO_KW
endif

ifneq ($(shell grep "pci_vpd_find_tag" $(LINUXSRC)/include/linux/pci.h  | grep off > /dev/null 2>&1 && echo xxx),)
        override EXTRA_CFLAGS += -D_HAS_PCI_VPD_FIND_TAG_OFFSET
endif

ifneq ($(shell sed -n -E "/\(\*get_coalesce\)/,/\;/p" $(LINUXSRC)/include/linux/ethtool.h 2>&1 | grep kernel_ethtool_coalesce > /dev/null 2>&1 && echo kernel_ethtool_coalesce),)
 	override EXTRA_CFLAGS += -D_HAS_KERNEL_ETHTOOL_COALESCE
endif

ifneq ($(shell grep "struct_group" $(LINUXSRC)/include/linux/stddef.h > /dev/null 2>&1 && echo pci_vpd_find_ro_info_keyword),)
        override EXTRA_CFLAGS += -D_HAS_STRUCT_GROUP
endif

ifneq ($(shell sed -n -E "/\(\*adjfreq\)/,/\;/p" $(LINUXSRC)/include/linux/ptp_clock_kernel.h 2>&1  | grep adjfreq > /dev/null 2>&1 && echo ptp_adjfreq),)
        override EXTRA_CFLAGS += -D_HAS_ADJFREQ
endif

ifneq ($(shell grep "local_clock(void)" $(LINUXSRC)/include/linux/sched/clock.h > /dev/null 2>&1 && echo new_local_clock),)
        override EXTRA_CFLAGS += -D_HAS_NEW_LOCAL_CLOCK
endif

ifneq ($(shell grep "DEFINE_SEMAPHORE" $(LINUXSRC)/include/linux/semaphore.h 2>&1 | grep , > /dev/null 2>&1 && echo new_define_semaphore),)
       override EXTRA_CFLAGS += -D_HAS_NEW_DEFINE_SEMAPHORE
endif

ifneq ($(shell grep "pci_disable_pcie_error_reporting" $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo pci_disable_pcie_error_reporting),)
        override EXTRA_CFLAGS += -D_HAS_PCI_ERR_REPORT_REQ
endif

ifneq ($(shell grep "struct ethtool_rxfh_param" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo ethtool_rxfh_param),)
	override EXTRA_CFLAGS += -D_HAS_ETHTOOL_RXFH_PARAM
endif

ifneq ($(shell grep "UIO_MEM_DMA_COHERENT" $(LINUXSRC)/include/linux/uio_driver.h > /dev/null 2>&1 && echo UIO_MEM_DMA_COHERENT),)
	override EXTRA_CFLAGS += -D_HAS_UIO_MEM_DMA
endif

ifneq ($(shell grep "struct ethtool_keee" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo ethtool_keee),)
       override EXTRA_CFLAGS += -D_HAS_ETHTOOL_KEEE
endif

rh_distro := $(wildcard /etc/redhat-release)
ifneq ($(rh_distro),)
XENSERVER_EXIST = $(shell cat /etc/redhat-release | grep XenServer)
ifneq ($(XENSERVER_EXIST),)
XENSERVER_DISTRO_MAIN = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=1)
XENSERVER_DISTRO_SUB = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=2 | tail --lines=1)
XENSERVER_DISTRO_SUBB = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=3 | tail --lines=1)
XENSERVER_DISTRO_VER = $(shell echo "$(XENSERVER_DISTRO_MAIN) * 65536 + $(XENSERVER_DISTRO_SUB) * 256 + $(XENSERVER_DISTRO_SUBB)" | bc)
override EXTRA_CFLAGS += -DXENSERVER_DISTRO=$(XENSERVER_DISTRO_VER)
endif
endif

ifneq ($(shell grep "ndo_udp_tunnel_add" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_udp_tunnel_add),)
ifneq ($(shell grep -B50 "\*ndo_udp_tunnel_add" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_ops_extended > /dev/null 2>&1 && echo set_vf_trust),)
	override EXTRA_CFLAGS += -D_HAS_NDO_EXT_UDP_TUNNEL_CONFIG
else
	override EXTRA_CFLAGS += -D_HAS_NDO_UDP_TUNNEL_CONFIG
endif
else
ifdef CONFIG_VXLAN
ifneq ($(shell grep "ndo_add_vxlan_port" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo add_vxlan_port),)
        override EXTRA_CFLAGS += -DCONFIG_BNX2X_VXLAN
endif
endif

ifdef CONFIG_GENEVE
# ~4.6.0 CONFIG_BNX2X_GENEVE was added as a Kconfig option.
# Problem is that while Makefile knows whether a config option is set or not,
# it has no way of knowing whether a given symbol can exist - so it can't
# differentiate between new kernels where it exists and is disabled by user,
# and between older kernels where the functionality [ndo] exists but
# CONFIG_BNX2X_GENEVE symbol is yet to be introduced.
# The compromise - OOB would disregard the config option in the .config file
# in case the geneve ndo feature exists.
ifneq ($(shell grep "ndo_add_geneve_port" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo add_vxlan_port),)
	override EXTRA_CFLAGS += -DCONFIG_BNX2X_GENEVE
endif
endif
endif

ifneq ($(shell grep -A1 "set_vf_vlan" $(LINUXSRC)/include/linux/netdevice.h | grep "proto" > /dev/null 2>&1 && echo set_vf_vlan_proto),)
ifneq ($(shell grep -B50 "\*ndo_set_vf_vlan" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_ops_extended > /dev/null 2>&1 && echo set_vf_trust),)
	override EXTRA_CFLAGS += -D_HAS_NDO_EXT_VF_VLAN_PROTO
else
	override EXTRA_CFLAGS += -D_HAS_NDO_VF_VLAN_PROTO
endif
endif

ifneq ($(shell grep -A1 "ndo_tx_timeout" $(LINUXSRC)/include/linux/netdevice.h | grep "txqueue" > /dev/null 2>&1 && echo ndo_tx_timeout_txqueue),)
	override EXTRA_CFLAGS += -D_HAS_NDO_TX_TIMEOUT_TXQUEQUE
endif

ifneq ($(shell grep "pci_cleanup_aer_uncorrect_error_status" $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo pci_cleanup_aer_uncorrect_error_status),)
	override EXTRA_CFLAGS += -D_HAS_CLEANUP_AER_ERROR
endif

ifeq ($(SOE),1)
override EXTRA_CFLAGS += -DBNX2X_STOP_ON_ERROR
endif

ifeq ($(NSTS),1)
override EXTRA_CFLAGS += -DBNX2X_NO_STATS
endif

ifeq ($(ALLOW_LB),1)
override EXTRA_CFLAGS += -DBNX2X_ALLOW_LB
endif

ifeq ($(CHAR_BUILD),1)
override EXTRA_CFLAGS += -DBNX2X_CHAR_DEV
bnx2x-objs += $(CHAR_FILES_C:%.c=%.o)
SOURCES_PF += $(CHAR_FILES)
endif


# targets and rules
.PHONY: all modules_install docs_install clean install

all default subsystem: $(obj-m)

soe:
	$(MAKE) SOE=1

nsts:
	$(MAKE) NSTS=1

char:
	$(MAKE) CHAR_BUILD=1

$(obj-m): $(SOURCES_PF) $(INIT_VAL_C) $(REG_DUMP_H) $(REG_HSI_H) $(CNIC_INC) $(HW_CHANNEL_H)
	$(MAKE) -C $(KERNEL_DIR) M=`pwd` modules

modules_install: $(obj-m)
	$(MAKE) -C $(KERNEL_DIR) M=`pwd` INSTALL_MOD_PATH=$(PREFIX) INSTALL_MOD_DIR=$(DRV_DIR) modules_install
	@if [ "$(sles_distro)" = "" -a "$(ubuntu_distro)" = "" ]; then\
		echo "override bnx2x * $(DRV_DIR)" > bnx2x.conf ;\
		install -m 644 -D bnx2x.conf $(PREFIX)/etc/depmod.d/bnx2x.conf ;\
	fi
	@if [ "$(PREFIX)" = "" ] ;\
		then /sbin/depmod -a ;\
		else echo " *** Run '/sbin/depmod -a' to update the module database." ;\
	fi
	@if [ -f /boot/initramfs-$(KVER).img ] && [ "`lsinitrd /boot/initramfs-$(KVER).img | grep bnx2x.ko`" ] ; then\
		echo "back up initrd to /boot/initramfs-$(KVER).img.bak" ;\
		cp /boot/initramfs-$(KVER).img /boot/initramfs-$(KVER).img.bak ;\
		echo "add driver bnx2x to initrd" ;\
		dracut -f --add-drivers bnx2x ;\
	fi

docs_install:
	gzip -c bnx2x.4 > bnx2x.4.gz
	install -m 644 bnx2x.4.gz $(MAN_DIR)

clean:
	$(MAKE) -C $(KERNEL_DIR) M=`pwd` clean
	@rm -f *.fw *.ihex

install: modules_install docs_install

# generate the include files
-include Makefile.gen
