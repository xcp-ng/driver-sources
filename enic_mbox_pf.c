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
#include "enic_dev.h"
#include "enic_sriov.h"
#include "enic_mbox.h"

static int enic_mbox_pf_rx(struct enic *enic)
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
					"enic_admin_rx : failed err %d\n", err);
			list_del(&msg_item->list);
			enic_mbox_free_rx_msg(enic, msg_item);
		}
	}

	enic_mbox_msg_list_unlock_bh(enic);

	return 0;
}

void enic_mbox_pf_work(struct work_struct *work)
{
	struct enic_sriov *enic_sriov = container_of(work, struct enic_sriov,
						     mbox_work);
	struct enic *enic = container_of(enic_sriov, struct enic, sriov);

	rtnl_lock();
	enic_sriov_lock_bh(enic);

	enic_mbox_work(enic, &enic_mbox_pf_rx);

	enic_sriov_unlock_bh(enic);
	rtnl_unlock();
}

/* enic: sender vnic
 * msg : msg to reply to
 */
static
int enic_mbox_tx_msg_vf_register_unregister_ack_locked(struct enic *enic,
						       struct enic_mbox_msg *msg,
						       u16 ret_code)
{
	enum enic_mbox_msg_t msg_type_request, msg_type_reply;
	struct enic_mbox_vf_reg_unreg_reply_msg *reply_msg;
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_msg *m = NULL;
	struct sk_buff *skb = NULL;
	u16 size, dst_vnic_id;
	int err = 0;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type_request = msg->hdr.msg_type;

	if (msg_type_request == ENIC_MBOX_VF_REGISTER_REQUEST)
		msg_type_reply = ENIC_MBOX_VF_REGISTER_REPLY;
	else
		msg_type_reply = ENIC_MBOX_VF_UNREGISTER_REPLY;

	/* The source of the request msg is the dest of the reply msg
	 */
	dst_vnic_id = msg->hdr.src_vnic_id;

	err = enic_mbox_msg_type_size(msg_type_reply, 0, 0, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u (param1=0, param2=0)\n",
			   __func__, msg_type_reply);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type_reply,
				     msg->hdr.msg_num, dst_vnic_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	reply_msg = (struct enic_mbox_vf_reg_unreg_reply_msg *)m->msg;
	reply_msg->generic_reply.ret_major = ret_code;

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);

	return err;

err_out:
	enic_mbox_stats_inc(enic, dst_vnic_id, msg_type_reply, num_tx_err);
	goto out;
}

static int enic_add_vf_mac_locked(struct enic *enic, u16 vf_id,
				  struct enic_mac_addr *mac_addr);
static int enic_del_vf_mac_locked(struct enic *enic, u16 vf_id,
				  struct enic_mac_addr_item *enic_addr);
static void enic_clear_vf_pkt_filter_flags(struct enic *enic, u16 vf_id);

/* It does not clear the PF applied config (admin MAC and VLAN)
 */
static int enic_sriov_clear_all_vf_requested_config(struct enic *enic,
						    u16 vf_id)
{
	struct enic_mac_addr_item *addr_item, *tmp;
	struct list_head *mac_addrs;
	int err, num_errs = 0;

	/* Remove all MAC/VLAN filters.
	 * The init of the admin MAC itself is not removed so that the VF
	 * will be able to get it at probe time.
	 * However we disable the admin MAC (for all vlans) filter/s because
	 * they will be added back when the VF will probe/init again.
	 */

	mac_addrs = &enic->sriov.pf.vfs[vf_id].mac_addrs;
	list_for_each_entry_safe(addr_item, tmp, mac_addrs, list) {

		err = enic_del_vf_mac_locked(enic, vf_id, addr_item);
		if (err)
			num_errs++;
	}

	/* Clear the cached packet filter flags
	 */
	enic_clear_vf_pkt_filter_flags(enic, vf_id);

	return num_errs;
}

/* Respond to CAPABILITY_REQUEST. Simply send a reply message that
 * carries PF driver admin channel version.
 */
static int
enic_mbox_rx_msg_vf_capability_locked(struct enic *enic, struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_capability_reply_msg *cap_reply;
	struct enic_mbox_vf_capability_msg *cap_req;
	enum enic_mbox_msg_t reply_type;
	struct enic_mbox_msg *reply;
	struct net_device *netdev;
	struct sk_buff *skb;
	int vf_id, err;
	u16 size;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	skb = NULL;
	netdev = enic->netdev;
	vf_id = msg->hdr.src_vnic_id;
	/* Ignore VF driver's admin channel version. There is nothing
	 * special to do in this version with respect to
	 * backward/forward compatibility.
	 */
	cap_req = (struct enic_mbox_vf_capability_msg *)msg->msg;
	netdev_dbg(netdev, "CAPABILITY_REQUEST: VF %u version %u\n",
		   vf_id, cap_req->version);
	reply_type = ENIC_MBOX_VF_CAPABILITY_REPLY;
	err = enic_mbox_msg_type_size(reply_type, 0, 0, &size);
	if (err)
		goto out;

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto out;
	}
	reply = ENIC_MBOX_MSG(skb);
	err = enic_mbox_init_msg_hdr(enic, &reply->hdr, reply_type,
				     msg->hdr.msg_num, vf_id, 0, size);
	if (err)
		goto out;
	cap_reply = (struct enic_mbox_vf_capability_reply_msg *)reply->msg;
	cap_reply->generic_reply.ret_major = 0;
	cap_reply->version = ENIC_MBOX_CAP_VERSION_1;

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err) {
		netdev_err(netdev, "%s enic_mbox_tx_skb failed err = %d\n", __func__, err);
		enic_mbox_stats_inc(enic, vf_id, reply_type, num_tx_err);
	}

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;
}

static int enic_mbox_rx_msg_vf_register_locked(struct enic *enic,
					       struct enic_mbox_msg *msg)
{
	int num_vfs_registered_old = enic->sriov.pf.num_vfs_registered;
	struct net_device *netdev = enic->netdev;
	int vf_id, err, ret_code = 0;
	struct enic_mbox_hdr *hdr;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;

	if (enic_is_vf_registered_locked(enic, vf_id)) {
		/* If the VF is already registered, we are going to assume
		 * that between previous REGISTER_REQUEST and this one
		 * there was an UNREGISTER_REQUEST that went lost or was never
		 * generated.
		 */
		netdev_warn(netdev, "%s - vf_id %u is already registered, clearing old/stale\n",
			    __func__, vf_id);

		/* Ignoring return code
		 */
		enic_sriov_clear_all_vf_requested_config(enic, vf_id);
		/* No need to increment num_vfs_registered in this case
		 */
	} else {
		enic->sriov.pf.num_vfs_registered++;
		enic->sriov.pf.vfs[vf_id].vf_id = vf_id;
		enic->sriov.pf.vfs[vf_id].registered = 1;
	}

	err = enic_mbox_tx_msg_vf_register_unregister_ack_locked(enic, msg,
								 ret_code);
	if (err) {
		netdev_err(netdev, "%s - TX of VF_REGISTER_REPLY to vf_id %d failed. Skypping link_state msg\n",
			   __func__, vf_id);
		return err;
	}

	/* In the duplicate VF_REGISTER_REQUEST case we may not have a 0->1
	 * transition for num_vfs_registered.
	 * num_vfs_registrered_old is used to avoid un-necessary calls to
	 * schedule the stats poller workqueue.
	 */
	if ((enic->sriov.pf.num_vfs_registered == 1) &&
	    (num_vfs_registered_old == 0) &&
	    (!enic_hw_rx_stats_ok(enic)))
		schedule_delayed_work(&enic->sriov.pf.vfs_stats_poller_work, HZ);

	return enic_mbox_tx_link_state_notif_locked(enic, vf_id);
}

static int enic_mbox_rx_msg_vf_unregister_locked(struct enic *enic,
						 struct enic_mbox_msg *msg)
{
	struct net_device *netdev = enic->netdev;
	int vf_id, err, ret_code = 0;
	struct enic_mbox_hdr *hdr;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;

	if (!enic_is_vf_registered_locked(enic, vf_id)) {
		netdev_err(netdev, "%s - vf_id %u is not registered\n",
			   __func__, vf_id);
		enic_mbox_stats_inc(enic, vf_id, ENIC_MBOX_VF_UNREGISTER_REQUEST,
				    num_rx_err);
		ret_code = -1;
	} else
		enic->sriov.pf.num_vfs_registered--;

	enic->sriov.pf.vfs[vf_id].registered = 0;

	err = enic_sriov_clear_all_vf_requested_config(enic, vf_id);
	if (err)
		netdev_warn(netdev, "%s - %d errors while cleaning VF %u filters\n",
			    __func__, vf_id, err);

	err = enic_mbox_tx_msg_vf_register_unregister_ack_locked(enic, msg,
								 ret_code);
	if (err) {
		/* This is not a hard error, the VF if closing anyway
		 * and does not wait for this.
		 */
		netdev_warn(netdev, "%s - TX of VF_UNREGISTER_REPLY to vf_id %d failed.\n",
			    __func__, vf_id);
		return err;
	}

	return 0;
}

int enic_mbox_tx_link_state_notif_locked(struct enic *enic, int vf_id)
{
	struct enic_mbox_pf_link_state_notif_msg *notif_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	u8 vf_link_state_mode;
	int err = 0;
	u16 size;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type = ENIC_MBOX_PF_LINK_STATE_NOTIF;

	if (!enic_is_vf_registered_locked(enic, vf_id)) {
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_skip);
		return 0;
	}

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
				     0, vf_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	notif_msg = (struct enic_mbox_pf_link_state_notif_msg *)&m->msg;

	vf_link_state_mode = enic->sriov.pf.vfs[vf_id].link_state_mode;

	switch (vf_link_state_mode) {
	case IFLA_VF_LINK_STATE_AUTO:
		notif_msg->link_state = !!netif_carrier_ok(netdev);
		break;
	case IFLA_VF_LINK_STATE_ENABLE:
		notif_msg->link_state = ENIC_MBOX_LINK_STATE_ENABLE;
		break;
	case IFLA_VF_LINK_STATE_DISABLE:
		notif_msg->link_state = ENIC_MBOX_LINK_STATE_DISABLE;
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	netdev_info(netdev, "%s - PF TX link state change <%s> to vf_id %u\n",
		    __func__, (notif_msg->link_state) ? "UP" : "down", vf_id);

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;
err_out:
	enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_err);
	goto out;
}

static int enic_mbox_rx_msg_pf_link_state_ack_locked(struct enic *enic,
						     struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_link_state_ack_msg *link_state_ack_msg;
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	u16 ret_major, ret_minor;
	u16 vf_id;

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;

	link_state_ack_msg = (struct enic_mbox_pf_link_state_ack_msg *)&msg->msg;
	ret_major = link_state_ack_msg->ack.ret_major;
	ret_minor = link_state_ack_msg->ack.ret_minor;

	if (ret_major)
		netdev_err(netdev, "%s - vf_id %u err = %u/%u\n",
			   __func__, vf_id, ret_major, ret_minor);

	return 0;
}

int enic_mbox_tx_pf_get_stats_request_locked(struct enic *enic, int vf_id)
{
	struct enic_mbox_pf_get_stats_msg *stats_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	int err = 0;
	u16 size;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type = ENIC_MBOX_PF_GET_STATS_REQUEST;

	if (!enic_is_vf_registered_locked(enic, vf_id)) {
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_skip);
		return 0;
	}

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
				     0, vf_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	stats_msg = (struct enic_mbox_pf_get_stats_msg *)&m->msg;
	stats_msg->flags = ENIC_MBOX_GET_STATS_ALL;
	stats_msg->pad = 0;

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);
	return err;
err_out:
	enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_err);
	goto out;
}

static int enic_mbox_rx_msg_pf_get_stats_reply_locked(struct enic *enic,
						      struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_get_stats_reply_msg *stats_msg;
	struct enic_mbox_pf_get_stats_reply *stats;
	struct net_device *netdev = enic->netdev;
	struct vnic_stats *vnic_stats_src, *vnic_stats_dst;
	u16 ret_major, ret_minor;
	u64 msg_num;
	u16 vf_id;

	msg_num = msg->hdr.msg_num;
	vf_id = msg->hdr.src_vnic_id;
	stats_msg = (struct enic_mbox_pf_get_stats_reply_msg *)msg->msg;
	ret_major = stats_msg->generic_reply.ret_major;
	ret_minor = stats_msg->generic_reply.ret_minor;

	if (!ret_major) {
		stats = stats_msg->detailed_reply;

		vnic_stats_src = &stats->vnic_stats;
		vnic_stats_dst = &enic->sriov.pf.vfs[vf_id].vnic_stats;

		u64_stats_update_begin(&enic->sriov.pf.vfs[vf_id].syncp);
		*vnic_stats_dst = *vnic_stats_src;
		u64_stats_update_end(&enic->sriov.pf.vfs[vf_id].syncp);
	} else
		netdev_err(netdev, "%s - [msg_num=%llu] ret_major:%u ret_minor:%u\n",
			   __func__, msg_num, ret_major, ret_minor);

	return 0;
}

/* msg is the original/request message.
 */
static int enic_mbox_tx_msg_vf_add_del_mac_ack_locked(struct enic *enic,
						      struct enic_mbox_msg *msg,
						      int ret_code,
						      int num_errors)
{
	struct enic_mbox_vf_add_del_mac_msg *orig_mac_msg, *reply_mac_msg;
	struct enic_mbox_vf_add_del_mac_reply_msg *reply_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct enic_mbox_msg *m = NULL;
	struct sk_buff *skb = NULL;
	u16 size, dst_vnic_id;
	int err = 0, i;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type = ENIC_MBOX_VF_ADD_DEL_MAC_REPLY;
	dst_vnic_id = msg->hdr.src_vnic_id;
	orig_mac_msg = (struct enic_mbox_vf_add_del_mac_msg *)msg->msg;

	err = enic_mbox_msg_type_size(msg_type,
				      1, /* num_errors
					  * Using '1' to force a detailed reply
					  */
				      orig_mac_msg->num_addrs, &size);
	if (err) {
		netdev_err(netdev, "%s - Unable to get msg size for msg_type %u (param1=%u, param2=%u)\n",
			   __func__, msg_type, 1 /* forcing detailed reply */,
			   orig_mac_msg->num_addrs);
		goto err_out;
	}

	skb = enic_mbox_alloc_skb(enic, size);
	if (!skb) {
		err = -ENOMEM;
		goto err_out;
	}
	m = ENIC_MBOX_MSG(skb);

	err = enic_mbox_init_msg_hdr(enic, &m->hdr, msg_type,
				     msg->hdr.msg_num, dst_vnic_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	reply_msg = (struct enic_mbox_vf_add_del_mac_reply_msg *)m->msg;
	reply_msg->generic_reply.ret_major = ret_code;
	reply_msg->generic_reply.ret_minor = num_errors;

	reply_mac_msg = reply_msg->detailed_reply;
	reply_mac_msg->num_addrs = orig_mac_msg->num_addrs;
	for (i = 0; i < orig_mac_msg->num_addrs; i++) {
		reply_mac_msg->mac_addr[i].flags =
			orig_mac_msg->mac_addr[i].flags;
		ether_addr_copy((u8 *)&reply_mac_msg->mac_addr[i].addr,
				(u8 *)&orig_mac_msg->mac_addr[i].addr);
	}

	err = enic_mbox_tx_skb_locked(enic, skb);
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

/* false: the msg includes a request to add a station MAC that will be rejected
 * true : the msg does not include any request for adding a station MAC
 *        or it includes one that will be accepted.
 */
static bool enic_mbox_is_station_mac_allowed(struct enic *enic, u16 vf_id,
					     struct enic_mac_addr *enic_mac)
{
	u8 trusted = enic->sriov.pf.vfs[vf_id].trusted;

	/* When the VF is not trusted, it can not request a station MAC address
	 * that does not match the one from vnic config (which was previously
	 * configured by the PF).
	 */
	if (!trusted &&
	    enic_is_vf_admin_mac_configured(enic, vf_id) &&
	    !enic_is_vf_admin_mac_matching(enic, vf_id, enic_mac->addr)) {
		return false;
	}

	return true;
}

static bool enic_mbox_is_station_mac_ok(struct enic *enic,
					struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_add_del_mac_msg *mac_msg;
	struct enic_mac_addr *enic_mac;
	struct enic_mbox_hdr *hdr;
	bool is_multicast;
	u16 num_addrs;
	u16 vf_id, i;
	bool is_add;

	/* This is possible when the mac address config request was initiated
	 * by the PF
	 */
	if (msg == NULL)
		return true;

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;
	mac_msg = (struct enic_mbox_vf_add_del_mac_msg *)&msg->msg;
	num_addrs = mac_msg->num_addrs;

	for (i = 0; i < num_addrs; i++) {
		enic_mac = &mac_msg->mac_addr[i];

		is_add = enic_mac->flags & MAC_ADDR_FLAG_ADD;
		is_multicast = is_multicast_ether_addr(enic_mac->addr);

		if (is_add &&
		    (enic_mac->flags & MAC_ADDR_FLAG_STATION) &&
		    (is_multicast ||
		     !enic_mbox_is_station_mac_allowed(enic, vf_id, enic_mac)))
			return false;
	}
	return true;
}

/* The caller is supposed to check for duplicates
 */
int enic_mbox_add_vf_mac_to_mac_addrs(struct enic *enic, u16 vf_id, u8 *mac_addr)
{
	struct enic_mac_addr_item *addr_item;
	struct list_head *mac_addrs;

	addr_item = kzalloc(sizeof(*addr_item), GFP_KERNEL);
	if (!addr_item) {
		enic_mbox_gen_stats_inc(enic, num_mac_alloc_err);
		return -ENOMEM;
	}

	enic_mbox_gen_stats_inc(enic, num_mac_alloc_ok);

	ether_addr_copy(addr_item->mac_addr.addr, mac_addr);
	mac_addrs = &enic->sriov.pf.vfs[vf_id].mac_addrs;
	list_add_tail(&addr_item->list, mac_addrs);

	return 0;
}

static struct enic_mac_addr_item *enic_find_vf_mac(struct enic *enic, u16 vf_id,
						   const u8 *mac_addr)
{
	struct list_head *mac_addrs, *item;
	struct enic_mac_addr_item *addr_item;

	mac_addrs = &enic->sriov.pf.vfs[vf_id].mac_addrs;
	list_for_each(item, mac_addrs) {
		addr_item = list_entry(item, struct enic_mac_addr_item, list);

		if (ether_addr_equal(mac_addr, addr_item->mac_addr.addr))
			return addr_item;
	}

	return NULL;
}

static int enic_add_vf_mac_locked(struct enic *enic, u16 vf_id,
				  struct enic_mac_addr *mac_addr)
{
	struct net_device *netdev = enic->netdev;
	int num_devcmd_errors = 0;
	int err = 0;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	ENIC_DEVCMD_PROXY_BY_INDEX(vf_id, err, enic, vnic_dev_add_addr_vlan,
				   mac_addr->addr, ENIC_VLAN_ANY,
				   0); /* vlan id not valid */
	err = enic_dev_status_to_errno(err);
	if (err)
		num_devcmd_errors++;

	err = enic_mbox_add_vf_mac_to_mac_addrs(enic, vf_id, mac_addr->addr);
	if (err) {
		netdev_warn(netdev, "%s - Unable to save VF MAC %pM into PF's local copy (err = %d).\n ",
			    __func__, mac_addr->addr, err);
		/* err = 0; */
	}

	return (num_devcmd_errors) ? -1 : 0;
}

static int enic_del_vf_mac_locked(struct enic *enic, u16 vf_id,
				  struct enic_mac_addr_item *enic_addr)
{
	int err = 0;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	ENIC_DEVCMD_PROXY_BY_INDEX(vf_id, err, enic, vnic_dev_del_addr_vlan,
				   enic_addr->mac_addr.addr, ENIC_VLAN_ANY,
				   0); /* vlan id not valid */
	err = enic_dev_status_to_errno(err);

	list_del(&enic_addr->list);
	kfree(enic_addr);
	enic_mbox_gen_stats_inc(enic, num_mac_free_ok);

	return (err) ? -1 : 0;
}

int enic_mbox_process_vf_mac_request(struct enic *enic, u16 vf_id,
				     struct enic_mac_addr *mac_addr,
				     bool add, struct enic_mbox_msg *msg,
				     u16 flags)
{
	struct net_device *netdev = enic->netdev;
	struct enic_mac_addr_item *tmp_mac_addr;
	bool station_addr_ok;
	int err = 0;

	if (is_zero_ether_addr(mac_addr->addr)) {
		netdev_err(netdev, "%s - vf_id %u, Invalid MAC %pM\n",
			   __func__, vf_id, mac_addr->addr);
		mac_addr->flags |= MAC_ADDR_FLAG_INVALID;
		return -EADDRNOTAVAIL;
	}

	tmp_mac_addr = enic_find_vf_mac(enic, vf_id, mac_addr->addr);

	if (add) {
		if (tmp_mac_addr) {
			netdev_warn(netdev, "%s - vf_id %u, MAC %pM already registered, ignoring request\n",
				    __func__, vf_id, mac_addr->addr);
			mac_addr->flags |= MAC_ADDR_FLAG_DUPLICATE;
			return -EEXIST;
		}

		/* When the VF is not trusted and the VF MAC addr has been set by
		 * the PF admin, the only mac filters the VF can add are:
		 * 1) the station MAC (which must match with the one the VF
		 *    reads from FW)
		 * 2) multicast addrs
		 */
		if (!(flags & MAC_ADDED_BY_PF) &&
		    /* FLAG_STATION implies 'unicast' too
		     */
		    (mac_addr->flags & MAC_ADDR_FLAG_STATION) &&
		    (!enic_mbox_is_station_mac_allowed(enic, vf_id, mac_addr))) {
			netdev_warn(netdev, "%s - vf_id %u, Add %pM not allowed\n",
				   __func__, vf_id, mac_addr);
			mac_addr->flags |= MAC_ADDR_FLAG_NOT_PERMITTED;
			return -EPERM;
		}

		err = enic_add_vf_mac_locked(enic, vf_id, mac_addr);
		if (err) {
			netdev_err(netdev, "%s - vf_id %u, Unable to add MAC %pM (err=%d)\n",
				   __func__, vf_id, &mac_addr->addr, err);
			mac_addr->flags |= MAC_ADDR_FLAG_FAILED;
		}
	} else {
		/* When the VF is running/open, a change of the station MAC addr
		 * generates two requests: 'del old addr'+ 'add new addr.
		 * (when the VF is not running/open it instead generates only
		 * the 'add new addr' request)
		 * If this msg includes a request to add a new station MAC addr
		 * that is going to be rejected (for example because the VF is
		 * not trusted) let's skip this request to delete the old
		 * station MAC addr.
		 */
		if (mac_addr->flags & MAC_ADDR_FLAG_STATION) {
			station_addr_ok = enic_mbox_is_station_mac_ok(enic, msg);
			if (!station_addr_ok) {
				netdev_warn(netdev, "%s - vf_id %u, Del MAC %pM skipped\n",
					    __func__, vf_id, mac_addr->addr);
				mac_addr->flags |= MAC_ADDR_FLAG_SKIPPED;
				return 0;
			}
		}

		/* This is not a fatal error. It is possible for the VF to ask
		 * for the deletion of a MAC address not (yet) installed, for
		 * example if a set mac addr is issued between probe and open.
		 */
		if (!tmp_mac_addr) {
			netdev_warn(netdev, "%s - vf_id %u, MAC %pM not found\n",
				    __func__, vf_id, mac_addr->addr);
			mac_addr->flags |= MAC_ADDR_FLAG_NOT_FOUND;
			return -ENODEV;
		}
		err = enic_del_vf_mac_locked(enic, vf_id, tmp_mac_addr);
		if (err) {
			netdev_err(netdev, "%s - vf_id %u, Unable to del MAC %pM (err=%d)\n",
				   __func__, vf_id, mac_addr->addr, err);
			mac_addr->flags |= MAC_ADDR_FLAG_FAILED;
		}
	}

	return err;
}

static int enic_mbox_num_vf_macs(struct enic *enic, u16 vf_id, u8 *num_ucast,
				 u8 *num_mcast, u8 *num_invalid)
{
	struct list_head *p, *list = &enic->sriov.pf.vfs[vf_id].mac_addrs;
	struct enic_mac_addr_item *mac_item;
	int count = 0;

	*num_ucast = 0;
	*num_mcast = 0;
	*num_invalid = 0;

	if (list == NULL)
		return 0;

	list_for_each(p, list) {
		mac_item = list_entry(p, struct enic_mac_addr_item, list);

		count++;
		/* This is supposed to be imposssible */
		if (is_zero_ether_addr(mac_item->mac_addr.addr))
			(*num_invalid)++;
		else if (is_multicast_ether_addr(mac_item->mac_addr.addr))
			(*num_mcast)++;
		else
			(*num_ucast)++;
	}

	return count;
}

static int enic_mbox_rx_msg_vf_add_del_mac_locked(struct enic *enic,
						  struct enic_mbox_msg *msg)
{
	u8 num_total, num_ucast, num_mcast, num_invalid;
	struct enic_mbox_vf_add_del_mac_msg *mac_msg;
	struct net_device *netdev = enic->netdev;
	struct enic_mac_addr *enic_mac;
	struct enic_mbox_hdr *hdr;
	u16 vf_id, i;
	int err = 0;
	int num_errors = 0;
	u16 num_addrs;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;
	mac_msg = (struct enic_mbox_vf_add_del_mac_msg *)&msg->msg;

	num_addrs = mac_msg->num_addrs;

	if (num_addrs > MAX_MAC_OPS) {
		netdev_err(netdev, "%s - request for %d perfect filters (max is %d)\n",
			   __func__, num_addrs, MAX_MAC_OPS);
		enic_mbox_stats_inc(enic, vf_id,
				    ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST,
				    num_rx_err);
		err = -E2BIG;
		goto out;
	}

	/* Get the number of already configured ucast and mcast addresses for
	 * this vf_id
	 */
	num_total = enic_mbox_num_vf_macs(enic, vf_id, &num_ucast, &num_mcast,
					  &num_invalid);
	netdev_info(netdev, "VF %u : total # mac addrs %u\n", vf_id, num_total);

	for (i = 0; i < num_addrs; i++) {
		bool is_multicast;
		bool mac_add;

		enic_mac = &mac_msg->mac_addr[i];
		is_multicast = is_multicast_ether_addr(enic_mac->addr);
		mac_add = enic_mac->flags & MAC_ADDR_FLAG_ADD;

		if (is_multicast && mac_add) {
			/* The station mac (ie, the main MAC) can not be mcast
			 */
			if (enic_mac->flags & MAC_ADDR_FLAG_STATION) {
				enic_mac->flags |= MAC_ADDR_FLAG_INVALID;
				netdev_err(netdev, "%s - Station addr (%pM) can not be multicast\n",
					   __func__, enic_mac->addr);
				num_errors++;
				continue;
			}

			/* Using '>=' instead of '==' to catch possible
			 * accounting bugs
			 */
			if (num_mcast >= ENIC_VF_V2_MAX_MCAST) {
				enic_mac->flags |= MAC_ADDR_FLAG_OVERFLOW;
				num_errors++;
				continue;
			}
		} else {
			/* The station addr is not included in the MAC_UCAST
			 * budget
			 */
			if (mac_add &&
			    !(enic_mac->flags & MAC_ADDR_FLAG_STATION) &&
			    (num_ucast >= ENIC_VF_V2_MAX_UCAST)) {
				enic_mac->flags |= MAC_ADDR_FLAG_OVERFLOW;
				num_errors++;
				continue;
			}
		}

		err = enic_mbox_process_vf_mac_request(enic, vf_id, enic_mac,
						       enic_mac->flags &
						       MAC_ADDR_FLAG_ADD,
						       msg, MAC_ADDED_BY_VF);

		if (!err) {
			if (is_multicast)
				if (mac_add)
					num_mcast++;
				else
					num_mcast--;
			else
				if (mac_add)
					num_ucast++;
				else
					num_ucast--;
		} else
			num_errors++;
	}

	err = 0;
out:
	return enic_mbox_tx_msg_vf_add_del_mac_ack_locked(enic, msg,
							  err, num_errors);
}

int enic_mbox_tx_pf_set_admin_mac_notif_locked(struct enic *enic, int vf_id,
					       const u8 *mac)
{
	struct enic_mbox_pf_set_admin_mac_notif_msg *notif_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct sk_buff *skb = NULL;
	struct enic_mbox_msg *m;
	int err = 0;
	u16 size;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type = ENIC_MBOX_PF_SET_ADMIN_MAC_NOTIF;

	if (!enic_is_vf_registered_locked(enic, vf_id)) {
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_skip);
		return 0;
	}

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
				     0, vf_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	notif_msg = (struct enic_mbox_pf_set_admin_mac_notif_msg *)&m->msg;
	ether_addr_copy((u8 *)&notif_msg->mac_addr.addr, mac);

	err = enic_mbox_tx_skb_locked(enic, skb);
	if (err)
		goto err_out;

out:
	if (err && skb)
		enic_mbox_free_skb(enic, skb);

	return err;

err_out:
	enic_mbox_stats_inc(enic, vf_id, msg_type, num_tx_err);
	goto out;
}

static
int enic_mbox_rx_msg_pf_set_admin_mac_ack_locked(struct enic *enic,
						 struct enic_mbox_msg *msg)
{
	struct enic_mbox_pf_set_admin_mac_ack_msg *ack_msg;
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	u16 vf_id;

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;

	ack_msg = (struct enic_mbox_pf_set_admin_mac_ack_msg *)&msg->msg;
	if (ack_msg->ack.ret_major) {
		netdev_warn(netdev, "%s - VF#%u rejected the admin MAC config (err = %d/%d)\n",
			    __func__, vf_id, ack_msg->ack.ret_major,
			    ack_msg->ack.ret_minor);

		/* The VF rejected the MAC addr configured by the PF.
		 * We are not removing the VF admin MAC configured filter
		 * and we are not resetting the VF either.
		 */
	}

	return 0; /* Not using ret_major */
}

/* These routines assume the vf_id has already been sanity-checked
 */
static void enic_get_vf_pkt_filter_flags(struct enic *enic, u16 vf_id,
					 u16 *flags, u16 *req_flags)
{
	*flags = enic->sriov.pf.vfs[vf_id].pkt_fltr_flags;
	*req_flags = enic->sriov.pf.vfs[vf_id].pkt_fltr_req_flags;
}

static void enic_set_vf_pkt_filter_flags(struct enic *enic, u16 vf_id,
					 u16 flags, u16 req_flags)
{
	enic->sriov.pf.vfs[vf_id].pkt_fltr_flags = flags;
	enic->sriov.pf.vfs[vf_id].pkt_fltr_req_flags = req_flags;
}

static void enic_clear_vf_pkt_filter_flags(struct enic *enic, u16 vf_id)
{
	enic->sriov.pf.vfs[vf_id].pkt_fltr_flags = 0;
	enic->sriov.pf.vfs[vf_id].pkt_fltr_req_flags = 0;
}

static
int enic_mbox_tx_msg_vf_set_pkt_filter_flags_reply_locked(struct enic *enic,
							  struct enic_mbox_msg *msg,
							  u16 ret_major,
							  int ret_minor)
{
	struct enic_mbox_vf_set_pkt_filter_flags_reply_msg *reply_msg;
	struct net_device *netdev = enic->netdev;
	enum enic_mbox_msg_t msg_type;
	struct enic_mbox_msg *m = NULL;
	struct sk_buff *skb = NULL;
	u16 size, dst_vnic_id;
	int err = 0;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg_type = ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REPLY;
	dst_vnic_id = msg->hdr.src_vnic_id;

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
				     msg->hdr.msg_num, dst_vnic_id, 0, size);
	if (err) {
		netdev_err(netdev, "%s - err = %d\n", __func__, err);
		goto err_out;
	}

	reply_msg = (struct enic_mbox_vf_set_pkt_filter_flags_reply_msg *)m->msg;
	reply_msg->generic_reply.ret_major = ret_major;
	/* The applied set of flags is returned only in case of success
	 */
	reply_msg->generic_reply.ret_minor = (ret_major) ? 0 : ret_minor;

	err = enic_mbox_tx_skb_locked(enic, skb);
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

static
int enic_mbox_rx_msg_vf_set_pkt_filter_flags_locked(struct enic *enic,
						    struct enic_mbox_msg *msg)
{
	struct enic_mbox_vf_set_pkt_filter_flags_msg *pkt_filter_msg;
	u16 vf_flags, vf_req_flags, vf_flags_old, vf_req_flags_old;
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	int err, err_devcmd = 0;
	u8 trusted;
	u16 vf_id;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	hdr = &msg->hdr;
	vf_id = hdr->src_vnic_id;

	pkt_filter_msg = (struct enic_mbox_vf_set_pkt_filter_flags_msg *)&msg->msg;
	vf_req_flags = pkt_filter_msg->flags;
	vf_flags = vf_req_flags;

	enic_get_vf_pkt_filter_flags(enic, vf_id, &vf_flags_old,
				     &vf_req_flags_old);

	/* The Directed flag is always set, its config is not exposed to user
	 */
	if (!(vf_flags & ENIC_MBOX_PKT_FILTER_DIRECTED)) {
		vf_flags |= ENIC_MBOX_PKT_FILTER_DIRECTED;
		netdev_err(netdev, "%s - vf_id %u Directed flag can not be cleared (ignoring request)\n",
			   __func__, vf_id);
	}

	/* Multicast and Broadcast flags do not require trusted mode
	 */
	trusted = enic->sriov.pf.vfs[vf_id].trusted;

	if (vf_flags & ENIC_MBOX_PKT_FILTER_ALLMULTI) {
		if (!trusted && enic_is_vf_admin_configured(enic, vf_id)) {
			netdev_warn(netdev, "%s - vf_id %u Allmulti req ignored: not allowed for untrusted VFs when admin mac/vlan are configured\n",
				    __func__, vf_id);
			vf_flags &= ~ENIC_MBOX_PKT_FILTER_ALLMULTI;
		}
	}

	if (vf_flags & ENIC_MBOX_PKT_FILTER_PROMISC) {
		if (!trusted && enic_is_vf_admin_configured(enic, vf_id)) {
			netdev_warn(netdev, "%s - vf_id %u Promisc req ignored: not allowed for untrusted VFs when admin mac/vlan are configured\n",
				    __func__, vf_id);
			vf_flags &= ~ENIC_MBOX_PKT_FILTER_PROMISC;
		}
	}

	/* No need to wait for the packet_filter devcmd result to update the
	 * vf_flags because in case of such failure we have no way to tell what
	 * went wrong exactly.
	 */
	enic_set_vf_pkt_filter_flags(enic, vf_id, vf_flags, vf_req_flags);

	if (vf_flags != vf_flags_old) {
		ENIC_DEVCMD_PROXY_BY_INDEX(vf_id, err, enic,
					   vnic_dev_packet_filter,
					   vf_flags & ENIC_MBOX_PKT_FILTER_DIRECTED,
					   vf_flags & ENIC_MBOX_PKT_FILTER_MULTICAST,
					   vf_flags & ENIC_MBOX_PKT_FILTER_BROADCAST,
					   vf_flags & ENIC_MBOX_PKT_FILTER_PROMISC,
					   vf_flags & ENIC_MBOX_PKT_FILTER_ALLMULTI);

		err_devcmd = enic_dev_status_to_errno(err);
		if (err_devcmd)
			netdev_err(netdev, "%s - vf %u:- failed to apply packet filter (err = %d)\n",
				   __func__, vf_id, err_devcmd);
	}

	err = enic_mbox_tx_msg_vf_set_pkt_filter_flags_reply_locked(enic, msg,
								    err_devcmd,
								    vf_flags);
	return err;
}

static int enic_mbox_pf_rcv_skb_locked(struct enic *enic, struct sk_buff *skb)
{
	struct net_device *netdev = enic->netdev;
	struct enic_mbox_hdr *hdr;
	struct enic_mbox_msg *msg;
	u64 msg_num;
	u8 msg_type;
	u16 vf_id;
	int err;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	msg = ENIC_MBOX_MSG(skb);
	hdr = ENIC_MBOX_HDR(skb);
	vf_id = hdr->src_vnic_id;
	msg_type = hdr->msg_type;
	msg_num = hdr->msg_num;

	/* Either this is a spurious message sent just before the VF completed
	 * its PCI remove, or PF and VFs are out of sync.
	 */
	if (!enic->sriov.pf.vfs) {
		netdev_err(netdev, "%s - [msg_num=%llu] enic->sriov.pf.vfs is not initialized\n",
			   __func__, msg_num);
		enic_mbox_gen_stats_inc(enic, pf_vfs_not_init);
		err = -EINVAL;
		goto out;
	}

	/* The vf_id is unreliable, we can not return an error to the sender
	 */
	if ((vf_id > enic->sriov.pf.num_vfs) || (vf_id == ENIC_DST_PARENT_PF)) {
		netdev_err(netdev, "%s - [msg_num=%llu] Error: VF id %u is out of allowed range (0 - %u)\n",
			   __func__, msg_num, vf_id, enic->sriov.pf.num_vfs-1);
		enic_mbox_gen_stats_inc(enic, bad_sender_id);
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx_drop);
		err = -EINVAL;
		goto out;
	}

	/* The registration and capability requests are the only
	 * messages we accept from unregistered VFs
	 */
	if (msg_type != ENIC_MBOX_VF_REGISTER_REQUEST &&
	    msg_type != ENIC_MBOX_VF_CAPABILITY_REQUEST &&
	    !enic_is_vf_registered_locked(enic, vf_id)) {
		netdev_err(netdev, "%s - [msg_num=%llu] msg_type=%u/%s Error: vf_id %u is not registered\n",
			   __func__, msg_num, msg_type,
			   enic_mbox_msg_type_to_str(msg_type), vf_id);
		enic_mbox_gen_stats_inc(enic, vf_not_registered);
		enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx_drop);
		err = -EINVAL;
		goto out;
	}

	enic_mbox_stats_inc(enic, vf_id, msg_type, num_rx);

	switch (msg_type) {
	case ENIC_MBOX_VF_CAPABILITY_REQUEST:
		err = enic_mbox_rx_msg_vf_capability_locked(enic, msg);
		break;
	case ENIC_MBOX_VF_REGISTER_REQUEST:
		err = enic_mbox_rx_msg_vf_register_locked(enic, msg);
		break;
	case ENIC_MBOX_VF_UNREGISTER_REQUEST:
		err = enic_mbox_rx_msg_vf_unregister_locked(enic, msg);
		break;
	case ENIC_MBOX_PF_LINK_STATE_ACK:
		err = enic_mbox_rx_msg_pf_link_state_ack_locked(enic, msg);
		break;
	case ENIC_MBOX_PF_GET_STATS_REPLY:
		err = enic_mbox_rx_msg_pf_get_stats_reply_locked(enic, msg);
		break;
	case ENIC_MBOX_VF_ADD_DEL_MAC_REQUEST:
		err = enic_mbox_rx_msg_vf_add_del_mac_locked(enic, msg);
		break;
	case ENIC_MBOX_PF_SET_ADMIN_MAC_ACK:
		err = enic_mbox_rx_msg_pf_set_admin_mac_ack_locked(enic, msg);
		break;
	case ENIC_MBOX_VF_SET_PKT_FILTER_FLAGS_REQUEST:
		err = enic_mbox_rx_msg_vf_set_pkt_filter_flags_locked(enic, msg);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	enic_mbox_flow_ctrl_rx(enic, vf_id, msg_type);
out:
	return err;
}

int enic_mbox_pf_rcv_skb(struct enic *enic, struct sk_buff *skb)
{
	int err;

	ENIC_ASSERT_SRIOV_LOCK(enic);

	err = enic_mbox_pf_rcv_skb_locked(enic, skb);
	return err;
}
