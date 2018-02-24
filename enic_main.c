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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/rtnetlink.h>
#include <linux/prefetch.h>
#include <net/ip6_checksum.h>
#include <linux/numa.h>

#include "enic_config.h"
#include "kcompat.h"
#include "cq_enet_desc.h"
#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_vic.h"
#include "vnic_rss.h"
#include "enic_res.h"
#include "enic.h"
#include "enic_api.h"
#include "enic_qp.h"
#include "enic_dev.h"
#include "enic_ethtool.h"
#include "enic_clsf.h"
#ifdef IFLA_VF_PORT_MAX
#include "enic_pp.h"
#endif
#include <linux/ktime.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif /*CONFIG_RFS_ACCEL*/
#if (VIC_HAVE_CRASH_DUMP_H)
#include <linux/crash_dump.h>
#endif
#if IS_ENABLED(CONFIG_VXLAN)
#include <net/vxlan.h>
#endif
#include "enic_clock.h"

#define CREATE_TRACE_POINTS
#include "enic_trace.h"

#define PCI_DEVICE_ID_CISCO_VIC_ENET         0x0043  /* ethernet vnic */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_DYN     0x0044  /* enet dynamic vnic */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF      0x0071  /* enet SRIOV VF */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V2   0x02b7

#define ENIC_LOG_DESC_COUNT		8
#define PREFIX_LEN			32
#define ENIC_WQERR_BUF_SZ (sizeof(struct wq_enet_desc) * ENIC_LOG_DESC_COUNT + \
		      sizeof(struct qerror_data_s) + \
		      sizeof(struct qerror_tlv_s) + sizeof(struct qerror_s))
#define ENIC_RQERR_BUF_SZ (sizeof(struct wq_enet_desc) * ENIC_LOG_DESC_COUNT + \
		      sizeof(struct qerror_data_s) + \
		      sizeof(struct qerror_tlv_s) + sizeof(struct qerror_s))

#ifdef RX_COPYBREAK
#define RXCOPYBREAK_DEFAULT 256

static unsigned int rxcopybreak __read_mostly = RXCOPYBREAK_DEFAULT;
module_param(rxcopybreak, uint, 0644);
MODULE_PARM_DESC(rxcopybreak, "Maximum size of packet that is copied to a new buffer on receive");
#endif

/* Supported devices */
static const struct pci_device_id enic_id_table[] = {
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET) },
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET_DYN) },
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET_VF) },
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V2) },
	{ 0, }	/* end of table */
};

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Scott Feldman <scofeldm@cisco.com>");
MODULE_LICENSE("GPL v2");

/* Whether to add MODULE_INFO(retpoline, "Y") or not is a complicated
   decision:

   1. As far as we can tell, if the MODULE_INFO "retpoline" field is
      there, its value is "Y".  The absense of this field effectively
      means "N".  I.e., we are intentionally never putting in a
      "retpoline" field with an "N" value.
   2. Distros that were released after about April 2018 will
      automatically add a MODULE_INFO(retpoline, "Y") to our source
      code for us (!) -- e.g., RHEL 7.5, SLES 15.  While there is no
      harm in having multiple identical MODULE_INFO(retpoline, "Y")
      instances in the source code, it makes multiple "retpoline"
      fields in the "modinfo enic" output... which is confusing.
   3. Kernel build systems that understand retpoline will define the
      RETPOLINE CPP macro.  Hence, if the RETPOLINE CPP macro has been
      defined, it means that the kernel build system has added
      MODULE_INFO(retpoline, "Y") if relevant.  If not, then we need
      to add it if relevant.

   Specifically: we need to manually add MODULE_INFO(retpoline, "Y")
   if a) RETPOLINE is not defined, and b) configure determined that
   gcc supports retpoline (and therefore added the relevant gcc CLI
   flags to compile with retpoline support).
*/
#if !defined(RETPOLINE) && VIC_HAVE_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, enic_id_table);

struct bus_type enic_rdma_bus;

#if (!ENIC_HAVE_CPUMASK_LOCAL_SPREAD && VIC_HAVE_IRQ_SET_AFFINITY_HINT)
static unsigned int cpumask_local_spread(unsigned int i, int node)
{
	int cpu;

	/* Wrap: we always want a cpu. */
	i %= num_online_cpus();

	if (node == -1) {
		for_each_cpu(cpu, cpu_online_mask)
			if (i-- == 0)
				return cpu;
	} else {
		/* NUMA first. */
		for_each_cpu_and(cpu, cpumask_of_node(node), cpu_online_mask)
			if (i-- == 0)
				return cpu;

		for_each_cpu(cpu, cpu_online_mask) {
			/* Skip NUMA nodes, done above. */
			if (cpumask_test_cpu(cpu, cpumask_of_node(node)))
				continue;

			if (i-- == 0)
				return cpu;
		}
	}
	BUG();
}
#endif

#if (VIC_HAVE_CPUMASK_SET_CPU && VIC_HAVE_IRQ_SET_AFFINITY_HINT)
static void enic_init_affinity_hint(struct enic *enic)
{
	int numa_node = dev_to_node(&enic->pdev->dev);
	int i;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX)
		return;
	for (i = 0; i < enic->qp_count; i++) {
		if ((enic->msix[i].affinity_mask &&
		     !cpumask_empty(enic->msix[i].affinity_mask)))
			continue;
		if (zalloc_cpumask_var(&enic->msix[i].affinity_mask,
				       GFP_KERNEL))
			cpumask_set_cpu(cpumask_local_spread(i, numa_node),
					enic->msix[i].affinity_mask);
	}
}

static void enic_free_affinity_hint(struct enic *enic)
{
	int i;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX)
		return;
	for (i = 0; i < enic->qp_count; i++)
		free_cpumask_var(enic->msix[i].affinity_mask);
}

static void enic_set_affinity_hint(struct enic *enic)
{
	int i;
	int err;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX)
		return;
	for (i = 0; i < enic->qp_count; i++) {
		if (!enic->msix[i].affinity_mask ||
		    cpumask_empty(enic->msix[i].affinity_mask))
			continue;
		err = irq_set_affinity_hint(enic->msix_entry[i].vector,
					    enic->msix[i].affinity_mask);
		if (err)
			netdev_warn(enic->netdev, "irq_set_affinity_hint failed, err %d\n",
				    err);
	}

	for (i = 0; i < enic->wq_count; i++) {
		if (enic->msix[i].affinity_mask &&
		    !cpumask_empty(enic->msix[i].affinity_mask))
			netif_set_xps_queue(enic->netdev,
					    enic->msix[i].affinity_mask,
					    i);
	}
}

static void enic_unset_affinity_hint(struct enic *enic)
{
	int i;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX)
		return;
	for (i = 0; i < enic->qp_count; i++)
		irq_set_affinity_hint(enic->msix_entry[i].vector, NULL);
}
#endif /* (VIC_HAVE_CPUMASK_SET_CPU && VIC_HAVE_IRQ_SET_AFFINITY_HINT) */


#if IS_ENABLED(CONFIG_VXLAN)
static void enic_enable_vxlan_offload(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;

	if (ENIC_SETTING(enic, VXLAN)) {
		netdev->hw_enc_features |= NETIF_F_IP_CSUM		|
					   NETIF_F_SG			|
					   NETIF_F_IPV6_CSUM		|
					   NETIF_F_HW_CSUM		|
					   NETIF_F_HIGHDMA		|
					   NETIF_F_SOFT_FEATURES	|
					   NETIF_F_RXCSUM		|
					   NETIF_F_TSO			|
					   NETIF_F_TSO6			|
					   NETIF_F_TSO_ECN		|
					   NETIF_F_CSUM_MASK		|
					   NETIF_F_GSO_UDP_TUNNEL	|
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
		netdev->features |= netdev->hw_enc_features;
#else
		netdev->hw_enc_features |= NETIF_F_HW_VLAN_CTAG_TX;
		netdev->hw_features |= netdev->hw_enc_features;
#endif
	}
}

#if ((ENIC_HAVE_DEL_VXLAN_PORT && VIC_HAVE_ADD_VXLAN_PORT) || \
	ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL)

#if (ENIC_HAVE_NETIF_F_GSO_UDP_TUNNEL)
static void enic_disable_vxlan_offload(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;

	netdev->hw_enc_features = 0;
	netdev->hw_features &= ~(NETIF_F_GSO_UDP_TUNNEL);
	netdev->features &= ~(NETIF_F_GSO_UDP_TUNNEL);
}
#endif

static void enic_udp_tunnel_add(struct net_device *netdev,
#if (VIC_HAVE_ADD_VXLAN_PORT)
				sa_family_t sa_family, __be16 port)
#else
				struct udp_tunnel_info *ti)
#endif
{
	struct enic *enic = netdev_priv(netdev);
	int err;
	int i;
#if (!VIC_HAVE_ADD_VXLAN_PORT)
	__be16 port = ti->port;
	sa_family_t sa_family = ti->sa_family;

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN) {
		netdev_info(netdev, "udp_tnl: only vxlan tunnel offload supported");
		return;
	}
#endif

	spin_lock_bh(&enic->devcmd_lock);

	switch (sa_family) {
	case AF_INET6:
		if (!(enic->vxlan.flags & ENIC_VXLAN_OUTER_IPV6)) {
			netdev_info(netdev, "vxlan: only IPv4 offload supported");
			goto error;
		}
		/* Fall through */
	case AF_INET:
		break;
	default:
		goto error;
	}

	if (enic->vxlan.vxlan_udp_port_number) {
		if (ntohs(port) == enic->vxlan.vxlan_udp_port_number) {
			netdev_warn(netdev, "vxlan: udp port already offloaded");
		} else {
			netdev_info(netdev, "vxlan: offload supported for only one UDP port");
			enic_disable_vxlan_offload(enic);
		}
		goto error;
	}

	if ((vnic_dev_get_res_count(enic->vdev, RES_TYPE_WQ) != 1) &&
	    !(enic->vxlan.flags & ENIC_VXLAN_MULTI_WQ)) {
		netdev_info(netdev, "vxlan: vxlan offload with multi wq not supported on this adapter");
		goto error;
	}

	err = vnic_dev_overlay_offload_cfg(enic->vdev,
					   OVERLAY_CFG_VXLAN_PORT_UPDATE,
					   ntohs(port));
	if (err)
		goto error;

	err = vnic_dev_overlay_offload_ctrl(enic->vdev, OVERLAY_FEATURE_VXLAN,
					    enic->vxlan.patch_level);
	if (err)
		goto error;

	enic->vxlan.vxlan_udp_port_number = ntohs(port);
	if (enic->qp)
		for (i = 0; i < enic->qp_count; i++)
			enic->qp[i].rq.vxlan_offload = true;

	enic_enable_vxlan_offload(enic);

	netdev_info(netdev, "vxlan fw-vers-%d: offload enabled for udp port: %d, family: %d\n",
		    (int)enic->vxlan.patch_level, ntohs(port), sa_family);

	goto unlock;

error:
	netdev_info(netdev, "vxlan: failed to offload vxlan udp port %d, IP protocol %d",
		    ntohs(port), sa_family);
unlock:
	spin_unlock_bh(&enic->devcmd_lock);
}

static void enic_udp_tunnel_del(struct net_device *netdev,
#if (ENIC_HAVE_DEL_VXLAN_PORT)
				sa_family_t sa_family, __be16 port)
#else
				struct udp_tunnel_info *ti)
#endif
{
	struct enic *enic = netdev_priv(netdev);
	int err;
	int i;
#if (!ENIC_HAVE_DEL_VXLAN_PORT)
	__be16 port = ti->port;
	sa_family_t sa_family = ti->sa_family;

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN) {
		netdev_info(netdev, "udp_tnl: port:%d, sa_family: %d, type: %d not offloaded",
			    ntohs(ti->port), ti->sa_family, ti->type);
		return;
	}
#endif

	spin_lock_bh(&enic->devcmd_lock);

	if (ntohs(port) != enic->vxlan.vxlan_udp_port_number) {
		netdev_info(netdev, "vxlan: udp port %d not offloaded",
			    ntohs(port));
		goto unlock;
	}

	enic_disable_vxlan_offload(enic);

	err = vnic_dev_overlay_offload_ctrl(enic->vdev, OVERLAY_FEATURE_VXLAN,
					    OVERLAY_OFFLOAD_DISABLE);

	if (err) {
		netdev_err(netdev, "vxlan: del offload udp port %d failed: %d",
			   ntohs(port), err);
		goto unlock;
	}

	enic->vxlan.vxlan_udp_port_number = 0;
	if (enic->qp)
		for (i = 0; i < enic->qp_count; i++)
			enic->qp[i].rq.vxlan_offload = false;

	netdev_info(netdev, "vxlan: del offload udp port %d, IP protocol %d\n",
		    ntohs(port), sa_family);

unlock:
	spin_unlock_bh(&enic->devcmd_lock);
}
/* ((ENIC_HAVE_DEL_VXLAN_PORT && VIC_HAVE_ADD_VXLAN_PORT) || \
 *  ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL)
 */
#elif (ENIC_HAVE_UDP_TUNNEL_NIC_INFO)

static int enic_udp_tunnel_set_port(struct net_device *netdev,
				    unsigned int table, unsigned int entry,
				    struct udp_tunnel_info *ti)
{
	struct enic *enic = netdev_priv(netdev);
	int err;

	spin_lock_bh(&enic->devcmd_lock);

	err = vnic_dev_overlay_offload_cfg(enic->vdev,
					   OVERLAY_CFG_VXLAN_PORT_UPDATE,
					   ntohs(ti->port));
	if (err)
		goto error;

	err = vnic_dev_overlay_offload_ctrl(enic->vdev, OVERLAY_FEATURE_VXLAN,
					    enic->vxlan.patch_level);
	if (err)
		goto error;

	enic->vxlan.vxlan_udp_port_number = ntohs(ti->port);
error:
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

static int enic_udp_tunnel_unset_port(struct net_device *netdev,
				      unsigned int table, unsigned int entry,
				      struct udp_tunnel_info *ti)
{
	struct enic *enic = netdev_priv(netdev);
	int err;

	spin_lock_bh(&enic->devcmd_lock);

	err = vnic_dev_overlay_offload_ctrl(enic->vdev, OVERLAY_FEATURE_VXLAN,
					    OVERLAY_OFFLOAD_DISABLE);
	if (err)
		goto unlock;

	enic->vxlan.vxlan_udp_port_number = 0;
unlock:
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

static const struct udp_tunnel_nic_info enic_udp_tunnels = {
	.set_port	= enic_udp_tunnel_set_port,
	.unset_port	= enic_udp_tunnel_unset_port,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN, },
	},
}, enic_udp_tunnels_v4 = {
	.set_port	= enic_udp_tunnel_set_port,
	.unset_port	= enic_udp_tunnel_unset_port,
	.flags		= UDP_TUNNEL_NIC_INFO_IPV4_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN, },
	},
};

/* ((ENIC_HAVE_DEL_VXLAN_PORT && VIC_HAVE_ADD_VXLAN_PORT) || \
 *  ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL)
 */
#endif

#if (VIC_HAVE_FEATURES_CHECK)
static netdev_features_t enic_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	struct enic *enic = netdev_priv(dev);
	struct udphdr *udph;
	u8 proto;
	u16 port = 0;
	const struct ethhdr *eth = (struct ethhdr*)skb_inner_mac_header(skb);

	if (!skb->encapsulation)
		return features;

	features = vxlan_features_check(skb, features);

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IPV6):
		if (!(enic->vxlan.flags & ENIC_VXLAN_OUTER_IPV6))
			goto out;
		proto = ipv6_hdr(skb)->nexthdr;
		break;
	case htons(ETH_P_IP):
		proto = ip_hdr(skb)->protocol;
		break;
	default:
		goto out;
	}

	switch (eth->h_proto) {
	case ntohs(ETH_P_IPV6):
		if (!(enic->vxlan.flags & ENIC_VXLAN_INNER_IPV6))
			goto out;
		/* Fall through */
	case ntohs(ETH_P_IP):
		break;
	default:
		goto out;
	}

	if (proto == IPPROTO_UDP) {
		udph = udp_hdr(skb);
		port = be16_to_cpu(udph->dest);
	}

	/* HW supports offload of only one UDP port. Remove CSUM and GSO MASK
	 * for other UDP port tunnels
	 */
	if (port  != enic->vxlan.vxlan_udp_port_number)
		goto out;

	return features;

out:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}
#endif
#endif /* CONFIG_VXLAN */

int enic_is_dynamic(struct enic *enic)
{
	return enic->pdev->device == PCI_DEVICE_ID_CISCO_VIC_ENET_DYN;
}

int enic_sriov_enabled(struct enic *enic)
{
	return (enic->priv_flags & ENIC_SRIOV_ENABLED) ? 1 : 0;
}

static int enic_is_sriov_vf(struct enic *enic)
{
	return enic->pdev->device == PCI_DEVICE_ID_CISCO_VIC_ENET_VF;
}

int enic_is_valid_vf(struct enic *enic, int vf)
{
#ifdef CONFIG_PCI_IOV
	return vf >= 0 && vf < enic->num_vfs;
#else
	return 0;
#endif
}

static void enic_msglvl_check(struct enic *enic)
{
	u32 msg_enable = vnic_dev_msg_lvl(enic->vdev);

	if (msg_enable != enic->msg_enable) {
		netdev_info(enic->netdev, "msg lvl changed from 0x%x to 0x%x\n",
			enic->msg_enable, msg_enable);
		enic->msg_enable = msg_enable;
	}
}

static void enic_mtu_check(struct enic *enic)
{
	u32 mtu = vnic_dev_mtu(enic->vdev);
	struct net_device *netdev = enic->netdev;

	if (mtu && mtu != enic->port_mtu) {
		enic->port_mtu = mtu;
		if (enic_is_dynamic(enic) || enic_is_sriov_vf(enic)) {
			mtu = max_t(int, ENIC_MIN_MTU,
				min_t(int, ENIC_MAX_MTU, mtu));
			if (mtu != netdev->mtu)
				schedule_work(&enic->change_mtu_work);
		} else {
			if (mtu < netdev->mtu)
				netdev_warn(netdev,
					"interface MTU (%d) set higher "
					"than switch port MTU (%d)\n",
					netdev->mtu, mtu);
		}
	}
}

static void enic_link_check(struct enic *enic)
{
	int link_status = vnic_dev_link_status(enic->vdev);
	int carrier_ok = netif_carrier_ok(enic->netdev);

	if (link_status && !carrier_ok) {
#ifndef CONFIG_MIPS
		netdev_info(enic->netdev, "Link UP\n");
#endif
		netif_carrier_on(enic->netdev);
	} else if (!link_status && carrier_ok) {
#ifndef CONFIG_MIPS
		netdev_info(enic->netdev, "Link DOWN\n");
#endif
		netif_carrier_off(enic->netdev);
	}
}

static void enic_notify_check(struct enic *enic)
{
	enic_msglvl_check(enic);
	enic_mtu_check(enic);
	enic_link_check(enic);
}

#define ENIC_TEST_INTR(pba, i) (pba & (1 << i))

static irqreturn_t enic_isr_legacy(int irq, void *data)
{
	struct net_device *netdev = data;
	struct enic *enic = netdev_priv(netdev);
	u32 pba;

	enic_intr_mask(&enic->qp[0]);

	pba = vnic_intr_legacy_pba(enic->legacy_pba);
	if (!pba) {
		enic_intr_unmask(&enic->qp[0]);
		return IRQ_NONE;	/* not our interrupt */
	}

	if (ENIC_TEST_INTR(pba, ENIC_LEGACY_NOTIFY_INTR)) {
		enic_notify_check(enic);
		enic_intr_return_all_credits(enic->notify_ctrl);
	}

	if (ENIC_TEST_INTR(pba, ENIC_LEGACY_ERR_INTR)) {
		enic_intr_return_all_credits(enic->err_ctrl);
		/* schedule recovery from WQ/RQ error */
		schedule_work(&enic->reset);
		return IRQ_HANDLED;
	}

	if (ENIC_TEST_INTR(pba, ENIC_LEGACY_IO_INTR))
		napi_schedule_irqoff(&enic->qp[0].napi);
	else
		enic_intr_unmask(&enic->qp[0]);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msi(int irq, void *data)
{
	struct napi_struct *napi = data;

	/* schedule NAPI polling for RQ/WQ cleanup */
	napi_schedule_irqoff(napi);
	if (trace_enic_isr_msi_enabled()) {
		struct enic_qp *qp = container_of(napi, struct enic_qp, napi);

		trace_enic_isr_msi(qp, irq);
	}

	return IRQ_HANDLED;
}

static bool enic_qerror(struct enic *enic)
{
	u32 error_status;
	int i;

	for (i = 0; i < enic->wq_count; i++) {
		error_status = enic_wq_error_status(&enic->qp[i]);
		if (error_status)
			return true;
	}

	for (i = 0; i < enic->rq_count; i++) {
		error_status = enic_rq_error_status(&enic->qp[i]);
		if (error_status)
			return true;
	}

	return false;
}

static irqreturn_t enic_isr_msix_err(int irq, void *data)
{
	struct enic *enic = data;

	enic_intr_return_all_credits(enic->err_ctrl);
	if (enic_qerror(enic))
		/* schedule recovery from WQ/RQ error */
		schedule_work(&enic->reset);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_notify(int irq, void *data)
{
	struct enic *enic = data;

	enic_notify_check(enic);
	enic_intr_return_all_credits(enic->notify_ctrl);

	return IRQ_HANDLED;
}

/* dev_base_lock rwlock held, nominally process context */
#if (!VIC_HAVE_NDO_GET_STATS64)
static struct net_device_stats *enic_get_stats(struct net_device *netdev)
#else
#if (!ENIC_HAVE_GET_STATS64_RET_VOID)
static struct rtnl_link_stats64 *enic_get_stats(struct net_device *netdev,
	struct rtnl_link_stats64 *net_stats)
#else
static void enic_get_stats(struct net_device *netdev,
			   struct rtnl_link_stats64 *net_stats)
#endif
#endif
{
	struct enic *enic = netdev_priv(netdev);
#if (!VIC_HAVE_NDO_GET_STATS64)
	struct net_device_stats *net_stats = &enic->net_stats;
#endif
	struct vnic_stats *stats;
	u64 val;
	int err;
	int i;

	err = enic_dev_stats_dump(enic, &stats);
	if (err == -ENOMEM) {
		memset(net_stats, 0, sizeof(*net_stats));
#if (!VIC_HAVE_NDO_GET_STATS64 || !ENIC_HAVE_GET_STATS64_RET_VOID)
		return net_stats;
#endif
	}

	net_stats->tx_packets = stats->tx.tx_frames_ok;
	net_stats->tx_bytes = stats->tx.tx_bytes_ok;
	net_stats->tx_errors = stats->tx.tx_errors;
	net_stats->tx_dropped = stats->tx.tx_drops;

	net_stats->rx_packets = stats->rx.rx_frames_ok;
	net_stats->rx_bytes = stats->rx.rx_bytes_ok;
	net_stats->rx_errors = stats->rx.rx_errors;
	net_stats->multicast = stats->rx.rx_multicast_frames_ok;

	for (i = 0, val = 0; i < enic->qp_count; i++) {
		struct enic_qp *qp = &enic->qp[i];

		if (!qp->rq.ctrl)
			break;
		val += qp->rq.stats.pkt_truncated;
	}
	net_stats->rx_over_errors = val;

	for (i = 0, val = 0; i < enic->qp_count; i++) {
		struct enic_qp *qp = &enic->qp[i];

		if (!qp->rq.ctrl)
			break;
		val += qp->rq.stats.bad_fcs;
	}
	net_stats->rx_crc_errors = val;

	net_stats->rx_dropped = stats->rx.rx_no_bufs + stats->rx.rx_drop;

#if (!VIC_HAVE_NDO_GET_STATS64 || !ENIC_HAVE_GET_STATS64_RET_VOID)
	return net_stats;
#endif
}

void enic_reset_addr_lists(struct enic *enic)
{
	enic->mc_count = 0;
	enic->uc_count = 0;
	enic->flags = 0;
}

static int enic_set_mac_addr(struct net_device *netdev, char *addr)
{
	struct enic *enic = netdev_priv(netdev);

	if (enic_is_dynamic(enic) || enic_is_sriov_vf(enic)) {
		if (!is_valid_ether_addr(addr) && !is_zero_ether_addr(addr))
			return -EADDRNOTAVAIL;
	} else {
		if (!is_valid_ether_addr(addr))
			return -EADDRNOTAVAIL;
	}

	memcpy(netdev->dev_addr, addr, netdev->addr_len);

	return 0;
}

static int enic_set_mac_address_dynamic(struct net_device *netdev, void *p)
{
	struct enic *enic = netdev_priv(netdev);
	struct sockaddr *saddr = p;
	char *addr = saddr->sa_data;
	int err;

	if (netif_running(enic->netdev)) {
		err = enic_dev_del_station_addr(enic);
		if (err)
			return err;
	}

	err = enic_set_mac_addr(netdev, addr);
	if (err)
		return err;

	if (netif_running(enic->netdev)) {
		err = enic_dev_add_station_addr(enic);
		if (err)
			return err;
	}

	return err;
}

static int enic_set_mac_address(struct net_device *netdev, void *p)
{
	struct sockaddr *saddr = p;
	char *addr = saddr->sa_data;
	struct enic *enic = netdev_priv(netdev);
	int err;

	err = enic_dev_del_station_addr(enic);
	if (err)
		return err;

	err = enic_set_mac_addr(netdev, addr);
	if (err)
		return err;

	return enic_dev_add_station_addr(enic);
}

static void enic_update_multicast_addr_list(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
#if (VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR_HA_ARG)
	struct netdev_hw_addr *ha;
#else
	struct dev_mc_list *list;
#endif
	unsigned int mc_count = netdev_mc_count(netdev);
	u8 mc_addr[ENIC_MULTICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int i, j;

	if (mc_count > ENIC_MULTICAST_PERFECT_FILTERS) {
		netdev_warn(netdev, "Registering only %d out of %d "
			"multicast addresses\n",
			ENIC_MULTICAST_PERFECT_FILTERS, mc_count);
		mc_count = ENIC_MULTICAST_PERFECT_FILTERS;
	}

	/* Is there an easier way?  Trying to minimize to
	 * calls to add/del multicast addrs.  We keep the
	 * addrs from the last call in enic->mc_addr and
	 * look for changes to add/del.
	 */

	i = 0;
#if (VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR_HA_ARG)
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == mc_count)
			break;
		memcpy(mc_addr[i++], ha->addr, ETH_ALEN);
	}
#else
	netdev_for_each_mc_addr(list, netdev) {
		if (i == mc_count)
			break;
		memcpy(mc_addr[i++], list->dmi_addr, ETH_ALEN);
	}
#endif

	for (i = 0; i < enic->mc_count; i++) {
		for (j = 0; j < mc_count; j++)
			if (ether_addr_equal(enic->mc_addr[i], mc_addr[j]))
				break;
		if (j == mc_count)
			enic_dev_del_addr(enic, enic->mc_addr[i]);
	}

	for (i = 0; i < mc_count; i++) {
		for (j = 0; j < enic->mc_count; j++)
			if (ether_addr_equal(mc_addr[i], enic->mc_addr[j]))
				break;
		if (j == enic->mc_count)
			enic_dev_add_addr(enic, mc_addr[i]);
	}

	/* Save the list to compare against next time
	 */

	for (i = 0; i < mc_count; i++)
		memcpy(enic->mc_addr[i], mc_addr[i], ETH_ALEN);

	enic->mc_count = mc_count;
}

static void enic_update_unicast_addr_list(struct enic *enic)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
	struct net_device *netdev = enic->netdev;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 30))
	struct netdev_hw_addr *ha;
#else
	struct dev_addr_list *list;
#endif
	unsigned int uc_count = netdev_uc_count(netdev);
	u8 uc_addr[ENIC_UNICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int i, j;

	if (uc_count > ENIC_UNICAST_PERFECT_FILTERS) {
		netdev_warn(netdev, "Registering only %d out of %d "
			"unicast addresses\n",
			ENIC_UNICAST_PERFECT_FILTERS, uc_count);
		uc_count = ENIC_UNICAST_PERFECT_FILTERS;
	}

	/* Is there an easier way?  Trying to minimize to
	 * calls to add/del unicast addrs.  We keep the
	 * addrs from the last call in enic->uc_addr and
	 * look for changes to add/del.
	 */

	i = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 30))
	netdev_for_each_uc_addr(ha, netdev) {
		if (i == uc_count)
			break;
		memcpy(uc_addr[i++], ha->addr, ETH_ALEN);
	}
#else
	netdev_for_each_uc_addr(list, netdev) {
		if (i == uc_count)
			break;
		memcpy(uc_addr[i++], list->da_addr, ETH_ALEN);
	}
#endif

	for (i = 0; i < enic->uc_count; i++) {
		for (j = 0; j < uc_count; j++)
			if (ether_addr_equal(enic->uc_addr[i], uc_addr[j]))
				break;
		if (j == uc_count)
			enic_dev_del_addr(enic, enic->uc_addr[i]);
	}

	for (i = 0; i < uc_count; i++) {
		for (j = 0; j < enic->uc_count; j++)
			if (ether_addr_equal(uc_addr[i],enic->uc_addr[j]))
				break;
		if (j == enic->uc_count)
			enic_dev_add_addr(enic, uc_addr[i]);
	}

	/* Save the list to compare against next time
	 */

	for (i = 0; i < uc_count; i++)
		memcpy(enic->uc_addr[i], uc_addr[i], ETH_ALEN);

	enic->uc_count = uc_count;
#endif
}

/* netif_tx_lock held, BHs disabled */
static void enic_set_rx_mode(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	int directed = 1;
	int multicast = (netdev->flags & IFF_MULTICAST) ? 1 : 0;
	int broadcast = (netdev->flags & IFF_BROADCAST) ? 1 : 0;
	int promisc = (netdev->flags & IFF_PROMISC) ||
		netdev_uc_count(netdev) > ENIC_UNICAST_PERFECT_FILTERS;
	int allmulti = (netdev->flags & IFF_ALLMULTI) ||
		netdev_mc_count(netdev) > ENIC_MULTICAST_PERFECT_FILTERS;
	unsigned int flags = netdev->flags |
		(allmulti ? IFF_ALLMULTI : 0) |
		(promisc ? IFF_PROMISC : 0);

	if (enic->flags != flags) {
		enic->flags = flags;
		enic_dev_packet_filter(enic, directed,
			multicast, broadcast, promisc, allmulti);
	}

	if (!promisc) {
		enic_update_unicast_addr_list(enic);
		if (!allmulti)
			enic_update_multicast_addr_list(enic);
	}
}
#if (ENIC_HAVE_VLAN_RX_REGISTER)
/* rtnl lock is held */
static void enic_vlan_rx_register(struct net_device *netdev,
	struct vlan_group *vlan_group)
{
	struct enic *enic = netdev_priv(netdev);
	enic->vlan_group = vlan_group;
}

#endif

/* netif_tx_lock held, BHs disabled */
#if (ENIC_HAVE_TXQUEUE_IN_NDO_TX_TIMEOUT)
static void enic_tx_timeout(struct net_device *netdev, unsigned int txqueue)
#else
static void enic_tx_timeout(struct net_device *netdev)
#endif
{
	struct enic *enic = netdev_priv(netdev);
	schedule_work(&enic->tx_hang_reset);
}

#ifdef IFLA_VF_PORT_MAX
static int enic_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_port_profile *pp;
	int err;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (is_valid_ether_addr(mac) || is_zero_ether_addr(mac)) {
		if (vf == PORT_SELF_VF) {
			memcpy(pp->vf_mac, mac, ETH_ALEN);
			return 0;
		}
		else {
			/*
		 	 * For sriov vf's set the mac in hw
		 	 */
			ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic,
				vnic_dev_set_mac_addr, mac);
			return enic_dev_status_to_errno(err);
		}
	} else
		return -EINVAL;
}

static int enic_set_vf_port(struct net_device *netdev, int vf,
	struct nlattr *port[])
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_port_profile prev_pp;
	struct enic_port_profile *pp;
	int err = 0, restore_pp = 1;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (!port[IFLA_PORT_REQUEST])
		return -EOPNOTSUPP;

	memcpy(&prev_pp, pp, sizeof(*enic->pp));
	memset(pp, 0, sizeof(*enic->pp));

	pp->set |= ENIC_SET_REQUEST;
	pp->request = nla_get_u8(port[IFLA_PORT_REQUEST]);

	if (port[IFLA_PORT_PROFILE]) {
		pp->set |= ENIC_SET_NAME;
		memcpy(pp->name, nla_data(port[IFLA_PORT_PROFILE]),
			PORT_PROFILE_MAX);
	}

	if (port[IFLA_PORT_INSTANCE_UUID]) {
		pp->set |= ENIC_SET_INSTANCE;
		memcpy(pp->instance_uuid,
			nla_data(port[IFLA_PORT_INSTANCE_UUID]), PORT_UUID_MAX);
	}

	if (port[IFLA_PORT_HOST_UUID]) {
		pp->set |= ENIC_SET_HOST;
		memcpy(pp->host_uuid,
			nla_data(port[IFLA_PORT_HOST_UUID]), PORT_UUID_MAX);
	}

	if (vf == PORT_SELF_VF) {
		/* Special case handling: mac came from IFLA_VF_MAC */
		if (!is_zero_ether_addr(prev_pp.vf_mac))
			memcpy(pp->mac_addr, prev_pp.vf_mac, ETH_ALEN);

		if (is_zero_ether_addr(netdev->dev_addr))
			random_ether_addr(netdev->dev_addr);
	}
	else {
		/* SR-IOV VF: get mac from adapter */
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic,
			vnic_dev_get_mac_addr, pp->mac_addr);
		if (err) {
			netdev_err(netdev, "Error getting mac for vf %d\n", vf);
			memcpy(pp, &prev_pp, sizeof(*pp));
			return enic_dev_status_to_errno(err);
		}
	}

	err = enic_process_set_pp_request(enic, vf, &prev_pp, &restore_pp);
	if (err) {
		if (restore_pp) {
			/* Things are still the way they were: Implicit
			 * DISASSOCIATE failed
			 */
			memcpy(pp, &prev_pp, sizeof(*pp));
		} else {
			memset(pp, 0, sizeof(*pp));
			if (vf == PORT_SELF_VF)
				memset(netdev->dev_addr, 0, ETH_ALEN);
		}
	} else {
		/* Set flag to indicate that the port assoc/disassoc
		 * request has been sent out to fw
		 */
		pp->set |= ENIC_PORT_REQUEST_APPLIED;

		/* If DISASSOCIATE, clean up all assigned/saved macaddresses */
		if (pp->request == PORT_REQUEST_DISASSOCIATE) {
			memset(pp->mac_addr, 0, ETH_ALEN);
			if (vf == PORT_SELF_VF)
				memset(netdev->dev_addr, 0, ETH_ALEN);
		}
	}

	if (vf == PORT_SELF_VF)
		memset(pp->vf_mac, 0, ETH_ALEN);

	return err;
}

static int enic_get_vf_port(struct net_device *netdev, int vf,
	struct sk_buff *skb)
{
	struct enic *enic = netdev_priv(netdev);
	u16 response = PORT_PROFILE_RESPONSE_SUCCESS;
	struct enic_port_profile *pp;
	int err;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (!(pp->set & ENIC_PORT_REQUEST_APPLIED))
		return -ENODATA;

	err = enic_process_get_pp_request(enic, vf, pp->request, &response);
	if (err)
		return err;

	if (nla_put_u16(skb, IFLA_PORT_REQUEST, pp->request) ||
	    nla_put_u16(skb, IFLA_PORT_RESPONSE, response) ||
	    ((pp->set & ENIC_SET_NAME) &&
	     nla_put(skb, IFLA_PORT_PROFILE, PORT_PROFILE_MAX, pp->name)) ||
	    ((pp->set & ENIC_SET_INSTANCE) &&
	     nla_put(skb, IFLA_PORT_INSTANCE_UUID, PORT_UUID_MAX,
		     pp->instance_uuid)) ||
	    ((pp->set & ENIC_SET_HOST) &&
	     nla_put(skb, IFLA_PORT_HOST_UUID, PORT_UUID_MAX, pp->host_uuid)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

#endif /* IFLA_VF_PORT_MAX */

#ifdef CONFIG_RFS_ACCEL
static void enic_free_rx_cpu_rmap(struct enic *enic)
{
	free_irq_cpu_rmap(enic_netdev_rmap(enic));
	enic_netdev_rmap(enic) = NULL;
}

static void enic_set_rx_cpu_rmap(struct enic *enic)
{
	int i, res;

	if(vnic_dev_get_intr_mode(enic->vdev) == VNIC_DEV_INTR_MODE_MSIX) {
		enic_netdev_rmap(enic) = alloc_irq_cpu_rmap(enic->rq_count);
		if(unlikely(!enic_netdev_rmap(enic)))
			return;
		for(i=0; i < enic->rq_count; i++) {
			res = irq_cpu_rmap_add(enic_netdev_rmap(enic),
					       enic->msix_entry[i].vector);
			if(unlikely(res)) {
				enic_free_rx_cpu_rmap(enic);
				return;
			}
		}
	}
}

#else

static void enic_set_rx_cpu_rmap(struct enic *enic)
{
}

static void enic_free_rx_cpu_rmap(struct enic *enic)
{
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
static void enic_notify_timer(unsigned long t)
#else
static void enic_notify_timer(struct timer_list *t)
#endif /* kernel < 4.15 */
{
	struct enic *enic = from_timer(enic, t, notify_timer);

	enic_notify_check(enic);

	mod_timer(&enic->notify_timer,
		round_jiffies(jiffies + ENIC_NOTIFY_TIMER_PERIOD));
}

static void enic_free_intr(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	unsigned int i;

	enic_free_rx_cpu_rmap(enic);
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		free_irq(enic->pdev->irq, netdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		free_irq(enic->pdev->irq, &enic->qp[0].napi);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->qp_count + 2; i++)
			if (enic->msix[i].requested)
				free_irq(enic->msix_entry[i].vector,
					enic->msix[i].devid);
		break;
	default:
		break;
	}
}

static int enic_request_intr(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	unsigned int i, intr;
	int err = 0;
	char *queue;

	enic_set_rx_cpu_rmap(enic);
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:

		err = request_irq(enic->pdev->irq, enic_isr_legacy,
			IRQF_SHARED, netdev->name, netdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:

		err = request_irq(enic->pdev->irq, enic_isr_msi,
			0, netdev->name, &enic->qp[0].napi);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->qp_count; i++) {
			if ((i < enic->rq_count) && (i < enic->wq_count))
				queue = "TxRx";
			else if (i < enic->rq_count)
				queue = "Rx";
			else if (i < enic->wq_count)
				queue = "Tx";
			else
				queue = "undef";
			snprintf(enic->msix[i].devname,
				 sizeof(enic->msix[i].devname), "%s-%s-%u",
				 netdev->name, queue, i);
			enic->msix[i].isr = enic_isr_msi;
			enic->msix[i].devid = &enic->qp[i].napi;
		}

		intr = enic->qp_count;		/* error interrupt */
		snprintf(enic->msix[intr].devname,
			 sizeof(enic->msix[intr].devname),
			 "%s-err", netdev->name);
		enic->msix[intr].isr = enic_isr_msix_err;
		enic->msix[intr].devid = enic;

		intr = enic->qp_count + 1;	/* notify interrupt */
		snprintf(enic->msix[intr].devname,
			 sizeof(enic->msix[intr].devname),
			 "%s-notify", netdev->name);
		enic->msix[intr].isr = enic_isr_msix_notify;
		enic->msix[intr].devid = enic;

		for (i = 0; i < enic->intr_count; i++)
			enic->msix[i].requested = 0;

		for (i = 0; i < enic->intr_count; i++) {
			err = request_irq(enic->msix_entry[i].vector,
				enic->msix[i].isr, 0,
				enic->msix[i].devname,
				enic->msix[i].devid);
			if (err) {
				enic_free_intr(enic);
				break;
			}
			enic->msix[i].requested = 1;
		}

		break;

	default:
		break;
	}

	return err;
}

static void enic_synchronize_irqs(struct enic *enic)
{
	unsigned int i;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
	case VNIC_DEV_INTR_MODE_MSI:
		synchronize_irq(enic->pdev->irq);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->intr_count; i++)
			synchronize_irq(enic->msix_entry[i].vector);
		break;
	default:
		break;
	}
}

static int enic_dev_notify_set(struct enic *enic)
{
        int err;

	spin_lock_bh(&enic->devcmd_lock);
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		err = vnic_dev_notify_set(enic->vdev, ENIC_LEGACY_NOTIFY_INTR);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		err = vnic_dev_notify_set(enic->vdev,
			enic_msix_notify_intr(enic));
		break;
	default:
		err = vnic_dev_notify_set(enic->vdev, -1 /* no intr */);
		break;
	}
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

static void enic_notify_timer_start(struct enic *enic)
{
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
		mod_timer(&enic->notify_timer, jiffies);
		break;
	default:
		/* Using intr for notification for INTx/MSI-X */
		break;
	}
}

/* rtnl lock is held, process context */
static int enic_open(struct net_device *netdev)
{
        struct enic *enic = netdev_priv(netdev);
        unsigned int i;
	int err;

	enic_print_resources(enic);
	err = enic_init_qp(enic);
	if (err)
		return err;

	for (i = 0; i < enic->qp_count; i++) {
		enic_netif_napi_add(netdev, &enic->qp[i].napi, enic_napi_poll);
		napi_enable(&enic->qp[i].napi);
	}

	err = enic_request_intr(enic);
	if (err) {
		netdev_err(netdev, "Unable to request irq.\n");
		goto err_napi_out;
	}
	enic_init_affinity_hint(enic);
	enic_set_affinity_hint(enic);

        err = enic_dev_notify_set(enic);
        if (err) {
                netdev_err(netdev,
                        "Failed to alloc notify buffer, aborting.\n");
		goto err_notify_set;
	}

	if (!enic_is_dynamic(enic) && !enic_is_sriov_vf(enic)) {
		err = enic_dev_add_station_addr(enic);
		if (err)
			goto release_intr;
	}

	enic_set_rx_mode(netdev);
	netif_tx_wake_all_queues(netdev);

	for (i = 0; i < enic->qp_count; i++)
		enic_intr_unmask(&enic->qp[i]);
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
	case VNIC_DEV_INTR_MODE_INTX:
		_enic_intr_unmask(enic->err_ctrl);
		_enic_intr_unmask(enic->notify_ctrl);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		break;
	default:
		netdev_err(netdev, "Unknown interrupt type");
		break;
	}

	enic_notify_timer_start(enic);
	enic_rfs_timer_init(enic);
	enic_dev_enable(enic);

	return 0;

release_intr:
	enic_unset_affinity_hint(enic);
	enic_free_intr(enic);
err_notify_set:
	enic_dev_notify_unset(enic);
	enic_unset_affinity_hint(enic);
	enic_free_intr(enic);
err_napi_out:
	for (i = 0; i < enic->qp_count; i++) {
		napi_disable(&enic->qp[i].napi);
		netif_napi_del(&enic->qp[i].napi);
	}

	return err;
}

/* rtnl lock is held, process context */
static int enic_stop(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int i;
	int err;

	enic_dev_disable(enic);
	for (i = 0; i < enic->qp_count; i++) {
		napi_disable(&enic->qp[i].napi);
		netif_napi_del(&enic->qp[i].napi);
		enic_intr_mask(&enic->qp[i]);
		/* flush write */
		enic_intr_masked(&enic->qp[i]);
	}
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
	case VNIC_DEV_INTR_MODE_INTX:
		_enic_intr_mask(enic->err_ctrl);
		_enic_intr_masked(enic->err_ctrl);
		_enic_intr_mask(enic->notify_ctrl);
		_enic_intr_masked(enic->notify_ctrl);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		break;
	default:
		netdev_err(netdev, "Unknown interrupt type");
		break;
	}

	enic_synchronize_irqs(enic);

	del_timer_sync(&enic->notify_timer);
	enic_rfs_flw_tbl_free(enic);


	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	if (!enic_is_dynamic(enic) && !enic_is_sriov_vf(enic)) {
		err = enic_dev_del_station_addr(enic);
		if (err)
			netdev_warn(netdev, "Unable to remove station addr %pM\n",
				    enic->mac_addr);
	}

	enic_dev_notify_unset(enic);
	enic_unset_affinity_hint(enic);
	enic_free_intr(enic);
	enic_deinit_qp(enic);

	return 0;
}

static int _enic_change_mtu(struct net_device *netdev, int new_mtu)
{
	int running = netif_running(netdev);
	int err = 0;

	ASSERT_RTNL();
	if (running){
		err = enic_stop(netdev);
		if (err)
			return err;
	}

	netdev->mtu = new_mtu;

	if (running){
		err = enic_open(netdev);
		if (err)
			return err;
	}

	return 0;
}

static int enic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct enic *enic = netdev_priv(netdev);

#if (!VIC_HAVE_EXTENDED_MIN_MAX_MTU && !VIC_HAVE_NETDEV_MIN_MAX_MTU)
	if (new_mtu < ENIC_MIN_MTU || new_mtu > ENIC_MAX_MTU)
		return -EINVAL;
#endif

	if (enic_is_dynamic(enic) || enic_is_sriov_vf(enic))
		return -EOPNOTSUPP;


	if (netdev->mtu > enic->port_mtu)
		netdev_warn(netdev,
			"interface MTU (%d) set higher than port MTU (%d)\n",
			netdev->mtu, enic->port_mtu);

	return _enic_change_mtu(netdev, new_mtu);
}

static void enic_change_mtu_work(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, change_mtu_work);
	struct net_device *netdev = enic->netdev;
	int new_mtu = max_t(int, ENIC_MIN_MTU,
		min_t(int, ENIC_MAX_MTU, vnic_dev_mtu(enic->vdev)));

	if (new_mtu != vnic_dev_mtu(enic->vdev))
		netdev_info(netdev, "MTU set to %d (Allowed range: %d-%d, requested value: %d)\n",
			    new_mtu, ENIC_MIN_MTU, ENIC_MAX_MTU,
			    vnic_dev_mtu(enic->vdev));

	rtnl_lock();
	(void)_enic_change_mtu(netdev, new_mtu);
	rtnl_unlock();

	netdev_info(netdev, "interface MTU set as %d\n", netdev->mtu);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void enic_poll_controller(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;
	unsigned int i;

	switch (vnic_dev_get_intr_mode(vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->qp_count; i++)
			napi_reschedule(&enic->qp[i].napi);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
	case VNIC_DEV_INTR_MODE_INTX:
		napi_schedule(&enic->qp[0].napi);
		break;
	default:
		break;
	}
}
#endif

static int enic_dev_wait(struct vnic_dev *vdev,
	int (*start)(struct vnic_dev *, int),
	int (*finished)(struct vnic_dev *, int *),
	int arg)
{
	unsigned long time;
	int done;
	int err;

	BUG_ON(in_interrupt());

	err = start(vdev, arg);
	if (err)
		return err;

	/* Wait for func to complete...2 seconds max
	 */

	time = jiffies + (HZ * 2);
	do {

		err = finished(vdev, &done);
		if (err)
			return err;

		if (done)
			return 0;

		schedule_timeout_uninterruptible(HZ / 10);

	} while (time_after(time, jiffies));

	return -ETIMEDOUT;
}

static int enic_dev_open(struct enic *enic)
{
	int err;
	u32 flags = CMD_OPENF_RQ_ENABLE_THEN_POST;

	err = enic_dev_wait(enic->vdev, vnic_dev_open,
		vnic_dev_open_done, flags);
	if (err)
		dev_err(enic_get_dev(enic), "vNIC device open failed, err %d\n",
			err);

	return err;
}

static int enic_dev_hang_reset(struct enic *enic)
{
	int err;

	err = enic_dev_wait(enic->vdev, vnic_dev_hang_reset,
		vnic_dev_hang_reset_done, 0);
	if (err)
		netdev_err(enic->netdev, "vNIC hang reset failed, err %d\n",
			err);

	return err;
}

int __enic_set_rsskey(struct enic *enic)
{
	union vnic_rss_key *rss_key_buf_va;
	dma_addr_t rss_key_buf_pa;
	int i, kidx, bidx, err;

	rss_key_buf_va = pci_zalloc_consistent(enic->pdev,
					       sizeof(union vnic_rss_key),
					       &rss_key_buf_pa);

	if (!rss_key_buf_va)
		return -ENOMEM;

	for (i = 0; i < ENIC_RSS_LEN; i++) {
		kidx = i / ENIC_RSS_BYTES_PER_KEY;
		bidx = i % ENIC_RSS_BYTES_PER_KEY;
		rss_key_buf_va->key[kidx].b[bidx] = enic->rss_key[i];
	}

	spin_lock_bh(&enic->devcmd_lock);
	err = enic_set_rss_key(enic,
		rss_key_buf_pa,
		sizeof(union vnic_rss_key));
	spin_unlock_bh(&enic->devcmd_lock);

	pci_free_consistent(enic->pdev, sizeof(union vnic_rss_key),
		rss_key_buf_va, rss_key_buf_pa);

	return err;
}

static int enic_set_rsskey(struct enic *enic)
{
	netdev_rss_key_fill(enic->rss_key, ENIC_RSS_LEN);

	return __enic_set_rsskey(enic);
}

static int enic_set_rsscpu(struct enic *enic, u8 rss_hash_bits)
{
	dma_addr_t rss_cpu_buf_pa;
	union vnic_rss_cpu *rss_cpu_buf_va = NULL;
	unsigned int i;
	int err;

	rss_cpu_buf_va = pci_alloc_consistent(enic->pdev,
		sizeof(union vnic_rss_cpu), &rss_cpu_buf_pa);
	if (!rss_cpu_buf_va)
		return -ENOMEM;

	for (i = 0; i < (1 << rss_hash_bits); i++)
		(*rss_cpu_buf_va).cpu[i/4].b[i%4] = i % enic->rq_count;

	spin_lock_bh(&enic->devcmd_lock);
	err = enic_set_rss_cpu(enic,
		rss_cpu_buf_pa,
		sizeof(union vnic_rss_cpu));
	spin_unlock_bh(&enic->devcmd_lock);

	pci_free_consistent(enic->pdev, sizeof(union vnic_rss_cpu),
		rss_cpu_buf_va, rss_cpu_buf_pa);

	return err;
}

static int enic_set_niccfg(struct enic *enic, u8 rss_default_cpu,
	u8 rss_hash_type, u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable)
{
	const u8 tso_ipid_split_en = 0;
	const u8 ig_vlan_strip_en = 1;
	int err;

	/* Enable VLAN tag stripping.
	*/

	spin_lock_bh(&enic->devcmd_lock);
	err = enic_set_nic_cfg(enic,
		rss_default_cpu, rss_hash_type,
		rss_hash_bits, rss_base_cpu,
		rss_enable, tso_ipid_split_en,
		ig_vlan_strip_en);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

static int enic_set_rss_nic_cfg(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	const u8 rss_default_cpu = 0;
	const u8 rss_hash_bits = 7;
	const u8 rss_base_cpu = 0;
	u8 rss_hash_type;
	int res;
	u8 rss_enable = ENIC_SETTING(enic, RSS) && (enic->rq_count > 1);

	spin_lock_bh(&enic->devcmd_lock);
	res = vnic_dev_capable_rss_hash_type(enic->vdev, &rss_hash_type);
	spin_unlock_bh(&enic->devcmd_lock);
	if (res) {
		/* defaults for old adapters
		 */
		rss_hash_type = NIC_CFG_RSS_HASH_TYPE_IPV4	|
				NIC_CFG_RSS_HASH_TYPE_TCP_IPV4	|
				NIC_CFG_RSS_HASH_TYPE_IPV6	|
				NIC_CFG_RSS_HASH_TYPE_TCP_IPV6;
	}

	if (rss_enable) {
		if (!enic_set_rsskey(enic)) {
			if (enic_set_rsscpu(enic, rss_hash_bits)) {
				rss_enable = 0;
				dev_warn(dev, "RSS disabled, "
					"Failed to set RSS cpu indirection table.");
			}
		} else {
			rss_enable = 0;
			dev_warn(dev, "RSS disabled, Failed to set RSS key.\n");
		}
	}

	return enic_set_niccfg(enic, rss_default_cpu, rss_hash_type,
		rss_hash_bits, rss_base_cpu, rss_enable);
}

/*
 * Note - fetch_index is not reliable for cards prior to Beverly.  Thus log
 * messages based on fetch_index should be ignored for cards prior to
 * Beverly. Details can be found in CDETS CSCvj73832.
 */
static int enic_rq_error(struct enic_qp *qp, bool fwlog, u32 error_status)
{

	struct rq_enet_desc rq_descs[ENIC_LOG_DESC_COUNT];
	struct enic *enic = qp->enic;
	dma_addr_t qerror_handle = 0;
	char prefix[PREFIX_LEN];
	struct qerror_s *qerr;
	u32 posted_index;
	u32 fetch_index;
	unsigned int k;
	u_int8_t *p;


	fetch_index = ioread32(&qp->rq.ctrl->fetch_index);
	posted_index = ioread32(&qp->rq.ctrl->posted_index);
	netdev_err(enic->netdev, "RQ[%d] error_status %d fetch_index %u posted_index %u\n",
		   qp->index, error_status, fetch_index, posted_index);
	fetch_index = (fetch_index + 2) % enic->config.rq_desc_count;

	for (k = 0; k < ENIC_LOG_DESC_COUNT; k++) {
		snprintf(prefix, PREFIX_LEN, "RQ[%u] desc[%u] :", qp->index,
			 fetch_index);
		memcpy(&rq_descs[k], &qp->rq.rq_base[fetch_index],
		       sizeof(struct rq_enet_desc));
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_ADDRESS, 16, 1,
			       &rq_descs[k], sizeof(struct rq_enet_desc), 0);
		fetch_index = (fetch_index - 1) % enic->config.rq_desc_count;
	}

	if (fwlog) {
		qerr = pci_zalloc_consistent(enic->pdev,
				     ENIC_RQERR_BUF_SZ,
				     &qerror_handle);
		if (!qerr)
			return -ENOMEM;

		p = vnic_queue_error_fill(qerr, QUEUE_ERROR_TYPE_V0,
					  sizeof(rq_descs),
					  QUEUE_ERROR_QTYPE_RQ,
					  qp->index, error_status,
					  ENIC_LOG_DESC_COUNT);

		memcpy(p, rq_descs, sizeof(rq_descs));

		enic_dev_log_qerror(enic, qerror_handle, ENIC_RQERR_BUF_SZ);
		pci_free_consistent(enic->pdev, ENIC_RQERR_BUF_SZ, qerr,
				    qerror_handle);
	}

	return 0;
}

/*
 * Note - fetch_index is not reliable for cards prior to Beverly.  Thus log
 * messages based on fetch_index should be ignored for cards prior to
 * Beverly. Details can be found in CDETS CSCvj73832.
 */
static int enic_wq_error(struct enic_qp *qp, bool fwlog, u32 error_status)
{

	struct wq_enet_desc wq_descs[ENIC_LOG_DESC_COUNT];
	struct enic *enic = qp->enic;
	dma_addr_t qerror_handle = 0;
	char prefix[PREFIX_LEN];
	struct qerror_s *qerr;
	u32 posted_index;
	u32 fetch_index;
	unsigned int k;
	u_int8_t *p;


	fetch_index = ioread32(&qp->wq.ctrl->fetch_index);
	posted_index = ioread32(&qp->wq.ctrl->posted_index);
	netdev_err(enic->netdev, "WQ[%d] error_status %d fetch_index %u posted_index %u\n",
		   qp->index, error_status, fetch_index, posted_index);
	fetch_index = (fetch_index + 2) % enic->config.wq_desc_count;

	for (k = 0; k < ENIC_LOG_DESC_COUNT; k++) {
		snprintf(prefix, PREFIX_LEN, "WQ[%u] desc[%u] :", qp->index,
			 fetch_index);
		memcpy(&wq_descs[k], &qp->wq.wq_base[fetch_index],
		       sizeof(struct wq_enet_desc));
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_ADDRESS, 16, 1,
			       &wq_descs[k], sizeof(struct wq_enet_desc), 0);
		fetch_index = (fetch_index - 1) % enic->config.wq_desc_count;
	}
	if (fwlog) {
		qerr = pci_zalloc_consistent(enic->pdev, ENIC_WQERR_BUF_SZ,
					     &qerror_handle);
		if (!qerr)
			return -ENOMEM;

		p = vnic_queue_error_fill(qerr, QUEUE_ERROR_TYPE_V0,
					  sizeof(wq_descs),
					  QUEUE_ERROR_QTYPE_WQ,
					  qp->index, error_status,
					  ENIC_LOG_DESC_COUNT);

		memcpy(p, wq_descs, sizeof(wq_descs));

		enic_dev_log_qerror(enic, qerror_handle, ENIC_WQERR_BUF_SZ);
		pci_free_consistent(enic->pdev, ENIC_WQERR_BUF_SZ, qerr,
				    qerror_handle);
	}

	return 0;
}

static void enic_log_qerror(struct enic *enic)
{
	bool fwlog = true;
	u32 error_status;
	unsigned int i;
	int err;

	err = enic_dev_log_qerror_capable(enic);
	if (err)
		fwlog = false;
	for (i = 0; i < enic->wq_count; i++) {
		error_status = enic_wq_error_status(&enic->qp[i]);
		if (error_status)
			enic_wq_error(&enic->qp[i], fwlog, error_status);
	}

	for (i = 0; i < enic->rq_count; i++) {
		error_status = enic_rq_error_status(&enic->qp[i]);
		if (error_status)
			enic_rq_error(&enic->qp[i], fwlog, error_status);
	}
}

static void enic_reset(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, reset);

	if (!netif_running(enic->netdev))
		return;

	rtnl_lock();
	spin_lock(&enic->enic_api_lock);
	enic_log_qerror(enic);
	enic_stop(enic->netdev);
	enic_reset_addr_lists(enic);
	enic_set_rss_nic_cfg(enic);
	enic_dev_set_ig_vlan_rewrite_mode(enic);
	enic_open(enic->netdev);
	spin_unlock(&enic->enic_api_lock);
	call_netdevice_notifiers(NETDEV_REBOOT, enic->netdev);
	rtnl_unlock();
}

static void enic_tx_hang_reset(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, tx_hang_reset);

	rtnl_lock();

	spin_lock(&enic->enic_api_lock);
	enic_log_qerror(enic);
	enic_dev_hang_notify(enic);
	enic_stop(enic->netdev);
	enic_dev_hang_reset(enic);
	enic_reset_addr_lists(enic);
	enic_set_rss_nic_cfg(enic);
	enic_dev_set_ig_vlan_rewrite_mode(enic);
	enic_open(enic->netdev);
	spin_unlock(&enic->enic_api_lock);
	call_netdevice_notifiers(NETDEV_REBOOT, enic->netdev);

	rtnl_unlock();
}

static void enic_free_intr_res(struct enic *enic)
{
	kfree(enic->intr);
	enic->intr = NULL;
	kfree(enic->intr_map);
	enic->intr_map = NULL;

	if (enic->config.intr_mode == VENET_INTR_MODE_ANY) {
		kfree(enic->msix_entry);
		enic->msix_entry = NULL;
		kfree(enic->msix);
		enic->msix = NULL;
	}
}

static int enic_alloc_intr_res(struct enic *enic)
{
	if (!enic->total_intr_count)
		return -EINVAL;

	enic->intr = kcalloc(enic->total_intr_count,
			     sizeof(struct vnic_intr), GFP_KERNEL);
	if (!enic->intr)
		return -ENOMEM;

	enic->intr_map = kcalloc(BITS_TO_LONGS(enic->total_intr_count),
				 sizeof(unsigned long),
				 GFP_KERNEL);
	if (!enic->intr_map)
		goto free_vnic_intr;

	if (enic->config.intr_mode == VENET_INTR_MODE_ANY) {
		enic->msix_entry = kcalloc(enic->total_intr_count,
					   sizeof(struct msix_entry),
					   GFP_KERNEL);
		if (!enic->msix_entry)
			goto free_intr_map;

		enic->msix = kcalloc(enic->total_intr_count,
				     sizeof(struct enic_msix_entry),
				     GFP_KERNEL);
		if (!enic->msix)
			goto free_msix_entry;
	}

	return 0;

free_msix_entry:
	kfree(enic->msix_entry);

free_intr_map:
	kfree(enic->intr_map);

free_vnic_intr:
	kfree(enic->intr);

	return -ENOMEM;
}

static void enic_release_intr(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	int err;
	int i;

	for (i = 0; i < enic->intr_count; i++) {
		err = enic_api_release_intr(enic->netdev, i);
		if (err < 0)
			dev_err(dev, "Failed to release intr %d\n", i);
	}
}

static int enic_reserve_intr(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	int err = 0;
	int i;

	for (i = 0; i < enic->intr_count; i++) {
		err = enic_api_reserve_intr(enic->netdev);
		if (err < 0) {
			dev_err(dev, "Failed to reserve intr %d\n", i);
			goto release_intr;
		}
	}

	return 0;

release_intr:
	enic_release_intr(enic);
	return -ENOSPC;
}

static int enic_set_intr_mode(struct enic *enic)
{
	unsigned int n = min_t(unsigned int, enic->rq_count, ENIC_RQ_MAX);
	unsigned int m = min_t(unsigned int, enic->wq_count, ENIC_WQ_MAX);
	unsigned int i;
	int ret;

	if (enic->total_intr_count < 3) {
		if (enic->config.intr_mode == VNIC_DEV_INTR_MODE_INTX) {
			netdev_err(enic->netdev,
				   "Atleast 3 interrupts are needed for INTx");
			return -EINVAL;
		}
		n = m = 1;
		enic->intr_count = 1;
		/* MSIX and INTx needs atleast 3 interrupts. If not try MSI */
		enic->config.intr_mode = VENET_INTR_MODE_MSI;
	} else {
		n = min_t(unsigned int, n, enic->total_intr_count - 2);
		m = min_t(unsigned int, m, enic->total_intr_count - 2);
	}

	n = min_t(unsigned int, n, num_online_cpus());
	m = min_t(unsigned int, m, num_online_cpus());

	/* Need one cq for every wq and one cq for ever rq. */
	if (enic->cq_count < (n + m)) {
		n = min_t(unsigned int, n, (enic->cq_count / 2));
		m = min_t(unsigned int, m, (enic->cq_count / 2));
		if (!n || !m)
			return -ENOSPC;
	}
	enic->rq_count = n;
	enic->wq_count = m;
	enic->cq_count = n + m;
	enic->qp_count = max_t(unsigned int, n, m);
	enic->intr_count = enic->qp_count + 2;

	switch (enic->config.intr_mode) {
	case VENET_INTR_MODE_ANY:
		for (i = 0; i < enic->total_intr_count; i++)
			enic->msix_entry[i].entry = i;
		ret = pci_enable_msix_range(enic->pdev, enic->msix_entry,
					    enic->intr_count,
					    enic->total_intr_count);
		if (ret >= enic->intr_count) {
			vnic_dev_set_intr_mode(enic->vdev,
					       VNIC_DEV_INTR_MODE_MSIX);
			enic->total_intr_count = ret;
			return 0;
		}
		fallthrough;
		/* Fall through */
	case VENET_INTR_MODE_MSI:
		enic->rq_count = 1;
		enic->wq_count = 1;
		enic->qp_count = 1;
		enic->intr_count = 1;
		enic->cq_count = 2;
		ret = pci_enable_msi(enic->pdev);
		if (!ret) {
			vnic_dev_set_intr_mode(enic->vdev,
					       VNIC_DEV_INTR_MODE_MSI);
			enic->total_intr_count = enic->intr_count;
			return 0;
		}
		if (enic->total_intr_count < 3)
			break;
		fallthrough;
		/* Fall through */
	case VENET_INTR_MODE_INTX:
		enic->rq_count = 1;
		enic->wq_count = 1;
		enic->qp_count = 1;
		enic->cq_count = 2;
		enic->intr_count = 3;
		enic->total_intr_count = enic->intr_count;
		vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_INTX);
		return 0;
	default:
		return -ENODEV;
	}

	return -ENOSPC;
}

static void enic_clear_intr_mode(struct enic *enic)
{
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		pci_disable_msix(enic->pdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		pci_disable_msi(enic->pdev);
		break;
	default:
		break;
	}

	vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);
}

static int enic_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		ret = enic_set_hwtstamp(netdev, ifr);
		break;
	case SIOCGHWTSTAMP:
		ret = enic_get_hwtstamp(netdev, ifr);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

#if (ENIC_HAVE_NET_DEVICE_OPS)
static const struct net_device_ops enic_netdev_dynamic_ops = {
#if (VIC_HAVE_EXTENDED_NDO_CHANGE_MTU)
	.ndo_size		= sizeof(const struct net_device_ops),
#endif
	.ndo_open		= enic_open,
	.ndo_stop		= enic_stop,
	.ndo_start_xmit		= enic_hard_start_xmit,
#if (!VIC_HAVE_NDO_GET_STATS64)
	.ndo_get_stats		= enic_get_stats,
#else
	.ndo_get_stats64	= enic_get_stats,
#endif
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= enic_set_rx_mode,
#if (ENIC_HAVE_SET_MULTICAST_LIST)
	.ndo_set_multicast_list = enic_set_rx_mode,
#endif
	.ndo_set_mac_address	= enic_set_mac_address_dynamic,
#if (VIC_HAVE_EXTENDED_NDO_CHANGE_MTU)
	.extended.ndo_change_mtu		= enic_change_mtu,
#else
	.ndo_change_mtu		= enic_change_mtu,
#endif
#if (ENIC_HAVE_VLAN_RX_REGISTER)
	.ndo_vlan_rx_register   = enic_vlan_rx_register,
#endif
	.ndo_vlan_rx_add_vid	= enic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enic_vlan_rx_kill_vid,
	.ndo_tx_timeout		= enic_tx_timeout,
#ifdef IFLA_VF_PORT_MAX
	.ndo_set_vf_port	= enic_set_vf_port,
	.ndo_get_vf_port	= enic_get_vf_port,
	.ndo_set_vf_mac		= enic_set_vf_mac,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= enic_poll_controller,
#endif
#ifdef CONFIG_RFS_ACCEL
#if (!RHEL_RELEASE_CODE || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 0)))
	.ndo_rx_flow_steer	= enic_rx_flow_steer,
#endif /*RHEL_RELEASE_VERSION >= 7.0*/
#endif
#if IS_ENABLED(CONFIG_VXLAN)
#if ((ENIC_HAVE_DEL_VXLAN_PORT) && (VIC_HAVE_ADD_VXLAN_PORT))
	.ndo_add_vxlan_port	= enic_udp_tunnel_add,
	.ndo_del_vxlan_port	= enic_udp_tunnel_del,
#elif (ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL)
	.ndo_udp_tunnel_add	= enic_udp_tunnel_add,
	.ndo_udp_tunnel_del	= enic_udp_tunnel_del,
#endif
#if (VIC_HAVE_FEATURES_CHECK)
	.ndo_features_check	= enic_features_check,
#endif
#endif
#if (VIC_HAVE_ETH_IOCTL)
	.ndo_eth_ioctl		= enic_do_ioctl,
#else
	.ndo_do_ioctl		= enic_do_ioctl,
#endif
};

static const struct net_device_ops enic_netdev_ops = {
#if (VIC_HAVE_EXTENDED_NDO_CHANGE_MTU)
	.ndo_size		= sizeof(const struct net_device_ops),
#endif
	.ndo_open		= enic_open,
	.ndo_stop		= enic_stop,
	.ndo_start_xmit		= enic_hard_start_xmit,
#if (!VIC_HAVE_NDO_GET_STATS64)
	.ndo_get_stats		= enic_get_stats,
#else
	.ndo_get_stats64	= enic_get_stats,
#endif
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= enic_set_mac_address,
	.ndo_set_rx_mode	= enic_set_rx_mode,
#if (ENIC_HAVE_SET_MULTICAST_LIST)
	.ndo_set_multicast_list	= enic_set_rx_mode,
#endif
#if (VIC_HAVE_EXTENDED_NDO_CHANGE_MTU)
	.extended.ndo_change_mtu		= enic_change_mtu,
#else
	.ndo_change_mtu		= enic_change_mtu,
#endif
#if (ENIC_HAVE_VLAN_RX_REGISTER)
	.ndo_vlan_rx_register	= enic_vlan_rx_register,
#endif
	.ndo_vlan_rx_add_vid	= enic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enic_vlan_rx_kill_vid,
	.ndo_tx_timeout		= enic_tx_timeout,
#ifdef IFLA_VF_PORT_MAX
	.ndo_set_vf_port	= enic_set_vf_port,
	.ndo_get_vf_port	= enic_get_vf_port,
	.ndo_set_vf_mac		= enic_set_vf_mac,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= enic_poll_controller,
#endif
#ifdef CONFIG_RFS_ACCEL
#if (VIC_HAVE_RX_FLOW_STEER)
	.ndo_rx_flow_steer	= enic_rx_flow_steer,
#endif
#endif
#if IS_ENABLED(CONFIG_VXLAN)
#if ((ENIC_HAVE_DEL_VXLAN_PORT) && (VIC_HAVE_ADD_VXLAN_PORT))
	.ndo_add_vxlan_port	= enic_udp_tunnel_add,
	.ndo_del_vxlan_port	= enic_udp_tunnel_del,
#elif (ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL)
	.ndo_udp_tunnel_add	= enic_udp_tunnel_add,
	.ndo_udp_tunnel_del	= enic_udp_tunnel_del,
#endif
#if (VIC_HAVE_FEATURES_CHECK)
	.ndo_features_check	= enic_features_check,
#endif
#endif
#if (VIC_HAVE_ETH_IOCTL)
	.ndo_eth_ioctl		= enic_do_ioctl,
#else
	.ndo_do_ioctl		= enic_do_ioctl,
#endif
};

#endif
static void enic_dev_deinit(struct enic *enic)
{
	enic_release_intr(enic);
	enic_clear_intr_mode(enic);
	enic_free_affinity_hint(enic);
	enic_free_intr_res(enic);
}

static void enic_kdump_kernel_config(struct enic *enic)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	if (is_kdump_kernel()) {
		dev_info(enic_get_dev(enic), "Running from within kdump kernel. Using minimal resources\n");
		enic->rq_count = 1;
		enic->wq_count = 1;
		enic->config.rq_desc_count = ENIC_MIN_RQ_DESCS;
		enic->config.wq_desc_count = ENIC_MIN_WQ_DESCS;
		enic->config.mtu = min_t(u16, 1500, enic->config.mtu);
	}
#endif
}

static int enic_dev_init(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	int err;

	/* Get interrupt coalesce timer info from fw */
	err = enic_dev_intr_coal_timer_info(enic);
	if (err) {
		dev_warn(dev, "Using default conversion factor for "
			"interrupt coalesce timer\n");
		vnic_dev_intr_coal_timer_info_default(enic->vdev);
	}

	/* Get vNIC configuration
	 */

	err = enic_get_vnic_config(enic);
	if (err) {
		dev_err(dev, "Get vNIC configuration failed, aborting\n");
		return err;
	}

	/* Get available resource counts
	 */

	enic_get_res_counts(enic);

	/* modify resource count if we are in kdump_kernel
	 */
	enic_kdump_kernel_config(enic);

	/* allocate interrupt resources before setting intr mode */
	err = enic_alloc_intr_res(enic);
	if (err) {
		dev_err(dev, "Failed to alloc intr resources, aborting\n");
		return err;
	}

	/* Set interrupt mode based on resource counts and system
	 * capabilities
	 */

	err = enic_set_intr_mode(enic);
	if (err) {
		dev_err(dev, "Failed to set intr mode based on resource "
			"counts and system capabilities, aborting\n");
		goto err_out_free_vnic_resources;
	}

	err = enic_reserve_intr(enic);
	if (err) {
		dev_err(dev, "Failed to reserve interrupts, aborting\n");
		goto err_out_free_vnic_resources;
	}

	err = enic_set_rss_nic_cfg(enic);
	if (err) {
		dev_err(dev, "Failed to config nic, aborting\n");
		goto err_out_release_intr_resources;
	}

	return 0;

err_out_release_intr_resources:
	enic_release_intr(enic);
err_out_free_vnic_resources:
	enic_free_affinity_hint(enic);
	enic_clear_intr_mode(enic);
	enic_free_intr_res(enic);

	return err;
}

static void enic_iounmap(struct enic *enic)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(enic->bar); i++)
		if (enic->bar[i].vaddr)
			iounmap(enic->bar[i].vaddr);
}

static void enic_find_rdma_capability(struct enic *enic)
{
	u64 rdma_cap, rdma_features;
	int err;

	rdma_cap = rdma_features = 0;

	err = vnic_dev_capable(enic->vdev, CMD_RDMA_CTRL);
	if (!err)
		return;

	err = vnic_dev_get_supported_feature_ver(enic->vdev,
						 VIC_FEATURE_RDMA,
						 &rdma_cap, &rdma_features);
	if (err)
		return;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX) {
		netdev_info(enic->netdev,
			    "RoCEv2 disabled. Please enable MSIx\n");
		return;
	}

	enic->rdma_cap = rdma_cap;
	enic->rdma_features = rdma_features;
}

static void enic_deinit_rdma_device(struct enic *enic)
{
	if (enic->rdma_cap & ENIC_RDMA_V3_ENABLED ||
		enic->rdma_cap & ENIC_RDMA_V4_ENABLED)
		device_unregister(&enic->rdma_dev);
}

static void enic_rdma_device_release(struct device *dev)
{

}

#define ENIC_RDMA_MOD "enic_rdma"
static void enic_init_rdma_device(struct enic *enic)
{
	spin_lock_bh(&enic->devcmd_lock);
	enic_find_rdma_capability(enic);
	spin_unlock_bh(&enic->devcmd_lock);

	if (enic->rdma_cap & ENIC_RDMA_V3_ENABLED ||
		enic->rdma_cap & ENIC_RDMA_V4_ENABLED) {
		struct device *rdma_dev = &enic->rdma_dev;
		int err;

		rdma_dev->parent = &enic->pdev->dev;
		rdma_dev->bus = &enic_rdma_bus;
		rdma_dev->release = enic_rdma_device_release;
		rdma_dev->platform_data = enic->pdev;
		dev_set_name(rdma_dev, "enic_rdma-%s", pci_name(enic->pdev));
		err = device_register(rdma_dev);
		if (err) {
			netdev_err(enic->netdev,
				   "rdma device registration failed\n");
			put_device(rdma_dev);
		}
		request_module_nowait(ENIC_RDMA_MOD);
	}
}

static int enic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct enic *enic;
	int using_dac = 0;
	unsigned int i;
	int err;
#ifdef CONFIG_PCI_IOV
	int pos = 0;
#endif
#ifdef IFLA_VF_PORT_MAX
	int num_pps = 1;
#endif

	/* Allocate net device structure and initialize.  Private
	 * instance data is initialized to zero.
	 */

	netdev = alloc_etherdev_mqs(sizeof(struct enic),
			ENIC_RQ_MAX, ENIC_WQ_MAX);
	if (!netdev)
		return -ENOMEM;

	pci_set_drvdata(pdev, netdev);

#if (ENIC_HAVE_SET_MODULE_OWNER)
	SET_MODULE_OWNER(netdev);
#endif
	SET_NETDEV_DEV(netdev, &pdev->dev);

	enic = netdev_priv(netdev);
	enic->netdev = netdev;
	enic->pdev = pdev;

	/* Setup PCI resources
	 */

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device, aborting\n");
		goto err_out_free_netdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "Cannot request PCI regions, aborting\n");
		goto err_out_disable_device;
	}

	pci_set_master(pdev);

	/* Query PCI controller on system for DMA addressing
	 * limitation for the device.  Try 47-bit first, and
	 * fail to 32-bit.
	 */

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(47));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(dev, "No usable DMA configuration, aborting\n");
			goto err_out_release_regions;
		}
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(dev, "Unable to obtain %u-bit DMA "
				"for consistent allocations, aborting\n", 32);
			goto err_out_release_regions;
		}
	} else {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(47));
		if (err) {
			dev_err(dev, "Unable to obtain %u-bit DMA "
				"for consistent allocations, aborting\n", 47);
			goto err_out_release_regions;
		}
		using_dac = 1;
	}

	/* Map vNIC resources from BAR0-5
	 */

	for (i = 0; i < ARRAY_SIZE(enic->bar); i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		enic->bar[i].len = pci_resource_len(pdev, i);
		enic->bar[i].vaddr = pci_iomap(pdev, i, enic->bar[i].len);
		if (!enic->bar[i].vaddr) {
			dev_err(dev, "Cannot memory-map BAR %d, aborting\n", i);
			err = -ENODEV;
			goto err_out_iounmap;
		}
		enic->bar[i].bus_addr = pci_resource_start(pdev, i);
	}

	/* Register vNIC device
	 */

	enic->vdev = vnic_dev_alloc_discover(NULL, enic, pdev, enic->bar,
		ARRAY_SIZE(enic->bar));
	if (!enic->vdev) {
		dev_err(dev, "vNIC registration failed, aborting\n");
		err = -ENODEV;
		goto err_out_iounmap;
	}

	/* Initialize devcmd2 only for PF
	 */
	err = vnic_devcmd_init(enic->vdev, 1);
	if (err) {
		dev_err(dev, "vnic_devcmd_init() returns %d, aborting\n", err);
		goto err_out_vnic_unregister;
	}

#ifdef CONFIG_PCI_IOV
	/* Get number of subvnics */
	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (pos) {
		pci_read_config_word(pdev, pos + PCI_SRIOV_TOTAL_VF,
				     &enic->num_vfs);
		if (enic->num_vfs){
			err = pci_enable_sriov(pdev, enic->num_vfs);
			if (err){
				dev_err(dev, "SRIOV enable failed, aborting.\
					pci_enable_sriov() returned %d\n", err);
				goto err_out_vnic_unregister;
			}
			enic->priv_flags |= ENIC_SRIOV_ENABLED;
#ifdef IFLA_VF_PORT_MAX
			num_pps = enic->num_vfs;
#endif
		}
	}
#endif

#ifdef IFLA_VF_PORT_MAX
        /* Allocate structure for port profiles */
	enic->pp = kcalloc(num_pps, sizeof(*enic->pp), GFP_KERNEL);
	if (!enic->pp) {
		err = -ENOMEM;
		goto err_out_disable_sriov_pp;
	}

#endif
	/* Issue device open to get device in known state
	 */

	err = enic_dev_open(enic);
	if (err) {
		dev_err(dev, "vNIC dev open failed, aborting\n");
		goto err_out_disable_sriov;
	}

	/* Setup devcmd lock
	 */

	spin_lock_init(&enic->devcmd_lock);

	/* Setup enic api lock
	 */
	spin_lock_init(&enic->enic_api_lock);

	/*
	 * Set ingress vlan rewrite mode before vnic initialization
	 */

	err = enic_dev_set_ig_vlan_rewrite_mode(enic);
	if (err) {
		dev_err(dev,
			"Failed to set ingress vlan rewrite mode, aborting.\n");
		goto err_out_dev_close;
	}

	/* Issue device init to initialize the vnic-to-switch link.
	 * We'll start with carrier off and wait for link UP
	 * notification later to turn on carrier.  We don't need
	 * to wait here for the vnic-to-switch link initialization
	 * to complete; link UP notification is the indication that
	 * the process is complete.
	 */

	netif_carrier_off(netdev);

	/* Do not call dev_init for a dynamic vnic.
	 * For a dynamic vnic, init_prov_info will be
	 * called later by an upper layer.
	 */

	if (!enic_is_dynamic(enic)) {
		err = vnic_dev_init(enic->vdev, 0);
		if (err) {
			dev_err(dev, "vNIC dev init failed, aborting\n");
			goto err_out_dev_close;
		}
	}

	mutex_init(&enic->intr_map_lock);

	err = enic_dev_init(enic);
	if (err) {
		dev_err(dev, "Device initialization failed, aborting\n");
		goto err_out_dev_close;
	}

	err = enic_alloc_qp(enic);
	if (err)
		goto err_out_dev_deinit;

	netif_set_real_num_tx_queues(netdev, enic->wq_count);
	netif_set_real_num_rx_queues(netdev, enic->rq_count);

	/* Setup notification timer, HW reset task, and wq locks
	 */

	enic_timer_setup(&enic->notify_timer, enic_notify_timer, enic, 0);
	enic_rfs_flw_tbl_init(enic);

	INIT_WORK(&enic->reset, enic_reset);
	INIT_WORK(&enic->tx_hang_reset, enic_tx_hang_reset);
	INIT_WORK(&enic->change_mtu_work, enic_change_mtu_work);

	/* Register net device
	 */

	enic->port_mtu = max_t(int, ENIC_MIN_MTU,
		min_t(int, ENIC_MAX_MTU, enic->config.mtu));

	if (enic->config.mtu != enic->port_mtu)
		dev_info(dev, "MTU set to %d (Allowed range: %d-%d, requested value: %d)\n",
			 enic->port_mtu, ENIC_MIN_MTU, ENIC_MAX_MTU,
			 enic->config.mtu);

	err = enic_set_mac_addr(netdev, enic->mac_addr);
	if (err) {
		dev_err(dev, "Invalid MAC address (%pM), aborting\n",
			enic->mac_addr);
		goto err_out_dev_deinit;
	}

	enic->rx_coal.coal_usecs = enic->config.intr_timer_usec;
	enic->rx_coal.acoal_high = enic->config.intr_timer_usec;
	enic->rx_coal.acoal_low = ENIC_AIC_MIN_DEFAULT;
	enic->rx_coal.use_adaptive_rx_coalesce = true;

#if (!ENIC_HAVE_NDO_OPEN)
	netdev->open = enic_open;
	netdev->stop = enic_stop;
	netdev->hard_start_xmit = enic_hard_start_xmit;
	netdev->get_stats = enic_get_stats;
#if (ENIC_HAVE_SET_RX_MODE)
	netdev->set_rx_mode = enic_set_rx_mode;
#endif
	netdev->set_multicast_list = enic_set_rx_mode;
	netdev->set_mac_address = enic_set_mac_address;
	netdev->change_mtu = enic_change_mtu;
	netdev->vlan_rx_register = enic_vlan_rx_register;
	netdev->vlan_rx_add_vid = enic_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = enic_vlan_rx_kill_vid;
	netdev->tx_timeout = enic_tx_timeout;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = enic_poll_controller;
#endif
#else
	if (enic_is_dynamic(enic) || enic_is_sriov_vf(enic))
		netdev->netdev_ops = &enic_netdev_dynamic_ops;
	else
		netdev->netdev_ops = &enic_netdev_ops;
#endif
#ifdef CONFIG_RFS_ACCEL
#if (VIC_HAVE_NETDEV_EXTENDED)
	netdev_extended(netdev)->rfs_data.ndo_rx_flow_steer = enic_rx_flow_steer;
#endif
#endif /*CONFIG_RFS_ACCEL*/

	netdev->watchdog_timeo = 5 * HZ;
	enic_set_ethtool_ops(enic);

	netdev->features |= NETIF_F_HW_VLAN_CTAG_RX;
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
	netdev->features |= NETIF_F_HW_VLAN_CTAG_TX;
#else
	netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
#endif
	if (ENIC_SETTING(enic, LOOP)) {
		netdev->features &= ~NETIF_F_HW_VLAN_CTAG_TX;
		enic->loop_enable = 1;
		enic->loop_tag = enic->config.loop_tag;
		dev_info(dev, "loopback tag=0x%04x\n", enic->loop_tag);
	}
	if (ENIC_SETTING(enic, TXCSUM))
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
		netdev->features |= NETIF_F_SG | NETIF_F_HW_CSUM;
#else
		netdev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM;
#endif
	if (ENIC_SETTING(enic, TSO))
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
		netdev->features |= NETIF_F_TSO |
#else
		netdev->hw_features |= NETIF_F_TSO |
#endif
			NETIF_F_TSO6 | NETIF_F_TSO_ECN;
#if ((RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 2)) || \
     LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	if (ENIC_SETTING(enic, RSS))
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
		netdev->features |= NETIF_F_RXHASH;
#else
		netdev->hw_features |= NETIF_F_RXHASH;
#endif
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 00))
	if (ENIC_SETTING(enic, LRO))
		netdev->features |= NETIF_F_GRO;

	enic->csum_rx_enabled = ENIC_SETTING(enic, RXCSUM);

#else
	if (ENIC_SETTING(enic, RXCSUM))
		netdev->hw_features |= NETIF_F_RXCSUM;
#if IS_ENABLED(CONFIG_VXLAN)
	if (ENIC_SETTING(enic, VXLAN)) {
		u64 patch_level;
		u64 a1 = 0;

		enic_enable_vxlan_offload(enic);
		/* get bit mask from hw about supported offload bit level
		 * BIT(0) = fw supports patch_level 0
		 *	    fcoe bit = encap
		 *	    fcoe_fc_crc_ok = outer csum ok
		 * BIT(1) = always set by fw
		 * BIT(2) = fw supports patch_level 2
		 *	    BIT(0) in rss_hash = encap
		 *	    BIT(1,2) in rss_hash = outer_ip_csum_ok/
		 *				   outer_tcp_csum_ok
		 * used in enic_rq_indicate_buf
		 */
		err = vnic_dev_get_supported_feature_ver(enic->vdev,
							 VIC_FEATURE_VXLAN,
							 &patch_level, &a1);
		if (err)
			patch_level = 0;
		enic->vxlan.flags = (u8)a1;
		/* mask bits that are supported by driver
		 */
		patch_level &= BIT_ULL(0) | BIT_ULL(2);
		patch_level = fls(patch_level);
		patch_level = patch_level ? patch_level - 1 : 0;
		enic->vxlan.patch_level = patch_level;

#if (ENIC_HAVE_UDP_TUNNEL_NIC_INFO && (!ENIC_HAVE_NDO_UDP_TUNNEL_ADD_DEL))
		if (vnic_dev_get_res_count(enic->vdev, RES_TYPE_WQ) == 1 ||
		    enic->vxlan.flags & ENIC_VXLAN_MULTI_WQ) {
			netdev->udp_tunnel_nic_info = &enic_udp_tunnels_v4;
			if (enic->vxlan.flags & ENIC_VXLAN_OUTER_IPV6)
				netdev->udp_tunnel_nic_info = &enic_udp_tunnels;
		}
#endif
	}
#else
	enic->vxlan.patch_level = 0;
	enic->vxlan.vxlan_udp_port_number = 0;
#endif /* CONFIG_VXLAN */

	netdev->features |= netdev->hw_features;
	netdev->vlan_features |= netdev->hw_features;
#endif /* kernel > 3.0 */

#ifdef CONFIG_RFS_ACCEL
#if (!ENIC_HAVE_NETDEV_HW_FEATURES)
	netdev->features |= NETIF_F_NTUPLE;
#else
	netdev->hw_features |= NETIF_F_NTUPLE;
#endif
#endif
	if (using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

#if (ENIC_HAVE_IFF_UNICAST_FLT)
	netdev->priv_flags |= IFF_UNICAST_FLT;
#endif

	/* MTU range: 68 - 9000 */
#if (VIC_HAVE_EXTENDED_MIN_MAX_MTU)
	netdev->extended->min_mtu = ENIC_MIN_MTU;
	netdev->extended->max_mtu = ENIC_MAX_MTU;
#elif (VIC_HAVE_NETDEV_MIN_MAX_MTU)
	netdev->min_mtu = ENIC_MIN_MTU;
	netdev->max_mtu = ENIC_MAX_MTU;
#endif
	netdev->mtu	= enic->port_mtu;

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Cannot register net device, aborting\n");
		goto err_out_dev_deinit;
	}

	enic_clock_init(enic);
	enic_init_rdma_device(enic);

	return 0;

err_out_dev_deinit:
	enic_dev_deinit(enic);
err_out_dev_close:
	vnic_dev_close(enic->vdev);
err_out_disable_sriov:
#ifdef IFLA_VF_PORT_MAX
	kfree(enic->pp);
err_out_disable_sriov_pp:
#endif
#ifdef CONFIG_PCI_IOV
	if (enic_sriov_enabled(enic)) {
		pci_disable_sriov(pdev);
		enic->priv_flags &= ~ENIC_SRIOV_ENABLED;
	}
#endif
err_out_vnic_unregister:
	vnic_dev_unregister(enic->vdev);
err_out_iounmap:
	enic_iounmap(enic);
err_out_release_regions:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
err_out_free_netdev:
#if (ENIC_HAVE_PCI_SET_DRVDATA)
	pci_set_drvdata(pdev, NULL);
#endif
	free_netdev(netdev);

	return err;
}

static void enic_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic;

	if (!netdev)
		return;

	enic = netdev_priv(netdev);

	enic_deinit_rdma_device(enic);
#if (ENIC_HAVE_CANCEL_WORK_SYNC)
	cancel_work_sync(&enic->reset);
	cancel_work_sync(&enic->change_mtu_work);
#else
	flush_scheduled_work();
#endif
	unregister_netdev(netdev);
	enic_dev_deinit(enic);
	enic_free_qp(enic);
	vnic_dev_close(enic->vdev);
#ifdef CONFIG_PCI_IOV
	if (enic_sriov_enabled(enic)) {
		pci_disable_sriov(pdev);
		enic->priv_flags &= ~ENIC_SRIOV_ENABLED;
	}
#endif
#ifdef IFLA_VF_PORT_MAX
	kfree(enic->pp);
#endif
	vnic_dev_unregister(enic->vdev);
	enic_iounmap(enic);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
#if (ENIC_HAVE_PCI_SET_DRVDATA)
	pci_set_drvdata(pdev, NULL);
#endif
	free_netdev(netdev);
	enic_clock_cleanup(enic);
}

static int enic_rdma_bus_match(struct device *dev, struct device_driver *drv)
{
	if (dev->bus != &enic_rdma_bus || drv->bus != &enic_rdma_bus)
		return 0;

	return 1;
}

/* bus to support RoCE v2 capable enic devices */
struct bus_type enic_rdma_bus = {
	.name = "enic_rdma",
	.match = enic_rdma_bus_match,
};
EXPORT_SYMBOL(enic_rdma_bus);

static struct pci_driver enic_driver = {
	.name = DRV_NAME,
	.id_table = enic_id_table,
	.probe = enic_probe,
	.remove = enic_remove,
};

static int __init enic_init_module(void)
{
	int err;

	pr_info("%s, ver %s\n", DRV_DESCRIPTION, DRV_VERSION);

	err = bus_register(&enic_rdma_bus);
	if (err)
		return err;

	return pci_register_driver(&enic_driver);
}

static void __exit enic_cleanup_module(void)
{
	pci_unregister_driver(&enic_driver);
	bus_unregister(&enic_rdma_bus);
}

module_init(enic_init_module);
module_exit(enic_cleanup_module);
