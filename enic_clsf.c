/*
 *  Copyright 2013-2018 Cisco Systems, Inc.  All rights reserved.
 */
#include "kcompat.h"
#include "enic_config.h"
#include "enic_clsf.h"

int enic_addfltr_mac_vlan(struct enic *enic, u8 *macaddr,
	u16 vlan_id, u16 rq_id, u16 *filter_id)
{
	int ret;
	struct filter data;
	struct filter_mac_vlan *mac_filter;
	
	memset(&data, 0, sizeof(data));
	data.type = FILTER_MAC_VLAN;
	mac_filter = &data.u.mac_vlan;
	memcpy(mac_filter->mac_addr, macaddr, ETH_ALEN);
	mac_filter->vlan = vlan_id;
	mac_filter->flags = FILTER_FIELDS_MAC_VLAN;

	spin_lock_bh(&enic->devcmd_lock);
	ret = vnic_dev_classifier(enic->vdev, CLSF_ADD, &rq_id, &data);
	*filter_id = rq_id;	
	spin_unlock_bh(&enic->devcmd_lock);
		
	return ret;
}

/*
 * enic_addfltr_5t - Add ipv4 5tuple filter
 *	@enic: enic struct of vnic
 *	@keys: flow_keys of ipv4 5tuple
 *	@rq: rq number to steer to
 *
 * This function returns filter_id(hardware_id) of the filter
 * added. In case of error it returns an negative number.
 */
int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq)
{
	int res;
	struct filter data;

#if (VIC_HAVE_FLOW_DISSECTOR_H)
	switch (keys->basic.ip_proto){
#else
	switch (keys->ip_proto){
#endif
	case IPPROTO_TCP:
		data.u.ipv4.protocol = PROTO_TCP;
		break;
	case IPPROTO_UDP:
		data.u.ipv4.protocol = PROTO_UDP;
		break;
	default:
		return -EPROTONOSUPPORT;
	};
	data.type = FILTER_IPV4_5TUPLE;
#if (ENIC_HAVE_FLOW_DISSECTOR_KEY_PORTS_STRUCT)
	data.u.ipv4.src_addr = ntohl(keys->addrs.v4addrs.src);
	data.u.ipv4.dst_addr = ntohl(keys->addrs.v4addrs.dst);
	data.u.ipv4.src_port = ntohs(keys->ports.src);
	data.u.ipv4.dst_port = ntohs(keys->ports.dst);
#else
	data.u.ipv4.src_addr = ntohl(keys->src);
	data.u.ipv4.dst_addr = ntohl(keys->dst);
	data.u.ipv4.src_port = ntohs(keys->port16[0]);
	data.u.ipv4.dst_port = ntohs(keys->port16[1]);
#endif
	data.u.ipv4.flags = FILTER_FIELDS_IPV4_5TUPLE;

	spin_lock_bh(&enic->devcmd_lock);
	res = vnic_dev_classifier(enic->vdev, CLSF_ADD, &rq, &data);
	spin_unlock_bh(&enic->devcmd_lock);
	res = (res == 0) ? rq : res;

	return res;
}

/*
 * enic_delfltr - Delete clsf filter
 * 	@enic: enic struct of vnic
 * 	@filter_id: filter_is(hardware_id) of filter to be deleted
 *
 * This function returns zero in case of success, negative number incase of
 * error.
 */
int enic_delfltr(struct enic *enic, u16 filter_id)
{
	int ret;

	spin_lock_bh(&enic->devcmd_lock);
	ret = vnic_dev_classifier(enic->vdev, CLSF_DEL, &filter_id, NULL); 
	spin_unlock_bh(&enic->devcmd_lock);
	
	return ret;
}

struct enic_rfs_fltr_node *htbl_fltr_search(struct enic *enic, u16 fltr_id)
{
	int i;

	for (i = 0; i < (1 << ENIC_RFS_FLW_BITSHIFT); i++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG)
		struct hlist_node *pos;
#endif

		hhead = &enic->rfs_h.ht_head[i];
		enic_hlist_for_each_entry_safe(n, pos, tmp, hhead, node)
			if (n->fltr_id == fltr_id)
				return n;
	}

	return NULL;
}

/*
 * enic_rfs_flw_tbl_init - initialize enic->rfs_h members
 *	@enic: enic data
 */
void enic_rfs_flw_tbl_init(struct enic *enic)
{
	int i;

	spin_lock_init(&enic->rfs_h.lock);

	for (i = 0; i <= ENIC_RFS_FLW_MASK; i++)
		INIT_HLIST_HEAD(&enic->rfs_h.ht_head[i]);

	enic->rfs_h.max = enic->config.num_arfs;
	enic->rfs_h.free = enic->rfs_h.max;
	enic->rfs_h.toclean = 0;
}

void enic_rfs_flw_tbl_free(struct enic *enic)
{
	int i, res;

	enic_rfs_timer_free(enic);
	spin_lock_bh(&enic->rfs_h.lock);

	for (i=0; i < (1 << ENIC_RFS_FLW_BITSHIFT); i++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG)
		struct hlist_node *pos;
#endif

		hhead = &enic->rfs_h.ht_head[i];
		enic_hlist_for_each_entry_safe(n, pos, tmp, hhead, node) {
			res = enic_delfltr(enic, n->fltr_id);
			hlist_del(&n->node);
			kfree(n);
			enic->rfs_h.free++;
		}
	}

	spin_unlock_bh(&enic->rfs_h.lock);
}

#ifdef CONFIG_RFS_ACCEL
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
void enic_flow_may_expire(unsigned long t)
#else
void enic_flow_may_expire(struct timer_list *t)
#endif
{
	struct enic *enic = from_timer(enic, t, rfs_h.rfs_may_expire);
	bool res;
	int j;

	spin_lock_bh(&enic->rfs_h.lock);
	for (j = 0; j < ENIC_CLSF_EXPIRE_COUNT; j++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG)
		struct hlist_node *pos;
#endif

		hhead = &enic->rfs_h.ht_head[enic->rfs_h.toclean++];
		enic_hlist_for_each_entry_safe(n, pos, tmp, hhead, node) {
			res = rps_may_expire_flow(enic->netdev, n->rq_id,
						  n->flow_id, n->fltr_id);
			if (res) {
				res = enic_delfltr(enic, n->fltr_id);
				if (unlikely(res))
					continue;
				hlist_del(&n->node);
				kfree(n);
				enic->rfs_h.free++;
			}
		}
	}
	spin_unlock_bh(&enic->rfs_h.lock);
	mod_timer(&enic->rfs_h.rfs_may_expire, jiffies + HZ/4);
}

static inline struct enic_rfs_fltr_node* htbl_key_search(struct hlist_head *h,
							 struct flow_keys *k)
{
	struct enic_rfs_fltr_node *tpos;
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_POS_ARG)
	struct hlist_node *pos;
#endif

#if (VIC_HAVE_FLOW_DISSECTOR_H)
	hlist_for_each_entry(tpos, h, node)
		if (tpos->keys.addrs.v4addrs.src == k->addrs.v4addrs.src &&
			tpos->keys.addrs.v4addrs.dst == k->addrs.v4addrs.dst &&
			tpos->keys.ports.ports == k->ports.ports &&
			tpos->keys.basic.ip_proto == k->basic.ip_proto &&
			tpos->keys.basic.n_proto == k->basic.n_proto)
			return tpos;
#else
	enic_hlist_for_each_entry(tpos, pos, h, node)
		if (tpos->keys.src == k->src &&
		    tpos->keys.dst == k->dst &&
		    tpos->keys.ports == k->ports &&
		    tpos->keys.ip_proto == k->ip_proto)
			return tpos;
#endif
	return NULL;
}

int enic_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id)
{
	struct flow_keys keys;
	struct enic_rfs_fltr_node *n;
	struct enic *enic;
	u16 tbl_idx;
	int res, i;

	enic = netdev_priv(dev);
#if ((VIC_HAVE_FLOW_DISSECTOR_H) || (ENIC_HAVE_SKB_FLOW_DISSECT_FLOW_KEYS))
	res = skb_flow_dissect_flow_keys(skb, &keys, 0);
	if (!res || keys.basic.n_proto != htons(ETH_P_IP) ||
		(keys.basic.ip_proto != IPPROTO_TCP &&
		keys.basic.ip_proto != IPPROTO_UDP))
		return -EPROTONOSUPPORT;
#else
	res = skb_flow_dissect(skb, &keys);
	if (!res)
		return -EPROTONOSUPPORT;
	if ((keys.ip_proto != IPPROTO_TCP) && (keys.ip_proto != IPPROTO_UDP))
		return -EPROTONOSUPPORT;
#endif

	tbl_idx = skb_get_hash_raw((struct sk_buff *)skb) & ENIC_RFS_FLW_MASK;

	spin_lock_bh(&enic->rfs_h.lock);
	n = htbl_key_search(&enic->rfs_h.ht_head[tbl_idx], &keys);

	if (n) { /* entry already present  */
		if (rxq_index == n->rq_id) {
			res = -EEXIST;
			goto ret_unlock;
		}

		/* desired rq changed for the flow, we need to delete
		 * old fltr and add new one
		 *
		 * The moment we delete the fltr, the upcoming pkts
		 * are put it default rq based on rss. When we add
		 * new filter, upcoming pkts are put in desired queue.
		 * This could cause ooo pkts.
		 *
		 * Lets 1st try adding new fltr and then del old one.
		 */
		i = --enic->rfs_h.free;
		if (unlikely(i < 0)) { /* clsf tbl is full, we have to del old */
			enic->rfs_h.free++;
			res = enic_delfltr(enic, n->fltr_id);
			if (unlikely(res < 0))
				goto ret_unlock;
			res = enic_addfltr_5t(enic, &keys, rxq_index);
			if (res < 0) {
				hlist_del(&n->node);
				enic->rfs_h.free++;
				goto ret_unlock;
			}
		} else { /* add new fltr 1st then del old fltr */
			int ret;

			res = enic_addfltr_5t(enic, &keys, rxq_index);
			if (res < 0) {
				enic->rfs_h.free++;
				goto ret_unlock;
			}
			ret = enic_delfltr(enic, n->fltr_id);
			/* deleting old fltr failed. Add old fltr to list.
			 * enic_flow_may_expire() will try to delete it later.
			 */
			if (unlikely(ret < 0)) {
				struct enic_rfs_fltr_node *d;
				struct hlist_head *head;

				head = &enic->rfs_h.ht_head[tbl_idx];

				d = kmalloc(sizeof(*n), GFP_ATOMIC);
				if (d) {
					d->fltr_id = n->fltr_id;;
					INIT_HLIST_NODE(&d->node);
					hlist_add_head(&d->node, head);
				}
			} else {
				enic->rfs_h.free++;
			}
		}
		n->rq_id = rxq_index;
		n->fltr_id = res;
		n->flow_id = flow_id;
	} else { /* entry not present */
		i = --enic->rfs_h.free;
		if (i <= 0) {
			enic->rfs_h.free++;
			res = -EBUSY;
			goto ret_unlock;
		}

		n = kmalloc(sizeof(*n), GFP_ATOMIC);
		if (!n) {
			res = -ENOMEM;
			enic->rfs_h.free++;
			goto ret_unlock;
		}

		res = enic_addfltr_5t(enic, &keys, rxq_index);
		if (res < 0) {
			kfree(n);
			enic->rfs_h.free++;
			goto ret_unlock;
		}
		n->rq_id = rxq_index;
		n->fltr_id = res;
		n->flow_id = flow_id;
		n->keys = keys;
		INIT_HLIST_NODE(&n->node);
		hlist_add_head(&n->node, &enic->rfs_h.ht_head[tbl_idx]);
	}

ret_unlock:
	spin_unlock_bh(&enic->rfs_h.lock);
	return res;
}
#endif /* CONFIG_RFS_ACCEL */
