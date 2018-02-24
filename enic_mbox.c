// SPDX-License-Identifier: GPL-2.0

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

#include "kcompat.h"
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include "enic.h"
#include "enic_sriov.h"
#include "enic_mbox.h"

char mbox_msg_names[ENIC_MBOX_MAX][32] = {
	"VF_CAPABILITY_REQ",
	"VF_CAPABILITY_REP",
	"VF_REG_REQ",
	"VF_REG_REP",
	"VF_UNREG_REQ",
	"VF_UNREG_REP",
	"PF_LINK_STATE_NOTIF",
	"PF_LINK_STATE_ACK",
	"PF_GET_STATS_REQUEST",
	"PF_GET_STATS_REPLY",
	"VF_ADD_DEL_MAC_REQ",
	"VF_ADD_DEL_MAC_REP",
	"PF_SET_ADMIN_MAC_NOTIF",
	"PF_SET_ADMIN_MAC_ACK",
	"VF_SET_PKT_FLTR_FL_REQ",
	"VF_SET_PKT_FLTR_FL_REP",
};

char *enic_mbox_msg_type_to_str(enum enic_mbox_msg_t msg_type)
{
	if (msg_type < ENIC_MBOX_MAX)
		return mbox_msg_names[msg_type];

	return NULL;
}

void enic_mbox_msg_list_lock(struct enic *enic)
{
	spin_lock(&enic->sriov.mbox_msg_list_lock);
}

void enic_mbox_msg_list_unlock(struct enic *enic)
{
	spin_unlock(&enic->sriov.mbox_msg_list_lock);
}

void enic_mbox_msg_list_lock_bh(struct enic *enic)
{
	spin_lock_bh(&enic->sriov.mbox_msg_list_lock);
}

void enic_mbox_msg_list_unlock_bh(struct enic *enic)
{
	spin_unlock_bh(&enic->sriov.mbox_msg_list_lock);
}

int enic_mbox_msg_list_len_locked(struct enic *enic)
{
	struct list_head *p, *list = &enic->sriov.mbox_msg_list;
	int count = 0;

	if (list == NULL)
		return 0;

	list_for_each(p, list) {
		count++;
	}

	return count;
}

struct sk_buff *enic_mbox_alloc_skb(struct enic *enic, int size)
{
	struct net_device *netdev = enic->netdev;
	struct sk_buff *skb;

	skb = netdev_alloc_skb(netdev, size);

	if (skb != NULL) {
		enic_mbox_gen_stats_inc(enic, num_skb_alloc_ok);
		skb_put(skb, size);
		memset(skb->data, 0, size);
	} else {
		enic_mbox_gen_stats_inc(enic, num_skb_alloc_err);
	}

	return skb;
}

void enic_mbox_free_skb(struct enic *enic, struct sk_buff *skb)
{
	if (skb) {
		napi_consume_skb(skb, 1);
		enic_mbox_gen_stats_inc(enic, num_skb_free_ok);
	} else {
		enic_mbox_gen_stats_inc(enic, num_skb_free_err);
	}
}

struct enic_mbox_msg_skb *enic_mbox_alloc_rx_msg(struct enic *enic)
{
	struct enic_mbox_msg_skb *rx_msg;

	rx_msg = kzalloc(sizeof(*rx_msg), GFP_ATOMIC);
	if (rx_msg != NULL)
		enic_mbox_gen_stats_inc(enic, num_rx_skb_alloc_ok);
	else
		enic_mbox_gen_stats_inc(enic, num_rx_skb_alloc_err);

	return rx_msg;
}

void enic_mbox_free_rx_msg(struct enic *enic, struct enic_mbox_msg_skb *rx_msg)
{
	struct net_device *netdev = enic->netdev;

	if (rx_msg) {
		if (!rx_msg->skb)
			netdev_warn(netdev, "%s - NULL skb pointer\n",
				    __func__);
		dev_kfree_skb(rx_msg->skb);
		kfree(rx_msg);
		enic_mbox_gen_stats_inc(enic, num_rx_skb_free_ok);
	} else
		enic_mbox_gen_stats_inc(enic, num_rx_skb_free_err);
}

int enic_admin_rx(struct enic *enic, struct sk_buff *skb, u16 sender_id)
{
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	int msg_min_size;
	u8 msg_type;
	int err;

	hdr = ENIC_MBOX_HDR(skb);

	/* Make sure the generic header is not truncated
	 */
	if (skb->len < sizeof(struct enic_mbox_msg)) {
		netdev_err(netdev, "%s - Msg truncated: len = %d, min_size = %ld\n",
			__func__, skb->len, sizeof(struct enic_mbox_hdr));
		enic_mbox_gen_stats_inc(enic, msg_hdr_truncated);
		err = -EIO;
		goto out;
	}

	msg_type = hdr->msg_type;

	if (msg_type >= ENIC_MBOX_MAX) {
		netdev_err(netdev, "%s - [msg_num=%llu] Error: msg_type %u is out of allowed range (0 - %u)\n",
			   __func__, hdr->msg_num, msg_type, ENIC_MBOX_MAX-1);
		enic_mbox_gen_stats_inc(enic, bad_msg_type);
		err = -EINVAL;
		goto out;
	}

	/* Make sure the message type header is not truncated (the payload size
	 * will be checked in the message type handler)
	 */
	msg_min_size = enic_mbox_msg_type_min_size(enic, msg_type);
	if (hdr->msg_len < msg_min_size) {
		if (msg_min_size == ENIC_MBOX_MSG_MIN_SIZE_NOT_AVAILABLE)
			netdev_err(netdev, "%s - min msg size not available for type %u\n",
				   __func__, msg_type);
		else
			netdev_err(netdev, "%s - Msg truncated: len = %d, min_size = %ld\n",
				   __func__, skb->len, sizeof(struct enic_mbox_hdr));
		enic_mbox_gen_stats_inc(enic, msg_type_hdr_truncated);
		err = -EIO;
		goto out;
	}

	/* Since the sender id seems to be unreliable, we only update global
	 * stats here (not the per-sender-id stats).
	 */
	if (sender_id != hdr->src_vnic_id) {
		netdev_err(netdev, "%s - [msg_num=%llu] FW init sender id %u does not match with pkt header sender id %u\n",
			   __func__, hdr->msg_num, sender_id, hdr->src_vnic_id);
		enic_mbox_gen_stats_inc(enic, spoof_sender_id);
		return -EIO;
	}

	switch (enic_get_vnic_type(enic)) {
	case ENIC_PF:
		err = enic_mbox_pf_rcv_skb(enic, skb);
		break;
	case ENIC_VF_V2:
		err = enic_mbox_vf_rcv_skb(enic, skb);
		break;
	case ENIC_DYN:
	case ENIC_VF_V1:
	default:
		netdev_err(netdev, "MBOX not supported on this vnic type (%u)\n",
			   enic_get_vnic_type(enic));
		err = -EOPNOTSUPP;
	}

out:
	return err;
}

void enic_mbox_work(struct enic *enic, enic_mbox_rx_fn rx_fn)
{
	rx_fn(enic);
}

int enic_mbox_flush_messages(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_msg_skb *item, *tmp;
	struct list_head *msg_list;
	int num = 0;

	enic_mbox_msg_list_lock_bh(enic);

	msg_list = &enic->sriov.mbox_msg_list;

	list_for_each_entry_safe(item, tmp, msg_list, list) {
		list_del(&item->list);
		dev_kfree_skb(item->skb);
		kfree(item);
		num++;
	}

	enic_mbox_msg_list_unlock_bh(enic);
	netdev_info(netdev, "%s - Flushed %d mbox msgs\n", __func__, num);
	return 0;
}

/* Called with sriov_lock taken */
void enic_dev_mbox_init(struct enic *enic)
{
	switch (enic_get_vnic_type(enic)) {
	case ENIC_PF:
		INIT_LIST_HEAD(&enic->sriov.mbox_msg_list);
		INIT_WORK(&enic->sriov.mbox_work, enic_mbox_pf_work);
		break;
	case ENIC_VF_V2:
		enic_mbox_pending_lists_lock_init(enic);
		INIT_LIST_HEAD(&enic->sriov.vf_v2.pending_macs);
		INIT_LIST_HEAD(&enic->sriov.mbox_msg_list);
		INIT_WORK(&enic->sriov.mbox_work, enic_mbox_vf_v2_work);
		break;
	default:
		/* nothing to do */
		break;
	}
}

int enic_dev_mbox_deinit(struct enic *enic)
{
	return 0;
}

static int enic_mbox_msg_is_reply(enum enic_mbox_msg_t msg_type)
{
	return (msg_type % 2);
}

/* This helper can be used to init both requests/notifications as well as
 * replies/acks.
 */
int enic_mbox_init_msg_hdr(struct enic *enic, struct enic_mbox_hdr *hdr,
			   enum enic_mbox_msg_t msg_type, u64 msg_num,
			   u16 dst_vnic_id, u8 flags, u16 msg_len)
{
	if (msg_type >= ENIC_MBOX_MAX) {
		netdev_err(enic->netdev, "%s - Invalid msg_type %u > ENIC_MBOX_MAX = %u\n",
			   __func__, msg_type, ENIC_MBOX_MAX);
		return -EINVAL;
	}

	if (enic_is_pf(enic))
		hdr->src_vnic_id = ENIC_DST_PARENT_PF;
	else
		hdr->src_vnic_id = enic->sriov.vf_v2.vf_id;

	/* Our caller passes us a valid msg_num only when this is a reply/ack
	 * msg type
	 */
	if (msg_num != 0) {
		if (!enic_mbox_msg_is_reply(msg_type)) {
			netdev_err(enic->netdev, "%s - msg_type = %u/%s , msg_num = %llu (should be 0)\n",
				   __func__,
				   msg_type, enic_mbox_msg_type_to_str(msg_type),
				   msg_num);
			return -EINVAL;
		}
		hdr->msg_num = msg_num;
	} else {
		if (enic_mbox_msg_is_reply(msg_type)) {
			netdev_err(enic->netdev, "%s - msg_type=%u/%s , msg_num = 0 (should be != 0)\n",
				   __func__, msg_type,
				   enic_mbox_msg_type_to_str(msg_type));
			return -EINVAL;
		}
		if (enic_is_sriov_pf(enic))
			hdr->msg_num = ++enic->sriov.pf.vfs[dst_vnic_id].mbox_tx_seq;
		else
			hdr->msg_num = ++enic->sriov.vf_v2.mbox_tx_seq;
	}

	hdr->dst_vnic_id = dst_vnic_id;
	hdr->msg_type = msg_type;
	hdr->flags = flags;
	hdr->msg_len = msg_len;

	return 0;
}

int enic_mbox_queue_msg_skb(struct enic *enic,
			    struct enic_mbox_msg_skb *msg_skb)
{
	struct list_head *list;

	enic_mbox_msg_list_lock(enic);

	list = &enic->sriov.mbox_msg_list;
	list_add_tail(&(msg_skb->list), list);

	enic_mbox_msg_list_unlock(enic);

	return 0;
}

static u16 enic_mbox_get_dst_from_msg_locked(struct enic *enic,
					     struct enic_mbox_hdr *hdr)
{
	return hdr->dst_vnic_id;
}

static u16 enic_mbox_get_msg_type_locked(struct enic *enic,
					 struct enic_mbox_hdr *hdr)
{
	return hdr->msg_type;
}

static inline
struct enic_mbox_msg_status *enic_mbox_get_msg_status_ptr(struct enic *enic,
							  u16 dst,
							  enum enic_mbox_msg_t msg_type)
{
	if (enic_is_vf_v2(enic))
		return &enic->sriov.vf_v2.msg_status[msg_type];
	else if (enic_is_sriov_pf(enic))
		return &enic->sriov.pf.vfs[dst].msg_status[msg_type];

	netdev_err(enic->netdev, "%s - vnic type %s checking status of sriov/mbox msg_type %s\n",
		   __func__,
		   enic_sriov_vnic_type_to_str(enic),
		   enic_mbox_msg_type_to_str(msg_type));
	return NULL;
}

/* WIP */
int enic_mbox_flow_ctrl_tx(struct enic *enic, u16 dst,
			   enum enic_mbox_msg_t msg_type)
{
/* Using a comment instead of #if 0 to silence checkpatch
 *
 *	struct enic_mbox_msg_status *msg_status;
 *
 *	msg_status = enic_mbox_get_msg_status_ptr(enic, dst, msg_type);
 *	if (!enic_mbox_msg_is_reply(msg_type)) {
 *	}
 */
	return 0;
}

int enic_mbox_flow_ctrl_rx(struct enic *enic, u16 dst,
			   enum enic_mbox_msg_t msg_type)
{
	struct enic_mbox_msg_status *msg_status;

	if (enic_mbox_msg_is_reply(msg_type)) {
		/* If msg_type is a reply/ack, msg_type-1 is the associated
		 * request/notification
		 */
		msg_status = enic_mbox_get_msg_status_ptr(enic, dst,
							  msg_type - 1);
		msg_status->tstamp = 0;
	}

	return 0;
}

static int enic_mbox_timestamp(struct enic *enic, u16 dst,
			       enum enic_mbox_msg_t msg_type)
{
	struct enic_mbox_msg_status *msg_status;
	unsigned long tstamp = jiffies;

	msg_status = enic_mbox_get_msg_status_ptr(enic, dst, msg_type);
	msg_status->tstamp = tstamp;
	msg_status->seq++;
	return 0;
}

const unsigned int enic_mbox_msg_hdr_min_size[ENIC_MBOX_MAX] = {
	[ENIC_MBOX_VF_CAPABILITY_REQUEST] =
		sizeof(struct enic_mbox_vf_capability_msg),
	[ENIC_MBOX_VF_CAPABILITY_REPLY] =
		sizeof(struct enic_mbox_vf_capability_reply_msg),
	[ENIC_MBOX_VF_REGISTER_REQUEST] =
		sizeof(struct enic_mbox_vf_register_msg),
	[ENIC_MBOX_VF_UNREGISTER_REQUEST] =
		sizeof(struct enic_mbox_vf_unregister_msg),
	[ENIC_MBOX_VF_REGISTER_REPLY] =
		sizeof(struct enic_mbox_vf_reg_unreg_reply_msg),
	[ENIC_MBOX_VF_UNREGISTER_REPLY] =
		sizeof(struct enic_mbox_vf_reg_unreg_reply_msg),
	[ENIC_MBOX_PF_LINK_STATE_NOTIF] =
		sizeof(struct enic_mbox_pf_link_state_notif_msg),
	[ENIC_MBOX_PF_LINK_STATE_ACK] =
		sizeof(struct enic_mbox_pf_link_state_ack_msg),
	[ENIC_MBOX_PF_GET_STATS_REQUEST] =
		sizeof(struct enic_mbox_pf_get_stats_msg),
	[ENIC_MBOX_PF_GET_STATS_REPLY] =
		sizeof(struct enic_mbox_pf_get_stats_reply_msg),
	[ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST] =
		sizeof(struct enic_mbox_vf_add_del_mac_msg),
	[ENIC_MBOX_VF_ADD_DEL_MAC_REPLY] =
		sizeof(struct enic_mbox_vf_add_del_mac_reply_msg),
	[ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF] =
		sizeof(struct enic_mbox_pf_set_admin_mac_notif_msg),
	[ENIC_MBOX_PF_SET_ADMIN_MAC_ACK] =
		sizeof(struct enic_mbox_pf_set_admin_mac_ack_msg),
	[ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST] =
		sizeof(struct enic_mbox_vf_set_pkt_filter_flags_msg),
	[ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY] =
		sizeof(struct enic_mbox_vf_set_pkt_filter_flags_reply_msg),
};

int enic_mbox_msg_type_min_size(struct enic *enic, u8 msg_type)
{
	/* DEBUG ONLY */
	if (msg_type >= ENIC_MBOX_MAX) {
		netdev_err(enic->netdev, "%s - msg_type %u out of range [0 - %d]\n",
			   __func__, msg_type, ENIC_MBOX_MAX);
		return ENIC_MBOX_MSG_MIN_SIZE_NOT_AVAILABLE;
	}

	return (sizeof(struct enic_mbox_msg) +
		enic_mbox_msg_hdr_min_size[msg_type]);
}

int enic_mbox_msg_type_size(u8 msg_type, u8 param1, u8 param2, u16 *size)
{
	u16 payload_size = 0;

	switch (msg_type) {
	case ENIC_MBOX_VF_CAPABILITY_REQUEST:
	case ENIC_MBOX_VF_CAPABILITY_REPLY:
		break;
	case ENIC_MBOX_VF_REGISTER_REQUEST:
	case ENIC_MBOX_VF_UNREGISTER_REQUEST:
	case ENIC_MBOX_VF_REGISTER_REPLY:
	case ENIC_MBOX_VF_UNREGISTER_REPLY:
		break;
	case ENIC_MBOX_PF_LINK_STATE_NOTIF:
	case ENIC_MBOX_PF_LINK_STATE_ACK:
		break;
	case ENIC_MBOX_PF_GET_STATS_REQUEST:
		break;
	case ENIC_MBOX_PF_GET_STATS_REPLY:
		/* param1 != 0 when there were errors in the STATS_REQUEST msg
		 */
		if (!param1)
			payload_size =
				sizeof(struct enic_mbox_pf_get_stats_reply);
		break;
	case ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST:
		/* param1 = num_macs
		 */
		payload_size = sizeof(struct enic_mac_addr) * param1;
		break;
	case ENIC_MBOX_VF_ADD_DEL_MAC_REPLY:
		/* param1 = num_errors
		 * param2 = num_macs
		 */
		if (param1)
			payload_size =
				sizeof(struct enic_mbox_vf_add_del_mac_msg) +
				sizeof(struct enic_mac_addr) * param2;
		break;
	case ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF:
	case ENIC_MBOX_PF_SET_ADMIN_MAC_ACK:
		break;
	case ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST:
	case ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY:
		break;
	default:
		return -EINVAL;
	}

	*size = sizeof(struct enic_mbox_msg) +
		enic_mbox_msg_hdr_min_size[msg_type] + payload_size;
	return 0;
}

int enic_mbox_tx_skb_locked(struct enic *enic, struct sk_buff *skb)
{
	struct enic_mbox_generic_reply_msg *gen_hdr;
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_msg *msg;
	struct enic_mbox_hdr *hdr;
	u16 ret_major;
	u8 msg_type;
	int err = 0;
	u16 dst;

	if (enic_is_sriov_pf(enic))
		ENIC_ASSERT_SRIOV_LOCK(enic);

	msg = ENIC_MBOX_MSG(skb);
	hdr = ENIC_MBOX_HDR(skb);

	msg_type = enic_mbox_get_msg_type_locked(enic, hdr);
	dst = enic_mbox_get_dst_from_msg_locked(enic, hdr);

	/* VFs can only send messages after the registration with the PF
	 */
	if (enic_is_vf_v2(enic) &&
	    (msg_type != ENIC_MBOX_VF_REGISTER_REQUEST) &&
	    (msg_type != ENIC_MBOX_VF_CAPABILITY_REQUEST) &&
	    !enic_vf_registered(enic)) {
		netdev_info(netdev, "%s - [msg_num=%llu] suppressing msg %s\n",
			    __func__, hdr->msg_num,
			    enic_mbox_msg_type_to_str(msg_type));
		enic_mbox_stats_inc(enic, enic->sriov.vf_v2.vf_id, msg_type,
				    num_tx_skip);
		return 0;
	}

	/* Reply messages always start with the generic reply msg header
	 */
	if (enic_mbox_msg_is_reply(msg_type)) {
		gen_hdr = (struct enic_mbox_generic_reply_msg *)(msg->msg);
		ret_major = gen_hdr->ret_major;

		if (ret_major) {
			netdev_warn(netdev, "%s - [msg_num=%llu] ret_major = %u\n",
				    __func__, hdr->msg_num, ret_major);
			enic_mbox_stats_inc(enic, dst, msg_type, num_rx_ret_err);
		} else {
			enic_mbox_stats_inc(enic, dst, msg_type, num_rx_ret_ok);
		}
	}

	if (!enic_is_res_intr_open(enic, ENIC_ADMIN_QP_INTR)) {
		netdev_err(netdev, "%s - [msg_num=%llu] msg_type=%u/%s - ADMIN QP not open, msg can not be TX\n",
			   __func__, hdr->msg_num,
			   msg_type, enic_mbox_msg_type_to_str(msg_type));
		err = -1;
		goto out;
	}

	err = enic_mbox_flow_ctrl_tx(enic, dst, msg_type);
	if (err) {
		netdev_warn(netdev, "%s - [msg_num=%llu] msg %s dst: %d/%s could not be sent (err=%d)\n",
			    __func__, hdr->msg_num,
			    enic_mbox_msg_type_to_str(msg_type),
			    dst, dst == ENIC_DST_PARENT_PF ? "PF" : "VF",
			    err);
		goto out;
	}

	err = enic_admin_xmit(skb, netdev, dst);
	if (err)
		netdev_err(netdev, "%s - [msg_num=%llu] unable to queue msg type=%u dst:%u (err=%d)\n",
			   __func__, hdr->msg_num, msg_type, dst, err);

out:
	if (err) {
		enic_mbox_stats_inc(enic, dst, msg_type, num_tx_err);
	} else {
		enic_mbox_timestamp(enic, dst, msg_type);
		enic_mbox_stats_inc(enic, dst, msg_type, num_tx);
	}

	return err;
}

int enic_mbox_tx_skb(struct enic *enic, struct sk_buff *skb)
{
	int err;

	enic_sriov_lock_bh(enic);
	err = enic_mbox_tx_skb_locked(enic, skb);
	enic_sriov_unlock_bh(enic);

	return err;
}
