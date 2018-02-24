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

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#include "kcompat_config.h"
#include "enic_config.h"

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

#if defined(__KERNEL__)

#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/numa.h>

#ifndef PCI_VENDOR_ID_CISCO
#define PCI_VENDOR_ID_CISCO	0x1137
#endif

#define RX_COPYBREAK

/*
 * Kernel backward-compatibility definitions
 */

#ifndef ioread8
#define ioread8 readb
#endif

#ifndef ioread16
#define ioread16 readw
#endif

#ifndef ioread32
#define ioread32 readl
#endif

#ifndef iowrite8
#define iowrite8 writeb
#endif

#ifndef iowrite16
#define iowrite16 writew
#endif

#ifndef iowrite32
#define iowrite32 writel
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

#ifndef NETIF_F_GSO
#define gso_size tso_size
#endif

#ifndef NETIF_F_TSO6
#define NETIF_F_TSO6 0
#endif

#ifndef NETIF_F_TSO_ECN
#define NETIF_F_TSO_ECN 0
#endif

#ifndef CHECKSUM_PARTIAL
#define CHECKSUM_PARTIAL CHECKSUM_HW
#define CHECKSUM_COMPLETE CHECKSUM_HW
#endif

#ifndef IRQ_HANDLED
#define IRQ_HANDLED 1
#define IRQ_NONE 0
#endif

#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif

#ifndef PCI_VDEVICE
#define PCI_VDEVICE(vendor, device) \
	PCI_VENDOR_ID_##vendor, (device), \
	PCI_ANY_ID, PCI_ANY_ID, 0, 0
#endif

#ifndef round_jiffies
#define round_jiffies(j) (j)
#endif

#ifndef netdev_tx_t
#define netdev_tx_t int
#endif

#ifndef __packed
#define __packed __attribute__ ((packed))
#endif

#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif
#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) 0
#endif

#define RHEL_RELEASE_VERSION_RANGE(a1, a2, b1, b2)	\
	(RHEL_RELEASE_CODE &&	\
	 (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(a1, a2)) &&	\
	 (RHEL_RELEASE_CODE <= RHEL_RELEASE_VERSION(b1, b2)))

#ifndef SLES_VERSION
#define SLES_VERSION 0
#define SLES_PATCHLEVEL 0
#endif

#define SLES_RELEASE_VERSION(a,b) (((a) << 8) + (b))
#define SLES_RELEASE_CODE SLES_RELEASE_VERSION(SLES_VERSION, SLES_PATCHLEVEL)

#if (!RHEL_RELEASE_CODE && (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36))) || \
    (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6, 4))
#define skb_tx_timestamp(skb) do { } while(0)
#endif /* rhel < 6.4 or kernel < 2.6.36 */

#if !(RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6, 3)) && \
      (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7, 0)))
#define set_ethtool_ops_ext(netdev, ops) do { } while(0)
#endif /* !(rhel > 6.3 && < 7)*/


/* Non-kernel version-specific definitions */
#ifndef IFLA_VF_PORT_MAX
#define PORT_PROFILE_MAX 40
#define PORT_UUID_MAX  16
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
/* definition of from_timer was added in 686fef928bba6be13cabe639f154af7d72b63120
 * ("timer: Prepare to change timer callback argument type"). At this point of
 * time we have not moved to new timer api. Undef to prevent warning.
 */
#undef from_timer
#define from_timer(e, t, rfs) ((struct enic *) (t))
#define enic_timer_setup(timer, fn, enic, flags) setup_timer(timer, fn, (unsigned long)enic)
#else
#define enic_timer_setup(timer, fn, enic, flags) timer_setup(timer, fn, flags)
#endif /* kernel < 4.15 */

#ifndef setup_timer
#define setup_timer(timer, fn, data)					\
	do {								\
		init_timer((timer));					\
		(timer)->function = (fn);				\
		(timer)->data = (data);					\
	} while (0)
#endif /* setup_timer */

#if (!VIC_HAVE_CPUMASK_SET_CPU || !VIC_HAVE_IRQ_SET_AFFINITY_HINT)
#define enic_set_affinity_hint(a) do { } while(0)
#define enic_unset_affinity_hint(a) do { } while(0)
#define enic_free_affinity_hint(a) do { } while(0)
#define enic_init_affinity_hint(a) do { } while(0)
#endif

#if (!VIC_HAVE_NETIF_SET_XPS_QUEUE)
#define netif_set_xps_queue(a, b, c) do { } while(0)
#endif

#if (!VIC_HAVE_PCI_ENABLE_MSIX_RANGE)
static inline int pci_enable_msix_range(struct pci_dev *dev,
					struct msix_entry *entries, int minvec,
					int maxvec)
{
	int rc;

	rc = pci_enable_msix(dev, entries, maxvec);

	return (rc == 0) ? maxvec : rc;
}
#endif

#if (RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 9) == RHEL_RELEASE_CODE))
#undef CONFIG_RFS_ACCEL
#endif /* RHEL 7.9 */

#if (VIC_HAVE_FLOW_DISSECTOR_H)
#include <net/flow_dissector.h>
#elif (VIC_HAVE_FLOW_KEYS_H)
#include <net/flow_keys.h>
#else
#include <net/ip.h>
#include <stdbool.h>

struct flow_keys {
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	u8 ip_proto;
};

#ifdef CONFIG_RFS_ACCEL
static inline bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow)
{
	int nhoff;
	const struct iphdr *ip;
	const __be16 *ports;

	nhoff = skb_network_offset(skb);
	if (skb->protocol != htons(ETH_P_IP))
		return false;
	ip = (const struct iphdr *)(skb->data + nhoff);
	if (ip->frag_off & htons(IP_MF | IP_OFFSET))
		return false;
	ports = (const __be16 *)(skb->data + nhoff + 4 * ip->ihl);

	flow->src = ip->saddr;
	flow->dst = ip->daddr;
	flow->ip_proto = ip->protocol;
	flow->port16[0] = ports[0];
	flow->port16[1] = ports[1];
	return true;
}
#endif /*CONFIG_RFS_ACCEL*/
#endif

#if (!VIC_HAVE_SKB_VLAN_TAG_GET)
#define skb_vlan_tag_get(skb) vlan_tx_tag_get(skb)
#endif

#if (!VIC_HAVE_SKB_VLAN_TAG_PRESENT)
#define skb_vlan_tag_present(skb) vlan_tx_tag_present(skb)
#endif

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) && \
       (!SLES_RELEASE_CODE || (SLES_RELEASE_CODE < SLES_RELEASE_VERSION(12, 1))))
#define enic_driver_encode_asic_info(a, b)	\
	driver_encode_asic_info(a->reserved1, sizeof(a->reserved1),b->asic_type, b->asic_rev);
#else
#define enic_driver_encode_asic_info(a, b)	\
	driver_encode_asic_info(a->reserved2, sizeof(a->reserved2),b->asic_type, b->asic_rev);
#endif /* kernel < 4.0 */

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) && \
       (!SLES_RELEASE_CODE || (SLES_RELEASE_CODE < SLES_RELEASE_VERSION(12, 1))))
#define enic_driver_encode_asic_info(a, b)	\
	driver_encode_asic_info(a->reserved1, sizeof(a->reserved1),b->asic_type, b->asic_rev);
#else
#define enic_driver_encode_asic_info(a, b)	\
	driver_encode_asic_info(a->reserved2, sizeof(a->reserved2),b->asic_type, b->asic_rev);
#endif /* kernel < 4.0 */

#if (!VIC_HAVE_PKT_HASH_TYPES)
enum pkt_hash_types {
	PKT_HASH_TYPE_NONE,	/* Undefined type */
	PKT_HASH_TYPE_L2,	/* Input: src_MAC, dest_MAC */
	PKT_HASH_TYPE_L3,	/* Input: src_IP, dst_IP */
	PKT_HASH_TYPE_L4,	/* Input: src_IP, dst_IP, src_port, dst_port */
};
#endif

#if (!VIC_HAVE_SKB_GET_HASH_RAW)
#define skb_get_hash_raw(skb) (skb)->rxhash
#endif
#if (!VIC_HAVE_SKB_SET_HASH)
#define skb_set_hash(skb, hash, type) skb->rxhash = (type == PKT_HASH_TYPE_L4) ? hash : 0;
#endif

#if (!VIC_HAVE_NAPI_STRUCT)
#define enic_wq_lock(wq_lock) spin_lock_irqsave(wq_lock, flags)
#define enic_wq_unlock(wq_lock) spin_unlock_irqrestore(wq_lock, flags)
#else
#define enic_wq_lock(wq_lock) spin_lock(wq_lock)
#define enic_wq_unlock(wq_lock) spin_unlock(wq_lock)
#endif

#ifdef CONFIG_RFS_ACCEL
#if (VIC_HAVE_NETDEV_EXTENDED)
#define enic_netdev_rmap(enic) netdev_extended(enic->netdev)->rfs_data.rx_cpu_rmap
#else
#define enic_netdev_rmap(enic) enic->netdev->rx_cpu_rmap
#endif /*VIC_HAVE_NETDEV_EXTENDED*/
#endif /*CONFIG_RFS_ACCEL*/

#if (!VIC_HAVE_ETHER_ADD_EQUAL)
#define ether_addr_equal(i, j) (!(compare_ether_addr(i, j)))
#endif

#if (!VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX)
#define NETIF_F_HW_VLAN_CTAG_RX NETIF_F_HW_VLAN_RX
#endif

#if (!VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX)
#define NETIF_F_HW_VLAN_CTAG_TX NETIF_F_HW_VLAN_TX
#endif

#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG)
#define enic_hlist_for_each_entry_safe(a, b, c, d, e) hlist_for_each_entry_safe(a, b, c, d, e)
#else
#define enic_hlist_for_each_entry_safe(a, b, c, d, e) hlist_for_each_entry_safe(a, c, d, e)
#endif
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_POS_ARG)
#define enic_hlist_for_each_entry(a, b, c, d) hlist_for_each_entry(a, b, c, d)
#else
#define enic_hlist_for_each_entry(a, b, c, d) hlist_for_each_entry(a, c, d)
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL_CSUM
#define NETIF_F_GSO_UDP_TUNNEL_CSUM 0
#endif

#ifndef CONFIG_NET_RX_BUSY_POLL
#define skb_mark_napi_id(skb, napi) do {} while(0)
#elif (!VIC_HAVE_NAPI_GRO_FLUSH_HAS_FLUSH_OLD_ARG)
#define napi_gro_flush(a, b) napi_gro_flush(a)
#endif /*CONFIG_NET_RX_BUSY_POLL*/

#if (!ENIC_HAVE_NETDEV_XXX_ONCE)
#define netdev_err_once(dev, fmt, ...) do {} while (0)
#define netdev_warn_once(dev, fmt, ...) do {} while (0)
#define netdev_info_once(dev, fmt, ...) do {} while (0)
#endif

#if (!VIC_HAVE_VLAN_HWACCEL_PUT_TAG_VLAN_PROTO_ARG)
#define __vlan_hwaccel_put_tag(a, b, c) __vlan_hwaccel_put_tag(a, c);
#endif /* VIC_HAVE_VLAN_HWACCEL_PUT_TAG_VLAN_PROTO_ARG */

#ifndef IS_ENABLED
#define IS_ENABLED(x) 0
#endif

#ifndef BIT_ULL
#define BIT_ULL(nr)		(1ULL << (nr))
#endif

/* offload was introduced in 3.12.
 * Before that CONFIG_VXLAN was only used for
 * vxlan tunnel code.
 * For kernels without vxlan offload disable CONFIG_VXLAN
 */
#if (!VIC_HAVE_ADD_VXLAN_PORT && !ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL && \
	!ENIC_HAVE_UDP_TUNNEL_NIC_INFO)
#undef CONFIG_VXLAN_MODULE
#define CONFIG_VXLAN_MODULE 0
#undef CONFIG_VXLAN
#define CONFIG_VXLAN 0
#endif

#ifndef NETIF_F_CSUM_MASK
#define NETIF_F_CSUM_MASK (NETIF_F_HW_CSUM |	\
			   NETIF_F_IPV6_CSUM |	\
			   NETIF_F_IP_CSUM)
#endif

#if (!VIC_HAVE_NET_WARN_RATELIMITED)
#define net_warn_ratelimited(fmt, ...)			\
	do {						\
		if (net_ratelimit())			\
			pr_warn(fmt, ##__VA_ARGS__);	\
	} while (0)
#endif

#if (VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV)
#define enic_pci_dma_mapping_error(pdev, dma) pci_dma_mapping_error(pdev, dma)
#else
#define enic_pci_dma_mapping_error(pdev, dma) pci_dma_mapping_error(dma)
#endif

#if (!VIC_HAVE_SCHEDULE_TIMEOUT_UNINTERRUPTIBLE)
static inline signed long schedule_timeout_uninterruptible(signed long timeout)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
#endif

#if (!VIC_HAVE_KZALLOC)
static inline void *kzalloc(size_t size, unsigned int flags)
{
	void *mem = kmalloc(size, flags);
	if (mem)
		memset(mem, 0, size);
	return mem;
}
#endif

#if (VIC_HAVE_SKB_LINEARIZE_GFP_ARG)
static inline int kcompat_skb_linearize(struct sk_buff *skb, int gfp)
{
	return skb_linearize(skb, gfp);
}
#undef skb_linearize
#define skb_linearize(skb) kcompat_skb_linearize(skb, GFP_ATOMIC)
#endif

#if (!VIC_HAVE_NETDEV_ALLOC_SKB)
#undef netdev_alloc_skb
#define netdev_alloc_skb(dev, len)	dev_alloc_skb(len)
#endif

#if (!VIC_HAVE_IRQRETURN_T_TYPEDEF)
typedef irqreturn_t (*irq_handler_t)(int, void*, struct pt_regs *);
#endif

#if (!VIC_HAVE_INIT_WORK)
#undef INIT_WORK
#define INIT_WORK(_work, _func) \
do { \
	INIT_LIST_HEAD(&(_work)->entry); \
	(_work)->pending = 0; \
	(_work)->func = (void (*)(void *))_func; \
	(_work)->data = _work; \
	init_timer(&(_work)->timer); \
} while (0)
#endif


#if (!VIC_HAVE_CSUM_OFFSET)
#define csum_offset csum
#endif

#if (!VIC_HAVE_SKB_CHECKSUM_START_OFFSET)
#if (!VIC_HAVE_SKB_CSUM_START)
#define skb_checksum_start_offset(skb) skb_transport_offset(skb)
#else
static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}
#endif
#endif

#if (!VIC_HAVE_IP_HDR)
#define ip_hdr(skb) (skb->nh.iph)
#endif

#if (!VIC_HAVE_IP_HDR)
#define ipv6_hdr(skb) (skb->nh.ipv6h)
#endif

#if (!VIC_HAVE_TCP_HDR)
#define tcp_hdr(skb) (skb->h.th)
#endif

#if (!VIC_HAVE_TCP_HDRLEN)
#define tcp_hdrlen(skb) (skb->h.th->doff << 2)
#endif

#if (!VIC_HAVE_SKB_TRANSPORT_OFFSET)
#define skb_transport_offset(skb) (skb->h.raw - skb->data)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
#undef kmem_cache_create
#define kmem_cache_create(name, size, align, flags, ctor) \
	kmem_cache_create(name, size, align, flags, ctor, NULL)
#define scsi_sglist(sc) ((struct scatterlist *) (sc)->request_buffer)
#if ((!RHEL_RELEASE_CODE) || \
		(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6, 0)))
#define netif_wake_subqueue(dev, q) netif_wake_queue(dev)
#define netif_tx_wake_all_queues(dev) netif_wake_queue(dev)
#if ((RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(5, 6)) || \
	(RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(5, 7)))
#define alloc_etherdev_mq(n, tx) alloc_etherdev_mq(n, 1)
#else
#define alloc_etherdev_mq(n, tx) alloc_etherdev(n)
#endif
#endif /* RHEL < 6.0 */
#endif /* LINUX_VERSION_CODE < 2.6.24 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define BIT(nr) (1UL << (nr))
#define for_each_sg(sglist, sg, nr, __i) \
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg++)
#if ((!RHEL_RELEASE_CODE) || \
		(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6, 0)))
#define skb_get_queue_mapping(skb) 0
#endif
#endif /* LINUX_VERSION_CODE < 2.6.24 */

#if (!VIC_HAVE_PCI_ENABLE_DEVICE_MEM)
#define pci_enable_device_mem pci_enable_device
#endif

#if (!VIC_HAVE_DEFINE_PCI_DEVICE_TABLE)
#define DEFINE_PCI_DEVICE_TABLE(_table) \
	const struct pci_device_id _table[]
#endif

#if (!VIC_HAVE_NETDEV_GET_TX_QUEUE)
#define netdev_get_tx_queue(dev, q) (dev)
#endif

#if (!VIC_HAVE_NETIF_TX_STOP_QUEUE)
#define netif_tx_stop_queue(dev) netif_stop_queue(dev)
#endif

#if (!VIC_HAVE_NETIF_TX_QUEUE_STOPPED)
#define netif_tx_queue_stopped(dev) netif_queue_stopped(dev)
#endif

#if (!VIC_HAVE_PRINTK_H)
#undef pr_err
#define pr_err(fmt, ...) \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#undef pr_warn
#define pr_warn pr_warning
#undef pr_warning
#define pr_warning(fmt, ...) \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#undef pr_info
#define pr_info(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

/*
 * We want this to be dependent on NETIF_F_GRO instead of kernel version,
 * because of past bugs we have seen.
 */
#ifndef NETIF_F_GRO
#define NETIF_F_GRO 0
#define vlan_gro_receive(napi, vlan_group, vlan_tci, skb) \
	vlan_hwaccel_receive_skb(skb, vlan_group, vlan_tci)
#define napi_gro_receive(napi, skb) \
	netif_receive_skb(skb)
#endif

#if (!VIC_HAVE_NETDEV_ALLOC_SKB_IP_ALIGN)
static inline struct sk_buff *kcompat_netdev_alloc_skb_ip_align(
	struct net_device *dev, unsigned int length)
{
	struct sk_buff *skb = netdev_alloc_skb(dev, length + NET_IP_ALIGN);

	if (NET_IP_ALIGN && skb)
		skb_reserve(skb, NET_IP_ALIGN);
	return skb;
}
#undef netdev_alloc_skb_ip_align
#define netdev_alloc_skb_ip_align(netdev, len) \
	kcompat_netdev_alloc_skb_ip_align(netdev, len)
#endif /*VIC_HAVE_NETDEV_ALLOC_SKB_IP_ALIGN*/

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))

#if (!VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR)
#define netdev_for_each_mc_addr(mclist, dev) \
	for (mclist = dev->mc_list; mclist; mclist = mclist->next)
#endif

#define netdev_mc_count(dev) ((dev)->mc_count)

#if (!VIC_HAVE_NETDEV_NAME)
static inline const char *netdev_name(const struct net_device *dev)
{
	if (dev->reg_state != NETREG_REGISTERED)
		return "(unregistered net_device)";
	return dev->name;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
#define netdev_uc_count(dev) 0
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
#define netdev_for_each_uc_addr(uclist, dev) \
	for (uclist = dev->uc_list; uclist; uclist = uclist->next)
#define netdev_uc_count(dev) ((dev)->uc_count)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
#define netdev_for_each_uc_addr(ha, dev) \
	list_for_each_entry(ha, &dev->uc.list, list)
#define netdev_uc_count(dev) ((dev)->uc.count)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21))
#if ((!RHEL_RELEASE_CODE) || \
	(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(5, 6)))
#define netdev_printk(level, netdev, format, args...) \
	dev_printk(level, (netdev)->class_dev.dev, \
		"%s: " format, \
		netdev_name(netdev), ##args)
#endif
#else
#define netdev_printk(level, netdev, format, args...) \
	dev_printk(level, (netdev)->dev.parent, \
		"%s: " format, \
		netdev_name(netdev), ##args)
#endif

#if (!VIC_HAVE_NETDEV_ERR)
#define netdev_err(dev, format, args...) \
	netdev_printk(KERN_ERR, dev, format, ##args)
#define netdev_warn(dev, format, args...) \
	netdev_printk(KERN_WARNING, dev, format, ##args)
#define netdev_info(dev, format, args...) \
	netdev_printk(KERN_INFO, dev, format, ##args)
#endif

#if (!VIC_HAVE_NETIF_SET_REAL_NUM_TX_QUEUES)
#if !defined(CONFIG_X86_XEN)
#define netif_set_real_num_tx_queues(_dev, _txq) do {} while (0)
#endif /*CONFIG_X86_XEN*/
#endif /* VIC_HAVE_NETIF_SET_REAL_NUM_TX_QUEUES */
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)*/

#if (!VIC_HAVE_NETIF_SET_REAL_NUM_RX_QUEUES)
#define netif_set_real_num_rx_queues(_dev, _rxq) do {} while (0)
#endif

#if (!VIC_HAVE_ALLOC_ETHERDEV_MQS)
#define alloc_etherdev_mqs(n, tx, rx) alloc_etherdev_mq(n, max_t(int, tx, rx))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38))
#define call_netdevice_notifiers(_val, _dev) do {} while (0)
#endif

#if (!VIC_HAVE_SKB_RECORD_RX_QUEUE)
#define skb_record_rx_queue(_skb, _rxq) do {} while (0)
#endif

#if (!VIC_HAVE_SKB_FRAG_DMA_MAP)
#define skb_frag_dma_map(_dev, _frag, _offset, _size, _dir) \
	dma_map_page(_dev, _frag->page, _frag->page_offset + _offset, \
		_size, _dir)
#endif

#if (!VIC_HAVE_SKB_FRAG_SIZE)
#define skb_frag_size(frag) frag->size
#endif

#ifndef READ_ONCE
#define READ_ONCE ACCESS_ONCE
#endif

#ifndef NAPI_POLL_WEIGHT
#define NAPI_POLL_WEIGHT (64)
#endif

#if ENIC_NETIF_NAPI_ADD_NPARAMS == 3
#define enic_netif_napi_add(ndev, napi, poll) \
	netif_napi_add(ndev, napi, poll)
#else
#define enic_netif_napi_add(ndev, napi, poll) \
	netif_napi_add(ndev, napi, poll, NAPI_POLL_WEIGHT)
#endif

#ifndef fallthrough
#define fallthrough	do {} while (0)
#endif

#if (!VIC_HAVE_NAPI_CONSUME_SKB)
#define napi_consume_skb(a, b) dev_kfree_skb_any(a)
#endif

#if (!VIC_HAVE_NAPI_SCHEDULE_IRQOFF)
#define napi_schedule_irqoff(a)	napi_schedule(a)
#endif

#if (!ENIC_HAVE_NAPI_COMPLETE_DONE)
#define napi_complete_done(napi, rq_work) napi_complete(napi)
#endif

/* Don't forget about MIPS, include it last */
#ifdef CONFIG_MIPS
#include "kcompat_mips.h"
#endif

#if (!ENIC_HAVE_PCI_ZALLOC_IN_LINUX && !ENIC_HAVE_PCI_ZALLOC_IN_ASM_GENERIC)
static inline void *pci_zalloc_consistent(struct pci_dev *hwdev, size_t size,
					  dma_addr_t *dma_handle)
{
	void *va;

	va = pci_alloc_consistent(hwdev, size, dma_handle);
	memset(va, 0, size);

	return va;
}
#endif

#if (!ENIC_HAVE_NETDEV_RSS_KEY_FILL)
static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	get_random_bytes(buffer, len);
}
#endif

#if ENIC_DEV_OPEN_NPARAMS == 1
#define enic_kcompat_dev_open(a, b) dev_open((a))
#else
#define enic_kcompat_dev_open(a, b) dev_open((a), (b))
#endif

#if (!ENIC_HAVE_GET_COAL_EXTACK)
#define enic_get_coalesce(netdev, ecmd, kernel_coal, extack)	\
	enic_get_coalesce(netdev, ecmd)
#define enic_set_coalesce(netdev, ecmd, kernel_coal, extack)	\
	enic_set_coalesce(netdev, ecmd)
#define enic_coalesce_valid(netdev, ecmd, kernel_coal, extack)	\
	enic_coalesce_valid(netdev, ecmd)
#define ENIC_COAL_NL_SET_ERR_MSG_MOD(extack, msg)
#else
#define ENIC_COAL_NL_SET_ERR_MSG_MOD(extack, msg)	NL_SET_ERR_MSG_MOD(extack, msg)
#endif

#if (!ENIC_HAVE_GET_RINGPARAM_EXTACK)
#define enic_get_ringparam(netdev, ring, kernel_ring, extack)	\
	enic_get_ringparam(netdev, ring)
#define enic_set_ringparam(netdev, ring, kernel_ring, extack)	\
	enic_set_ringparam(netdev, ring)
#endif

#ifndef NL_SET_ERR_MSG_MOD
#define NL_SET_ERR_MSG_MOD(extack, msg)
#endif


#else /* __KERNEL__ */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux_types.h>
#include <netinet/in.h>
#include <kcompat_priv.h>
#include <assert.h>
#include <stdbool.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ALIGN(x, a)              __ALIGN_MASK(x, (typeof(x))(a)-1)
#define __ALIGN_MASK(x, mask)    (((x)+(mask))&~(mask))
#define ETH_ALEN 6
#define BUG() assert(0)
#define BUG_ON(x) assert(!x)
#define kzalloc(x, flags) calloc(1, x)
#define kfree(x) free(x)

#define __iomem
#define udelay usleep
#define readl ioread32
#define writel iowrite32

typedef int gfp_t;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef offsetof
#define offsetof(t, m) ((size_t) &((t *)0)->m)
#endif

static inline uint32_t ioread32(const volatile void *addr)
{
	return *(volatile uint32_t *)addr;
}

static inline uint16_t ioread16(const volatile void *addr)
{
	return *(volatile uint16_t *)addr;
}

static inline uint8_t ioread8(const volatile void *addr)
{
	return *(volatile uint8_t *)addr;
}

static inline void iowrite64(uint64_t val, const volatile void *addr)
{
	*(volatile uint64_t *)addr = val;
}

static inline void iowrite32(uint32_t val, const volatile void *addr)
{
	*(volatile uint32_t *)addr = val;
}

#endif /* __KERNEL__ */
#endif /* _KCOMPAT_H_ */
