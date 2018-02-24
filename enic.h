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

#ifndef _ENIC_H_
#define _ENIC_H_

#include <linux/types.h>
#include <linux/clocksource.h>
#include <linux/timecounter.h>
#include <linux/net_tstamp.h>

#include "vnic_enet.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_nic.h"
#include "vnic_rss.h"
#include "enic_clock.h"
#include "enic_qp.h"
#include "enic_res.h"
#include "enic_config.h"

#define DRV_NAME		PACKAGE_NAME
#define DRV_DESCRIPTION		"Cisco VIC Ethernet NIC Driver"
#define DRV_VERSION		PACKAGE_VERSION
#define DRV_COPYRIGHT		"Copyright 2008-2017 Cisco Systems, Inc"

#ifdef CONFIG_MIPS
#define ENIC_BARS_MAX		RES_TYPE_MAX
#else
#define ENIC_BARS_MAX		6
#endif

#define ENIC_WQ_MAX		256
#define ENIC_RQ_MAX		256
#define ENIC_CQ_MAX		(ENIC_WQ_MAX + ENIC_RQ_MAX)

#define ENIC_NOTIFY_TIMER_PERIOD	(2 * HZ)
#define WQ_ENET_MAX_DESC_LEN		(1 << WQ_ENET_LEN_BITS)
#define MAX_TSO				(1 << 16)
#define ENIC_DESC_MAX_SPLITS		(MAX_TSO / WQ_ENET_MAX_DESC_LEN + 1)
#define ENIC_QERROR_TYPE_V0 MK_QUEUE_ERROR_TYPE(QUEUE_ERROR_TYPE_V0)

struct enic_msix_entry {
	int requested;
	char devname[IFNAMSIZ + 8];
	irqreturn_t (*isr)(int, void *);
	void *devid;
#if ((!RHEL_RELEASE_CODE || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 0))))
	cpumask_var_t affinity_mask;
#endif
};

struct enic_intr_mod_table {
        u32 rx_rate;
        u32 range_percent;
};

#define ENIC_AIC_TS_BREAK	100
#define ENIC_AIC_MIN_DEFAULT	3

struct enic_rx_coal {
	u16 coal_usecs;
	u16 acoal_high;
	u16 acoal_low;
	bool use_adaptive_rx_coalesce;
};

/* priv_flags */
#define ENIC_SRIOV_ENABLED		(1 << 0)

/* enic port profile set flags */
#define ENIC_PORT_REQUEST_APPLIED	(1 << 0)
#define ENIC_SET_REQUEST		(1 << 1)
#define ENIC_SET_NAME			(1 << 2)
#define ENIC_SET_INSTANCE		(1 << 3)
#define ENIC_SET_HOST			(1 << 4)

struct enic_port_profile {
	u32 set;
	u8 request;
	char name[PORT_PROFILE_MAX];
	u8 instance_uuid[PORT_UUID_MAX];
	u8 host_uuid[PORT_UUID_MAX];
	u8 vf_mac[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
};

/*
 * enic_rfs_fltr_node - rfs filter node in hash table
 *	@keys: IPv4 5 tuple
 *	@flow_id: flow_id of clsf filter provided by kernel
 *	@fltr_id: filter id of clsf filter returned by adaptor
 *	@rq_id: desired rq index
 *	@node: hlist_node
 */
struct enic_rfs_fltr_node {
	struct flow_keys keys;
	u32 flow_id;
	u16 fltr_id;
	u16 rq_id;
	struct hlist_node node;
};

/*
 * enic_rfs_flw_tbl - rfs flow table
 *	@max: Maximum number of filters vNIC supports
 *	@free: Number of free filters available
 *	@toclean: hash table index to clean next
 *	@ht_head: hash table list head
 *	@lock: spin lock
 *	@rfs_may_expire: timer function for enic_rps_may_expire_flow
 */
struct enic_rfs_flw_tbl {
	u16 max;	/* Max clsf filters vNIC supports */
	int free;	/* number of free clsf filters */

#define ENIC_RFS_FLW_BITSHIFT	(10)
#define ENIC_RFS_FLW_MASK	((1 << ENIC_RFS_FLW_BITSHIFT) - 1)
	u16 toclean:ENIC_RFS_FLW_BITSHIFT;
	struct hlist_head ht_head[1 << ENIC_RFS_FLW_BITSHIFT];
	spinlock_t lock;
	struct timer_list rfs_may_expire;
};

struct vxlan_offload {
	u16 vxlan_udp_port_number;
	u8 patch_level;
	u8 flags;
};

enum ext_cq {
	ENIC_RQ_CQ_ENTRY_SIZE_16,
	ENIC_RQ_CQ_ENTRY_SIZE_32,
	ENIC_RQ_CQ_ENTRY_SIZE_64,
	ENIC_RQ_CQ_ENTRY_SIZE_MAX,
};

/* Per-instance private data structure */
struct enic {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct vnic_enet_config config;
	struct vnic_dev_bar bar[ENIC_BARS_MAX];
	struct vnic_dev *vdev;
	struct net_device_stats net_stats;
	struct timer_list notify_timer;
	struct work_struct reset;
	struct work_struct tx_hang_reset;
	struct work_struct change_mtu_work;
	struct msix_entry *msix_entry;
	struct enic_msix_entry *msix;
	u32 msg_enable;
	spinlock_t devcmd_lock;
	u8 mac_addr[ETH_ALEN];
	u8 mc_addr[ENIC_MULTICAST_PERFECT_FILTERS][ETH_ALEN];
	u8 uc_addr[ENIC_UNICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int flags;
	unsigned int priv_flags;
	unsigned int mc_count;
	unsigned int uc_count;
	unsigned int ext_cq;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 00))
	int csum_rx_enabled;
#endif
	u32 port_mtu;
	struct enic_rx_coal rx_coal;
#ifdef CONFIG_PCI_IOV
        u16 num_vfs;
#endif
	spinlock_t enic_api_lock;
	struct enic_port_profile *pp;
	struct vxlan_offload vxlan;

	/* work queue cache line section */
	unsigned int wq_count;
#if (ENIC_HAVE_VLAN_GROUP)
	struct vlan_group *vlan_group;
#endif
	u16 loop_enable;
	u16 loop_tag;

	struct enic_qp *qp;
	struct enic_qp_ring *qp_ring;
	unsigned int qp_count;
	unsigned int intr_count;
	unsigned int total_intr_count;
	struct vnic_intr_ctrl __iomem *err_ctrl;
	struct vnic_intr_ctrl __iomem *notify_ctrl;
	struct vnic_ptp_ctrl __iomem *ptp_ctrl;

	struct vnic_intr *intr;
	unsigned long *intr_map;
	/* protect inter_map from multiple ENIC interfaces */
	struct mutex intr_map_lock;

	unsigned int rq_count;
	u32 __iomem *legacy_pba;		/* memory-mapped */

	unsigned int cq_count;
	struct enic_rfs_flw_tbl rfs_h;	/* for accel rfs */
	u8 rss_key[ENIC_RSS_LEN];
	struct vnic_gen_stats gen_stats;

	/* Additional enic_rdma capability */
	u64 rdma_cap;
	/* Additional enic_rdma features */
	u64 rdma_features;
	struct device rdma_dev;
	____cacheline_aligned struct enic_hwtstamp tstamp;
};

static inline struct device *enic_get_dev(struct enic *enic)
{
	return &(enic->pdev->dev);
}

#define ENIC_LEGACY_IO_INTR	0
#define ENIC_LEGACY_ERR_INTR	1
#define ENIC_LEGACY_NOTIFY_INTR	2

static inline unsigned int enic_msix_notify_intr(struct enic *enic)
{
	return enic->qp_count + 1;
}

static inline void enic_intr_return_credits(struct vnic_intr_ctrl *ctrl,
					    unsigned int credits,
					    int unmask, int reset_timer)
{
	u32 value = (credits & 0xffff) |
		    (unmask ? (1 << VNIC_INTR_UNMASK_SHIFT) : 0) |
		    (reset_timer ? (1 << VNIC_INTR_RESET_TIMER_SHIFT) : 0);
	iowrite32(value, &ctrl->int_credit_return);
}

static inline void enic_intr_return_all_credits(struct vnic_intr_ctrl *ctrl)
{
	unsigned int credits;
	int unmask = 1;
	int reset_timer = 1;

	credits = ioread32(&ctrl->int_credits);
	enic_intr_return_credits(ctrl, credits, unmask, reset_timer);
}

void enic_reset_addr_lists(struct enic *enic);
int enic_is_dynamic(struct enic *enic);
int enic_sriov_enabled(struct enic *enic);
int enic_is_valid_vf(struct enic *enic, int vf);
int __enic_set_rsskey(struct enic *enic);
void enic_intr_ctrl_init(struct vnic_intr_ctrl __iomem *ctrl,
				       u32 coalescing_timer,
				       u32 coalescing_type,
				       u32 mask_on_assertion, u32 int_credits);

#endif /* _ENIC_H_ */
