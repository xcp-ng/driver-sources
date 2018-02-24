/* enic_config.h.  Generated from enic_config.h.in by configure.  */
/* enic_config.h.in.  Generated from configure.ac by autoheader.  */

/*
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



/* Number of params to dev_open() */
#define ENIC_DEV_OPEN_NPARAMS 1

/* Does netdevice.h define ndo_add_vxlan_port? */
#define ENIC_HAVE_ADD_VXLAN_PORT 0

/* Does netdevice.h define call_netdevice_notifiers? */
#define ENIC_HAVE_CALL_NETDEVICE_NOTIFIER 1

/* Does workqueue.h define cancel_work_sync? */
#define ENIC_HAVE_CANCEL_WORK_SYNC 1

/* Does cpumask.h define cpumask_local_spread? */
#define ENIC_HAVE_CPUMASK_LOCAL_SPREAD 1

/* Does netdevice.h define ndo_del_vxlan_port? */
#define ENIC_HAVE_DEL_VXLAN_PORT 0

/* Does ethtool.h define ethtool_ops_ext? */
#define ENIC_HAVE_ETHTOOL_OPS_EXT 0

/* Does flow_dissector.h define struct flow_dissector_key_ports? */
#define ENIC_HAVE_FLOW_DISSECTOR_KEY_PORTS_STRUCT 1

/* checks whether get_coalesce is defined and not deprecated */
#define ENIC_HAVE_GET_COALESCE 1

/* Does ethtool.h define deprecated get_coalesce? */
#define ENIC_HAVE_GET_COALESCE_DEPRECATED 0

/* Does ethtool.h define get_coalesce? */
#define ENIC_HAVE_GET_COALESCE_OP 1

/* if ethtool_ops->get_coalesce has extack options */
#define ENIC_HAVE_GET_COAL_EXTACK 0

/* checks whether get_flags is defined and not deprecated */
#define ENIC_HAVE_GET_FLAGS 0

/* Does ethtool.h define deprecated get_flags? */
#define ENIC_HAVE_GET_FLAGS_DEPRECATED 0

/* Does ethtool.h define get_flags? */
#define ENIC_HAVE_GET_FLAGS_OP 0

/* Does ethtool.h define get_link_ksettings? */
#define ENIC_HAVE_GET_LINK_KSETTINGS 1

/* if ethtool_ops->get_ringparam has extack options */
#define ENIC_HAVE_GET_RINGPARAM_EXTACK 0

/* Does ethtool_ops have get_rxfh()? */
#define ENIC_HAVE_GET_RXFH_OPS 1

/* Does ethtool_ops_ext have get_rxfh()? */
#define ENIC_HAVE_GET_RXFH_OPS_EXT 0

/* checks whether get_rxnfc is defined and not deprecated */
#define ENIC_HAVE_GET_RXNFC 1

/* Does ethtool.h define deprecated get_rxnfc? */
#define ENIC_HAVE_GET_RXNFC_DEPRECATED 0

/* Does ethtool.h define get_rxnfc? */
#define ENIC_HAVE_GET_RXNFC_OP 1

/* Does get_rxnfc have void rule locks? */
#define ENIC_HAVE_GET_RXNFC_VOID_RULE_LOCKS 0

/* checks whether get_rx_csum is defined and not deprecated */
#define ENIC_HAVE_GET_RX_CSUM 0

/* Does ethtool.h define deprecated get_rx_csum? */
#define ENIC_HAVE_GET_RX_CSUM_DEPRECATED 0

/* Does ethtool.h define get_rx_csum? */
#define ENIC_HAVE_GET_RX_CSUM_OP 0

/* Does ethtool.h define get_settings? */
#define ENIC_HAVE_GET_SETTINGS 1

/* checks whether get_sg is defined and not deprecated */
#define ENIC_HAVE_GET_SG 0

/* Does ethtool.h define deprecated get_sg? */
#define ENIC_HAVE_GET_SG_DEPRECATED 0

/* Does ethtool.h define get_sg? */
#define ENIC_HAVE_GET_SG_OP 0

/* Does ndo_get_stats64 have void return type? */
#define ENIC_HAVE_GET_STATS64_RET_VOID 1

/* Does ethtool.h define get_stats_count? */
#define ENIC_HAVE_GET_STATS_COUNT 0

/* checks whether get_tso is defined and not deprecated */
#define ENIC_HAVE_GET_TSO 0

/* Does ethtool.h define deprecated get_tso? */
#define ENIC_HAVE_GET_TSO_DEPRECATED 0

/* Does ethtool.h define get_tso? */
#define ENIC_HAVE_GET_TSO_OP 0

/* Does ethtool.h define get_ts_info? */
#define ENIC_HAVE_GET_TS_INFO 1

/* checks whether get_tx_csum is defined and not deprecated */
#define ENIC_HAVE_GET_TX_CSUM 0

/* Does ethtool.h define deprecated get_tx_csum? */
#define ENIC_HAVE_GET_TX_CSUM_DEPRECATED 0

/* Does ethtool.h define get_tx_csum? */
#define ENIC_HAVE_GET_TX_CSUM_OP 0

/* Does netdevice.h define IFF_UNICAST_FLT? */
#define ENIC_HAVE_IFF_UNICAST_FLT 1

/* Does netdevice.h define napi_complete_done? */
#define ENIC_HAVE_NAPI_COMPLETE_DONE 1

/* Does napi_complete_done return a value? */
#define ENIC_HAVE_NAPI_COMPLETE_DONE_RET 1

/* Does netdevice.h define ndo_open? */
#define ENIC_HAVE_NDO_OPEN 1

/* Does netdevice.h define ndo_udp_tunnel_add/del? */
#define ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL 1

/* Does netdevice.h define netdev_for_each_mc_addr? */
#define ENIC_HAVE_NETDEV_FOR_EACH_MC_ADDR 1

/* Does netdevice.h define netdev_for_each_uc_addr? */
#define ENIC_HAVE_NETDEV_FOR_EACH_UC_ADDR 1

/* Does netdev_for_each_uc_addr have arg hardware address? */
#define ENIC_HAVE_NETDEV_FOR_EACH_UC_ADDR_HA_ARG 1

/* Does netdevice.h define hw_features? */
#define ENIC_HAVE_NETDEV_HW_FEATURES 1

/* Does netdevice.h define netdev_rss_key_fill? */
#define ENIC_HAVE_NETDEV_RSS_KEY_FILL 1

/* Does netdevice.h define netdev_uc_count? */
#define ENIC_HAVE_NETDEV_UC_COUNT 1

/* Do netdev_xxx_once() logging functions exist? */
#define ENIC_HAVE_NETDEV_XXX_ONCE 1

/* checks whether NETIF_F_GSO_UDP_TUNNEL is defined or not */
#define ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL 1

/* Does netdevice.h define NETIF_F_GSO_UDP_TUNNEL? */
#define ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV 1

/* Does netdevice_features.h define NETIF_F_GSO_UDP_TUNNEL? */
#define ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL_NETDEV_FEATURES 1

/* checks whether NETIF_F_RXHASH is defined or not */
#define ENIC_HAVE_NETIF_F_RXHASH 1

/* Does netdevice.h define NETIF_F_RXHASH? */
#define ENIC_HAVE_NETIF_F_RXHASH_NETDEV 0

/* Does netdev_features.h define NETIF_F_RXHASH? */
#define ENIC_HAVE_NETIF_F_RXHASH_NETDEV_FEATURES 1

/* Does netdevice.h define net_device_ops? */
#define ENIC_HAVE_NET_DEVICE_OPS 1

/* Does pci.h define pci_set_drvdata? */
#define ENIC_HAVE_PCI_SET_DRVDATA 1

/* Does kernel have pci_zalloc_consistent? */
#define ENIC_HAVE_PCI_ZALLOC_IN_ASM_GENERIC 0

/* Does kernel have pci_zalloc_consistent? */
#define ENIC_HAVE_PCI_ZALLOC_IN_LINUX 1

/* Does ndo_vlan_rx_add_vid have a proto member? */
#define ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_ADD_VID 1

/* Does ndo_vlan_rx_kill_vid have a proto member? */
#define ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_KILL_VID 1

/* checks whether set_coalesce is defined and not deprecated */
#define ENIC_HAVE_SET_COALESCE 1

/* Does ethtool.h define deprecated set_coalesce? */
#define ENIC_HAVE_SET_COALESCE_DEPRECATED 0

/* Does ethtool.h define set_coalesce? */
#define ENIC_HAVE_SET_COALESCE_OP 1

/* checks whether set_flags is defined and not deprecated */
#define ENIC_HAVE_SET_FLAGS 0

/* Does ethtool.h define deprecated set_flags? */
#define ENIC_HAVE_SET_FLAGS_DEPRECATED 0

/* Does ethtool.h define set_flags? */
#define ENIC_HAVE_SET_FLAGS_OP 0

/* Does netdevice.h define SET_MODULE_OWNER? */
#define ENIC_HAVE_SET_MODULE_OWNER 0

/* Does netdevice.h define ndo_set_multicast_list? */
#define ENIC_HAVE_SET_MULTICAST_LIST 0

/* checks whether set_rx_csum is defined and not deprecated */
#define ENIC_HAVE_SET_RX_CSUM 0

/* Does ethtool.h define deprecated set_rx_csum? */
#define ENIC_HAVE_SET_RX_CSUM_DEPRECATED 0

/* Does ethtool.h define set_rx_csum? */
#define ENIC_HAVE_SET_RX_CSUM_OP 0

/* Does netdevice.h define set_rx_mode? */
#define ENIC_HAVE_SET_RX_MODE 0

/* checks whether set_sg is defined and not deprecated */
#define ENIC_HAVE_SET_SG 0

/* Does ethtool.h define deprecated set_sg? */
#define ENIC_HAVE_SET_SG_DEPRECATED 0

/* Does ethtool.h define set_sg? */
#define ENIC_HAVE_SET_SG_OP 0

/* checks whether set_tso is defined and not deprecated */
#define ENIC_HAVE_SET_TSO 0

/* Does ethtool.h define deprecated set_tso? */
#define ENIC_HAVE_SET_TSO_DEPRECATED 0

/* Does ethtool.h define set_tso? */
#define ENIC_HAVE_SET_TSO_OP 0

/* checks whether set_tx_csum is defined and not deprecated */
#define ENIC_HAVE_SET_TX_CSUM 0

/* Does ethtool.h define deprecated set_tx_csum? */
#define ENIC_HAVE_SET_TX_CSUM_DEPRECATED 0

/* Does ethtool.h define set_tx_csum? */
#define ENIC_HAVE_SET_TX_CSUM_OP 0

/* Does flow_dissector.h define skb_flow_dissect_flow_keys? */
#define ENIC_HAVE_SKB_FLOW_DISSECT_FLOW_KEYS 0

/* Does skbuff.h define skb_tx_timestamp? */
#define ENIC_HAVE_SKB_TX_TIMESTAMP 1

/* Does ethtool.h define supported_coalesce_params? */
#define ENIC_HAVE_SUPP_COAL_PARAMS 0

/* if ndo_tx_timeout() supports txqueue parameter */
#define ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT 0

/* Whether we have member struct net_device.udp_tunnel_nic_info in
   <linux/netdevice.h> or not */
#define ENIC_HAVE_UDP_TUNNEL_NIC_INFO 0

/* Does if_vlan.h define vlan_group? */
#define ENIC_HAVE_VLAN_GROUP 0

/* Does ndo_vlan_rx_add_vid return an int? */
#define ENIC_HAVE_VLAN_RX_ADD_VID_RET_TYPE_INT 1

/* Does ndo_vlan_rx_kill_vid return an int? */
#define ENIC_HAVE_VLAN_RX_KILL_VID_RET_TYPE_INT 1

/* Does netdevice.h define ndo_vlan_rx_register? */
#define ENIC_HAVE_VLAN_RX_REGISTER 0

/* Number of params to netif_napi_add() */
#define ENIC_NETIF_NAPI_ADD_NPARAMS 4

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <linux/crash_dump.h> header file. */
#define HAVE_LINUX_CRASH_DUMP_H 1

/* Define to 1 if you have the <linux/ethtool.h> header file. */
#define HAVE_LINUX_ETHTOOL_H 1

/* Define to 1 if you have the <linux/netdevice.h> header file. */
#define HAVE_LINUX_NETDEVICE_H 1

/* Define to 1 if you have the <linux/printk.h> header file. */
#define HAVE_LINUX_PRINTK_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <net/flow_dissector.h> header file. */
#define HAVE_NET_FLOW_DISSECTOR_H 1

/* Define to 1 if you have the <net/flow_keys.h> header file. */
/* #undef HAVE_NET_FLOW_KEYS_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "enic"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "enic 4.5.0.7-939.23"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "enic"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "4.5.0.7-939.23"

/* defines sles patchlevel if building for a SLES distro */
#define SLES_PATCHLEVEL 0

/* defines sles version if building for a SLES distro */
#define SLES_VERSION 0

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Extra KCFLAGS from VIC_CHECK_GCC_RETPOLINE */
#define VIC_EXTRA_KCFLAGS "-mindirect-branch-register -mindirect-branch=thunk-inline "

/* Does netdevice.h define ndo_add_vxlan_port? */
#define VIC_HAVE_ADD_VXLAN_PORT 0

/* Whether we have declaration alloc_etherdev_mqs in <alloc_etherdev_mqs> or
   not */
#define VIC_HAVE_ALLOC_ETHERDEV_MQS 1

/* Does cpumask.h define cpumask_set_cpu? */
#define VIC_HAVE_CPUMASK_SET_CPU 1

/* Whether <linux/crash_dump.h> exists or not */
#define VIC_HAVE_CRASH_DUMP_H 1

/* Whether we have member struct sk_buff.csum_offset in <linux/skbuff.h> or
   not */
#define VIC_HAVE_CSUM_OFFSET 1

/* Does pci.h define DEFINE_PCI_DEVICE_TABLE? */
#define VIC_HAVE_DEFINE_PCI_DEVICE_TABLE 0

/* Does etherdevice.h define ether_addr_equal? */
#define VIC_HAVE_ETHER_ADD_EQUAL 1

/* Whether <linux/ethtool.h> exists or not */
#define VIC_HAVE_ETHTOOL_H 1

/* Whether we have member struct net_device_ops.ndo_eth_ioctl in
   <linux/netdevice.h> or not */
#define VIC_HAVE_ETH_IOCTL 0

/* Whether we have member struct net_device_extended.min_mtu in
   <linux/netdevice.h> or not */
#define VIC_HAVE_EXTENDED_MIN_MAX_MTU 0

/* Whether we have member struct net_device_ops_extended.ndo_change_mtu in
   <linux/netdevice.h> or not */
#define VIC_HAVE_EXTENDED_NDO_CHANGE_MTU 0

/* Does netdevice.h define ndo_features_check? */
#define VIC_HAVE_FEATURES_CHECK 1

/* Whether <net/flow_dissector.h> exists or not */
#define VIC_HAVE_FLOW_DISSECTOR_H 1

/* Whether <net/flow_keys.h> exists or not */
#define VIC_HAVE_FLOW_KEYS_H 0

/* Does workqueue.h define INIT_WORK? */
#define VIC_HAVE_INIT_WORK 1

/* Does ip.h define ip_hdr? */
#define VIC_HAVE_IP_HDR 1

/* Does interrupt.h define irqreturn_t typedef? */
#define VIC_HAVE_IRQRETURN_T_TYPEDEF 1

/* Does interrupt.h define irq_set_affinity_hint? */
#define VIC_HAVE_IRQ_SET_AFFINITY_HINT 1

/* Does slab.h define kzalloc? */
#define VIC_HAVE_KZALLOC 1

/* Does hlist_for_each_entry have pos arg? */
#define VIC_HAVE_LIST_FOR_EACH_ENTRY_POS_ARG 0

/* Does hlist_for_each_entry_safe have tpos arg? */
#define VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG 0

/* Does skbuff.h define napi_consume_skb? */
#define VIC_HAVE_NAPI_CONSUME_SKB 1

/* Does napi_gro_flush has flush_old arg? */
#define VIC_HAVE_NAPI_GRO_FLUSH_HAS_FLUSH_OLD_ARG 1

/* Does netdevice.h define napi_schedule_irqoff? */
#define VIC_HAVE_NAPI_SCHEDULE_IRQOFF 1

/* Does netdevice.h define napi_struct? */
#define VIC_HAVE_NAPI_STRUCT 1

/* Whether we have member struct net_device_ops.ndo_get_stats64 in
   <linux/netdevice.h> or not */
#define VIC_HAVE_NDO_GET_STATS64 1

/* Does skbuff.h define netdev_alloc_skb? */
#define VIC_HAVE_NETDEV_ALLOC_SKB 1

/* Does skbuff.h define netdev_alloc_skb_ip_align? */
#define VIC_HAVE_NETDEV_ALLOC_SKB_IP_ALIGN 1

/* Does netdevice.h define netdev_err? */
#define VIC_HAVE_NETDEV_ERR 1

/* Does netdevice.h define VIC_HAVE_NETDEV_EXTENDED? */
#define VIC_HAVE_NETDEV_EXTENDED 0

/* Does netdevice.h define netdev_for_each_mc_addr? */
#define VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR 1

/* Does netdev_for_each_mc_addr have arg hardware address? */
#define VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR_HA_ARG 1

/* Does netdevice.h define netdev_get_tx_queue? */
#define VIC_HAVE_NETDEV_GET_TX_QUEUE 1

/* Whether we have member struct net_device.min_mtu in <linux/netdevice.h> or
   not */
#define VIC_HAVE_NETDEV_MIN_MAX_MTU 1

/* Does netdevice.h define netdev_name? */
#define VIC_HAVE_NETDEV_NAME 1

/* Whether we have member struct net_device.trans_start in <linux/netdevice.h>
   or not */
#define VIC_HAVE_NETDEV_TRANS_START 0

/* checks whether NETIF_F_HW_VLAN_CTAG_RX is defined or not */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX 1

/* Does netdevice.h define NETIF_F_HW_VLAN_CTAG_RX? */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV 0

/* Does netdev_features.h define NETIF_F_HW_VLAN_CTAG_RX? */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV_FEATURES 1

/* checks whether NETIF_F_HW_VLAN_CTAG_TX is defined or not */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX 1

/* Does netdevice.h define NETIF_F_HW_VLAN_CTAG_TX? */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV 0

/* Does netdev_features.h define NETIF_F_HW_VLAN_CTAG_TX? */
#define VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV_FEATURES 1

/* Does netdevice.h define netif_set_real_num_rx_queues? */
#define VIC_HAVE_NETIF_SET_REAL_NUM_RX_QUEUES 1

/* Does netdevice.h define netif_set_real_num_tx_queues? */
#define VIC_HAVE_NETIF_SET_REAL_NUM_TX_QUEUES 1

/* Does netdevice.h define netif_set_xps_queue? */
#define VIC_HAVE_NETIF_SET_XPS_QUEUE 1

/* Does netdevice.h define netif_tx_queue_stopped? */
#define VIC_HAVE_NETIF_TX_QUEUE_STOPPED 1

/* Does netdevice.h define netif_tx_stop_queue? */
#define VIC_HAVE_NETIF_TX_STOP_QUEUE 1

/* Does net.h define net_warn_ratelimited? */
#define VIC_HAVE_NET_WARN_RATELIMITED 1

/* checks whether pci_dma_mapping_error have pdev arg */
#define VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV 1

/* Does pci_dma_mapping_error have pdev arg? */
#define VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV1 1

/* Does pci_dma_mapping_error have pdev arg? */
#define VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV2 0

/* Does pci.h define pci_enable_device_mem? */
#define VIC_HAVE_PCI_ENABLE_DEVICE_MEM 1

/* Does pci.h define pci_enable_msix_range? */
#define VIC_HAVE_PCI_ENABLE_MSIX_RANGE 1

/* kernel src have pkt_hash_types */
#define VIC_HAVE_PKT_HASH_TYPES 1

/* Whether <linux/printk.h> exists or not */
#define VIC_HAVE_PRINTK_H 1

/* Whether this driver was compiled with retpoline support or not */
#define VIC_HAVE_RETPOLINE 1

/* Whether we have member struct net_device_ops.ndo_rx_flow_steer in
   <linux/netdevice.h> or not */
#define VIC_HAVE_RX_FLOW_STEER 1

/* Does sched.h define schedule_timeout_uninterruptible? */
#define VIC_HAVE_SCHEDULE_TIMEOUT_UNINTERRUPTIBLE 1

/* Whether we have member struct sk_buff.csum_level in <linux/skbuff.h> or not
   */
#define VIC_HAVE_SKBUFF_CSUM_LEVEL 1

/* Does skbuff.h define skb_checksum_start_offset? */
#define VIC_HAVE_SKB_CHECKSUM_START_OFFSET 1

/* Whether we have member struct sk_buff.csum_start in <linux/skbuff.h> or not
   */
#define VIC_HAVE_SKB_CSUM_START 1

/* Does skbuff.h define skb_frag_size? */
#define VIC_HAVE_SKB_FRAG_DMA_MAP 1

/* Does skbuff.h define skb_frag_size? */
#define VIC_HAVE_SKB_FRAG_SIZE 1

/* Does skbuff.h define skb_get_hash_raw? */
#define VIC_HAVE_SKB_GET_HASH_RAW 1

/* Does skb_linearize have gfp_t arg? */
#define VIC_HAVE_SKB_LINEARIZE_GFP_ARG 0

/* Does skbuff.h define skb_record_rx_queue? */
#define VIC_HAVE_SKB_RECORD_RX_QUEUE 1

/* Does skbuff.h define skb_set_hash? */
#define VIC_HAVE_SKB_SET_HASH 1

/* Does skbuff.h define skb_transport_offset? */
#define VIC_HAVE_SKB_TRANSPORT_OFFSET 1

/* Does if_vlan.h define skb_vlan_tag_get? */
#define VIC_HAVE_SKB_VLAN_TAG_GET 1

/* Does if_vlan.h define skb_vlan_tag_get? */
#define VIC_HAVE_SKB_VLAN_TAG_PRESENT 1

/* Whether we have member struct sk_buff.xmit_more in <linux/skbuff.h> or not
   */
#define VIC_HAVE_SKB_XMIT_MORE 1

/* Does tcp.h define tcp hdr? */
#define VIC_HAVE_TCP_HDR 1

/* Does tcp.h define tcp hdrlen? */
#define VIC_HAVE_TCP_HDRLEN 1

/* Does __vlan_hwaccel_put_tag have a vlan_proto arg? */
#define VIC_HAVE_VLAN_HWACCEL_PUT_TAG_VLAN_PROTO_ARG 1


#endif /* ENIC_CONFIG_H */

