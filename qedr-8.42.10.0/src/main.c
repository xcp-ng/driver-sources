/* QLogic (R)NIC Driver/Library
 * Copyright (c) 2010-2017  Cavium, Inc.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_mad.h>
#include <linux/netdevice.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <net/addrconf.h>
#include <linux/idr.h>
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
#include <linux/stat.h>
#endif

#include "qed_chain.h"
#include "qed_if.h"
#include "qedr.h"
#include "verbs.h"
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
#include "qedr_user.h"
#include "qedr_debugfs.h"
#include "qedr_compat.h"
#else
#include <rdma/qedr-abi.h>
#endif
#include "qedr_iw_cm.h"

MODULE_DESCRIPTION("QLogic 40G/100G ROCE Driver");
MODULE_AUTHOR("Cavium Inc");
MODULE_LICENSE("Dual BSD/GPL");

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM*/
MODULE_VERSION(QEDR_MODULE_VERSION);

static uint debug;
module_param(debug, uint, S_IRUGO);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static uint delayed_ack;
module_param(delayed_ack, uint, S_IRUGO);
MODULE_PARM_DESC(delayed_ack, " iWARP: Delayed Ack: 0 - Disabled 1 - Enabled. Default: Disabled");

static uint timestamp = 1;
module_param(timestamp, uint, S_IRUGO);
MODULE_PARM_DESC(timestamp, " iWARP: Timestamp: 0 - Disabled 1 - Enabled. Default: Enabled");

static uint rcv_wnd_size;
module_param(rcv_wnd_size, uint, S_IRUGO);
MODULE_PARM_DESC(rcv_wnd_size, " iWARP: Receive Window Size in K. Minimum is 64K. Default is set according to device configuration");

static uint crc_needed = 1;
module_param(crc_needed, uint, S_IRUGO);
MODULE_PARM_DESC(crc_needed, " iWARP: CRC needed 0 - Disabled 1 - Enabled. Default:Enabled");

static uint peer2peer = 1;
module_param(peer2peer, uint, S_IRUGO);
MODULE_PARM_DESC(peer2peer, " iWARP: Support peer2peer ULPs 0 - Disabled 1 - Enabled. Default:Enabled");

static uint mpa_enhanced = 1;
module_param(mpa_enhanced, uint, S_IRUGO);
MODULE_PARM_DESC(mpa_enhanced, " iWARP: MPA Enhanced mode. Default:1");

static uint rtr_type = 7;
module_param(rtr_type, uint, S_IRUGO);
MODULE_PARM_DESC(rtr_type, " iWARP: RDMAP opcode to use for the RTR message: BITMAP "
		 "1: RDMA_SEND 2: RDMA_WRITE 4: RDMA_READ. Default: 7");

static uint iwarp_cmt;
module_param(iwarp_cmt, uint, S_IRUGO);
MODULE_PARM_DESC(iwarp_cmt, " iWARP: Support CMT mode. 0 - Disabled, 1 - Enabled. Default: Disabled");

static uint insert_udp_src_port = 1;
module_param(insert_udp_src_port, uint, S_IRUGO);
MODULE_PARM_DESC(insert_udp_src_port, " Insert a non-zero UDP source port for RoCEv2 packets that is unique per QP. 0 - Disabled, 1 - Enabled. Default:Enabled)");

#endif

#define QEDR_WQ_MULTIPLIER_DFT	(3)
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
#define QEDR_WQ_MULTIPLIER_MIN	(1)
#define QEDR_WQ_MULTIPLIER_MAX	(256)
static uint wq_multiplier = QEDR_WQ_MULTIPLIER_DFT;
module_param(wq_multiplier, uint, S_IRUGO);
MODULE_PARM_DESC(wq_multiplier, " When creating a WQ the actual number of WQE created will be multiplied by this number (default is 3).\n");
#endif

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
static void qedr_config_debug(u32 *p_dp_module, u8 *p_dp_level);
#endif

static void qedr_ib_dispatch_event(struct qedr_dev *dev, u8 port_num,
				   enum ib_event_type type)
{
	struct ib_event ibev;

	ibev.device = &dev->ibdev;
	ibev.element.port_num = port_num;
	ibev.event = type;

	ib_dispatch_event(&ibev);
}

#if !DEFINE_ROCE_GID_TABLE /* !QEDR_UPSTREAM */
static bool qedr_add_sgid(struct qedr_dev *dev, union ib_gid *new_sgid)
{
	union ib_gid zero_sgid = { { 0 } };
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dev->sgid_lock, flags);
	for (i = 0; i < QEDR_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], &zero_sgid,
			    sizeof(union ib_gid))) {
			/* found free entry */
			memcpy(&dev->sgid_tbl[i], new_sgid,
			       sizeof(union ib_gid));
			spin_unlock_irqrestore(&dev->sgid_lock, flags);
#if DEFINE_EVENT_ON_GID_CHANGE /* ! QEDR_UPSTREAM */
			qedr_ib_dispatch_event(dev, QEDR_PORT,
					       IB_EVENT_GID_CHANGE);
#endif
			return true;
		} else if (!memcmp(&dev->sgid_tbl[i], new_sgid,
				   sizeof(union ib_gid))) {
			/* entry already present, no addition is required. */
			spin_unlock_irqrestore(&dev->sgid_lock, flags);
			return false;
		}
	}
	if (i == QEDR_MAX_SGID) {
		DP_ERR(dev, "didn't find an empty entry in sgid_tbl... \n");
	}
	spin_unlock_irqrestore(&dev->sgid_lock, flags);
	return false;
}

static bool qedr_del_sgid(struct qedr_dev *dev, union ib_gid *sgid)
{
	int found = false;
	int i;
	unsigned long flags;

	DP_VERBOSE(dev, QEDR_MSG_INIT, "removing gid %llx %llx\n", sgid->global.interface_id, sgid->global.subnet_prefix);

	spin_lock_irqsave(&dev->sgid_lock, flags);
	/* first is default sgid, which cannot be deleted. */
	for (i = 1; i < QEDR_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], sgid, sizeof(union ib_gid))) {
			/* found matching entry */
			memset(&dev->sgid_tbl[i], 0, sizeof(union ib_gid));
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->sgid_lock, flags);
#if DEFINE_EVENT_ON_GID_CHANGE /* ! QEDR_UPSTREAM */
	if (found)
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_GID_CHANGE);
#endif
	return found;
}

#ifndef DEFINE_NO_IP_BASED_GIDS
static void qedr_add_ip_based_gids(struct qedr_dev *dev, struct net_device *ndev) {
	struct in_device *in_dev;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct inet6_dev *in6_dev;
	union ib_gid  *pgid;
	struct inet6_ifaddr *ifp;
#endif
	union ib_gid gid;

	/* IPv4 gids */
	in_dev = in_dev_get(ndev);
	if (in_dev) {
		for_ifa(in_dev) {
			/*ifa->ifa_address;*/
			ipv6_addr_set_v4mapped(ifa->ifa_address,
					       (struct in6_addr *)&gid);
			qedr_add_sgid(dev, &gid);
		}
		endfor_ifa(in_dev);
		in_dev_put(in_dev);
	}
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	/* IPv6 gids */
	in6_dev = in6_dev_get(ndev);
	if (in6_dev) {
		read_lock_bh(&in6_dev->lock);
		#ifdef DEFINE_IFNET6_WITHOUT_IF_LIST
		ifp = in6_dev->addr_list;
		while (ifp) {
			pgid = (union ib_gid *)&ifp->addr;
			qedr_add_sgid(dev, &gid);
			ifp = ifp->if_next;
		}
		#else
		list_for_each_entry(ifp, &in6_dev->addr_list, if_list) {
			pgid = (union ib_gid *)&ifp->addr;
			qedr_add_sgid(dev, &gid);
		}
		#endif
		read_unlock_bh(&in6_dev->lock);
		in6_dev_put(in6_dev);
	}
#endif
}
#else
static void qedr_add_ip_based_gids(struct qedr_dev *dev, struct net_device *ndev) {
}
#endif

static void qedr_build_sgid_mac(union ib_gid *sgid, unsigned char *mac_addr,
				  bool is_vlan, u16 vlan_id)
{
	sgid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	sgid->raw[8] = mac_addr[0] ^ 2;
	sgid->raw[9] = mac_addr[1];
	sgid->raw[10] = mac_addr[2];
	if (is_vlan) {
		sgid->raw[11] = vlan_id >> 8;
		sgid->raw[12] = vlan_id & 0xff;
	} else {
		sgid->raw[11] = 0xff;
		sgid->raw[12] = 0xfe;
	}
	sgid->raw[13] = mac_addr[3];
	sgid->raw[14] = mac_addr[4];
	sgid->raw[15] = mac_addr[5];
}

static void qedr_add_sgids(struct qedr_dev *dev)
{
	struct net_device *netdev, *tmp;
	u16 vlan_id;
	bool is_vlan;
	union ib_gid vgid;

	netdev = dev->ndev;

	if (!netdev) return;

#ifdef DEFINE_NETDEV_RCU
	rcu_read_lock();
	for_each_netdev_rcu(&init_net, tmp) {
#else
	read_lock(&dev_base_lock);
	rcu_read_lock();
	for_each_netdev (&init_net, tmp) {
#endif
		if (netdev == tmp || rdma_vlan_dev_real_dev(tmp) == netdev) {
			if (!netif_running(tmp))
				continue;

			/* IP based GIDs */
			qedr_add_ip_based_gids(dev, tmp);

			/* MAC/VLAN base GIDs */
			is_vlan = tmp->priv_flags & IFF_802_1Q_VLAN;
			vlan_id = (is_vlan)?vlan_dev_vlan_id(tmp):0;
			qedr_build_sgid_mac(&vgid, tmp->dev_addr, is_vlan, vlan_id);
			qedr_add_sgid(dev, &vgid);
		}
	}
#ifdef DEFINE_NETDEV_RCU
	rcu_read_unlock();
#else
	rcu_read_unlock();
	read_unlock(&dev_base_lock);
#endif
}

static void qedr_add_default_sgid(struct qedr_dev *dev)
{
	/* GID Index 0 - Invariant manufacturer-assigned EUI-64 */
	union ib_gid *sgid = &dev->sgid_tbl[0];

	sgid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	memcpy(&sgid->raw[8], &dev->attr.node_guid,
	       sizeof(dev->attr.node_guid));

	DP_VERBOSE(dev, QEDR_MSG_INIT, "DEFAULT sgid=[%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x]\n",
		   sgid->raw[0], sgid->raw[1], sgid->raw[2], sgid->raw[3], sgid->raw[4], sgid->raw[5],
		   sgid->raw[6], sgid->raw[7], sgid->raw[8], sgid->raw[9], sgid->raw[10], sgid->raw[11],
		   sgid->raw[12], sgid->raw[13], sgid->raw[14], sgid->raw[15]);
}

static int qedr_build_sgid_tbl(struct qedr_dev *dev)
{
	qedr_add_default_sgid(dev);
	qedr_add_sgids(dev);

	return 0;
}

static int qedr_addr_event(struct qedr_dev *dev,
			   unsigned long event, struct net_device *netdev,
			   union ib_gid *gid)
{
	bool is_vlan = false;
	union ib_gid vgid;
	u16 vlan_id = 0xffff;

	is_vlan = netdev->priv_flags & IFF_802_1Q_VLAN;
	if (is_vlan) {
		vlan_id = rdma_vlan_dev_vlan_id(netdev);
		netdev = vlan_dev_real_dev(netdev);
	}

	if (dev->ndev != netdev) {
		return NOTIFY_DONE;
	}

	switch (event) {
	case NETDEV_UP:
		qedr_add_sgid(dev, gid);
		if (is_vlan) {
			qedr_build_sgid_mac(&vgid, netdev->dev_addr, is_vlan, vlan_id);
			qedr_add_sgid(dev, &vgid);
		}
		break;
	case NETDEV_DOWN:
		qedr_del_sgid(dev, gid);
		if (is_vlan) {
			qedr_build_sgid_mac(&vgid, netdev->dev_addr, is_vlan, vlan_id);
			qedr_del_sgid(dev, &vgid);
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

#ifndef DEFINE_NO_IP_BASED_GIDS
static int qedr_inetaddr_event(struct notifier_block *notifier,
			       unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	union ib_gid gid;
	struct net_device *netdev = ifa->ifa_dev->dev;
	struct qedr_dev *dev = container_of(notifier, struct qedr_dev, nb_inet);

	ipv6_addr_set_v4mapped(ifa->ifa_address, (struct in6_addr *)&gid);
	return qedr_addr_event(dev, event, netdev, &gid);
}
#endif

#if IS_ENABLED(CONFIG_IPV6)

static int qedr_inet6addr_event(struct notifier_block *notifier,
				  unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	union  ib_gid *gid = (union ib_gid *)&ifa->addr;
	struct net_device *netdev = ifa->idev->dev;
	struct qedr_dev *dev = container_of(notifier, struct qedr_dev, nb_inet6);

	return qedr_addr_event(dev, event, netdev, gid);
}

#endif /* IPV6 and VLAN */

static int qedr_register_inet(struct qedr_dev *dev)
{
	int rc;

#ifndef DEFINE_NO_IP_BASED_GIDS
	dev->nb_inet.notifier_call = qedr_inetaddr_event;
	rc = register_inetaddr_notifier(&dev->nb_inet);
	if (rc) {
		DP_ERR(dev, "Failed to register inetaddr\n");
		return rc;
	}
#endif

#if IS_ENABLED(CONFIG_IPV6)
	dev->nb_inet6.notifier_call = qedr_inet6addr_event;
	rc = register_inet6addr_notifier(&dev->nb_inet6);
	if (rc) {
		DP_ERR(dev, "Failed to register inet6addr\n");
		return rc;
	}
#endif
	return 0;
}

static void qedr_unregister_inet(struct qedr_dev *dev)
{
#ifndef DEFINE_NO_IP_BASED_GIDS
	if (dev->nb_inet.notifier_call) {
		if (unregister_inetaddr_notifier(&dev->nb_inet))
			DP_NOTICE(dev, "failure unregistering notifier\n");
		dev->nb_inet.notifier_call = NULL;
	}
#endif
	#if IS_ENABLED(CONFIG_IPV6)
	if (dev->nb_inet6.notifier_call) {
		if (unregister_inet6addr_notifier(&dev->nb_inet6))
			DP_NOTICE(dev, "failure unregistering inet6 notifier\n");
		dev->nb_inet6.notifier_call = NULL;
	}
	#endif
}
#endif

static enum rdma_link_layer qedr_link_layer(struct ib_device *device,
					    u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

#if DEFINE_GET_DEV_FW_STR /* QEDR_UPSTREAM */
#if DEFINE_GET_DEV_FW_STR_FIX_LEN /* QEDR_UPSTREAM */
static void qedr_get_dev_fw_str(struct ib_device *ibdev, char *str)
#else
static void qedr_get_dev_fw_str(struct ib_device *ibdev, char *str,
				size_t str_len)
#endif
{
	struct qedr_dev *qedr = get_qedr_dev(ibdev);
	u32 fw_ver = (u32)qedr->attr.fw_ver;

#if DEFINE_GET_DEV_FW_STR_FIX_LEN /* QEDR_UPSTREAM */
	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d. %d. %d. %d",
		 (fw_ver >> 24) & 0xFF, (fw_ver >> 16) & 0xFF,
		 (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
#else
	snprintf(str, str_len, "%d. %d. %d. %d",
		 (fw_ver >> 24) & 0xFF, (fw_ver >> 16) & 0xFF,
		 (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
#endif
}
#else
static ssize_t show_fw_ver(struct device *device, struct device_attribute *attr,
			char *str)
{
	struct qedr_dev *qedr =
		rdma_device_to_drv_device(device, struct qedr_dev, ibdev);
	u32 fw_ver = (u32)qedr->attr.fw_ver;

	return sprintf(str, "%d.%d.%d.%d",
		       (fw_ver >> 24) & 0xFF, (fw_ver >> 16) & 0xFF,
		       (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
}
#endif

#ifdef DEFINE_GET_NETDEV /* QEDR_UPSTREAM */
static struct net_device *qedr_get_netdev(struct ib_device *dev, u8 port_num)
{
	struct net_device *ndev = NULL;
	struct qedr_dev *qdev;

	qdev = get_qedr_dev(dev);

	rcu_read_lock();

	if (qdev->lag_enabled)
		ndev = netdev_master_upper_dev_get_rcu(qdev->ndev);

	if (!ndev)
		ndev = qdev->ndev;

	dev_hold(ndev);

	/* The HW vendor's device driver must guarantee
	 * that this function returns NULL before the net device reaches
	 * NETDEV_UNREGISTER_FINAL state.
	 */
	rcu_read_unlock();
	return ndev;
}
#endif

#if DEFINE_PORT_IMMUTABLE /* QEDR_UPSTREAM */
static int qedr_roce_port_immutable(struct ib_device *ibdev, u8 port_num,
				    struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qedr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
#if DEFINE_ROCE_V2_SUPPORT /* QEDR_UPSTREAM */
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE |
				    RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
#else
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
#endif
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

static int qedr_iw_port_immutable(struct ib_device *ibdev, u8 port_num,
				  struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qedr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = 1;
	immutable->gid_tbl_len = 1;
	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
	immutable->max_mad_size = 0;

	return 0;
}
#endif

static const struct ib_device_ops qedr_iw_dev_ops = {
#if DEFINE_PORT_IMMUTABLE /* QEDR_UPSTREAM */
	.get_port_immutable = qedr_iw_port_immutable,
#endif
	.query_gid = qedr_iw_query_gid,
};


static int qedr_iw_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_RNIC;

	ib_set_device_ops(&dev->ibdev, &qedr_iw_dev_ops);

	dev->ibdev.iwcm = kzalloc(sizeof(*dev->ibdev.iwcm), GFP_KERNEL);
	if (!dev->ibdev.iwcm)
		return -ENOMEM;

	dev->ibdev.iwcm->connect = qedr_iw_connect;
	dev->ibdev.iwcm->accept = qedr_iw_accept;
	dev->ibdev.iwcm->reject = qedr_iw_reject;
	dev->ibdev.iwcm->create_listen = qedr_iw_create_listen;
	dev->ibdev.iwcm->destroy_listen = qedr_iw_destroy_listen;
	dev->ibdev.iwcm->add_ref = qedr_iw_qp_add_ref;
	dev->ibdev.iwcm->rem_ref = qedr_iw_qp_rem_ref;
	dev->ibdev.iwcm->get_qp = qedr_iw_get_qp;
#ifdef DEFINED_IW_IFNAME /* QEDR_UPSTREAM */

	memcpy(dev->ibdev.iwcm->ifname,
	       dev->ndev->name, sizeof(dev->ibdev.iwcm->ifname));

#endif
	return 0;
}

static const struct ib_device_ops qedr_roce_dev_ops = {
#if DEFINE_PORT_IMMUTABLE /* QEDR_UPSTREAM */
	.get_port_immutable = qedr_roce_port_immutable,
#endif
#ifndef NOT_DEFINED_GET_CACHED_GID /* !QEDR_UPSTREAM */
	.query_gid = qedr_query_gid,
#endif
#ifdef _HAS_XRC_SUPPORT /* QEDR_UPSTREAM */
	.alloc_xrcd = qedr_alloc_xrcd,
	.dealloc_xrcd = qedr_dealloc_xrcd,
#endif

#ifndef REMOVE_DEVICE_ADD_DEL_GID /* !QEDR_UPSTREAM */
#if DEFINE_ROCE_GID_TABLE
	.add_gid = qedr_add_gid,
	.del_gid = qedr_del_gid,
#endif
#endif
};

static void qedr_roce_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_IB_CA;

	ib_set_device_ops(&dev->ibdev, &qedr_roce_dev_ops);

	dev->ibdev.uverbs_cmd_mask |= QEDR_UVERBS(OPEN_XRCD) |
		QEDR_UVERBS(CLOSE_XRCD) |
		QEDR_UVERBS(CREATE_XSRQ);
}

static const struct ib_device_ops qedr_dev_ops = {
#ifdef DEFINE_ALLOC_MR /* QEDR_UPSTREAM */
	.alloc_mr = qedr_alloc_mr,
#endif
	.alloc_pd = qedr_alloc_pd,
	.alloc_ucontext = qedr_alloc_ucontext,
	.create_ah = qedr_create_ah,
	.create_cq = qedr_create_cq,
	.create_qp = qedr_create_qp,
	.create_srq = qedr_create_srq,
	.dealloc_pd = qedr_dealloc_pd,
	.dealloc_ucontext = qedr_dealloc_ucontext,
	.dereg_mr = qedr_dereg_mr,
	.destroy_ah = qedr_destroy_ah,
	.destroy_cq = qedr_destroy_cq,
	.destroy_qp = qedr_destroy_qp,
	.destroy_srq = qedr_destroy_srq,
#if DEFINE_GET_DEV_FW_STR /* QEDR_UPSTREAM */
	.get_dev_fw_str = qedr_get_dev_fw_str,
#endif
	.get_dma_mr = qedr_get_dma_mr,
	.get_link_layer = qedr_link_layer,
#ifdef DEFINE_GET_NETDEV /* QEDR_UPSTREAM */
	.get_netdev = qedr_get_netdev,
#endif
#ifdef DEFINE_MAP_MR_SG /* QEDR_UPSTREAM */
	.map_mr_sg = qedr_map_mr_sg,
#endif
	.mmap = qedr_mmap,
	.modify_port = qedr_modify_port,
	.modify_qp = qedr_modify_qp,
	.modify_srq = qedr_modify_srq,
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	.modify_ah = qedr_modify_ah,
#endif
	.poll_cq = qedr_poll_cq,
	.post_recv = qedr_post_recv,
	.post_send = qedr_post_send,
	.post_srq_recv = qedr_post_srq_recv,
	.process_mad = qedr_process_mad,
	.query_device = qedr_query_device,
	.query_pkey = qedr_query_pkey,
	.query_port = qedr_query_port,
	.query_qp = qedr_query_qp,
	.query_srq = qedr_query_srq,
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	.query_ah = qedr_query_ah,
#endif
	.reg_user_mr = qedr_reg_user_mr,
	.req_notify_cq = qedr_arm_cq,
	.resize_cq = qedr_resize_cq,
#ifdef _HAS_MW_SUPPORT /* QEDR_UPSTREAM */
	.alloc_mw = qedr_alloc_mw,
	.dealloc_mw = qedr_dealloc_mw,
#endif
#ifdef _HAS_PD_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_pd, qedr_pd, ibpd),
#endif
#ifdef _HAS_UCONTEXT_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_ucontext, qedr_ucontext, ibucontext),
#endif
};

static const struct ib_device_ops qedr_vf_dev_ops = {
#ifdef DEFINE_ALLOC_MR /* QEDR_UPSTREAM */
	.alloc_mr = qedr_alloc_mr,
#endif
	.alloc_pd = qedr_alloc_pd,
	.alloc_ucontext = qedr_alloc_ucontext,
	.create_ah = qedr_create_ah,
	.create_cq = qedr_create_cq,
	.create_qp = qedr_create_qp,
	.create_srq = NULL, /* NOT IMPLEMENTED */
	.dealloc_pd = qedr_dealloc_pd,
	.dealloc_ucontext = qedr_dealloc_ucontext,
	.dereg_mr = qedr_dereg_mr,
	.destroy_ah = qedr_destroy_ah,
	.destroy_cq = qedr_destroy_cq,
	.destroy_qp = qedr_destroy_qp,
	.destroy_srq = NULL, /* NOT IMPLEMENTED */
#if DEFINE_GET_DEV_FW_STR /* QEDR_UPSTREAM */
	.get_dev_fw_str = qedr_get_dev_fw_str,
#endif
	.get_dma_mr = qedr_get_dma_mr,
	.get_link_layer = qedr_link_layer,
#ifdef DEFINE_GET_NETDEV /* QEDR_UPSTREAM */
	.get_netdev = qedr_get_netdev,
#endif
#ifdef DEFINE_MAP_MR_SG /* QEDR_UPSTREAM */
	.map_mr_sg = qedr_map_mr_sg,
#endif
	.mmap = qedr_mmap,
	.modify_port = qedr_modify_port,
	.modify_qp = qedr_modify_qp,
	.modify_srq = NULL, /* NOT IMPLEMENTED */
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	.modify_ah = qedr_modify_ah,
#endif
	.poll_cq = qedr_poll_cq,
	.post_recv = qedr_post_recv,
	.post_send = qedr_post_send,
	.post_srq_recv = NULL, /* NOT IMPLEMENTED */
	.process_mad = qedr_process_mad,
	.query_srq = NULL, /* NOT IMPLEMENTED */
	.query_device = qedr_query_device,
	.query_pkey = qedr_query_pkey,
	.query_port = qedr_query_port,
	.query_qp = qedr_query_qp,
	.reg_user_mr = qedr_reg_user_mr,
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	.query_ah = qedr_query_ah,
#endif
	.req_notify_cq = qedr_arm_cq,
	.resize_cq = qedr_resize_cq,
#ifdef _HAS_PD_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_pd, qedr_pd, ibpd),
#endif
#ifdef _HAS_UCONTEXT_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_ucontext, qedr_ucontext, ibucontext),
#endif
};

static int qedr_register_device(struct qedr_dev *dev, bool lag_enabled)
{
	u8 name[IB_DEVICE_NAME_MAX];
	int rc;

	if (lag_enabled)
		strlcpy(name, "qedr_bond%d", IB_DEVICE_NAME_MAX);
	else if (dev->is_vf)
		strlcpy(name, "qedr_vf%d", IB_DEVICE_NAME_MAX);
	else
		strlcpy(name, "qedr%d", IB_DEVICE_NAME_MAX);

	dev->ibdev.node_guid = dev->attr.node_guid;
	memcpy(dev->ibdev.node_desc, QEDR_NODE_DESC, sizeof(QEDR_NODE_DESC));
	dev->ibdev.owner = THIS_MODULE;
	dev->ibdev.uverbs_abi_ver = QEDR_ABI_VERSION;

	dev->ibdev.uverbs_cmd_mask = QEDR_UVERBS(GET_CONTEXT) |
				     QEDR_UVERBS(QUERY_DEVICE) |
				     QEDR_UVERBS(QUERY_PORT) |
				     QEDR_UVERBS(ALLOC_PD) |
				     QEDR_UVERBS(DEALLOC_PD) |
				     QEDR_UVERBS(CREATE_COMP_CHANNEL) |
				     QEDR_UVERBS(CREATE_CQ) |
				     QEDR_UVERBS(RESIZE_CQ) |
				     QEDR_UVERBS(DESTROY_CQ) |
				     QEDR_UVERBS(REQ_NOTIFY_CQ) |
				     QEDR_UVERBS(CREATE_QP) |
				     QEDR_UVERBS(MODIFY_QP) |
				     QEDR_UVERBS(QUERY_QP) |
				     QEDR_UVERBS(DESTROY_QP) |
				     QEDR_UVERBS(REG_MR) |
				     QEDR_UVERBS(DEREG_MR) |
				     QEDR_UVERBS(POLL_CQ) |
				     QEDR_UVERBS(POST_SEND) |
#ifdef _HAS_MW_SUPPORT /* QEDR_UPSTREAM */
				     QEDR_UVERBS(ALLOC_MW) |
				     QEDR_UVERBS(DEALLOC_MW) |
#endif
				     QEDR_UVERBS(POST_RECV);

	if (!dev->is_vf) {
		/* SRQ is not supported on VF yet */
		dev->ibdev.uverbs_cmd_mask |= QEDR_UVERBS(CREATE_SRQ) |
					      QEDR_UVERBS(DESTROY_SRQ) |
					      QEDR_UVERBS(QUERY_SRQ) |
					      QEDR_UVERBS(MODIFY_SRQ) |
					      QEDR_UVERBS(POST_SRQ_RECV);
	}

	if (IS_IWARP(dev)) {
		rc = qedr_iw_register_device(dev);
		if (rc)
			return rc;
	} else {
		qedr_roce_register_device(dev);
	}

	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = dev->num_cnq;

/* legacy... */
#ifdef DEFINE_REG_PHYS_MR /* ! QEDR_UPSTREAM */
	dev->ibdev.reg_phys_mr = qedr_reg_kernel_mr;
#endif

#ifndef DEFINE_ALLOC_MR /* !QEDR_UPSTREAM */
	dev->ibdev.alloc_fast_reg_mr = qedr_alloc_frmr;
	dev->ibdev.alloc_fast_reg_page_list = qedr_alloc_frmr_page_list;
	dev->ibdev.free_fast_reg_page_list = qedr_free_frmr_page_list;
#endif

#if	DEFINE_CONSOLIDATED_DMA_MAPPING /* QEDR_UPSTREAM */
	dev->ibdev.dev.parent = &dev->pdev->dev;
#else
	dev->ibdev.dma_device = &dev->pdev->dev;
#endif

	if (dev->is_vf)
		ib_set_device_ops(&dev->ibdev, &qedr_vf_dev_ops);
	else
		ib_set_device_ops(&dev->ibdev, &qedr_dev_ops);

	COMPAT_SET_DRIVER_ID(dev->ibdev.driver_id = RDMA_DRIVER_QEDR);

	COMPAT_COPY_DRIVER_NAME(strlcpy(dev->ibdev.name, name, IB_DEVICE_NAME_MAX));

	return ib_register_device(&dev->ibdev,
				  COMPAT_IB_DEVICE_NAME(name)
				  COMPAT_IB_PORT_CALLBACK(NULL));
}

/* This function allocates fast-path status block memory */
static int qedr_alloc_mem_sb(struct qedr_dev *dev,
			     struct qed_sb_info *sb_info, u16 sb_id)
{
	dma_addr_t sb_phys;
#ifndef QEDR_UPSTREAM_FW_HSI /* !QEDR_UPSTREAM_FW_HSI */
	void *sb_virt;
#else
	struct status_block *sb_virt;
#endif
	u32 sb_size;
	int rc;

#ifndef QEDR_UPSTREAM_FW_HSI /* !QEDR_UPSTREAM_FW_HSI */
	sb_size = sizeof(struct status_block);
#else
	sb_size = sizeof(*sb_virt);
#endif
	sb_virt = dma_alloc_coherent(&dev->pdev->dev, sb_size, &sb_phys,
				     GFP_KERNEL);
	if (!sb_virt)
		return -ENOMEM;

	rc = dev->ops->common->sb_init(dev->cdev, sb_info, sb_virt, sb_phys,
				       sb_id, QED_SB_TYPE_CNQ);
	if (rc) {
		pr_err("Status block initialization failed\n");
		dma_free_coherent(&dev->pdev->dev, sb_size, sb_virt, sb_phys);
		return rc;
	}

	return 0;
}

static void qedr_free_mem_sb(struct qedr_dev *dev,
			     struct qed_sb_info *sb_info, int sb_id)
{
	if (!sb_info->sb_virt)
		return;

	dev->ops->common->sb_release(dev->cdev, sb_info, sb_id,
				     QED_SB_TYPE_CNQ);
	dma_free_coherent(&dev->pdev->dev, sb_info->sb_size, sb_info->sb_virt,
			  sb_info->sb_phys);
}

static void qedr_free_resources(struct qedr_dev *dev)
{
	int i;

	if (IS_IWARP(dev))
		destroy_workqueue(dev->iwarp_wq);

	for (i = 0; i < dev->num_cnq; i++) {
		qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
		dev->ops->common->chain_free(dev->cdev, &dev->cnq_array[i].pbl);
	}

	kfree(dev->cnq_array);
	kfree(dev->sb_array);
	kfree(dev->sgid_tbl);

	if (dev->is_vf)
		kfree(dev->vf_ll2_tasklet);
}

static int qedr_alloc_resources(struct qedr_dev *dev)
{
	struct qed_chain_params chain_params;
	struct qedr_cnq *cnq;
	__le16 *cons_pi;
	u16 n_entries;
	int i, rc;

	dev->sgid_tbl = kzalloc(sizeof(union ib_gid) *
				QEDR_MAX_SGID, GFP_KERNEL);
	if (!dev->sgid_tbl)
		return -ENOMEM;

	spin_lock_init(&dev->sgid_lock);

	if (IS_IWARP(dev)) {
		spin_lock_init(&dev->qpidr.idr_lock);
		idr_init(&dev->qpidr.idr);
		dev->iwarp_wq = create_singlethread_workqueue("qedr_iwarpq");
	}
	spin_lock_init(&dev->srqidr.idr_lock);
	idr_init(&dev->srqidr.idr);

	/* Allocate Status blocks for CNQ */
	dev->sb_array = kcalloc(dev->num_cnq, sizeof(*dev->sb_array),
				GFP_KERNEL);
	if (!dev->sb_array) {
		rc = -ENOMEM;
		goto err1;
	}

	dev->cnq_array = kcalloc(dev->num_cnq,
				 sizeof(*dev->cnq_array), GFP_KERNEL);
	if (!dev->cnq_array) {
		rc = -ENOMEM;
		goto err2;
	}

	dev->sb_start = dev->ops->rdma_get_start_sb(dev->cdev);

	/* Allocate CNQ PBLs */
	n_entries = min_t(u32, QED_RDMA_MAX_CNQ_SIZE, QEDR_ROCE_MAX_CNQ_SIZE);
	for (i = 0; i < dev->num_cnq; i++) {
		cnq = &dev->cnq_array[i];

		rc = qedr_alloc_mem_sb(dev, &dev->sb_array[i],
				       dev->sb_start + i);
		if (rc)
			goto err3;

		dev->ops->common->chain_params_init(&chain_params,
						    QED_CHAIN_USE_TO_CONSUME,
						    QED_CHAIN_MODE_PBL,
						    QED_CHAIN_CNT_TYPE_U16,
						    n_entries,
						    sizeof(struct regpair *));
		rc = dev->ops->common->chain_alloc(dev->cdev, &cnq->pbl,
						   &chain_params);
		if (rc)
			goto err4;

		/* INTERNAL: configure cnq, except name since ibdev.name is still NULL */
		cnq->dev = dev;
		cnq->sb = &dev->sb_array[i];
		cons_pi = dev->sb_array[i].sb_pi_array;
		cnq->hw_cons_ptr = &cons_pi[QED_ROCE_PROTOCOL_INDEX];
		cnq->index = i;
		sprintf(cnq->name, "qedr%d@pci:%s", i, pci_name(dev->pdev));

		DP_DEBUG(dev, QEDR_MSG_INIT, "cnq[%d].cons=%d\n",
			 i, qed_chain_get_cons_idx(&cnq->pbl));
	}

	if (dev->is_vf) {
		dev->vf_ll2_tasklet = kmalloc(sizeof(*dev->vf_ll2_tasklet),
					  GFP_KERNEL);
		if (!dev->vf_ll2_tasklet) {
			rc = -ENOMEM;
			goto err4;
		}
	}

	return 0;
err4:
	qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
err3:
	for (--i; i >= 0; i--) {
		dev->ops->common->chain_free(dev->cdev, &dev->cnq_array[i].pbl);
		qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
	}
	kfree(dev->cnq_array);
err2:
	kfree(dev->sb_array);
err1:
	kfree(dev->sgid_tbl);
	return rc;
}

/* QEDR sysfs interface */
static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct qedr_dev *dev =
		rdma_device_to_drv_device(device, struct qedr_dev, ibdev);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dev->pdev->vendor);
}

#define QEDR_MAX_DEVICE_NAME_LEN	(8)
static ssize_t show_hca_type(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct qedr_dev *dev =
		rdma_device_to_drv_device(device, struct qedr_dev, ibdev);

	u8 dname[QEDR_MAX_DEVICE_NAME_LEN];

	dev->ops->common->get_dev_name(dev->cdev, dname,
				       QEDR_MAX_DEVICE_NAME_LEN);

	return scnprintf(buf, PAGE_SIZE, "%s\n", dname);
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
#if !DEFINE_GET_DEV_FW_STR /* ! QEDR_UPSTREAM */
static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);
#endif
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);

static struct device_attribute *qedr_attributes[] = {
	&dev_attr_hw_rev,
#if !DEFINE_GET_DEV_FW_STR /* ! QEDR_UPSTREAM */
	&dev_attr_fw_ver,
#endif
	&dev_attr_hca_type
};

static void qedr_remove_sysfiles(struct qedr_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		device_remove_file(&dev->ibdev.dev, qedr_attributes[i]);
}

/* INTERNAL: we expect this part will be done by the kernel in the future */
static void qedr_pci_set_atomic(struct qedr_dev *dev, struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	u32 ctl2, cap2;
	u16 flags;
	int rc;

	bridge = pdev->bus->self;
	if (!bridge)
		goto disable;

	/* Check atomic routing support all the way to root complex */
	while (bridge->bus->parent) {
		rc = pcie_capability_read_word(bridge, PCI_EXP_FLAGS, &flags);
		if (rc || ((flags & PCI_EXP_FLAGS_VERS) < 2))
			goto disable;

		rc = pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap2);
		if (rc)
			goto disable;

		rc = pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2, &ctl2);
		if (rc)
			goto disable;

		if (!(cap2 & PCI_EXP_DEVCAP2_ATOMIC_ROUTE) ||
		    (ctl2 & PCI_EXP_DEVCTL2_ATOMIC_EGRESS_BLOCK))
			goto disable;
		bridge = bridge->bus->parent->self;
	}

	rc = pcie_capability_read_word(bridge, PCI_EXP_FLAGS, &flags);
	if (rc || ((flags & PCI_EXP_FLAGS_VERS) < 2))
		goto disable;

	rc = pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap2);
	if (rc || !(cap2 & PCI_EXP_DEVCAP2_ATOMIC_COMP64))
		goto disable;

	/* Set atomic operations */
	pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
				 PCI_EXP_DEVCTL2_ATOMIC_REQ);
	dev->atomic_cap = IB_ATOMIC_GLOB;

	DP_DEBUG(dev, QEDR_MSG_INIT, "Atomic capability enabled\n");

	return;

disable:
	pcie_capability_clear_word(pdev, PCI_EXP_DEVCTL2,
				   PCI_EXP_DEVCTL2_ATOMIC_REQ);
	dev->atomic_cap = IB_ATOMIC_NONE;

	DP_DEBUG(dev, QEDR_MSG_INIT, "Atomic capability disabled\n");

}

static const struct qed_rdma_ops *qed_ops;

#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))

static void qedr_ll2_dpc(long unsigned int cookie)
{
	struct qedr_dev *dev = (struct qedr_dev *)cookie;

	dev->ops->ll2_completion(dev->rdma_ctx, dev->gsi_ll2_handle);
}

static irqreturn_t qedr_irq_handler(int irq, void *handle)
{
	u16 hw_comp_cons, sw_comp_cons;
	struct qedr_cnq *cnq = handle;
	struct regpair *cq_handle;
	struct qedr_cq *cq;

	qed_sb_ack(cnq->sb, IGU_INT_DISABLE, 0);

	qed_sb_update_sb_idx(cnq->sb);

	hw_comp_cons = le16_to_cpu(*cnq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&cnq->pbl);

	/* Align protocol-index and chain reads */
	rmb();

	while (sw_comp_cons != hw_comp_cons) {
		cq_handle = (struct regpair *)qed_chain_consume(&cnq->pbl);
		cq = (struct qedr_cq *)(uintptr_t)HILO_U64(cq_handle->hi,
				cq_handle->lo);

		if (cq == NULL) {
			DP_ERR(cnq->dev,
			       "Received NULL CQ cq_handle->hi=%d cq_handle->lo=%d sw_comp_cons=%d hw_comp_cons=%d\n",
			       cq_handle->hi, cq_handle->lo, sw_comp_cons,
			       hw_comp_cons);

			break;
		}

		if (cq->sig != QEDR_CQ_MAGIC_NUMBER) {
			DP_ERR(cnq->dev,
			       "Problem with cq signature, cq_handle->hi=%d ch_handle->lo=%d cq=%p\n",
			       cq_handle->hi, cq_handle->lo, cq);
			break;
		}

		cq->arm_flags = 0;

		if (!cq->destroyed && cq->ibcq.comp_handler)
			(*cq->ibcq.comp_handler)
				(&cq->ibcq, cq->ibcq.cq_context);

		/* The CQ's CNQ notification counter is checked before
		 * destroying the CQ in a busy-wait loop that waits for all of
		 * the CQ's CNQ interrupts to be processed. It is increased
		 * here, only after the completion handler, to ensure that the
		 * the handler is not running when the CQ is destroyed.
		 */
		cq->cnq_notif++;

		sw_comp_cons = qed_chain_get_cons_idx(&cnq->pbl);

		cnq->n_comp++;
	}

	qed_ops->rdma_cnq_prod_update(cnq->dev->rdma_ctx, cnq->index,
				      sw_comp_cons);

	/* In VF, LL2 completions will be using the same SB as CNQ */
	if (cnq->dev->is_vf)
		tasklet_schedule(cnq->dev->vf_ll2_tasklet);

	qed_sb_ack(cnq->sb, IGU_INT_ENABLE, 1);

	return IRQ_HANDLED;
}

static void qedr_sync_free_irqs(struct qedr_dev *dev)
{
	u32 vector;
	u16 idx;
	int i;

	for (i = 0; i < dev->int_info.used_cnt; i++) {
		if (dev->int_info.msix_cnt) {
			idx = i * dev->num_hwfns + dev->affin_hwfn_idx;
			vector = dev->int_info.msix[idx].vector;
			synchronize_irq(vector);
			free_irq(vector, &dev->cnq_array[i]);
		}
#if 0
		else {
			/* @@@TODO - how to sync. simd ? */
			//dev->ops->common->simd_handler_clean(edev->cdev, i);
		}
#endif
	}

	if (dev->is_vf && dev->b_vf_ll2_tasklet_en) {
		tasklet_disable(dev->vf_ll2_tasklet);
		dev->b_vf_ll2_tasklet_en = false;
	}

	dev->int_info.used_cnt = 0;
}

static int qedr_req_msix_irqs(struct qedr_dev *dev)
{
	int i, rc = 0;
	u16 idx;

	/* INTERNAL: Sanitize number of interrupts == number of prepared RSS queues */
	if (dev->num_cnq > dev->int_info.msix_cnt) {
		DP_ERR(dev,
		       "Interrupt mismatch: %d CNQ queues > %d MSI-x vectors\n",
		       dev->num_cnq, dev->int_info.msix_cnt);
		return -EINVAL;
	}

	for (i = 0; i < dev->num_cnq; i++) {
		idx = i * dev->num_hwfns + dev->affin_hwfn_idx;
		rc = request_irq(dev->int_info.msix[idx].vector,
				 qedr_irq_handler, 0, dev->cnq_array[i].name,
				 &dev->cnq_array[i]);
		if (rc) {
			DP_ERR(dev, "Request cnq %d irq failed\n", i);
			qedr_sync_free_irqs(dev);
		} else {
			DP_DEBUG(dev, QEDR_MSG_INIT,
				 "Requested cnq irq for %s [entry %d]. Cookie is at %p\n",
				 dev->cnq_array[i].name, i,
				 &dev->cnq_array[i]);
			dev->int_info.used_cnt++;
		}
	}

	if (dev->is_vf) {
		/* For VF we will have a dedicated tasklet for handling LL2
		 * completions which will be scheduled from the CNQ irq handler
		 */
		tasklet_init(dev->vf_ll2_tasklet, qedr_ll2_dpc,
			     (long unsigned int)dev);

		dev->b_vf_ll2_tasklet_en = true;
	}

	return rc;
}

static int qedr_setup_irqs(struct qedr_dev *dev)
{
	int rc;

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr_setup_irqs\n");

	/* Learn Interrupt configuration */
	rc = dev->ops->rdma_set_rdma_int(dev->cdev, dev->num_cnq);
	if (rc < 0) {
		DP_ERR(dev, "set_rdma_int failed\n");
		return rc;
	}

	rc = dev->ops->rdma_get_rdma_int(dev->cdev, &dev->int_info);
	if (rc) {
		DP_DEBUG(dev, QEDR_MSG_INIT, "get_rdma_int failed\n");
		return rc;
	}

	if (dev->int_info.msix_cnt) {
		DP_DEBUG(dev, QEDR_MSG_INIT, "rdma msix_cnt = %d\n",
			 dev->int_info.msix_cnt);
		rc = qedr_req_msix_irqs(dev);
		if (rc)
			return rc;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr_setup_irqs succeeded\n");

	return 0;
}

static int qedr_set_device_attr(struct qedr_dev *dev)
{
	struct qed_rdma_device *qed_attr;
	struct qedr_device_attr *attr;
	u32 page_size;

	/* Part 1 - query core capabilities */
	qed_attr = dev->ops->rdma_query_device(dev->rdma_ctx);

	if (qed_attr == NULL) {
		printk("query device returned null. aborting qedr_set_device_attr\n");
		return 0;
	}

	/* Part 2 - check capabilities */
	page_size = ~qed_attr->page_size_caps + 1;
	if (page_size > PAGE_SIZE) {
		DP_ERR(dev,
		       "Kernel PAGE_SIZE is %ld which is smaller than minimum page size (%d) required by qedr\n",
		       PAGE_SIZE, page_size);
		return -ENODEV;
	}

	/* Part 3 - copy and update capabilities */
	attr = &dev->attr;
	attr->vendor_id = qed_attr->vendor_id;
	attr->vendor_part_id = qed_attr->vendor_part_id;
	attr->hw_ver = qed_attr->hw_ver;
	attr->fw_ver = qed_attr->fw_ver;
	attr->node_guid = qed_attr->node_guid;
	attr->sys_image_guid = qed_attr->sys_image_guid;
	attr->max_cnq = qed_attr->max_cnq;
	attr->max_sge = qed_attr->max_sge;
	attr->max_inline = qed_attr->max_inline;
	attr->max_sqe = min_t(u32, qed_attr->max_wqe, QEDR_MAX_SQE);
	attr->max_rqe = min_t(u32, qed_attr->max_wqe, QEDR_MAX_RQE);
	attr->max_qp_resp_rd_atomic_resc = qed_attr->max_qp_resp_rd_atomic_resc;
	attr->max_qp_req_rd_atomic_resc = qed_attr->max_qp_req_rd_atomic_resc;
	attr->max_dev_resp_rd_atomic_resc =
	    qed_attr->max_dev_resp_rd_atomic_resc;
	attr->max_cq = qed_attr->max_cq;
	attr->max_qp = qed_attr->max_qp;
	attr->max_mr = qed_attr->max_mr;
	attr->max_mr_size = qed_attr->max_mr_size;
	attr->max_cqe = min_t(u64, qed_attr->max_cqe, QEDR_MAX_CQES);
	attr->max_mw = qed_attr->max_mw;
	attr->max_fmr = qed_attr->max_fmr;
	attr->max_mr_mw_fmr_pbl = qed_attr->max_mr_mw_fmr_pbl;
	attr->max_mr_mw_fmr_size = qed_attr->max_mr_mw_fmr_size;
	attr->max_pd = qed_attr->max_pd;
	attr->max_ah = qed_attr->max_ah;
	attr->max_pkey = qed_attr->max_pkey;
	attr->max_srq = qed_attr->max_srq;
	attr->max_srq_wr = qed_attr->max_srq_wr;
	attr->max_srq_sge = qed_attr->max_srq_sge;
	attr->page_size_caps = qed_attr->page_size_caps;
	attr->dev_ack_delay = qed_attr->dev_ack_delay;
	attr->reserved_lkey = qed_attr->reserved_lkey;
	attr->bad_pkey_counter = qed_attr->bad_pkey_counter;
	attr->max_stats_queues = qed_attr->max_stats_queues;

	return 0;
}

static void qedr_unaffiliated_event(void *context, u8 event_code)
{
	pr_err("unaffiliated event not implemented yet\n");
}

static void qedr_affiliated_event(void *context, u8 e_code, void *fw_handle)
{
#define EVENT_TYPE_NOT_DEFINED	0
#define EVENT_TYPE_CQ		1
#define EVENT_TYPE_QP		2
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
#define EVENT_TYPE_GENERAL	3
#endif
#define EVENT_TYPE_SRQ		4
	struct qedr_dev *dev = (struct qedr_dev *)context;
	struct regpair *async_handle = (struct regpair *)fw_handle;
	u64 roce_handle64 = ((u64) async_handle->hi << 32) + async_handle->lo;
	u8 event_type = EVENT_TYPE_NOT_DEFINED;
	struct ib_event event;
	struct ib_cq *ibcq;
	struct ib_qp *ibqp;
	struct ib_srq *ibsrq;
	struct qedr_srq *srq;
	struct qedr_cq *cq;
	struct qedr_qp *qp;
	u16 srq_id;

	if (IS_IWARP(dev)) {
		switch (e_code) {
		case QED_IWARP_EVENT_CQ_OVERFLOW:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;
		case QED_IWARP_EVENT_SRQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			event_type = EVENT_TYPE_SRQ;
			break;
		case QED_IWARP_EVENT_SRQ_EMPTY:
			event.event = IB_EVENT_SRQ_ERR;
			event_type = EVENT_TYPE_SRQ;
			break;
		default:
		DP_ERR(dev, "unsupported event %d on handle=%llx\n", e_code,
		       roce_handle64);
		}
	} else {

		switch (e_code) {
		case ROCE_ASYNC_EVENT_CQ_OVERFLOW_ERR:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;
		case ROCE_ASYNC_EVENT_SQ_DRAINED:
			event.event = IB_EVENT_SQ_DRAINED;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_QP_CATASTROPHIC_ERR:
			event.event = IB_EVENT_QP_FATAL;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_LOCAL_INVALID_REQUEST_ERR:
			event.event = IB_EVENT_QP_REQ_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_LOCAL_ACCESS_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_SRQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			event_type = EVENT_TYPE_SRQ;
			break;
		case ROCE_ASYNC_EVENT_SRQ_EMPTY:
			event.event = IB_EVENT_SRQ_ERR;
			event_type = EVENT_TYPE_SRQ;
			break;
		case ROCE_ASYNC_EVENT_XRC_DOMAIN_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_INVALID_XRCETH_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_XRC_SRQ_CATASTROPHIC_ERR:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;
		/* NOTE the following are not implemented in FW
		 *	ROCE_ASYNC_EVENT_CQ_ERR
		 *	ROCE_ASYNC_EVENT_COMM_EST
		 */
		/* TODO associate the following events -
		 *	ROCE_ASYNC_EVENT_LAST_WQE_REACHED
		 *	ROCE_ASYNC_EVENT_LOCAL_CATASTROPHIC_ERR (un-affiliated)
		 */
		default:
			DP_ERR(dev, "unsupported event %d on handle=%llx\n",
			       e_code, roce_handle64);
		}
	}

	switch (event_type) {
	case EVENT_TYPE_CQ:
		cq = (struct qedr_cq *)(uintptr_t)roce_handle64;
		if (cq && cq->sig == QEDR_CQ_MAGIC_NUMBER) {
			ibcq = &cq->ibcq;

			/* INTERNAL: There's no need to check if the CQ is in
			 * destroyed state because the FW won't trigger an
			 * event after the CQ is destroyed. And since the event
			 * and the destroy CQ ramrod are generated both on the
			 * EQ there is no risk of a race.
			 */

			if (ibcq->event_handler) {
				event.device = ibcq->device;
				event.element.cq = ibcq;
				ibcq->event_handler(&event, ibcq->cq_context);
			}
		} else {
			WARN(1,
			     "Error: CQ event with NULL pointer ibcq. Handle=%llx\n",
			     roce_handle64);
		}
		DP_ERR(dev, "CQ event %d on hanlde %p\n", e_code, cq);
		break;
	case EVENT_TYPE_QP:
		qp = (struct qedr_qp *)(uintptr_t)roce_handle64;
		if (qp && qp->sig == QEDR_QP_MAGIC_NUMBER) {
			ibqp = &qp->ibqp;
			if (ibqp->event_handler) {
				event.device = ibqp->device;
				event.element.qp = ibqp;
				ibqp->event_handler(&event, ibqp->qp_context);
			}
		} else {
			WARN(1,
			     "Error: QP event with NULL pointer ibqp. Handle=%llx\n",
			     roce_handle64);
		}
		DP_ERR(dev, "QP event %d on handle %p\n", e_code, qp);
		break;
	case EVENT_TYPE_SRQ:
		srq_id = (u16)roce_handle64;
		srq = idr_find(&dev->srqidr.idr, srq_id);
		if (srq) {
			ibsrq = &srq->ibsrq;
			if (ibsrq->event_handler) {
				event.device = ibsrq->device;
				event.element.srq = ibsrq;
				ibsrq->event_handler(&event,
						     ibsrq->srq_context);
			}
		} else {
			DP_NOTICE(dev,
				  "SRQ event with NULL pointer ibsrq. Handle=%llx\n",
				  roce_handle64);
		}
		DP_NOTICE(dev, "SRQ event %d on handle %p\n", e_code, srq);
		break;
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	case EVENT_TYPE_GENERAL:
		/* NTD, for now */
		break;
#endif
	default:
		break;
	}
}

static int qedr_init_hw(struct qedr_dev *dev)
{
	struct qed_rdma_add_user_out_params out_params;
	struct qed_rdma_start_in_params *in_params;
	struct qed_rdma_cnq_params *cur_pbl;
	struct qed_rdma_events events;
	dma_addr_t p_phys_table;
	u32 page_cnt;
	int rc = 0;
	int i;

	in_params =  kzalloc(sizeof(*in_params), GFP_KERNEL);
	if (!in_params) {
		rc = -ENOMEM;
		goto out;
	}

	in_params->desired_cnq = dev->num_cnq;
	for (i = 0; i < dev->num_cnq; i++) {
		cur_pbl = &in_params->cnq_pbl_list[i];

		page_cnt = qed_chain_get_page_cnt(&dev->cnq_array[i].pbl);
		cur_pbl->num_pbl_pages = page_cnt;

		p_phys_table = qed_chain_get_pbl_phys(&dev->cnq_array[i].pbl);
		cur_pbl->pbl_ptr = (u64)p_phys_table;
	}

	events.affiliated_event = qedr_affiliated_event;
	events.unaffiliated_event = qedr_unaffiliated_event;
	events.context = dev;

	in_params->events = &events;
	in_params->roce.cq_mode = QED_RDMA_CQ_MODE_32_BITS;
	in_params->max_mtu = dev->ndev->mtu;
	dev->iwarp_max_mtu = dev->ndev->mtu;

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	if (delayed_ack)
		in_params->iwarp.flags |= QED_IWARP_DA_EN;

	if (timestamp)
		in_params->iwarp.flags |= QED_IWARP_TS_EN;

	in_params->iwarp.flags |= QED_IWARP_VALIDATE_MAC;
	in_params->iwarp.rcv_wnd_size = rcv_wnd_size*1024;
	in_params->iwarp.crc_needed = crc_needed;
	in_params->iwarp.ooo_num_rx_bufs = 0; /* Use defaults */

	in_params->iwarp.mpa_peer2peer = peer2peer;
	in_params->iwarp.mpa_rev = mpa_enhanced ? QED_MPA_REV2 : QED_MPA_REV1;
	in_params->iwarp.mpa_rtr = rtr_type;
#endif
	ether_addr_copy(&in_params->mac_addr[0], dev->ndev->dev_addr);

	rc = dev->ops->rdma_init(dev->cdev, in_params);
	if (rc)
		goto out;

	rc = dev->ops->rdma_add_user(dev->rdma_ctx, &out_params);
	if (rc)
		goto out;

	dev->db_addr = (void __iomem *)(uintptr_t)out_params.dpi_addr;
	dev->db_phys_addr = out_params.dpi_phys_addr;
	dev->db_size = out_params.dpi_size;
	dev->dpi = out_params.dpi;
	dev->wid_count = out_params.wid_count;

	rc = qedr_set_device_attr(dev);
out:
	kfree(in_params);
	if (rc)
		DP_ERR(dev, "Init HW Failed rc = %d\n", rc);

	return rc;
}

static void qedr_stop_hw(struct qedr_dev *dev)
{
	dev->ops->rdma_remove_user(dev->rdma_ctx, dev->dpi);
	dev->ops->rdma_stop(dev->rdma_ctx);
}

static struct qedr_dev *qedr_add(struct qed_dev *cdev, struct pci_dev *pdev,
				 struct net_device *ndev, bool lag_enabled,
				 bool is_vf)
{
	struct qed_dev_rdma_info dev_info;
	struct qedr_dev *dev;
	int rc = 0, i;

#ifdef _HAS_SAFE_IB_ALLOC /* QEDR_UPSTREAM */
	dev = ib_alloc_device(qedr_dev, ibdev);
#else
	dev = (struct qedr_dev *)ib_alloc_device(sizeof(*dev));
#endif
	if (!dev) {
		pr_err("Unable to allocate ib device\n");
		return NULL;
	}
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qedr_config_debug(&dev->dp_module, &dev->dp_level);
	dev->debug_msglvl = debug;
#endif

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr add device called\n");

	dev->pdev = pdev;
	dev->ndev = ndev;
	dev->cdev = cdev;
	dev->is_vf = is_vf;

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qed_ops = qed_get_rdma_ops(QEDR_ROCE_INTERFACE_VERSION);
#else
	qed_ops = qed_get_rdma_ops();
#endif
	if (!qed_ops) {
		DP_ERR(dev, "Failed to get qed roce operations\n");
		goto init_err;
	}

	dev->ops = qed_ops;
	rc = qed_ops->fill_dev_info(cdev, &dev_info);
	if (rc)
		goto init_err;

	dev->num_hwfns = dev_info.common.num_hwfns;
	dev->rdma_type = dev_info.rdma_type;
	dev->lag_enabled = lag_enabled;

	if (IS_IWARP(dev) && QEDR_IS_CMT(dev)) {
		if (!iwarp_cmt) {
			DP_ERR(dev,
			       "By default, iWARP is not supported over a 100G device. Can use the \"iwarp_cmt\" module parameter to enable it.\n");
			rc = -EINVAL;
			goto init_err;
		}

		rc = dev->ops->iwarp_set_engine_affin(cdev, false /* !reset */);
		if (rc)
			goto init_err;
	}
	dev->affin_hwfn_idx = dev->ops->common->get_affin_hwfn_idx(cdev);

#ifndef QEDR_UPSTREAM_DPM /* !QEDR_UPSTREAM_DPM */
	/* SAGIV TODO - For now, DPM is disabled for VF */
	dev->dpm_enabled = is_vf ? 0 : dev_info.dpm_enabled;
#endif
	dev->rdma_ctx = dev->ops->rdma_get_rdma_ctx(cdev);

	dev->num_cnq = dev->ops->rdma_get_min_cnq_msix(cdev);

	if (!dev->num_cnq) {
		DP_ERR(dev, "Failed. At least one CNQ is required.\n");
		rc = -ENOMEM;
		goto init_err;
	}

#if DEFINE_THIS_CPU_INC /* ! QEDR_UPSTREAM */
	dev->stats = alloc_percpu(struct qedr_stats);
	if (!dev->stats) {
		DP_ERR(dev, "Unable to allocate memory for stats\n");
		goto init_err;
	}
#endif
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	if ((wq_multiplier < QEDR_WQ_MULTIPLIER_MIN) ||
	    (wq_multiplier > QEDR_WQ_MULTIPLIER_MAX)) {
		DP_ERR(dev,
		       "Invalid wq_multiplier %d. Must be between %d and %d inclusive.\n",
		       wq_multiplier, QEDR_WQ_MULTIPLIER_MIN,
		       QEDR_WQ_MULTIPLIER_MAX);
		goto init_err;
	}
	dev->wq_multiplier = wq_multiplier;
#else
	dev->wq_multiplier = QEDR_WQ_MULTIPLIER_DFT;
#endif
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	dev->insert_udp_src_port = !!insert_udp_src_port;
#endif
	/* INTERNAL: until kernel does not provide this functionality, do it*/
	qedr_pci_set_atomic(dev, pdev);

	rc = qedr_alloc_resources(dev);
	if (rc)
		goto init_err;

	rc = qedr_init_hw(dev);
	if (rc)
		goto alloc_err;

	rc = qedr_setup_irqs(dev);
	if (rc)
		goto irq_err;

	rc = qedr_register_device(dev, lag_enabled);
	if (rc) {
		DP_ERR(dev, "Unable to allocate register device\n");
		goto reg_err;
	}

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		if (device_create_file(&dev->ibdev.dev, qedr_attributes[i]))
			goto sysfs_err;

#if !DEFINE_ROCE_GID_TABLE  /* !QEDR_UPSTREAM */
	qedr_build_sgid_tbl(dev);
	rc = qedr_register_inet(dev);
	if (rc)
		goto inet_err;
#endif

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qedr_debugfs_add_stats(dev);
#endif

	if (!test_and_set_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ACTIVE);

	pr_info("qedr %s: registered %s\n", pci_name(dev->pdev),
		dev->ibdev.name);

	return dev;

#if !DEFINE_ROCE_GID_TABLE  /* !QEDR_UPSTREAM */
inet_err:
	qedr_remove_sysfiles(dev);
#endif
sysfs_err:
	ib_unregister_device(&dev->ibdev);
reg_err:
	qedr_sync_free_irqs(dev);
irq_err:
	qedr_stop_hw(dev);
alloc_err:
#if !DEFINE_ROCE_GID_TABLE  /* !QEDR_UPSTREAM */
	qedr_unregister_inet(dev);
#endif
	qedr_free_resources(dev);
init_err:
#if DEFINE_THIS_CPU_INC /* ! QEDR_UPSTREAM */
	free_percpu(dev->stats);
#endif
	ib_dealloc_device(&dev->ibdev);
	DP_ERR(dev, "qedr driver load failed rc=%d\n", rc);

	return NULL;
}

static void qedr_remove(struct qedr_dev *dev, enum qede_rdma_remove_mode mode)
{
	/* First unregister with stack to stop all the active traffic
	 * of the registered clients.
	 */
#ifndef QEDR_UPSTREAM_RECOVER
	bool first_death;

	first_death = (mode == QEDE_RDMA_REMOVE_RECOVERY) && QEDR_ALIVE(dev);

	if (QEDR_DEAD(dev)) {
		/* if in recovery mode (second time at least, since the device
		 * is already dead) then there's nothing to do. But if the user
		 * requested to remove the module the  then de-register from
		 * OFED (the device, ECORE and Linux resources were already
		 * freed when the device transitioned into dead state)
		 */
		if (mode == QEDE_RDMA_REMOVE_NORMAL) {
			ib_unregister_device(&dev->ibdev);
			ib_dealloc_device(&dev->ibdev);
		}
		return;
	}

	if (first_death)
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_DEVICE_FATAL);
#else
	ib_unregister_device(&dev->ibdev);
#endif

	qedr_remove_sysfiles(dev);

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qedr_debugfs_remove_stats(dev);
#endif

#ifndef QEDR_UPSTREAM_RECOVER /* ! QEDR_UPSTREAM_RECOVER */
	/* deregister from OFED first to stop all the active traffic of the
	 * registered clients. This step is skipped in recovery.
	 */
	if (mode == QEDE_RDMA_REMOVE_NORMAL)
		ib_unregister_device(&dev->ibdev);
	else if (first_death)
		mdelay(250);	/* allow running operations to terminate */
#endif
#if !DEFINE_ROCE_GID_TABLE  /* !QEDR_UPSTREAM */
	qedr_unregister_inet(dev);
#endif

	qedr_stop_hw(dev);
	qedr_sync_free_irqs(dev);
	qedr_free_resources(dev);

	if (IS_IWARP(dev) && QEDR_IS_CMT(dev) && iwarp_cmt)
		dev->ops->iwarp_set_engine_affin(dev->cdev, true /* reset */);

#ifndef QEDR_UPSTREAM_RECOVER /* ! QEDR_UPSTREAM_RECOVER */
	if (mode == QEDE_RDMA_REMOVE_NORMAL) {
#if DEFINE_THIS_CPU_INC /* ! QEDR_UPSTREAM */
		free_percpu(dev->stats);
#endif
		ib_dealloc_device(&dev->ibdev);
	} else if (first_death) {
		dev->dead = true;	/* mark the device as dead */
	}
#else
	ib_dealloc_device(&dev->ibdev);
#endif
}

static void qedr_close(struct qedr_dev *dev)
{
	if (test_and_clear_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ERR);
}

static void qedr_open(struct qedr_dev *dev)
{
	if (!test_and_set_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ACTIVE);
}

static void qedr_mac_address_change(struct qedr_dev *dev)
{
	union ib_gid *sgid = &dev->sgid_tbl[0];
	u8 guid[8], mac_addr[6] __attribute__((aligned(16)));
	int rc;

	/* Update SGID */
	ether_addr_copy(&mac_addr[0], dev->ndev->dev_addr);
	guid[0] = mac_addr[0] ^ 2;
	guid[1] = mac_addr[1];
	guid[2] = mac_addr[2];
	guid[3] = 0xff;
	guid[4] = 0xfe;
	guid[5] = mac_addr[3];
	guid[6] = mac_addr[4];
	guid[7] = mac_addr[5];
	sgid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	memcpy(&sgid->raw[8], guid, sizeof(guid));

	/* Update LL2 */
	rc = dev->ops->ll2_set_mac_filter(dev->cdev,
					  dev->gsi_ll2_mac_address,
					  dev->ndev->dev_addr);

	ether_addr_copy(dev->gsi_ll2_mac_address, dev->ndev->dev_addr);

#if DEFINE_EVENT_ON_GID_CHANGE /* QEDR_UPSTREAM */
	qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_GID_CHANGE);
#endif

	if (rc)
		DP_ERR(dev, "Error updating mac filter\n");
}

/* event handling via NIC driver ensures that all the NIC specific
 * initialization done before RoCE driver notifies
 * event to stack.
 */
static void qedr_notify(struct qedr_dev *dev, enum qede_rdma_event event)
{
	switch (event) {
	case QEDE_UP:
		qedr_open(dev);
		break;
	case QEDE_CHANGE_ADDR:
		qedr_mac_address_change(dev);
		break;
	case QEDE_DOWN:
		qedr_close(dev);
		break;
	case QEDE_CHANGE_MTU:
		if (dev->ndev->mtu != dev->iwarp_max_mtu) {
			if (IS_IWARP(dev)) {
				DP_NOTICE(dev,
					  "Mtu was changed from %d to %d. This will not take affect for iWARP until qedr is reloaded\n",
					  dev->iwarp_max_mtu, dev->ndev->mtu);
			}
		}
		break;
	}
}

static struct qedr_driver qedr_drv = {
	.name = "qedr_driver",
	.add = qedr_add,
	.remove = qedr_remove,
	.notify = qedr_notify,
};

static int __init qedr_init_module(void)
{
	int status;

#if !defined(__L64__) && !defined(_LP64)	/* ! QEDR_UPSTREAM */
	pr_err("qedr isn't supported in 32 bit environment\n");
	return -EINVAL;
#endif
	pr_info("QLogic FastLinQ 4xxxx RDMA Driver qedr " QEDR_MODULE_VERSION
		"\n");

#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qedr_init_debugfs();
#endif
	status = qede_rdma_register_driver(&qedr_drv);

	return status;
}

static void __exit qedr_exit_module(void)
{
	qede_rdma_unregister_driver(&qedr_drv);
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
	qedr_remove_debugfs();
#endif
}

module_init(qedr_init_module);
module_exit(qedr_exit_module);
#ifndef QEDR_UPSTREAM /* !QEDR_UPSTREAM */
static void qedr_config_debug(u32 *p_dp_module, u8 *p_dp_level)
{
	*p_dp_level = 0;
	*p_dp_module = 0;

	if (debug & QED_LOG_VERBOSE_MASK) {
		*p_dp_level = QED_LEVEL_VERBOSE;
		*p_dp_module = (debug & 0x3FFFFFFF);
	} else if (debug & QED_LOG_INFO_MASK) {
		*p_dp_level = QED_LEVEL_INFO;
	} else if (debug & QED_LOG_NOTICE_MASK) {
		*p_dp_level = QED_LEVEL_NOTICE;
	}
}
#endif
