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
#include <linux/ethtool.h>
#include "enic.h"
#include "enic_sriov.h"
#include "enic_mbox.h"
#include "enic_dev.h"
#include "enic_ethtool.h"

static int enic_mbox_vf_rx(struct enic *enic)
{
	struct enic_mbox_msg_skb *msg_item, *tmp;
	struct list_head *msg_list;
	int msg_queue_len;
	int err;

	enic_mbox_msg_list_lock_bh(enic);

	msg_list = &enic->sriov.mbox_msg_list;
	msg_queue_len = enic_mbox_msg_list_len_locked(enic);

	if (msg_queue_len > 0) {
		msg_list = &enic->sriov.mbox_msg_list;
		list_for_each_entry_safe(msg_item, tmp, msg_list, list) {
			err = enic_admin_rx(enic, msg_item->skb,
					    msg_item->vf_id);
			if (err < 0)
				netdev_err(enic->netdev,
					"enic_admin_rx failed err %d\n", err);
			list_del(&msg_item->list);
			enic_mbox_free_rx_msg(enic, msg_item);
		}
	}

	enic_mbox_msg_list_unlock_bh(enic);

	return 0;
}

void enic_mbox_vf_v2_work(struct work_struct *work)
{
	struct enic_sriov *enic_sriov = container_of(work, struct enic_sriov,
						     mbox_work);
	struct enic *enic = container_of(enic_sriov, struct enic, sriov);

	rtnl_lock();
	enic_mbox_work(enic, &enic_mbox_vf_rx);
	rtnl_unlock();
}

/* Used for both ENIC_MBOX_MSG_VF_{REGISTER, UNREGISTER}
 */
static int enic_mbox_tx_msg_vf_registration(struct enic *enic, u16 dst_vnic_id,
					    bool reg)
{
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	int err = 0;
	u16 size;

	msg_type = (reg) ? ENIC_MBOX_VF_REGISTER_REQUEST : ENIC_MBOX_VF_UNREGISTER_REQUEST;

	err = enic_mbox_msg_type_size(msg_type, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u\n",
			   __func__, msg_type);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type, 0, dst_vnic_id,
				     0, size);
	if (err)
		goto err_out;

	err = enic_mbox_tx_skb(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);

	return err;

err_out:
	enic_mbox_stats_inc(enic, dst_vnic_id, msg_type, num_tx_err);
	goto out;
}

static int enic_send_check_capability(struct enic *enic)
{
	struct enic_mbox_vf_capability_msg *cap_req;
	enum enic_mbox_msg_t msg_type;
	struct enic_mbox_msg *m;
	struct sk_buff *skb;
	u16 size;
	int err;

	skb = NULL;
	msg_type = ENIC_MBOX_VF_CAPABILITY_REQUEST;
	err = enic_mbox_msg_type_size(msg_type, 0, 0, &size);
	if (err)
		goto out;
	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto out;
	}
	m = ENIC_MBOX_MSG(skb);
	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type, 0, ENIC_DST_PARENT_PF, 0, size);
	if (err)
		goto out;
	cap_req = (struct enic_mbox_vf_capability_msg *)m->msg;
	cap_req->version = ENIC_MBOX_CAP_VERSION_1;
	err = enic_mbox_tx_skb(enic, skb);
	if (err) {
		dev_err(enic_get_dev(enic), "%s enic_mbox_tx_skb failed err = %d\n", __func__, err);
		enic_mbox_stats_inc(enic, ENIC_DST_PARENT_PF, msg_type, num_tx_err);
		goto out;
	}
out:
	if (err && skb)
		dev_kfree_skb(skb);
	return err;
}

int enic_sriov_check_capability(struct enic *enic)
{
	struct enic_vf_v2 *vf;
	struct device *dev;
	int tries;
	int err;

	if (!enic_is_vf_v2(enic))
		return 0;

	dev = enic_get_dev(enic);
	vf = &enic->sriov.vf_v2;
	vf->pf_cap_version = ENIC_MBOX_CAP_VERSION_INVALID;
	tries = 0;
	while (tries++ < ENIC_MBOX_VF_CAPABILITY_TRIES) {
		err = enic_send_check_capability(enic);
		if (err)
			goto out;
		init_completion(&vf->pf_cap_comp);
		/* Wait for reply. enic_mbox_rx_msg_vf_capability_reply() signals */
		err = wait_for_completion_timeout(&vf->pf_cap_comp,
						  ENIC_MBOX_VF_CAPABILITY_TIMEOUT * HZ);
		if (err == 0) {
			err = -ETIMEDOUT;
			/* dev_warn to avoid "netdev is (unnamed net_device) (uninitialized)" */
			dev_warn(dev, "No CAPABILITY_REPLY from PF driver try %d\n", tries);
		} else {
			dev_dbg(dev, "CAPABILITY_REPLY from PF cap_version %u\n",
				vf->pf_cap_version);
			err = 0;
			break;
		}
	}
	if (err == -ETIMEDOUT || vf->pf_cap_version == ENIC_MBOX_CAP_VERSION_0) {
		dev_warn(dev, "PF driver does not have adequate admin channel support. VF works in backward compatible mode\n");
		/* Stop/free admin channel and other SR-IOV specific functions */
		enic_sriov_deinit(enic);
		/* Override device ID to act as a regular vNIC, not VF.
		 * enic_init_ndos() has already set ndos = enic_netdev_vf_v2_ops.
		 * That is ok, and no need to undo/re-do that function.
		 */
		enic->override_dev_id = PCI_DEVICE_ID_CISCO_VIC_ENET;
		err = 0;
	} else if (vf->pf_cap_version == ENIC_MBOX_CAP_VERSION_INVALID) {
		dev_warn(dev, "Unexpected version in CAPABILITY_REPLY from PF driver. cap_version %u\n",
			 vf->pf_cap_version);
		err = -EINVAL;
	}
	/* For other versions, nothing special to do now
	 * pf_cap_version == ENIC_MBOX_CAP_VERSION_1:
	 * PF and VF use the same version
	 *
	 * pf_cap_version > ENIC_MBOX_CAP_VERSION_1:
	 * PF admin channel version is newer than VF. PF driver should be
	 * backward compatible.
	 */
out:
	return err;
}

static int
enic_mbox_rx_msg_vf_capability_reply(struct enic *enic, struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_capability_reply_msg *cap_reply;
	struct enic_vf_v2 *vf;

	cap_reply = (struct enic_mbox_vf_capability_reply_msg *)msg->msg;
	vf = &enic->sriov.vf_v2;
	if (cap_reply->generic_reply.ret_major == 0)
		vf->pf_cap_version = cap_reply->version;
	complete(&vf->pf_cap_comp);
	return 0;
}

int enic_mbox_tx_msg_vf_register(struct enic *enic, u16 dst_vnic_id)
{
	return enic_mbox_tx_msg_vf_registration(enic, dst_vnic_id, 1);
}

int enic_mbox_tx_msg_vf_unregister(struct enic *enic, u16 dst_vnic_id)
{
	return enic_mbox_tx_msg_vf_registration(enic, dst_vnic_id, 0);
}

static int enic_mbox_rx_msg_vf_register_reply(struct enic *enic,
					      struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_reg_unreg_reply_msg *register_reply_msg;
	struct net_device *netdev = enic->netdev;
	int ret_major, ret_minor;

	register_reply_msg = (struct enic_mbox_vf_reg_unreg_reply_msg *)msg->msg;
	ret_major = register_reply_msg->generic_reply.ret_major;
	ret_minor = register_reply_msg->generic_reply.ret_minor;

	if (ret_major) {
		netdev_warn(netdev, "%s - msg ENIC_MBOX_VF_REGISTER_REQUEST to PF failed, err = %d/%d\n",
			    __func__, ret_major, ret_minor);
		return 0;
	}

	enic->sriov.vf_v2.flags |= ENIC_VF_REGISTERED;
	return 0;
}

static int enic_mbox_rx_msg_vf_unregister_reply(struct enic *enic,
						struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_reg_unreg_reply_msg *register_reply_msg;
	struct net_device *netdev = enic->netdev;
	int ret_major, ret_minor;

	register_reply_msg = (struct enic_mbox_vf_reg_unreg_reply_msg *)msg->msg;
	ret_major = register_reply_msg->generic_reply.ret_major;
	ret_minor = register_reply_msg->generic_reply.ret_minor;

	if (ret_major)
		netdev_warn(netdev, "%s - msg ENIC_MBOX_VF_UNREGISTER_REQUEST to PF failed, err = %d/%d\n",
			    __func__, ret_major, ret_minor);
	else
		enic->sriov.vf_v2.flags &= ~ENIC_VF_REGISTERED;

	complete(&enic->sriov.vf_v2.unregister_comp);
	return 0;
}

static int enic_mbox_tx_msg_pf_link_state_ack(struct enic *enic,
					      struct enic_mbox_msg *msg,
					      u16 ret_major)
{
	struct enic_mbox_pf_link_state_ack_msg *ack_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	u16 size;
	int err;

	msg_type = ENIC_MBOX_PF_LINK_STATE_ACK;

	err = enic_mbox_msg_type_size(msg_type, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u\n",
			   __func__, msg_type);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type, msg->hdr.msg_num,
				     ENIC_DST_PARENT_PF, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	ack_msg = (struct enic_mbox_pf_link_state_ack_msg *)m->msg;
	ack_msg->ack.ret_major = ret_major;

	err = enic_mbox_tx_skb(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;

err_out:
	enic_mbox_stats_inc(enic, ENIC_DST_PARENT_PF, msg_type, num_tx_err);
	goto out;
}

static int enic_mbox_rx_msg_pf_link_state_notif(struct enic *enic,
						struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_link_state_notif_msg *notif_msg;
	int carrier_ok = netif_carrier_ok(enic->netdev);
	struct net_device *netdev = enic->netdev;
	u32 link_state;
	u64 msg_num;
	int err = 0;

	msg_num = msg->hdr.msg_num;
	notif_msg = (struct enic_mbox_pf_link_state_notif_msg *)msg->msg;
	link_state = notif_msg->link_state;

	switch (link_state) {
	case ENIC_MBOX_LINK_STATE_DISABLE:
		if (carrier_ok)
			netif_carrier_off(netdev);
		netdev_info(netdev, "[msg_num=%llu] RX link state change from PF: %s -> down\n",
			    msg_num, (carrier_ok) ? "UP" : "down");
		break;
	case ENIC_MBOX_LINK_STATE_ENABLE:
		if (!carrier_ok)
			netif_carrier_on(netdev);
		netdev_info(netdev, "[msg_num=%llu] RX link state change from PF: %s -> UP\n",
			    msg_num, (carrier_ok) ? "UP" : "down");
		break;
	default:
		err = -EINVAL;
		break;
	}

	return enic_mbox_tx_msg_pf_link_state_ack(enic, msg, err);
}

/* As of now only VF_V2 vnics are supposed to get this kind of requests, but we
 * are not enforcing it here to allow the PF to support more use cases if
 * needed (with no need to upgrade the VF driver too)
 */
static int enic_mbox_rx_msg_pf_get_stats(struct enic *enic,
					 struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_get_stats_reply_msg *stats_reply_msg;
	struct enic_mbox_pf_get_stats_reply *stats_reply;
	struct enic_mbox_pf_get_stats_msg *stats_req_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t reply_msg_type;
	struct sk_buff *skb = NULL;
	int stats_count, stats_size;
	struct enic_mbox_hdr *hdr;
	struct enic_mbox_msg *m;
	unsigned int offset;
	int num_bytes;
	u64 msg_num;
	int err = 0;
	u16 vf_id;
	u16 size;
	int i;

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;
	msg_num = hdr->msg_num;
	stats_req_msg = (struct enic_mbox_pf_get_stats_msg *)msg->msg;

	reply_msg_type = ENIC_MBOX_PF_GET_STATS_REPLY;

	err = enic_mbox_msg_type_size(reply_msg_type, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u (param1=%u, param2=%u)\n",
			   __func__, reply_msg_type, 0, 0);
		goto rx_err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto tx_err_out;
	}

	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, reply_msg_type,
				     msg_num, ENIC_DST_PARENT_PF, 0, size);
	stats_reply_msg = (struct enic_mbox_pf_get_stats_reply_msg *)&m->msg;
	stats_reply_msg->generic_reply.ret_major = 0;
	stats_reply_msg->generic_reply.ret_minor = 0;
	stats_reply = stats_reply_msg->detailed_reply;

	/* Get size of ethtool stats array and allocate a buffer of that size.
	 */
	if ((!enic->sriov.vf_v2.ethtool_sset_count) ||
	    (!enic->sriov.vf_v2.ethtool_stats)) {
#if (ENIC_HAVE_GET_STATS_COUNT)
		stats_count = enic_get_stats_count(netdev);
#else
		stats_count = enic_get_sset_count(netdev, ETH_SS_STATS);
#endif
		stats_size = stats_count * sizeof(u64);

		enic->sriov.vf_v2.ethtool_stats = kzalloc(stats_size, GFP_KERNEL);
		if  (!enic->sriov.vf_v2.ethtool_stats)
			goto tx_err_out;

		enic->sriov.vf_v2.ethtool_sset_count = stats_count;
	}

	enic_get_ethtool_stats(netdev, NULL, enic->sriov.vf_v2.ethtool_stats);

	if (stats_req_msg->flags & ENIC_MBOX_GET_STATS_RX) {
		num_bytes = offsetof(struct vnic_rx_stats, rsvd);
		stats_reply->num_rx_stats = num_bytes / sizeof(u64);
	} else
		stats_reply->num_rx_stats = 0;

	if (stats_req_msg->flags & ENIC_MBOX_GET_STATS_TX) {
		num_bytes = offsetof(struct vnic_tx_stats, rsvd);
		stats_reply->num_tx_stats = num_bytes / sizeof(u64);
	} else
		stats_reply->num_tx_stats = 0;

	memset(&stats_reply->pad, 0, sizeof(stats_reply->pad));

	err = enic_get_sset_block_offset(netdev, ENIC_STATS_TX, 0, &offset);
	if (err)
		goto tx_err_out;

	for (i = 0; i < enic_get_num_stats(enic, ENIC_STATS_TX); i++) {
		if ((offset + i) < enic->sriov.vf_v2.ethtool_sset_count)
			((u64 *)&stats_reply->vnic_stats.tx)[i] =
				enic->sriov.vf_v2.ethtool_stats[offset + i];
	}

	err = enic_get_sset_block_offset(netdev, ENIC_STATS_RX, 0, &offset);
	if (err)
		goto tx_err_out;

	for (i = 0; i < enic_get_num_stats(enic, ENIC_STATS_RX); i++) {
		if ((offset + i) < enic->sriov.vf_v2.ethtool_sset_count)
			((u64 *)&stats_reply->vnic_stats.rx)[i] =
				enic->sriov.vf_v2.ethtool_stats[offset + i];
	}

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err)
		goto tx_err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;

rx_err_out:
	enic_mbox_stats_inc(enic, vf_id, reply_msg_type - 1, num_rx_err);
	goto out;
tx_err_out:
	enic_mbox_stats_inc(enic, vf_id, reply_msg_type, num_tx_err);
	goto out;
}

static int enic_mbox_rx_msg_vf_add_del_mac_reply(struct enic *enic,
						 struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_add_del_mac_reply_msg *mac_reply_msg;
	struct enic_mbox_vf_add_del_mac_msg *mac_msg;
	struct net_device *netdev = enic->netdev;
	int ret_major, ret_minor, i;
	u16 flags, reply_flags;
	u16 expected_msg_len;
	u64 msg_num;
	int err;

	msg_num = msg->hdr.msg_num;
	mac_reply_msg = (struct enic_mbox_vf_add_del_mac_reply_msg *)msg->msg;
	ret_major = mac_reply_msg->generic_reply.ret_major;
	ret_minor = mac_reply_msg->generic_reply.ret_minor;

	err = enic_mbox_msg_type_size(msg->hdr.msg_type,
				      1,  /* always expect detailed reply */
				      mac_reply_msg->detailed_reply->num_addrs,
				      &expected_msg_len);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for type %u\n",
			   __func__, msg->hdr.msg_type);
		return -EINVAL;
	}

	if (msg->hdr.msg_len != expected_msg_len) {
		netdev_err(netdev, "%s -  Msg len (from hdr): %d != Expected msg len: %d (ret_minor=%d num_addrs=%d\n",
			   __func__, msg->hdr.msg_len, expected_msg_len,
			   ret_minor, mac_reply_msg->detailed_reply->num_addrs);
		enic_mbox_gen_stats_inc(enic, msg_len_mismatch);
		return -EINVAL;
	}

	/* A single mbox msg request can include mutliple add/del ucast/mcast
	 * MAC addresses.
	 * ret_major!=0 means the mbox request itself failed, the individual
	 * MACs in the request have not been processed at atll.
	 */
	if (ret_major) {
		netdev_err(netdev, "%s - [msg_num=%llu] msg %s to PF failed, err = %d/%d\n",
			   __func__, msg_num,
			   /* requests and replies have consecutive msg_type
			    * numbers: msg_type-1 is (request) type for the
			    *          (reply) msg_type type
			    */
			   enic_mbox_msg_type_to_str(msg->hdr.msg_type - 1),
			   ret_major, ret_minor);
		return 0;
	}

	mac_msg = mac_reply_msg->detailed_reply;
	for (i = 0; i < mac_msg->num_addrs; i++) {
		flags = mac_msg->mac_addr[i].flags;
		reply_flags =  flags & MAC_ADDR_FLAG_REPLY_MASK;

		if (ret_minor && reply_flags)
			netdev_warn(netdev, "Addr #%2d - %s %pM %s- Result: %s%s%s%s%s%s%s (flags=0x%x)\n",
				    i,
				    (flags & MAC_ADDR_FLAG_ADD) ? "Add" : "Del",
				    &mac_msg->mac_addr[i].addr,
				    (flags & MAC_ADDR_FLAG_STATION)	? "(Station)    " : "",
				    (flags & MAC_ADDR_FLAG_OVERFLOW)	? "Overflow     " : "",
				    (flags & MAC_ADDR_FLAG_DUPLICATE)	? "Duplicate    " : "",
				    (flags & MAC_ADDR_FLAG_FAILED)	? "Failed       " : "",
				    (flags & MAC_ADDR_FLAG_NOT_FOUND)	? "Not found    " : "",
				    (flags & MAC_ADDR_FLAG_NOT_PERMITTED) ? "Not permitted" : "",
				    (flags & MAC_ADDR_FLAG_INVALID)	? "Invalid      " : "",
				    (flags & MAC_ADDR_FLAG_SKIPPED)	? "Skipped      " : "",
				    flags);

		if (flags & MAC_ADDR_FLAG_STATION) {
			if (reply_flags) {
				netdev_warn(netdev, "%s - Unable to set station MAC to %pM: restoring old addr %pM\n",
					    __func__, mac_msg->mac_addr[i].addr,
					    enic->mac_addr_old);
				enic_restore_old_station_addr(enic);
			} else
				enic_clear_mac_addr_change_pending(enic);
		}
	}

	return 0;
}

void enic_mbox_pending_lists_lock_init(struct enic *enic)
{
	mutex_init(&enic->sriov.vf_v2.pending_lists_lock);
}

void enic_mbox_pending_lists_lock(struct enic *enic)
{
	mutex_lock(&enic->sriov.vf_v2.pending_lists_lock);
}

void enic_mbox_pending_lists_unlock(struct enic *enic)
{
	mutex_unlock(&enic->sriov.vf_v2.pending_lists_lock);
}

int enic_mbox_are_pending_lists_locked(struct enic *enic)
{
	return mutex_is_locked(&enic->sriov.vf_v2.pending_lists_lock);
}

static int enic_mbox_num_pending_macs_locked(struct enic *enic)
{
	struct list_head *p, *list;
	int count = 0;

	ENIC_ASSERT_MBOX_PENDING_LIST_LOCK(enic);

	list = &enic->sriov.vf_v2.pending_macs;

	if (list == NULL)
		return 0;

	list_for_each(p, list) {
		count++;
	}

	return count;
}

/* New elements are added to the tail.
 * Elements are sent to the PF starting from the head.
 * This rouitne is used to remove the first num_macs elements from the head.
 */
static int enic_mbox_cleanup_pending_macs_locked(struct enic *enic,
						 int num_macs)
{
	struct list_head *cur, *next, *pending_macs = &enic->sriov.vf_v2.pending_macs;
	struct net_device *netdev = enic->netdev;
	struct enic_mac_addr_item *mac_item;
	int num_deleted = 0;

	ENIC_ASSERT_MBOX_PENDING_LIST_LOCK(enic);

	list_for_each_safe(cur, next, pending_macs) {
		mac_item = list_entry(cur, struct enic_mac_addr_item, list);
		list_del(&mac_item->list);
		kfree(mac_item);
		enic_mbox_gen_stats_inc(enic, num_mac_free_ok);

		if (++num_deleted == num_macs)
			break;
	}
	if (num_deleted !=  num_macs) {
		netdev_err(netdev, "%s - deleted only %d of the %d requested\n",
			   __func__, num_deleted, num_macs);
		return -1;
	}

	return 0;
}

void enic_mbox_flush_pending_macs(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	int num_macs;

	enic_mbox_pending_lists_lock(enic);
	num_macs = enic_mbox_num_pending_macs_locked(enic);
	if (num_macs) {
		enic_mbox_cleanup_pending_macs_locked(enic, num_macs);
		netdev_info(netdev, "%s - flushed pending macs list (num_macs = %d)\n",
			    __func__, num_macs);
	}
	enic_mbox_pending_lists_unlock(enic);
}

static int enic_mbox_add_mac_to_pending_macs(struct enic *enic, u8 mac_flags,
					     const u8 *mac)
{
	struct enic_mac_addr_item *addr_item;

	addr_item = kzalloc(sizeof(*addr_item), GFP_KERNEL);
	if (!addr_item) {
		enic_mbox_gen_stats_inc(enic, num_mac_alloc_err);
		return -ENOMEM;
	}
	enic_mbox_gen_stats_inc(enic, num_mac_alloc_ok);

	addr_item->mac_addr.flags = mac_flags;
	ether_addr_copy((u8 *)&addr_item->mac_addr.addr, mac);
	INIT_LIST_HEAD(&addr_item->list);

	enic_mbox_pending_lists_lock(enic);
	list_add_tail(&addr_item->list, &enic->sriov.vf_v2.pending_macs);
	enic_mbox_pending_lists_unlock(enic);

	return 0;
}

int enic_mbox_push_vf_macs(struct enic *enic)
{
	struct enic_mbox_vf_add_del_mac_msg *mac_msg;
	struct net_device *netdev = enic->netdev;
	struct enic_mac_addr_item *mac_item;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	struct list_head *item;
	int pos, err = 0;
	int num_macs;
	u16 size;

	msg_type = ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST;

	enic_mbox_pending_lists_lock(enic);

	num_macs = enic_mbox_num_pending_macs_locked(enic);
	if (num_macs == 0)
		goto out;

	if (num_macs > MAX_MAC_OPS) {
		netdev_err(netdev, "%s - Request for %d perfect filters (max is %d)\n",
			   __func__, num_macs, MAX_MAC_OPS);
		err = -E2BIG;
		goto err_out;
	}

	err = enic_mbox_msg_type_size(msg_type, num_macs, 0, &size);

	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u (param1=%u, param2=%u)\n",
			   __func__, msg_type, num_macs, 0);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}

	m = ENIC_MBOX_MSG(skb);
	err = enic_mbox_init_msg_hdr(enic, &m->hdr,
				     ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST,
				     0, ENIC_DST_PARENT_PF, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	mac_msg = (struct enic_mbox_vf_add_del_mac_msg *)&m->msg;
	pos = 0;
	list_for_each(item, &enic->sriov.vf_v2.pending_macs) {
		mac_item = list_entry(item, struct enic_mac_addr_item, list);
		mac_msg->mac_addr[pos].flags = mac_item->mac_addr.flags;
		ether_addr_copy((u8 *)&mac_msg->mac_addr[pos].addr,
				 mac_item->mac_addr.addr);
		pos++;
	}
	mac_msg->num_addrs = num_macs;

	err = enic_mbox_tx_skb(enic, skb);
	if (err)
		goto err_out;
	else
		/* FIXME: Ignoring return code for now. */
		enic_mbox_cleanup_pending_macs_locked(enic, num_macs);
out:
	enic_mbox_pending_lists_unlock(enic);

	if (err && skb)
		enic_mbox_free_skb(enic, skb);

	return err;
err_out:
	enic_mbox_stats_inc(enic, ENIC_DST_PARENT_PF, msg_type, num_tx_err);
	goto out;
}

/* flags    : flags for the API
 * mac_flags: flags for the mac entry
 */

static int enic_mbox_vf_add_del_mac(struct enic *enic, u8 flags, const u8 *mac,
				    u8 mac_flags)
{
	struct net_device *netdev = enic->netdev;
	int err = 0;

	err = enic_mbox_add_mac_to_pending_macs(enic, mac_flags, mac);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		return err;
	}

	if (flags & ENIC_MBOX_FLAG_PUSH_MACS)
		return enic_mbox_push_vf_macs(enic);

	return 0;
}

int enic_mbox_vf_add_mac(struct enic *enic, u8 flags, const u8 *mac, u8 mac_flags)
{
	return enic_mbox_vf_add_del_mac(enic, flags,
					mac, mac_flags | MAC_ADDR_FLAG_ADD);
}

int enic_mbox_vf_del_mac(struct enic *enic, u8 flags, const u8 *mac, u8 mac_flags)
{
	return enic_mbox_vf_add_del_mac(enic, flags, mac, mac_flags);
}

static int enic_mbox_tx_msg_pf_set_admin_mac_ack(struct enic *enic,
						 struct enic_mbox_msg *msg,
						 int ret_major)
{
	struct enic_mbox_pf_set_admin_mac_ack_msg *ack_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	u16 size;
	int err;

	msg_type = ENIC_MBOX_PF_SET_ADMIN_MAC_ACK;

	err = enic_mbox_msg_type_size(msg_type, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u\n",
			   __func__, msg_type);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type, msg->hdr.msg_num,
				     ENIC_DST_PARENT_PF, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	ack_msg = (struct enic_mbox_pf_set_admin_mac_ack_msg *)m->msg;
	ack_msg->ack.ret_major = ret_major;

	err = enic_mbox_tx_skb(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);

	return err;

err_out:
	enic_mbox_stats_inc(enic, ENIC_DST_PARENT_PF, msg_type, num_tx_err);
	goto out;
}

static int enic_mbox_rx_msg_pf_set_admin_mac_notif(struct enic *enic,
						   struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_set_admin_mac_notif_msg *notif_msg;
	struct net_device *netdev = enic->netdev;
	u8 *admin_mac;
	int err = 0;

	notif_msg = (struct enic_mbox_pf_set_admin_mac_notif_msg *)msg->msg;
	admin_mac = notif_msg->mac_addr.addr;

	/* Not using is_valid_ether_addr() because the 0 MAC address is valid
	 * in this case:
	 * the PF can send us the 0 MAC address (to remove the admin MAC config).
	 * In such case the VF will assign a random MAC and register it with the
	 * PF.
	 */
	if (is_multicast_ether_addr(admin_mac)) {
		netdev_err(netdev, "%s - Station MAC addr %pM (multicast) received from PF is not valid\n",
			   __func__, admin_mac);
		err = -EINVAL;
	}

	err = enic_mbox_tx_msg_pf_set_admin_mac_ack(enic, msg, err);

	if (is_zero_ether_addr(admin_mac)) {
		eth_hw_addr_random(enic->netdev);
		ether_addr_copy(enic->mac_addr, netdev->dev_addr);

		/* Ask PF to install the new random MAC
		 */
		err = enic_mbox_vf_add_mac(enic, ENIC_MBOX_FLAG_PUSH_MACS,
					   enic->mac_addr,
					   MAC_ADDR_FLAG_ADD |
					   MAC_ADDR_FLAG_STATION);

	} else {
		/* If there was any pending MAC addr change applied by the
		 * VF itself, we need to clear it.
		 */
		enic_clear_mac_addr_change_pending(enic);

		ether_addr_copy(enic->mac_addr, admin_mac);
		eth_hw_addr_set(netdev, admin_mac);

		/* No need to call enic_set_mac_addr_change_pending()
		 * because the PF has already pushed the admin MAC to HW
		 */
	}

	/* Generating notification for both cases
	 * - Old PF configured VF admin mac to New PF configured VF admin mac
	 * - Old PF configured VF admin mac to New random mac
	 */
	call_netdevice_notifiers(NETDEV_CHANGEADDR, netdev);

	return err;
}

int enic_mbox_vf_set_pkt_filter(struct enic *enic, int directed, int multicast,
				int broadcast, int promisc, int allmulti)
{
	struct enic_mbox_vf_set_pkt_filter_flags_msg *pkt_filter_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	u16 flags = 0, size;
	int err;

	if (directed)
		flags |= ENIC_MBOX_PKT_FILTER_DIRECTED;
	if (multicast)
		flags |= ENIC_MBOX_PKT_FILTER_MULTICAST;
	if (broadcast)
		flags |= ENIC_MBOX_PKT_FILTER_BROADCAST;
	if (promisc)
		flags |= ENIC_MBOX_PKT_FILTER_PROMISC;
	if (allmulti)
		flags |= ENIC_MBOX_PKT_FILTER_ALLMULTI;

	msg_type = ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST;

	err = enic_mbox_msg_type_size(msg_type, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u\n",
			   __func__, msg_type);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type,
				     0, ENIC_DST_PARENT_PF, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	pkt_filter_msg = (struct enic_mbox_vf_set_pkt_filter_flags_msg *)m->msg;
	pkt_filter_msg->flags = flags;
	pkt_filter_msg->pad = 0;

	err = enic_mbox_tx_skb(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;

err_out:
	enic_mbox_stats_inc(enic, ENIC_DST_PARENT_PF, msg_type, num_tx_err);
	goto out;
}

int enic_mbox_rx_msg_vf_set_pkt_filter_flags_reply(struct enic *enic,
						   struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_set_pkt_filter_flags_reply_msg *filter_reply_msg;
	struct net_device *netdev = enic->netdev;
	int ret_major, ret_minor;
	u64 msg_num;

	msg_num = msg->hdr.msg_num;
	filter_reply_msg =
		(struct enic_mbox_vf_set_pkt_filter_flags_reply_msg *)msg->msg;
	ret_major = filter_reply_msg->generic_reply.ret_major;
	ret_minor = filter_reply_msg->generic_reply.ret_minor;

	if (ret_major) {
		netdev_warn(netdev, "%s - [msg_num=%llu] msg ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST to PF failed, err = %d/%d\n",
			    __func__, msg_num, ret_major, ret_minor);
		enic_restore_old_station_addr(enic);
		return 0;
	}

	if (ret_minor)
		netdev_dbg(netdev, "%s - msg filter flags applied: %s%s%s%s%s\n",
			   __func__,
			   (ret_minor & ENIC_MBOX_PKT_FILTER_DIRECTED) ? "DIRECTED " : "",
			   (ret_minor & ENIC_MBOX_PKT_FILTER_MULTICAST) ? "MULTICAST " : "",
			   (ret_minor & ENIC_MBOX_PKT_FILTER_BROADCAST) ? "BROADCAST " : "",
			   (ret_minor & ENIC_MBOX_PKT_FILTER_PROMISC) ? "PROMISC " : "",
			   (ret_minor & ENIC_MBOX_PKT_FILTER_ALLMULTI) ? "ALLMULTI " : "");

	return 0;
}

int enic_mbox_vf_rcv_skb(struct enic *enic, struct sk_buff *skb)
{
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	struct enic_mbox_msg *msg;
	u64 msg_num;
	u8 msg_type;
	u16 vf_id;
	int err;

	msg = ENIC_MBOX_MSG(skb);
	hdr = ENIC_MBOX_HDR(skb);
	vf_id = hdr->src_vnic_id;
	msg_type = hdr->msg_type;
	msg_num = hdr->msg_num;

	/* Only messages from PF are allowed
	 */
	if (vf_id != ENIC_DST_PARENT_PF) {
		netdev_err(netdev, "%s - [msg_num=%llu][msg_type=%s] - Bad sender PF/VF ID %u, VF V2 vnics only support message exchanges with the PF\n",
			   __func__, msg_num,
			   enic_mbox_msg_type_to_str(msg_type), vf_id);
		enic_mbox_gen_stats_inc(enic, bad_sender_id);
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx_drop);
		err = -EINVAL;
		goto out;
	}

	if (msg_type != ENIC_MBOX_VF_REGISTER_REPLY &&
	    msg_type != ENIC_MBOX_VF_CAPABILITY_REPLY &&
	    !enic_vf_registered(enic)) {
		netdev_err(netdev, "%s - [msg_num=%llu][msg_type=%s/%d] VF is not registered\n",
			   __func__, msg_num,
			   enic_mbox_msg_type_to_str(msg_type), msg_type);
		enic_mbox_gen_stats_inc(enic, vf_not_registered);
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx_drop);
		err = -EINVAL;
		goto out;
	}

	enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx);

	switch (msg_type) {
	case ENIC_MBOX_VF_CAPABILITY_REPLY:
		err = enic_mbox_rx_msg_vf_capability_reply(enic, msg);
		break;
	case ENIC_MBOX_VF_REGISTER_REPLY:
		err = enic_mbox_rx_msg_vf_register_reply(enic, msg);
		break;
	case ENIC_MBOX_VF_UNREGISTER_REPLY:
		err = enic_mbox_rx_msg_vf_unregister_reply(enic, msg);
		break;
	case ENIC_MBOX_PF_LINK_STATE_NOTIF:
		err = enic_mbox_rx_msg_pf_link_state_notif(enic, msg);
		break;
	case ENIC_MBOX_PF_GET_STATS_REQUEST:
		err = enic_mbox_rx_msg_pf_get_stats(enic, msg);
		break;
	case ENIC_MBOX_VF_ADD_DEL_MAC_REPLY:
		err = enic_mbox_rx_msg_vf_add_del_mac_reply(enic, msg);
		break;
	case ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF:
		err = enic_mbox_rx_msg_pf_set_admin_mac_notif(enic, msg);
		break;
	case ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY:
		err = enic_mbox_rx_msg_vf_set_pkt_filter_flags_reply(enic, msg);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	/* Here vf_id is actually the PF ID
	 */
	enic_mbox_flow_ctrl_rx(enic, vf_id, msg_type);
out:
	return err;
}
