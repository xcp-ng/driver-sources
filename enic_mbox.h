/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright 2008-2023 Cisco Systems, Inc.  All rights reserved.
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

#ifndef __ENIC_MBOX_H__
#define __ENIC_MBOX_H__

#include "kcompat.h"
#include <linux/list.h>
#include <linux/netdevice.h>

#define ENIC_MBOX_VERSION "1.0"

#define ENIC_DST_PARENT_PF SRIOV_PF_IDX

#define ENIC_VLAN_ANY 4096

/* Even numbers are requests, odd numbers are replies/acks.
 * _PF_ and _VF_ here refer to who starts the request (initiator).
 */
enum enic_mbox_msg_t {
	ENIC_MBOX_VF_CAPABILITY_REQUEST,
	ENIC_MBOX_VF_CAPABILITY_REPLY,
	ENIC_MBOX_VF_REGISTER_REQUEST,
	ENIC_MBOX_VF_REGISTER_REPLY,
	ENIC_MBOX_VF_UNREGISTER_REQUEST,
	ENIC_MBOX_VF_UNREGISTER_REPLY,
	ENIC_MBOX_PF_LINK_STATE_NOTIF,
	ENIC_MBOX_PF_LINK_STATE_ACK,
	ENIC_MBOX_PF_GET_STATS_REQUEST,
	ENIC_MBOX_PF_GET_STATS_REPLY,
	ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST,
	ENIC_MBOX_VF_ADD_DEL_MAC_REPLY,
	ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF,
	ENIC_MBOX_PF_SET_ADMIN_MAC_ACK,
	ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST,
	ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY,
	ENIC_MBOX_MAX
};

struct enic_mbox_msg_stat_t {
	u16 num_tx;
	u16 num_tx_err;
	u16 num_tx_skip;

	u16 num_rx;
	u16 num_rx_drop;
	u16 num_rx_err;
	u16 num_rx_ret_ok;
	u16 num_rx_ret_err;
};

struct enic_mbox_gen_stat_t {
	/* Stats of egress MBOX msgs
	 */
	u32 num_skb_alloc_ok;
	u32 num_skb_alloc_err;
	u32 num_skb_free_ok;
	u32 num_skb_free_err;

	/* Stats for ingress MBOX msgs
	 */
	u32 num_rx_skb_alloc_ok;
	u32 num_rx_skb_alloc_err;
	u32 num_rx_skb_free_ok;
	u32 num_rx_skb_free_err;

	u32 num_mac_alloc_ok;
	u32 num_mac_alloc_err;
	u32 num_mac_free_ok;
	u32 num_mac_free_err;

	u32 num_vlan_alloc_ok;
	u32 num_vlan_alloc_err;
	u32 num_vlan_free_ok;
	u32 num_vlan_free_err;

	u16 mbox_not_init;
	u16 mbox_dev_not_init;
	u16 pf_vfs_not_init;
	u16 bad_sender_id;
	u16 spoof_sender_id;
	u16 bad_msg_type;
	u16 vf_not_registered;
	u16 msg_hdr_truncated;
	u16 msg_type_hdr_truncated;
	u16 msg_len_mismatch;
};

/* When enic is a PF, peer_id is the remote VF ID.
 * When enic is a VF, peer_id is the parent PF and it will be ignored.
 */
#define enic_mbox_stats_inc(enic, peer_id, msg_type, param)	\
do {								\
	((struct enic *)enic)->sriov.mbox_stats[msg_type].param++; \
	if (enic_is_sriov_pf(enic)) {				\
		struct enic_mbox_msg_stat_t *mbox_stats =	\
			((struct enic *)enic)->sriov.pf.vfs[peer_id].mbox_stats; \
		if (mbox_stats)					\
			mbox_stats[msg_type].param++;		\
		else						\
			netdev_err(enic->netdev,		\
				"enic_mbox_stats_inc() - mbox_stats not init\n"); \
	}							\
} while (0)

#define enic_mbox_gen_stats_inc(enic, param)			\
	(((struct enic *)enic)->sriov.mbox_gen_stats.param++)

struct enic_mbox_msg_skb {
	struct list_head list;
	struct enic *enic;
	struct sk_buff *skb;
	u16 vf_id; /* peer vnic the rcvd msg was sent from */
};

#define ENIC_MBOX_MSG(skb) ((struct enic_mbox_msg *)((skb)->data))
#define ENIC_MBOX_HDR(skb) (&((struct enic_mbox_msg *)((skb)->data))->hdr)

struct enic_mbox_hdr {
	u16 src_vnic_id;
	u16 dst_vnic_id;
	u8 msg_type;
	u8 flags;
	u16 msg_len;	/* This includes both header and payload */
	u64 msg_num;	/* Global (not per-type) message num (= seq number).
			 * Starts from 1
			 */
};

struct enic_mbox_msg {
	struct enic_mbox_hdr hdr;
	char msg[];
};

struct enic_mbox_item {
	struct list_head list;
	struct enic_mbox_msg *msg;
};

/* ret_major errors */
#define ENIC_MBOX_ERR_GENERIC		BIT(0)
#define ENIC_MBOX_ERR_VF_NOT_REGISTERED	BIT(1)
#define ENIC_MBOX_ERR_MSG_NOT_SUPPORTED	BIT(2)
#define ENIC_MBOX_ERR_VF_NOT_ALLOWED	BIT(3) /* NOTE: This may go away */

struct enic_mbox_generic_reply_msg {
	u16 ret_major;	/* Generic return code for the request msg */
	u16 ret_minor;	/* Per-msg-type additional information */
};


/*
 * ENIC_MBOX_VF_CAPABILITY_REQUEST
 * ENIC_MBOX_VF_CAPABILITY_REPLY
 *
 * PF/VF drivers may use different admin channel protocol/messaging
 * versions. These messages are used to inform each other of the
 * protocol version.
 *
 * As the very first message, the VF driver sends CAPABILITY_REQUEST
 * to the PF driver to query PF driver's protocol version.
 */

/* ESX PF driver's initial support for the admin channel. It only
 * responds to CAPABILITY_REQUEST and drops everything else. The VF
 * driver should stop using the admin channel and fall back to the
 * backward compatible mode with limited features (e.g. no trust
 * mode).
 */
#define ENIC_MBOX_CAP_VERSION_0  0

/* Linux PF/VF driver's initial support for the admin channel */
#define ENIC_MBOX_CAP_VERSION_1  1

#define ENIC_MBOX_CAP_VERSION_INVALID  0xffffffff
#define ENIC_MBOX_VF_CAPABILITY_TIMEOUT 3 /* wait up to 3 seconds */
#define ENIC_MBOX_VF_CAPABILITY_TRIES 2 /* re-send twice before giving up */

struct enic_mbox_vf_capability_msg {
	u32 version;
	u32 reserved[32]; /* 128B for future use */
};

struct enic_mbox_vf_capability_reply_msg {
	struct enic_mbox_generic_reply_msg generic_reply;
	u32 version;
	u32 reserved[32]; /* 128B for future use */
};

/*
 * ENIC_MBOX_VF_REGISTER_REQUEST,
 * ENIC_MBOX_VF_REGISTER_REPLY,
 * ENIC_MBOX_VF_UNREGISTER_REQUEST,
 * ENIC_MBOX_VF_UNREGISTER_REPLY,
 */

struct enic_mbox_vf_register_msg {
};

struct enic_mbox_vf_unregister_msg {
};

struct enic_mbox_vf_reg_unreg_reply_msg {
	struct enic_mbox_generic_reply_msg generic_reply;
};

/*
 * ENIC_MBOX_PF_LINK_STATE_NOTIF
 * ENIC_MBOX_PF_LINK_STATE_ACK
 */

#define ENIC_MBOX_LINK_STATE_DISABLE	0
#define ENIC_MBOX_LINK_STATE_ENABLE	1
struct enic_mbox_pf_link_state_notif_msg {
	u32 link_state;
};

struct enic_mbox_pf_link_state_ack_msg {
	struct enic_mbox_generic_reply_msg ack;
};

/*
 * ENIC_MBOX_PF_GET_STATS_REQUEST
 * ENIC_MBOX_PF_GET_STATS_REPLY
 */

#define ENIC_MBOX_GET_STATS_RX	BIT(0)
#define ENIC_MBOX_GET_STATS_TX	BIT(1)
#define ENIC_MBOX_GET_STATS_ALL	(ENIC_MBOX_GET_STATS_RX | ENIC_MBOX_GET_STATS_TX)

struct enic_mbox_pf_get_stats_msg {
	u16 flags;
	u16 pad;
};

struct enic_mbox_pf_get_stats_reply {
	struct vnic_stats vnic_stats;

	/* The size of the struct vnic_stats is guaranteed to not change, but
	 * the number of counters (in the rx/tx elements of that struct) that
	 * are actually init may vary depending on the driver version (new
	 * fields may be added to the rsvd blocks).
	 * These two variables tell us how much of the tx/rx blocks inside
	 * struct vnic_stats the VF driver knows about according to its
	 * definition of that data structure.
	 */
	u8 num_rx_stats;
	u8 num_tx_stats;

	u8 pad[6];
};

struct enic_mbox_pf_get_stats_reply_msg {
	struct enic_mbox_generic_reply_msg generic_reply;
	struct enic_mbox_pf_get_stats_reply detailed_reply[];
};

/*
 * ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST
 * ENIC_MBOX_VF_ADD_DEL_MAC_REPLY
 */

#define ENIC_VF_V2_MAX_UCAST ENIC_UNICAST_PERFECT_FILTERS
#define ENIC_VF_V2_MAX_MCAST ENIC_MULTICAST_PERFECT_FILTERS

/* The max number of MAC address changes (ADD or DEL) that a
 * VF_ADD_DEL_MAC_REQUEST MBOX message can carry can not exceed the worst case
 * scenario, which is when:
 * - all UCAST addresses and
 * - all MCAST addresses and
 * - the station (UCAST) address
 * get replaced with new ones.
 */
#define MAX_MAC_OPS ((ENIC_VF_V2_MAX_UCAST + ENIC_VF_V2_MAX_MCAST + 1) * 2)

/* enic_mac_addr.flags: Lower 8 bits are used in VF->PF direction (request) */
#define MAC_ADDR_FLAG_ADD		BIT(0)
#define MAC_ADDR_FLAG_STATION		BIT(1)

/* enic_mac_addr.flags: Upper 8 bits are used in PF->VF direction (reply) */
#define MAC_ADDR_FLAG_OVERFLOW		BIT(8)
#define MAC_ADDR_FLAG_DUPLICATE		BIT(9)
#define MAC_ADDR_FLAG_FAILED		BIT(10)
#define MAC_ADDR_FLAG_NOT_FOUND		BIT(11)
#define MAC_ADDR_FLAG_ERROR		BIT(12)
#define MAC_ADDR_FLAG_NOT_PERMITTED	BIT(13)
#define MAC_ADDR_FLAG_INVALID		BIT(14)
#define MAC_ADDR_FLAG_SKIPPED		BIT(15)

#define MAC_ADDR_FLAG_REQUEST_MASK	0xFF
#define MAC_ADDR_FLAG_REPLY_MASK	0xFF00

/* mac flags used by the PF in its per-VF mac lists */
#define MAC_ADDED_BY_VF	BIT(0)
#define MAC_ADDED_BY_PF	BIT(1) /* Admin MAC */
#define MAC_NOT_ACTIVE	BIT(2) /* Used only for VF-added macs */

struct enic_mac_addr {
	u8 addr[ETH_ALEN];
	u16 flags;
};

struct enic_mac_addr_item {
	struct list_head list;
	struct enic_mac_addr mac_addr;
};

struct enic_mbox_vf_add_del_mac_msg {
	u16 num_addrs;
	u16 pad;
	struct enic_mac_addr mac_addr[];
};

struct enic_mbox_vf_add_del_mac_reply_msg {
	struct enic_mbox_generic_reply_msg generic_reply;
	struct enic_mbox_vf_add_del_mac_msg detailed_reply[];
};

/*
 * ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF,
 * ENIC_MBOX_PF_SET_ADMIN_MAC_ACK,
 */

struct enic_mbox_pf_set_admin_mac_notif_msg {
	struct enic_mac_addr mac_addr;
};

struct enic_mbox_pf_set_admin_mac_ack_msg {
	struct enic_mbox_generic_reply_msg ack;
};

/*
 * ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST,
 * ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY,
 */

#define ENIC_MBOX_PKT_FILTER_DIRECTED	BIT(0)
#define ENIC_MBOX_PKT_FILTER_MULTICAST	BIT(1)
#define ENIC_MBOX_PKT_FILTER_BROADCAST	BIT(2)
#define ENIC_MBOX_PKT_FILTER_PROMISC	BIT(3)
#define ENIC_MBOX_PKT_FILTER_ALLMULTI	BIT(4)

struct enic_mbox_vf_set_pkt_filter_flags_msg {
	u16 flags;
	u16 pad;
};

struct enic_mbox_vf_set_pkt_filter_flags_reply_msg {
	struct enic_mbox_generic_reply_msg generic_reply;
};

typedef int (*enic_mbox_rx_fn)(struct enic *);

struct sk_buff *enic_mbox_alloc_skb(struct enic *enic, int size);
void enic_mbox_free_skb(struct enic *enic, struct sk_buff *skb);
struct enic_mbox_msg_skb *enic_mbox_alloc_rx_msg(struct enic *enic);
void enic_mbox_free_rx_msg(struct enic *enic, struct enic_mbox_msg_skb *rx_msg);

void enic_mbox_msg_list_lock(struct enic *enic);
void enic_mbox_msg_list_unlock(struct enic *enic);
void enic_mbox_msg_list_lock_bh(struct enic *enic);
void enic_mbox_msg_list_unlock_bh(struct enic *enic);
void enic_dev_mbox_init(struct enic *enic);
int enic_dev_mbox_deinit(struct enic *enic);

int enic_admin_rx(struct enic *enic, struct sk_buff *skb, u16 sender_id);
void enic_mbox_pf_work(struct work_struct *work);
int enic_mbox_tx_skb(struct enic *enic, struct sk_buff *skb);
int enic_mbox_tx_skb_locked(struct enic *enic, struct sk_buff *skb);
int enic_mbox_flush_messages(struct enic *enic);
int enic_mbox_init_msg_hdr(struct enic *enic, struct enic_mbox_hdr *hdr,
			   enum enic_mbox_msg_t msg_type, u64 msg_num,
			   u16 dst_vnic_id, u8 flags, u16 msg_len);
int enic_mbox_queue_msg_skb(struct enic *enic,
			    struct enic_mbox_msg_skb *msg_skb);
int enic_mbox_msg_list_len_locked(struct enic *enic);
void enic_mbox_work(struct enic *enic, enic_mbox_rx_fn rx_fn);
int enic_mbox_msg_type_size(u8 msg_type, u8 param1, u8 param2, u16 *size);

#define ENIC_MBOX_MSG_MIN_SIZE_NOT_AVAILABLE INT_MAX
int enic_mbox_msg_type_min_size(struct enic *enic, u8 msg_type);
int enic_mbox_tx_msg_vf_register(struct enic *enic, u16 dst_vnic_id);
int enic_mbox_tx_msg_vf_unregister(struct enic *enic, u16 dst_vnic_id);
void enic_mbox_vf_v2_work(struct work_struct *work);
char *enic_mbox_msg_type_to_str(enum enic_mbox_msg_t msg_type);
int enic_mbox_push_vf_macs(struct enic *enic);
void enic_mbox_flush_pending_macs(struct enic *enic);
int enic_mbox_tx_link_state_notif_locked(struct enic *enic, int vf_id);
int enic_mbox_pf_rcv_skb(struct enic *enic, struct sk_buff *skb);
int enic_mbox_vf_rcv_skb(struct enic *enic, struct sk_buff *skb);
void enic_mbox_pending_lists_lock_init(struct enic *enic);
int enic_mbox_are_pending_lists_locked(struct enic *enic);
int enic_mbox_flow_ctrl_rx(struct enic *enic, u16 dst,
			   enum enic_mbox_msg_t msg_type);
int enic_mbox_tx_link_state_notif_locked(struct enic *enic, int vf_id);
int enic_mbox_tx_pf_get_stats_request_locked(struct enic *enic, int vf_id);
int enic_mbox_process_vf_mac_request(struct enic *enic, u16 vf_id,
				     struct enic_mac_addr *mac_addr, bool add,
				     struct enic_mbox_msg *msg, u16 flags);
int enic_mbox_tx_pf_set_admin_mac_notif_locked(struct enic *enic, int vf,
					       const u8 *mac);

int enic_mbox_vf_add_mac(struct enic *enic, u8 flags, const u8 *mac,
			 u8 mac_flags);
int enic_mbox_vf_del_mac(struct enic *enic, u8 flags, const u8 *mac,
			 u8 mac_flags);
int enic_mbox_vf_set_pkt_filter(struct enic *enic, int directed, int multicast,
				int broadcast, int promisc, int allmulti);

/* Flags for the 'flags' input to the two APIs
 */
#define ENIC_MBOX_FLAG_PUSH_MACS 1
int enic_mbox_vf_add_mac(struct enic *enic, u8 flags, const u8 *mac,
			 u8 mac_flags);
int enic_mbox_vf_del_mac(struct enic *enic, u8 flags, const u8 *mac,
			 u8 mac_flags);


#define ENIC_ASSERT_MBOX_PENDING_LIST_LOCK(enic) \
	WARN(!enic_mbox_are_pending_lists_locked(enic), \
	     "enic - BUG: pending_lists_lock not held: assertion failed at %s (%d)\n", \
	     __FILE__,  __LINE__)

int enic_mbox_flow_ctrl_tx(struct enic *enic, u16 dst,
			   enum enic_mbox_msg_t msg_type);
int enic_mbox_add_vf_mac_to_mac_addrs(struct enic *enic, u16 vf_id,
				      u8 *mac_addr);
int enic_mbox_rx_msg_vf_set_pkt_filter_flags_reply(struct enic *enic,
						   struct enic_mbox_msg *msg);
void enic_mbox_pending_lists_lock(struct enic *enic);
void enic_mbox_pending_lists_unlock(struct enic *enic);
#endif /* __ENIC_MBOX_H */
