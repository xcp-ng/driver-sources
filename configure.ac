dnl
dnl Copyright 2018 Cisco Systems, Inc.  All rights reserved.
dnl
dnl This program is free software; you may redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; version 2 of the License.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
dnl EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
dnl MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
dnl NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
dnl BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
dnl ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
dnl CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
dnl SOFTWARE.
dnl
dnl configure.ac script for Linux enic driver
dnl

AC_PREREQ(2.53)
AC_INIT(enic, m4_normalize(esyscmd([./version.sh --full])))

AC_CONFIG_AUX_DIR(config)
AC_CANONICAL_SYSTEM
AC_PROG_CC

#
# Find our own version numbers
#
ENIC_RPM_RELEASE=`$srcdir/version.sh --build-id`
ENIC_RPM_VERSION=`$srcdir/version.sh --version`

AC_SUBST(ENIC_RPM_RELEASE)
AC_SUBST(ENIC_RPM_VERSION)

AC_ARG_VAR([KNAME], [If defined, enic is built against this kernel])
AC_MSG_CHECKING([KNAME value])
u=$(uname -r)
KNAME=${KNAME:-$u}
AC_MSG_RESULT([$KNAME])

AC_ARG_ENABLE(kname-in-pkg-filename,
AS_HELP_STRING([--enable-kname-in-pkg-filename],
               [enable-kname-in-pkg-filename default: no]),
[case "${enableval}" in
             yes) KNAME_PKGNAME=$(echo -$KNAME | sed 's/.x86_64//') ;;
             no)  KNAME_PKGNAME="" ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-kname-in-pkg-name]) ;;
esac],
[KNAME_PKGNAME=""])

AC_SUBST([KNAME_PKGNAME])

#
# Find out what linux distribution we are on
#
VIC_LINUX_DISTRO

ENIC_WHOLE_VERSION=$PACKAGE_VERSION
ENIC_VERSION=$ENIC_RPM_VERSION
KDIR=/lib/modules/${KNAME}/build

AC_SUBST(KNAME)
AC_SUBST(KDIR)
AC_SUBST(ENIC_VERSION)
AC_SUBST(ENIC_WHOLE_VERSION)

AC_CONFIG_HEADER([enic_config.h])
AH_TOP(
[/*
 * Copyright 2008-2018 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef ENIC_CONFIG_H
#define ENIC_CONFIG_H

])

AH_BOTTOM([
#endif /* ENIC_CONFIG_H */
])

#
# Setup crypto signing
#
VIC_SWIMS_SETUP

#
# Checks needed for the VIC common code
#
VIC_KCOMPAT_TESTS
AC_CONFIG_HEADER([kcompat_config.h])

#
# Does gcc support the retpoline CLI options?
#
VIC_CHECK_GCC_RETPOLINE

#
# Checks specific to the enic Linux driver
#

VIC_LINUX_DEF(linux/skbuff.h, skb_tx_timestamp, ENIC_HAVE_SKB_TX_TIMESTAMP,
		  [Does skbuff.h define skb_tx_timestamp?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, call_netdevice_notifiers,
		  ENIC_HAVE_CALL_NETDEVICE_NOTIFIER,
		  [Does netdevice.h define call_netdevice_notifiers?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, netdev_uc_count, ENIC_HAVE_NETDEV_UC_COUNT,
		  [Does netdevice.h define netdev_uc_count?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, netdev_for_each_uc_addr,
		  ENIC_HAVE_NETDEV_FOR_EACH_UC_ADDR,
		  [Does netdevice.h define netdev_for_each_uc_addr?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, netdev_for_each_mc_addr,
		  ENIC_HAVE_NETDEV_FOR_EACH_MC_ADDR,
		  [Does netdevice.h define netdev_for_each_mc_addr?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, [[netdev_for_each_uc_addr(ha]],
		  ENIC_HAVE_NETDEV_FOR_EACH_UC_ADDR_HA_ARG,
		  [Does netdev_for_each_uc_addr have arg hardware address?],
		  PATTERN)

VIC_LINUX_DEF(linux/skbuff.h, napi_consume_skb, VIC_HAVE_NAPI_CONSUME_SKB,
		[Does skbuff.h define napi_consume_skb?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, napi_complete_done, ENIC_HAVE_NAPI_COMPLETE_DONE,
		  [Does netdevice.h define napi_complete_done?], WORD)

AC_MSG_CHECKING([if napi_complete_done returns a non-void])
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/netdevice.h>
	int foo() {
	struct napi_struct *n;

	return napi_complete_done(n, 0);
	}]])],
	[AC_MSG_RESULT([yes])
	 value=1],
	[AC_MSG_RESULT([no])
	 value=0])

AC_DEFINE_UNQUOTED(ENIC_HAVE_NAPI_COMPLETE_DONE_RET, $value,
			[Does napi_complete_done return a value?])

VIC_AC_CHECK_MEMBER_DEFINE([linux/skbuff.h],
			[struct sk_buff.xmit_more],
			[VIC_HAVE_SKB_XMIT_MORE])

VIC_LINUX_DEF(linux/netdevice.h, ndo_vlan_rx_register,
		  ENIC_HAVE_VLAN_RX_REGISTER,
		  [Does netdevice.h define ndo_vlan_rx_register?], WORD)

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
		    [struct net_device_ops.ndo_rx_flow_steer],
		    [VIC_HAVE_RX_FLOW_STEER])

VIC_AC_CHECK_MEMBER_DEFINE([linux/skbuff.h],
		    [struct sk_buff.csum_level],
		    [VIC_HAVE_SKBUFF_CSUM_LEVEL])

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
		    [struct net_device_ops.ndo_eth_ioctl],
		    [VIC_HAVE_ETH_IOCTL])

VIC_LINUX_DEF(linux/netdevice.h, ndo_add_vxlan_port,
		 ENIC_HAVE_ADD_VXLAN_PORT,
		 [Does netdevice.h define ndo_add_vxlan_port?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, get_link_ksettings,
		ENIC_HAVE_GET_LINK_KSETTINGS,
		[Does ethtool.h define get_link_ksettings?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, get_settings,
		ENIC_HAVE_GET_SETTINGS,
		[Does ethtool.h define get_settings?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, ndo_udp_tunnel_add,
		 ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL,
		 [Does netdevice.h define ndo_udp_tunnel_add/del?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, ndo_del_vxlan_port, ENIC_HAVE_DEL_VXLAN_PORT,
		 [Does netdevice.h define ndo_del_vxlan_port?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, SET_MODULE_OWNER, ENIC_HAVE_SET_MODULE_OWNER,
		 [Does netdevice.h define SET_MODULE_OWNER?], WORD)

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device_ops_extended.ndo_change_mtu],
			  [VIC_HAVE_EXTENDED_NDO_CHANGE_MTU])

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device_extended.min_mtu],
			  [VIC_HAVE_EXTENDED_MIN_MAX_MTU])

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device.min_mtu],
			  [VIC_HAVE_NETDEV_MIN_MAX_MTU])

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device.trans_start],
			  [VIC_HAVE_NETDEV_TRANS_START])

VIC_LINUX_DEF(asm-generic/pci-dma-compat.h, pci_zalloc_consistent,
		 ENIC_HAVE_PCI_ZALLOC_IN_ASM_GENERIC,
		 [Does kernel have pci_zalloc_consistent?],
		 WORD)

VIC_LINUX_DEF(linux/pci-dma-compat.h, pci_zalloc_consistent,
		 ENIC_HAVE_PCI_ZALLOC_IN_LINUX,
		 [Does kernel have pci_zalloc_consistent?],
		 WORD)

VIC_LINUX_DEF(linux/netdevice.h, netdev_rss_key_fill,
		 ENIC_HAVE_NETDEV_RSS_KEY_FILL,
		 [Does netdevice.h define netdev_rss_key_fill?],
		 WORD)

VIC_LINUX_DEF(linux/netdevice.h, ndo_set_multicast_list,
		 ENIC_HAVE_SET_MULTICAST_LIST,
		 [Does netdevice.h define ndo_set_multicast_list?], WORD)

VIC_LINUX_DEF_MULTILINE(linux/netdevice.h, ndo_vlan_rx_add_vid,
			   ENIC_HAVE_VLAN_RX_ADD_VID_RET_TYPE_INT, [int], [;],
			   [Does ndo_vlan_rx_add_vid return an int?])

VIC_LINUX_DEF_MULTILINE(linux/netdevice.h, proto,
			   ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_ADD_VID,
			   [(*ndo_vlan_rx_add_vid)(], [);],
			    [Does ndo_vlan_rx_add_vid have a proto member?])

VIC_LINUX_DEF_MULTILINE(linux/netdevice.h, ndo_vlan_rx_kill_vid,
			    ENIC_HAVE_VLAN_RX_KILL_VID_RET_TYPE_INT,
			    [int], [;],
			    [Does ndo_vlan_rx_kill_vid return an int?])

VIC_LINUX_DEF_MULTILINE(linux/netdevice.h, proto,
			    ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_KILL_VID,
			    [(*ndo_vlan_rx_kill_vid)(], [);],
			    [Does ndo_vlan_rx_kill_vid have a proto member?])

VIC_LINUX_DEF(linux/netdevice.h, [[dev->hw_features]],
		  ENIC_HAVE_NETDEV_HW_FEATURES,
		  [Does netdevice.h define hw_features?], PATTERN)

VIC_LINUX_DEF(linux/netdevice.h, ndo_open, ENIC_HAVE_NDO_OPEN,
		  [Does netdevice.h define ndo_open?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, set_rx_mode, ENIC_HAVE_SET_RX_MODE,
		  [Does netdevice.h define set_rx_mode?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, net_device_ops,
		  ENIC_HAVE_NET_DEVICE_OPS,
		  [Does netdevice.h define net_device_ops?], WORD)

VIC_LINUX_DEF(linux/netdevice.h,
                  [[void (\*ndo_get_stats64)]], ENIC_HAVE_GET_STATS64_RET_VOID,
                  [Does ndo_get_stats64 have void return type?], PATTERN)

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device_ops.ndo_get_stats64],
			  [VIC_HAVE_NDO_GET_STATS64])

VIC_LINUX_DEF(linux/netdevice.h, [[NETIF_F_GSO_UDP_TUNNEL ]],
		  ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV,
		  [Does netdevice.h define NETIF_F_GSO_UDP_TUNNEL?],
		  PATTERN)

VIC_LINUX_DEF(linux/netdev_features.h, [[NETIF_F_GSO_UDP_TUNNEL ]],
		  ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV_FEATURES,
		  [Does netdevice_features.h define NETIF_F_GSO_UDP_TUNNEL?],
		  PATTERN)

VIC_AC_CHECK_MEMBER_DEFINE([linux/netdevice.h],
			  [struct net_device.udp_tunnel_nic_info],
			  [ENIC_HAVE_UDP_TUNNEL_NIC_INFO])

if test $ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV -eq 1 ||
   test $ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV_FEATURES -eq 1; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED([ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL], [$RESULT],
		   [checks whether NETIF_F_GSO_UDP_TUNNEL is defined or not])

VIC_LINUX_DEF(linux/netdevice.h, [[NETIF_F_RXHASH ]],
		  ENIC_HAVE_NETIF_F_RXHASH_NETDEV,
		  [Does netdevice.h define NETIF_F_RXHASH?], PATTERN)

VIC_LINUX_DEF(linux/netdev_features.h, [[NETIF_F_RXHASH]],
		  ENIC_HAVE_NETIF_F_RXHASH_NETDEV_FEATURES,
		  [Does netdev_features.h define NETIF_F_RXHASH?], PATTERN)

if test $ENIC_HAVE_NETIF_F_RXHASH_NETDEV -eq 1 ||
   test $ENIC_HAVE_NETIF_F_RXHASH_NETDEV_FEATURES -eq 1; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_NETIF_F_RXHASH, [$RESULT],
		   [checks whether NETIF_F_RXHASH is defined or not])


VIC_LINUX_DEF(linux/ethtool.h, get_rx_csum, ENIC_HAVE_GET_RX_CSUM_OP,
		  [Does ethtool.h define get_rx_csum?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_rx_csum: Deprecated]],
		  ENIC_HAVE_GET_RX_CSUM_DEPRECATED,
		  [Does ethtool.h define deprecated get_rx_csum?], PATTERN)

if test $ENIC_HAVE_GET_RX_CSUM_OP -eq 1 &&
   test $ENIC_HAVE_GET_RX_CSUM_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RX_CSUM, [$RESULT],
		   [checks whether get_rx_csum is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_rx_csum, ENIC_HAVE_SET_RX_CSUM_OP,
		  [Does ethtool.h define set_rx_csum?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_rx_csum: Deprecated]],
		  ENIC_HAVE_SET_RX_CSUM_DEPRECATED,
		  [Does ethtool.h define deprecated set_rx_csum?], PATTERN)

if test $ENIC_HAVE_SET_RX_CSUM_OP -eq 1 &&
   test $ENIC_HAVE_SET_RX_CSUM_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_RX_CSUM, [$RESULT],
		   [checks whether set_rx_csum is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, get_tx_csum, ENIC_HAVE_GET_TX_CSUM_OP,
		  [Does ethtool.h define get_tx_csum?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_tx_csum: Deprecated]],
		  ENIC_HAVE_GET_TX_CSUM_DEPRECATED,
		  [Does ethtool.h define deprecated get_tx_csum?], PATTERN)

if test $ENIC_HAVE_GET_TX_CSUM_OP -eq 1 &&
   test $ENIC_HAVE_GET_TX_CSUM_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_TX_CSUM, [$RESULT],
		   [checks whether get_tx_csum is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_tx_csum, ENIC_HAVE_SET_TX_CSUM_OP,
		  [Does ethtool.h define set_tx_csum?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_tx_csum: Deprecated]],
		  ENIC_HAVE_SET_TX_CSUM_DEPRECATED,
		  [Does ethtool.h define deprecated set_tx_csum?], PATTERN)

if test $ENIC_HAVE_SET_TX_CSUM_OP -eq 1 &&
   test $ENIC_HAVE_SET_TX_CSUM_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_TX_CSUM, [$RESULT],
		   [checks whether set_tx_csum is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, get_sg, ENIC_HAVE_GET_SG_OP,
		  [Does ethtool.h define get_sg?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_sg: Deprecated]],
		  ENIC_HAVE_GET_SG_DEPRECATED,
		  [Does ethtool.h define deprecated get_sg?], PATTERN)

if test $ENIC_HAVE_GET_SG_OP -eq 1 &&
   test $ENIC_HAVE_GET_SG_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_SG, [$RESULT],
		   [checks whether get_sg is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_sg, ENIC_HAVE_SET_SG_OP,
		  [Does ethtool.h define set_sg?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_sg: Deprecated]],
		  ENIC_HAVE_SET_SG_DEPRECATED,
		  [Does ethtool.h define deprecated set_sg?], PATTERN)

if test $ENIC_HAVE_SET_SG_OP -eq 1 &&
   test $ENIC_HAVE_SET_SG_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_SG, [$RESULT],
		   [checks whether set_sg is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, get_tso, ENIC_HAVE_GET_TSO_OP,
		  [Does ethtool.h define get_tso?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_tso: Deprecated]],
		  ENIC_HAVE_GET_TSO_DEPRECATED,
		  [Does ethtool.h define deprecated get_tso?], PATTERN)

if test $ENIC_HAVE_GET_TSO_OP -eq 1 &&
   test $ENIC_HAVE_GET_TSO_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_TSO, [$RESULT],
		   [checks whether get_tso is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_tso, ENIC_HAVE_SET_TSO_OP,
		  [Does ethtool.h define set_tso?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_tso: Deprecated]],
		  ENIC_HAVE_SET_TSO_DEPRECATED,
		  [Does ethtool.h define deprecated set_tso?], PATTERN)

if test $ENIC_HAVE_SET_TSO_OP -eq 1 &&
   test $ENIC_HAVE_SET_TSO_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_TSO, [$RESULT],
		   [checks whether set_tso is defined and not deprecated])


VIC_LINUX_DEF(linux/ethtool.h, get_flags, ENIC_HAVE_GET_FLAGS_OP,
		  [Does ethtool.h define get_flags?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_flags: Deprecated]],
		  ENIC_HAVE_GET_FLAGS_DEPRECATED,
		  [Does ethtool.h define deprecated get_flags?], PATTERN)

if test $ENIC_HAVE_GET_FLAGS_OP -eq 1 &&
   test $ENIC_HAVE_GET_FLAGS_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_FLAGS, [$RESULT],
		   [checks whether get_flags is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_flags, ENIC_HAVE_SET_FLAGS_OP,
		  [Does ethtool.h define set_flags?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_flags: Deprecated]],
		  ENIC_HAVE_SET_FLAGS_DEPRECATED,
		  [Does ethtool.h define deprecated set_flags?], PATTERN)

if test $ENIC_HAVE_SET_FLAGS_OP -eq 1 &&
   test $ENIC_HAVE_SET_FLAGS_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_FLAGS, [$RESULT],
		   [checks whether set_flags is defined and not deprecated])

if test $DISTRO_VER = "rhel7u9" ||
   test $DISTRO_VER = "centos7u9"; then
	AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXNFC_OP, [0],
		   [disable arfs for rhel 7.9])

else
	VIC_LINUX_DEF(linux/ethtool.h, get_rxnfc, ENIC_HAVE_GET_RXNFC_OP,
		  [Does ethtool.h define get_rxnfc?], WORD)
fi

AC_MSG_CHECKING([if ethool_ops have get_rxfh])
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <linux/netdevice.h>
#include <linux/ethtool.h>
int foo() {
struct ethtool_ops a;
struct net_device at;
void *c = NULL;

return a.get_rxfh(&at, c, c, c);
}]])],
       [AC_MSG_RESULT([yes])
        value=1],
       [AC_MSG_RESULT([no])
        value=0])

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXFH_OPS, $value,
       [Does ethtool_ops have get_rxfh()?])


AC_MSG_CHECKING([if ethool_ops_ext have get_rxfh])
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <linux/netdevice.h>
#include <linux/ethtool.h>
int foo() {
struct ethtool_ops_ext a;
struct net_device at;
void *c = NULL;

return a.get_rxfh(&at, c, c, c);
}]])],
       [AC_MSG_RESULT([yes])
        value=1],
       [AC_MSG_RESULT([no])
        value=0])

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXFH_OPS_EXT, $value,
       [Does ethtool_ops_ext have get_rxfh()?])

if test $DISTRO_VER = "rhel7u9" ||
   test $DISTRO_VER = "centos7u9"; then
	AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXNFC_DEPRECATED, [0],
		   [disable arfs for rhel 7.9])

else
	VIC_LINUX_DEF(linux/ethtool.h, [[@get_rxnfc: Deprecated]],
		  ENIC_HAVE_GET_RXNFC_DEPRECATED,
		  [Does ethtool.h define deprecated get_rxnfc?], PATTERN)
fi

if test $ENIC_HAVE_GET_RXNFC_OP -eq 1 &&
   test $ENIC_HAVE_GET_RXNFC_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

if test $DISTRO_VER = "rhel7u9" ||
   test $DISTRO_VER = "centos7u9"; then
	AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXNFC, [0],
		   [disable arfs for rhel 7.9])
else
	AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXNFC, [$RESULT],
		   [checks whether get_rxnfc is defined and not deprecated])
fi

VIC_LINUX_DEF(linux/ethtool.h, get_coalesce, ENIC_HAVE_GET_COALESCE_OP,
		  [Does ethtool.h define get_coalesce?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_coalesce: Deprecated]],
		  ENIC_HAVE_GET_COALESCE_DEPRECATED,
		  [Does ethtool.h define deprecated get_coalesce?], PATTERN)

if test $ENIC_HAVE_GET_COALESCE_OP -eq 1 &&
   test $ENIC_HAVE_GET_COALESCE_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_COALESCE, [$RESULT],
		   [checks whether get_coalesce is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_coalesce, ENIC_HAVE_SET_COALESCE_OP,
		  [Does ethtool.h define set_coalesce?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_coalesce: Deprecated]],
		  ENIC_HAVE_SET_COALESCE_DEPRECATED,
		  [Does ethtool.h define deprecated set_coalesce?], PATTERN)

if test $ENIC_HAVE_SET_COALESCE_OP -eq 1 &&
   test $ENIC_HAVE_SET_COALESCE_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_COALESCE, [$RESULT],
		   [checks whether set_coalesce is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, get_tso, ENIC_HAVE_GET_TSO_OP,
		  [Does ethtool.h define get_tso?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@get_tso: Deprecated]],
		  ENIC_HAVE_GET_TSO_DEPRECATED,
		  [Does ethtool.h define deprecated get_tso?], PATTERN)

if test $ENIC_HAVE_GET_TSO_OP -eq 1 &&
   test $ENIC_HAVE_GET_TSO_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_TSO, [$RESULT],
		   [checks whether get_tso is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, set_tso, ENIC_HAVE_SET_TSO,
		  [Does ethtool.h define set_tso?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[@set_tso: Deprecated]],
		  ENIC_HAVE_SET_TSO_DEPRECATED,
		  [Does ethtool.h define deprecated set_tso?], PATTERN)

if test $ENIC_HAVE_SET_TSO_OP -eq 1 &&
   test $ENIC_HAVE_SET_TSO_DEPRECATED -eq 0; then
	RESULT=1
else
	RESULT=0
fi

AC_DEFINE_UNQUOTED(ENIC_HAVE_SET_TSO, [$RESULT],
		   [checks whether set_tso is defined and not deprecated])

VIC_LINUX_DEF(linux/ethtool.h, get_stats_count, ENIC_HAVE_GET_STATS_COUNT,
		  [Does ethtool.h define get_stats_count?], WORD)

VIC_LINUX_DEF(linux/ethtool.h, [[(*get_ts_info)]], ENIC_HAVE_GET_TS_INFO,
                  [Does ethtool.h define get_ts_info?], PATTERN)

VIC_LINUX_DEF(linux/ethtool.h, ethtool_ops_ext, ENIC_HAVE_ETHTOOL_OPS_EXT,
		  [Does ethtool.h define ethtool_ops_ext?], WORD)

if test $DISTRO_VER = "rhel7u9" ||
   test $DISTRO_VER = "centos7u9"; then
	AC_DEFINE_UNQUOTED(ENIC_HAVE_GET_RXNFC_VOID_RULE_LOCKS, [0],
		   [disable arfs for rhel 7.9])

else
	VIC_LINUX_DEF_MULTILINE(linux/ethtool.h, void,
 			    ENIC_HAVE_GET_RXNFC_VOID_RULE_LOCKS,
			    [(*get_rxnfc)], [);],
			    [Does get_rxnfc have void rule locks?])
fi

VIC_LINUX_DEF(linux/ethtool.h, supported_coalesce_params, ENIC_HAVE_SUPP_COAL_PARAMS,
		  [Does ethtool.h define supported_coalesce_params?], WORD)

VIC_LINUX_DEF(linux/pci.h, pci_set_drvdata, ENIC_HAVE_PCI_SET_DRVDATA,
		  [Does pci.h define pci_set_drvdata?], WORD)


VIC_LINUX_DEF(linux/if_vlan.h, vlan_group, ENIC_HAVE_VLAN_GROUP,
		  [Does if_vlan.h define vlan_group?], WORD)


VIC_LINUX_DEF(linux/workqueue.h, cancel_work_sync,
		  ENIC_HAVE_CANCEL_WORK_SYNC,
		  [Does workqueue.h define cancel_work_sync?], WORD)

VIC_LINUX_DEF(net/flow_dissector.h, skb_flow_dissect_flow_keys,
		  ENIC_HAVE_SKB_FLOW_DISSECT_FLOW_KEYS,
		  [Does flow_dissector.h define skb_flow_dissect_flow_keys?],
		  WORD)

VIC_LINUX_DEF(net/flow_dissector.h, [[struct flow_dissector_key_ports]],
		  ENIC_HAVE_FLOW_DISSECTOR_KEY_PORTS_STRUCT,
		  [Does flow_dissector.h define struct flow_dissector_key_ports?],
		  PATTERN)

VIC_LINUX_DEF(linux/cpumask.h, cpumask_local_spread,
		  ENIC_HAVE_CPUMASK_LOCAL_SPREAD,
		  [Does cpumask.h define cpumask_local_spread?], WORD)

VIC_LINUX_DEF(linux/netdevice.h, IFF_UNICAST_FLT, ENIC_HAVE_IFF_UNICAST_FLT,
		  [Does netdevice.h define IFF_UNICAST_FLT?], WORD)

# Kernel 5.19 moved the netdev_XXX_once functions to net_debug.h so now we have
# to check there if they aren't in netdevice.h.
VIC_GREP($LINUX_HEADER_DIR/include/linux/netdevice.h, netdev_warn_once,
	ENIC_HAVE_NETDEV_XXX_ONCE=1, ENIC_HAVE_NETDEV_XXX_ONCE=0, WORD)
AS_IF([test $ENIC_HAVE_NETDEV_XXX_ONCE -eq 0],
	[VIC_GREP($LINUX_HEADER_DIR/include/net/net_debug.h, netdev_warn_once,
		ENIC_HAVE_NETDEV_XXX_ONCE=1, ENIC_HAVE_NETDEV_XXX_ONCE=0, WORD)
	])
AC_SUBST(ENIC_HAVE_NETDEV_XXX_ONCE)
AC_DEFINE_UNQUOTED(ENIC_HAVE_NETDEV_XXX_ONCE, $ENIC_HAVE_NETDEV_XXX_ONCE,
	[Do netdev_xxx_once() logging functions exist?])

# Kernel v5.0 changed the signature of linux/netdevice.h dev_open()
# from having 1 parameter to 2 parameters.  See which flavor we
# have.
AC_MSG_CHECKING([for number of parameters to dev_open()])
CFLAGS_save=$CFLAGS
CFLAGS="-Werror"
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/netdevice.h>
	int foo() {
	    return dev_open(NULL);
	}]])],
	[ENIC_DEV_OPEN_NPARAMS=1],
	[ENIC_DEV_OPEN_NPARAMS=2])
AC_MSG_RESULT([$ENIC_DEV_OPEN_NPARAMS])
AC_DEFINE_UNQUOTED([ENIC_DEV_OPEN_NPARAMS],
	[$ENIC_DEV_OPEN_NPARAMS], [Number of params to dev_open()])
CFLAGS=$CFLAGS_save

# Kernel 6.1 dropped the weight parameter in netif_napi_add()
# so it now has only 3 paramaters instead of 4.  Find out which version we
# have.

AC_MSG_CHECKING([for number of parameters to netif_napi_add()])
CFLAGS_save=$CFLAGS
CFLAGS="-Werror"
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/netdevice.h>
	int foo() {
	    netif_napi_add(NULL, NULL, NULL);
	}]])],
	[ENIC_NETIF_NAPI_ADD_NPARAMS=3],
	[ENIC_NETIF_NAPI_ADD_NPARAMS=4])
AC_MSG_RESULT([$ENIC_NETIF_NAPI_ADD_NPARAMS])
AC_DEFINE_UNQUOTED([ENIC_NETIF_NAPI_ADD_NPARAMS],
	[$ENIC_NETIF_NAPI_ADD_NPARAMS], [Number of params to netif_napi_add()])
CFLAGS=$CFLAGS_save

AC_MSG_CHECKING([if ndo_tx_timeout() supports txqueue parameter])
CFLAGS_save=$CFLAGS
CFLAGS="-Werror"
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/netdevice.h>
	void foo() {
		struct net_device *netdev;
		struct net_device_ops *ops;
		ops->ndo_tx_timeout(netdev, 0);
	}]])],
	[ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT=1],
	[ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT=0])
AC_MSG_RESULT([$ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT])
AC_DEFINE_UNQUOTED([ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT],
	[$ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT], [if ndo_tx_timeout() supports txqueue parameter])
CFLAGS=$CFLAGS_save

AC_MSG_CHECKING([if ethtool_ops->get_coalesce has extack])
CFLAGS_save=$CFLAGS
CFLAGS="-Werror"
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/ethtool.h>
	#include <linux/netdevice.h>
	void foo() {
		struct ethtool_ops *ops;
		ops->get_coalesce(0, 0, 0, 0);
	}]])],
	[ENIC_HAVE_GET_COAL_EXTACK=1],
	[ENIC_HAVE_GET_COAL_EXTACK=0])
AC_MSG_RESULT([$ENIC_HAVE_GET_COAL_EXTACK])
AC_DEFINE_UNQUOTED([ENIC_HAVE_GET_COAL_EXTACK],
	[$ENIC_HAVE_GET_COAL_EXTACK], [if ethtool_ops->get_coalesce has extack options])
CFLAGS=$CFLAGS_save

AC_MSG_CHECKING([if ethtool_ops->get_ringparam has extack])
CFLAGS_save=$CFLAGS
CFLAGS="-Werror"
VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <linux/ethtool.h>
	#include <linux/netdevice.h>
	void foo() {
		struct ethtool_ops *ops;
		ops->get_ringparam(0, 0, 0, 0);
	}]])],
	[ENIC_HAVE_GET_RINGPARAM_EXTACK=1],
	[ENIC_HAVE_GET_RINGPARAM_EXTACK=0])
AC_MSG_RESULT([$ENIC_HAVE_GET_RINGPARAM_EXTACK])
AC_DEFINE_UNQUOTED([ENIC_HAVE_GET_RINGPARAM_EXTACK],
	[$ENIC_HAVE_GET_RINGPARAM_EXTACK], [if ethtool_ops->get_ringparam has extack options])
CFLAGS=$CFLAGS_save

VIC_SETUP_KERNEL_SNARFING_CLEANUP

#
# Files to generate
#

AC_CONFIG_FILES([Makefile
		package-rhel/enic.spec package-rhel/ddiskit/Makefile
		package-sles/Makefile package-sles/cisco-enic.spec
		package-debian/debian/rules
		package-debian/debian/changelog	package-xs/Makefile
		package-debian/debian/postrm package-debian/debian/postinst
		package-debian/debian/control package-xs/enic.spec])

AC_OUTPUT
