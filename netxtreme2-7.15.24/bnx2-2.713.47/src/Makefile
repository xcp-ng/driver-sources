#!/usr/bin/make
# Makefile for building Linux QLogic Gigabit ethernet driver as a module.
# $id$
#
rhel_distro := $(wildcard /etc/redhat-release)

KVER=
ifeq ($(KVER),)
  KVER=$(shell uname -r)
endif

__ARCH=$(shell uname -m)

# PREFIX may be set by the RPM build to set the effective root.
PREFIX=
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

ifeq ($(shell ls $(LINUXSRC)/include/uapi > /dev/null 2>&1 && echo uapi),)
  UAPI=
else
  UAPI=uapi
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
        BCMMODDIR=/lib/modules/$(KVER)/kernel/drivers/net
      endif
    endif
  endif
endif

ifneq ($(shell grep netdump_mode $(LINUXSRC)/include/linux/kernel.h > /dev/null 2>&1 && echo rh),)
  NETDUMP_CFLAG = -DRED_HAT_LINUX_KERNEL
else
  NETDUMP_CFLAG =
endif

ifneq ($(shell grep skb_transport_offset $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo sk),)
  SKB_CFLAG = -DNEW_SKB
else
  SKB_CFLAG =
endif

ifneq ($(shell grep "static inline struct iphdr \*ip_hdr" $(LINUXSRC)/include/linux/ip.h > /dev/null 2>&1 && echo ip),)
  IP_CFLAG = -DHAVE_IP_HDR
else
  IP_CFLAG =
endif

ifneq ($(shell grep "__le32" $(LINUXSRC)/include/linux/types.h > /dev/null 2>&1 && echo le32),)
  LE32_CFLAG = -DHAVE_LE32
else
   ifneq ($(shell grep "__le32" $(LINUXSRC)/include/uapi/linux/types.h > /dev/null 2>&1 && echo le32),)
      LE32_CFLAG = -DHAVE_LE32
   else
      LE32_CFLAG =
   endif
endif

ifneq ($(shell grep "gfp_t" $(LINUXSRC)/include/linux/types.h > /dev/null 2>&1 && echo gfp),)
  GFP_CFLAG = -DHAVE_GFP
else
ifneq ($(shell grep "gfp_t" $(LINUXSRC)/include/linux/gfp.h > /dev/null 2>&1 && echo gfp),)
  GFP_CFLAG = -DHAVE_GFP
else
  GFP_CFLAG =
endif
endif

ifneq ($(shell grep "bool" $(LINUXSRC)/include/linux/types.h > /dev/null 2>&1 && echo bool),)
  BOOL_CFLAG = -DHAVE_BOOL -D_HAS_BOOL
else
  BOOL_CFLAG =
endif

ifeq ($(shell ls $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo aer),)
  AER_CFLAG =
else
  AER_CFLAG = -DHAVE_AER
endif

ifneq ($(shell grep "dev_err" $(LINUXSRC)/include/linux/device.h > /dev/null 2>&1 && echo dev_err),)
  DEV_ERR_CFLAG = -DHAVE_DEV_ERR
else
  DEV_ERR_CFLAG =
endif

ifneq ($(shell grep "dev_printk" $(LINUXSRC)/include/linux/device.h > /dev/null 2>&1 && echo dev_printk),)
  DEV_PRINTK_CFLAG = -DHAVE_DEV_PRINTK
else
  DEV_PRINTK_CFLAG =
endif

ifneq ($(shell grep "netif_set_real_num_tx" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo real_tx),)
  REAL_TX_CFLAG = -DHAVE_REAL_TX
else
  REAL_TX_CFLAG =
endif

ifneq ($(shell grep "netif_set_real_num_rx" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo real_rx),)
  REAL_RX_CFLAG = -DHAVE_REAL_RX
else
  REAL_RX_CFLAG =
endif

ifneq ($(shell grep "ndo_vlan_rx_register" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo vlan_rx),)
  VLAN_RX_CFLAG = -DHAVE_NDO_VLAN_RX_REGISTER
else
  VLAN_RX_CFLAG =
endif

ifneq ($(shell grep "ndo_fix_features" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo set_feat),)
  FIX_FEAT_CFLAG = -DHAVE_FIX_FEATURES
else
  FIX_FEAT_CFLAG =
endif

ifneq ($(shell grep "ndo_change_mtu" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_change_mtu),)
ifneq ($(shell grep -B50 "\*ndo_change_mtu" $(LINUXSRC)/include/linux/netdevice.h | sed 's/[ ,\t]//gi' | grep net_device_ops_extended > /dev/null 2>&1 && echo ndo_change_mtu),)
  EXT_NETDEV_CFLAG = -DHAVE_NDO_EXT_CHANGE_MTU
endif
endif

ifeq ($(shell ls $(LINUXSRC)/include/linux/netdev_features.h > /dev/null 2>&1 && echo netdev_features),)
  NETDEV_FEAT_CFLAG = -DHAVE_NETDEV_FEATURES
else
  NETDEV_FEAT_CFLAG =
endif

ifneq ($(shell grep "ethtool_op_get_tx_csum" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h > /dev/null 2>&1 && echo get_tx_csum),)
  ETHTOOL_CFLAG =
else
  ETHTOOL_CFLAG = -DNEW_ETHTOOL
endif

ifneq ($(shell grep "eth_tp_mdix" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h > /dev/null 2>&1 && echo mdix_status),)
  MDIX_CFLAG = -DHAVE_MDIX_STATUS
else
  MDIX_CFLAG =
endif

ifneq ($(shell grep "eth_tp_mdix_ctrl" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h > /dev/null 2>&1 && echo mdix_ctrl),)
  MDIX_CTRL_CFLAG = -DHAVE_MDIX_CTRL
else
  MDIX__CTRL_CFLAG =
endif

ifneq ($(shell grep "skb_frag_size" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_frag),)
  SKB_FRAG_CFLAG = -DHAVE_SKB_FRAG
else
  SKB_FRAG_CFLAG =
endif

ifneq ($(shell grep "skb_frag_page" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_frag_page),)
  SKB_FRAG_PAGE_CFLAG = -DHAVE_SKB_FRAG_PAGE
else
  SKB_FRAG_PAGE_CFLAG =
endif

ifneq ($(shell grep "ethtool_adv_to_mii_adv_t" $(LINUXSRC)/include/linux/mii.h > /dev/null 2>&1 && echo ethtool_mii),)
  ETHTOOL_MII_CFLAG = -DHAVE_ETHTOOL_TO_MII
else
  ETHTOOL_MII_CFLAG =
endif

ifneq ($(shell grep "pci_is_pcie" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pcie),)
  IS_PCIE_CFLAG = -DHAVE_IS_PCIE
else
  IS_PCIE_CFLAG =
endif

ifneq ($(shell grep "device_set_wakeup_capable" $(LINUXSRC)/include/linux/pm*.h > /dev/null 2>&1 && echo wakeup_cap),)
  WAKE_CAP_CFLAG = -DHAVE_DEVICE_SET_WAKEUP_CAP
else
  WAKE_CAP_CFLAG =
endif

ifneq ($(shell grep "pci_pme_capable" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pme),)
  PME_CFLAG = -DHAVE_PCI_PME_CAPABLE
else
  PME_CFLAG =
endif

ifneq ($(shell grep "pci_wake_from_d3" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo d3),)
  WAKE_D3_CFLAG = -DHAVE_PCI_WAKE_FROM_D3
else
  WAKE_D3_CFLAG =
endif

ifneq ($(shell grep "timer_setup" $(LINUXSRC)/include/linux/timer.h > /dev/null 2>&1 && echo timer_setup),)
  TIMER_SETUP_CFLAG = -DHAVE_TIMER_SETUP
else
  TIMER_SETUP_CFLAG =
endif

ifneq ($(shell sed -n -E "/\(\*get_coalesce\)/,/\;/p" $(LINUXSRC)/include/linux/ethtool.h 2>&1 | grep kernel_ethtool_coalesce > /dev/null 2>&1 && echo kernel_ethtool_coalesce),)
  ETHTOOL_COALESCE_CFLAG = -DHAVE_KERNEL_ETHTOOL_COALESCE
else
  ETHTOOL_COALESCE_CFLAG =
endif

ifneq ($(shell grep "kernel_ethtool_ringparam" $(LINUXSRC)/include/linux/ethtool.h > /dev/null 2>&1 && echo kernel_ethtool_ringparam),)
   ETHTOOL_KERNEL_ERING = -DHAVE_ETHTOOL_KERNEL_ERING
else
   ETHTOOL_KERNEL_ERING =
endif

ifneq ($(shell grep "UIO_MEM_DMA_COHERENT" $(LINUXSRC)/include/linux/uio_driver.h > /dev/null 2>&1 && echo UIO_MEM_DMA_COHERENT),)
   UIO_MEM_DMA = -D_HAS_UIO_MEM_DMA
else
   UIO_MEM_DMA =
endif

sles_distro := $(wildcard /etc/SuSE-release)
ifneq ($(sles_distro),)
SLES_VERSION = $(shell cat /etc/SuSE-release | grep VERSION | grep -o -P [0-9]+)
SLES_PATCHLEVEL = $(shell cat /etc/SuSE-release | grep PATCHLEVEL | grep -o -P [0-9]+)
PADDED_PATCHLEVEL = $(shell if [ 10 -gt $(SLES_PATCHLEVEL) ]; then echo 0$(SLES_PATCHLEVEL); else echo $(SLES_PATCHLEVEL); fi)
SLES_DISTRO_VER = "0x$(SLES_VERSION)$(PADDED_PATCHLEVEL)"
BNX2X_EXTRA_CFLAGS += -DSLES_DISTRO=$(SLES_DISTRO_VER)
endif

ifeq ($(shell grep eth_zero_addr $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_zero_addr),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_ETH_ZERO_ADDR_
endif

ifeq ($(shell grep pcie_capability_read_word $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pcie_capability_read_word),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_PCIE_CAPABILITY_READ_WORD_
endif

ifeq ($(shell grep tcp_v6_check $(LINUXSRC)/include/net/ip6_checksum.h > /dev/null 2>&1 && echo tcp_v6_check),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_TCP_V6_CHECK_
endif

ifeq ($(shell grep "prandom_bytes" $(LINUXSRC)/include/linux/random.h > /dev/null 2>&1 && echo prandom_bytes),)
  ifeq ($(shell grep "prandom_bytes" $(LINUXSRC)/include/linux/prandom.h > /dev/null 2>&1 && echo prandom_bytes),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_PRANDOM_BYTES_
  endif
endif

ifeq ($(shell grep "pci_vfs_assigned" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_vfs_assigned),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_PCI_VFS_ASSIGNED_
endif

ifneq ($(shell grep "ndo_set_vf_link_state" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo ndo_set_vf_link_state),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_SET_VF_LINK_STATE
endif

ifeq ($(shell grep "skb_set_hash" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_set_hash),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_SKB_SET_HASH
endif

ifeq ($(shell grep "__skb_frag_set_page" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_frag_set_page),)
        BNX2X_EXTRA_CFLAGS += -D_DEFINE_SKB_FRAG_SET_PAGE
endif

ifeq ($(shell grep "netdev_name" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo netdev_name),)
        BNX2X_EXTRA_CFLAGS += -D_DEFINE_NETDEV_NAME
endif

ifeq ($(shell grep "inner_ipv6_hdr" $(LINUXSRC)/include/linux/ipv6.h > /dev/null 2>&1 && echo inner_ipv6_hdr),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_INNER_IPV6_HDR
endif

ifeq ($(shell grep "inner_ip_hdr" $(LINUXSRC)/include/linux/ip.h > /dev/null 2>&1 && echo inner_ip_hdr),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_INNER_IP_HDR
endif

ifeq ($(shell grep "inner_tcp_hdr" $(LINUXSRC)/include/linux/tcp.h > /dev/null 2>&1 && echo inner_tcp_hdr),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_INNER_TCP_HDR
endif

ifeq ($(shell grep "READ_ONCE" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
ifeq ($(shell grep "READ_ONCE" $(LINUXSRC)/include/asm-generic/rwonce.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_READ_ONCE
endif
endif

ifeq ($(shell grep "WRITE_ONCE" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
ifeq ($(shell grep "WRITE_ONCE" $(LINUXSRC)/include/asm-generic/rwonce.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_WRITE_ONCE
endif
endif

ifneq ($(shell grep "pci_disable_pcie_error_reporting" $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo pci_disable_pcie_error_reporting),)
  BNX2X_EXTRA_CFLAGS += -D_HAS_PCI_ERR_REPORT_REQ
endif

ifeq ($(shell grep "data_access_exceeds_word_size" $(LINUXSRC)/include/linux/compiler.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_DATA_ACCESS_EXCEEDS_WORD_SIZE
endif

ifeq ($(shell grep "__smp_mb__before_atomic" $(LINUXSRC)/include/linux/bitops.h > /dev/null 2>&1 && echo __smp_mb__before_atomic),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_SMP_MB_BEFORE_ATOMIC
endif

ifeq ($(shell grep "smp_mb__before_atomic" $(LINUXSRC)/include/asm-generic/barrier.h > /dev/null 2>&1 && echo smp_mb__before_atomic),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_SMP_MB_BEFORE_ATOMIC_V2
endif

ifneq ($(shell grep "clamp" $(LINUXSRC)/include/linux/kernel.h > /dev/null 2>&1 && echo clamp),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_CLAMP
endif

ifneq ($(shell grep "clamp" $(LINUXSRC)/include/linux/minmax.h > /dev/null 2>&1 && echo clamp),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_CLAMP
endif

ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/time64.h > /dev/null 2>&1 && echo timespec64),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_TIMESPEC64
endif

ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/time.h > /dev/null 2>&1 && echo timespec64),)
# In RH6.8 there's no time64.h
	BNX2X_EXTRA_CFLAGS += -D_HAS_TIMESPEC64
endif

ifneq ($(shell grep "timespec64" $(LINUXSRC)/include/linux/ptp_clock_kernel.h > /dev/null 2>&1 && echo timespec64),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_CALLBACK_TIMESPEC64
endif

ifneq ($(shell grep "ktime_get_boottime" $(LINUXSRC)/include/linux/timekeeping.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_BOOTTIME
endif

ifeq ($(shell grep "ktime_ms_delta" $(LINUXSRC)/include/linux/ktime.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_KTIME_MS_DELTA
endif

ifeq ($(shell grep "ether_addr_equal" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo XXX),)
	BNX2X_EXTRA_CFLAGS += -D_DEFINE_ETHER_ADDR_EQUAL
endif

ifneq ($(shell grep -A1 "ndo_tx_timeout" $(LINUXSRC)/include/linux/netdevice.h | grep "txqueue" > /dev/null 2>&1 && echo ndo_tx_timeout_txqueue),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_NDO_TX_TIMEOUT_TXQUEQUE
endif

ifneq ($(shell grep "pci_cleanup_aer_uncorrect_error_status" $(LINUXSRC)/include/linux/aer.h > /dev/null 2>&1 && echo pci_cleanup_aer_uncorrect_error_status),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_CLEANUP_AER_ERROR
endif

ifneq ($(shell grep "compiler-gcc" $(LINUXSRC)/include/linux/compiler_types.h > /dev/null 2>&1 && echo compiler-gcc),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_COMPILER_GCC_HEADER
endif

ifneq ($(shell grep "compiler_attributes" $(LINUXSRC)/include/linux/compiler_types.h > /dev/null 2>&1 && echo compiler_attributes),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_COMPILER_ATTRIBUTES_HEADER
endif

ifneq ($(shell grep "pci_is_pcie" $(LINUXSRC)/include/linux/pci.h > /dev/null 2>&1 && echo pci_is_pcie),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_PCI_IS_PCIE
endif

ifneq ($(shell grep "eth_hw_addr_set" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_hw_addr_set),)
 	BNX2X_EXTRA_CFLAGS += -D_HAS_ETH_HW_ADDR_SET
endif

ifneq ($(shell grep "eth_random_addr" $(LINUXSRC)/include/linux/etherdevice.h > /dev/null 2>&1 && echo eth_random_addr),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_ETH_RANDOM_ADDR
endif

ifneq ($(shell grep "get_random_bytes" $(LINUXSRC)/include/linux/random.h > /dev/null 2>&1 && echo get_random_bytes),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_ETH_RANDOM_BYTES
endif

ifneq ($(shell sed -n -E "/netif_napi_add\(struct/,/\)/p" $(LINUXSRC)/include/linux/netdevice.h 2>&1 | grep weight > /dev/null 2>&1 && echo weight),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_NETIF_NAPI_ADD_WEIGHT
endif

ifneq ($(shell grep "struct_group" $(LINUXSRC)/include/linux/stddef.h > /dev/null 2>&1 && echo pci_vpd_find_ro_info_keyword),)
	BNX2X_EXTRA_CFLAGS += -D_HAS_STRUCT_GROUP
endif

xenserver_distro := $(wildcard /etc/redhat-release)
ifneq ($(xenserver_distro),)
XENSERVER_EXIST = $(shell cat /etc/redhat-release | grep XenServer)
ifneq ($(XENSERVER_EXIST),)
XENSERVER_DISTRO_MAIN = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=1)
XENSERVER_DISTRO_SUB = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=2 | tail --lines=1)
XENSERVER_DISTRO_SUBB = $(shell cat /etc/redhat-release | grep -o -P [0-9]+ | head --lines=3 | tail --lines=1)
XENSERVER_DISTRO_VER = $(shell echo "$(XENSERVER_DISTRO_MAIN) * 65536 + $(XENSERVER_DISTRO_SUB) * 256 + $(XENSERVER_DISTRO_SUBB)" | bc)
BNX2X_EXTRA_CFLAGS += -DXENSERVER_DISTRO=$(XENSERVER_DISTRO_VER)
endif
endif

BCM_CNIC:=0
# check if 2.4 kernel or 2.5+ kernel
KVER_MAJOR:=$(shell echo $(KVER) | cut -d. -f1)
KVER_MINOR:=$(shell echo $(KVER) | cut -d. -f2)
KVER_GT_2_4:=0
KVER_GT_2_4:=$(shell bc <<< "$(KVER_MAJOR) > 2 || ($(KVER_MAJOR) == 2 && $(KVER_MINOR) > 4)")

ifeq ($(KVER_GT_2_4),1)
# Makefile for 2.5+ kernel
BCM_CNIC:=1

ifneq ($(shell ls $(LINUXSRC)/include/scsi/iscsi_if2.h > /dev/null 2>&1 && echo isci2),)
BCM_CNIC:=1
endif

ifeq ($(BCM_CNIC), 1)
BCM_DRV = bnx2.ko cnic.ko
else
BCM_DRV = bnx2.ko
endif

# Get RHEL version if available
ifneq ($(rhel_distro),)
RHEL_MAJVER := $(shell grep "RHEL_MAJOR" /usr/include/linux/version.h | sed -e 's/.*MAJOR \([0-9]\)/\1/')
RHEL_MINVER := $(shell grep "MINOR" /usr/include/linux/version.h | sed -e 's/.*MINOR \([0-9]\)/\1/')
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

ifneq ($(KERNELRELEASE),)

obj-m += bnx2.o
ifeq ($(BCM_CNIC), 1)
obj-m += cnic.o
endif

EXTRA_CFLAGS = $(NETDUMP_CFLAG) $(SKB_CFLAG) $(IP_CFLAG) $(LE32_CFLAG) $(BOOL_CFLAG) $(GFP_CFLAG) $(AER_CFLAG) $(DEV_ERR_CFLAG) $(DEV_PRINTK_CFLAG) $(REAL_TX_CFLAG) $(REAL_RX_CFLAG) $(IS_PCIE_CFLAG) $(SKB_FRAG_CFLAG) $(SKB_FRAG_PAGE_CFLAG) $(ETHTOOL_MII_CFLAG) $(FIX_FEAT_CFLAG) $(EXT_NETDEV_CFLAG) $(NETDEV_FEAT_CFLAG) $(ETHTOOL_CFLAG) $(BNX2X_EXTRA_CFLAGS) $(PME_CFLAG) $(WAKE_D3_CFLAG) $(WAKE_CAP_CFLAG) $(MDIX_CFLAG) $(MDIX_CTRL_CFLAG) $(TIMER_SETUP_CFLAG) $(ETHTOOL_COALESCE_CFLAG) $(ETHTOOL_KERNEL_ERING) $(UIO_MEM_DMA)

# check for Gcc Retpoline support
ifeq ($(shell gcc -Werror -E $(THUNK_OPTIONS) - < /dev/null > /dev/null 2>&1; echo $$?),0)
EXTRA_CFLAGS += $(THUNK_OPTIONS)
endif

else

default:
	./bnx2x_copy.sh
	$(MAKE) -C $(LINUX) M=$(shell pwd) modules

p4:
	./bnx2x_copy_p4.sh
	$(MAKE) -C $(LINUX) M=$(shell pwd) modules

endif

else # ifeq ($(KVER_GT_2_4),1)
# Makefile for 2.4 kernel

BCM_DRV = bnx2.o

ifeq ($(LINUX),)
  $(error Linux kernel source tree not found)
endif

CC = gcc

CFLAGS=-DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -I$(LINUX)/include $(NETDUMP_CFLAG) $(GFP_CFLAG)

ifeq ($(shell grep netif_poll_disable $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo newnetif),)
  CFLAGS+=-DOLD_NETIF
endif

ifeq ($(wildcard ~/rpmdir),)
  rpmdir = /usr/src/redhat
else
  rpmdir = $(wildcard ~/rpmdir)
endif

ifeq ($(__ARCH),x86_64)
  CFLAGS+=-mno-red-zone -mcmodel=kernel -pipe -finline-limit=2000
endif

ifeq ($(__ARCH),ia64)
  CFLAGS+=-pipe -ffixed-r13 -mfixed-range=f10-f15,f32-f127 -falign-functions=32
endif

ifeq ($(__ARCH),ppc64)
  ifneq ($(shell ls /opt/cross/bin/powerpc64-linux-gcc > /dev/null 2>&1 && echo ppcgcc),)
    CC=/opt/cross/bin/powerpc64-linux-gcc
  endif

  CFLAGS+=-fno-strict-aliasing -fno-common -fomit-frame-pointer -msoft-float -pipe -mminimal-toc -fno-builtin
endif

ifdef SMALL
  CFLAGS += -Os
else
  CFLAGS += -O2
endif

all: bnx2.o

endif # ifeq ($(KVER_GT_2_4),1)

bnx2.4.gz:
	gzip -c bnx2.4 > bnx2.4.gz

ifeq ($(KVER_GT_2_4),1)
install: default bnx2.4.gz
else
install: $(BCM_DRV) bnx2.4.gz
endif
	mkdir -p $(PREFIX)/$(BCMMODDIR);
	install -m 444 $(BCM_DRV) $(PREFIX)/$(BCMMODDIR);
	install -m 444 bnx2.4.gz $(PREFIX)/usr/share/man/man4;
	@if [ "$(PREFIX)" = "" ]; then /sbin/depmod -a ;\
	else echo " *** Run '/sbin/depmod -a' to update the module database.";\
	fi
	@if [ -f /boot/initramfs-$(KVER).img ] && [ "`lsinitrd /boot/initramfs-$(KVER).img | grep -e bnx2.ko -e cnic.ko`" ] ; then\
		echo "back up initrd to /boot/initramfs-$(KVER).img.bak" ;\
		cp /boot/initramfs-$(KVER).img /boot/initramfs-$(KVER).img.bak ;\
		echo "add driver bnx2 & cnic to initrd" ;\
		dracut -f --add-drivers cnic bnx2;\
	fi

.PHONEY: all clean install

clean:
	-rm -f bnx2.o bnx2.ko bnx2.mod.c bnx2.mod.o bnx2.4.gz cnic.o cnic.ko cnic.mod.c cnic.mod.o .bnx2.*.cmd .cnic.*.cmd *.markers *.order *.symvers
	-rm -rf .tmp_versions
	-rm -rf *.mod
	-rm -rf .Module.symvers.cmd
	-rm -rf .modules.order.cmd 
