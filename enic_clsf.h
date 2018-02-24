 /*
  *  Copyright 2013 Cisco Systems, Inc.  All rights reserved.
  */

#ifndef _ENIC_CLSF_H_
#define _ENIC_CLSF_H_

#include "vnic_dev.h"
#include "enic.h"
#include "kcompat.h"

#define ENIC_CLSF_EXPIRE_COUNT 128

int enic_addfltr_mac_vlan(struct enic *enic, u8 *macaddr,
	u16 vlan_id, u16 rq_id, u16 *filter_id);
int enic_delfltr(struct enic *enic, u16 filter_id);
int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq);
void enic_rfs_flw_tbl_init(struct enic *enic);
void enic_rfs_flw_tbl_free(struct enic *enic);
struct enic_rfs_fltr_node *htbl_fltr_search(struct enic *enic, u16 fltr_id);

#ifdef CONFIG_RFS_ACCEL

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
void enic_flow_may_expire(unsigned long data);
#else
void enic_flow_may_expire(struct timer_list *t);
#endif /* kernel < 4.15 */

int enic_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id);
static inline void enic_rfs_timer_init(struct enic *enic)
{
	enic_timer_setup(&enic->rfs_h.rfs_may_expire, enic_flow_may_expire,
			 enic, 0);
	mod_timer(&enic->rfs_h.rfs_may_expire, jiffies + HZ/4);
}

static inline void enic_rfs_timer_free(struct enic *enic)
{
	del_timer_sync(&enic->rfs_h.rfs_may_expire);
}

#else
static inline void enic_rfs_timer_init(struct enic *enic) {}
static inline void enic_rfs_timer_free(struct enic *enic) {}
#endif /* CONFIG_RFS_ACCEL */

#endif /* _ENIC_CLSF_H_ */
