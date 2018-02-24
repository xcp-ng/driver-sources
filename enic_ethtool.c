/*
 * Copyright 2011-2018 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>

#include "kcompat.h"
#include "enic_config.h"
#include "driver_utils.h"
#include "enic_res.h"
#include "enic.h"
#include "enic_dev.h"
#include "enic_clsf.h"
#include "enic_ethtool.h"
#include "vnic_stats.h"
#include "vnic_rss.h"
#include "enic_qp.h"

struct enic_stat {
	char name[ETH_GSTRING_LEN];
	unsigned int offset;
};

#if ENIC_HAVE_GET_RXFH_OPS || ENIC_HAVE_GET_RXFH_OPS_EXT
static u32 enic_get_rxfh_key_size(struct net_device *netdev)
{
	return ENIC_RSS_LEN;
}

static int enic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *hkey,
			 u8 *hfunc)
{
	struct enic *enic = netdev_priv(netdev);

	if (hkey)
		memcpy(hkey, enic->rss_key, ENIC_RSS_LEN);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int enic_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *hkey, const u8 hfunc)
{
	struct enic *enic = netdev_priv(netdev);

	if ((hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP) ||
	    indir)
		return -EINVAL;

	if (hkey)
		memcpy(enic->rss_key, hkey, ENIC_RSS_LEN);

	return __enic_set_rsskey(enic);
}
#endif /* ENIC_HAVE_GET_RXFH_OPS || ENIC_HAVE_GET_RXFH_OPS_EXT */

/* DATA QPs macros/data_structures */

struct enic_rx_to_rq_stat {
	char rx_stat_name[ETH_GSTRING_LEN]; /* DEBUG ONLY */
	char rq_stat_name[ETH_GSTRING_LEN]; /* DEBUG ONLY */
	unsigned int rx_index;
	unsigned int rq_index;
};

#define ENIC_RX_STAT_INDEX(stat) \
	(offsetof(struct vnic_rx_stats, stat) / sizeof(u64))

#define ENIC_RQ_STAT_INDEX(stat) \
	(offsetof(struct enic_rq_stats, stat) / sizeof(u64))

#define ENIC_TX_STAT_INDEX(stat) \
	(offsetof(struct vnic_tx_stats, stat) / sizeof(u64))

#define ENIC_WQ_STAT_INDEX(stat) \
	(offsetof(struct enic_wq_stats, stat) / sizeof(u64))

#define ENIC_RX_TO_RQ_STAT(rx_stat, rq_stat) {		\
	.rx_stat_name = #rx_stat,			\
	.rq_stat_name = #rq_stat,			\
	.rx_index = ENIC_RX_STAT_INDEX(rx_stat),	\
	.rq_index = ENIC_RQ_STAT_INDEX(rq_stat)		\
}

/* Order of the array elements matters.
 */
struct enic_rx_to_rq_stat enic_rx_to_rq_stats[] = {
	ENIC_RX_TO_RQ_STAT(rx_frames_ok, packets),
	ENIC_RX_TO_RQ_STAT(rx_bytes_ok, bytes),
	//ENIC_RX_TO_RQ_STAT(rx_unicast_frames_ok, rx_unicast_frames),
	//ENIC_RX_TO_RQ_STAT(rx_unicast_bytes_ok, rx_unicast_bytes),
	ENIC_RX_TO_RQ_STAT(rx_multicast_frames_ok, rx_multicast_frames),
	ENIC_RX_TO_RQ_STAT(rx_multicast_bytes_ok, rx_multicast_bytes),
	ENIC_RX_TO_RQ_STAT(rx_broadcast_frames_ok, rx_broadcast_frames),
	ENIC_RX_TO_RQ_STAT(rx_broadcast_bytes_ok, rx_broadcast_bytes),
	ENIC_RX_TO_RQ_STAT(rx_rss, l4_rss_hash),
	ENIC_RX_TO_RQ_STAT(rx_rss, l3_rss_hash),
	ENIC_RX_TO_RQ_STAT(rx_errors, bad_fcs),
	ENIC_RX_TO_RQ_STAT(rx_errors, pkt_truncated),
	ENIC_RX_TO_RQ_STAT(rx_frames_64, rx_frames_64),
	ENIC_RX_TO_RQ_STAT(rx_frames_127, rx_frames_127),
	ENIC_RX_TO_RQ_STAT(rx_frames_255, rx_frames_255),
	ENIC_RX_TO_RQ_STAT(rx_frames_511, rx_frames_511),
	ENIC_RX_TO_RQ_STAT(rx_frames_1023, rx_frames_1023),
	ENIC_RX_TO_RQ_STAT(rx_frames_1518, rx_frames_1518),
	ENIC_RX_TO_RQ_STAT(rx_frames_to_max, rx_frames_to_max),
};

unsigned int enic_rx_hw_stat_ok[] = {
	ENIC_RX_STAT_INDEX(rx_no_bufs),
};

#define ENIC_TX_STAT(stat) {			\
	.name = #stat,				\
	.offset = ENIC_TX_STAT_INDEX(stat)	\
}

#define ENIC_RX_STAT(stat) {			\
	.name = #stat,				\
	.offset = ENIC_RX_STAT_INDEX(stat)	\
}

#define ENIC_PER_RQ_STAT(stat) {		\
	.name = "rq[%d]_"#stat,			\
	.offset = ENIC_RQ_STAT_INDEX(stat)	\
}

#define ENIC_PER_WQ_STAT(stat) {		\
	.name = "wq[%d]_"#stat,			\
	.offset = ENIC_WQ_STAT_INDEX(stat)	\
}

/* The order of the entries must match the order in struct enic_rq_stats.
 * (this requirement is needed by the enic_rx_to_rq_stat use case)
 */
static const struct enic_stat enic_per_rq_stats[] = {
	ENIC_PER_RQ_STAT(packets),
	ENIC_PER_RQ_STAT(bytes),
	ENIC_PER_RQ_STAT(l4_rss_hash),
	ENIC_PER_RQ_STAT(l3_rss_hash),
	ENIC_PER_RQ_STAT(csum_unnecessary),
	ENIC_PER_RQ_STAT(csum_unnecessary_encap),
	ENIC_PER_RQ_STAT(vlan_stripped),
	ENIC_PER_RQ_STAT(napi_complete),
	ENIC_PER_RQ_STAT(napi_repoll),
	ENIC_PER_RQ_STAT(dma_map_error),
	ENIC_PER_RQ_STAT(bad_fcs),
	ENIC_PER_RQ_STAT(pkt_truncated),
	ENIC_PER_RQ_STAT(no_skb),
	ENIC_PER_RQ_STAT(desc_skip),
	ENIC_PER_RQ_STAT(alloc_error),
	ENIC_PER_RQ_STAT(desc_order_error),
	ENIC_PER_RQ_STAT(num_frags_error),
	ENIC_PER_RQ_STAT(pkt_trunc_drop),
	ENIC_PER_RQ_STAT(rx_unicast_frames),
	ENIC_PER_RQ_STAT(rx_unicast_bytes),
	ENIC_PER_RQ_STAT(rx_multicast_frames),
	ENIC_PER_RQ_STAT(rx_multicast_bytes),
	ENIC_PER_RQ_STAT(rx_broadcast_frames),
	ENIC_PER_RQ_STAT(rx_broadcast_bytes),
	ENIC_PER_RQ_STAT(rx_frames_64),
	ENIC_PER_RQ_STAT(rx_frames_127),
	ENIC_PER_RQ_STAT(rx_frames_255),
	ENIC_PER_RQ_STAT(rx_frames_511),
	ENIC_PER_RQ_STAT(rx_frames_1023),
	ENIC_PER_RQ_STAT(rx_frames_1518),
	ENIC_PER_RQ_STAT(rx_frames_to_max),
};

#define NUM_ENIC_PER_RQ_STATS	ARRAY_SIZE(enic_per_rq_stats)

static const struct enic_stat enic_per_wq_stats[] = {
	ENIC_PER_WQ_STAT(packets),
	ENIC_PER_WQ_STAT(stopped),
	ENIC_PER_WQ_STAT(wake),
	ENIC_PER_WQ_STAT(tso),
	ENIC_PER_WQ_STAT(encap_tso),
	ENIC_PER_WQ_STAT(encap_csum),
	ENIC_PER_WQ_STAT(csum_partial),
	ENIC_PER_WQ_STAT(csum),
	ENIC_PER_WQ_STAT(bytes),
	ENIC_PER_WQ_STAT(add_vlan),
	ENIC_PER_WQ_STAT(cq_work),
	ENIC_PER_WQ_STAT(cq_bytes),
	ENIC_PER_WQ_STAT(null_pkt),
	ENIC_PER_WQ_STAT(skb_linear_fail),
	ENIC_PER_WQ_STAT(dma_map_error),
	ENIC_PER_WQ_STAT(desc_full_awake),
	ENIC_PER_WQ_STAT(pull_ok),
	ENIC_PER_WQ_STAT(pull_err_api),
	ENIC_PER_WQ_STAT(pull_err_trunc),
	ENIC_PER_WQ_STAT(tx_unicast_frames),
	ENIC_PER_WQ_STAT(tx_unicast_bytes),
	ENIC_PER_WQ_STAT(tx_multicast_frames),
	ENIC_PER_WQ_STAT(tx_multicast_bytes),
	ENIC_PER_WQ_STAT(tx_broadcast_frames),
	ENIC_PER_WQ_STAT(tx_broadcast_bytes),
	ENIC_PER_WQ_STAT(rwe_tso_skbs),
	ENIC_PER_WQ_STAT(rwe_tso_frags),
	ENIC_PER_WQ_STAT(rwe_tso_segs),
	ENIC_PER_WQ_STAT(rwe_tso_segs_lt_512),
	ENIC_PER_WQ_STAT(rwe_tso_segs_16k),
	ENIC_PER_WQ_STAT(rwe_tso_bytes),
};

#define NUM_ENIC_PER_WQ_STATS	ARRAY_SIZE(enic_per_wq_stats)

static const struct enic_stat enic_tx_stats[] = {
	ENIC_TX_STAT(tx_frames_ok),
	ENIC_TX_STAT(tx_unicast_frames_ok),
	ENIC_TX_STAT(tx_multicast_frames_ok),
	ENIC_TX_STAT(tx_broadcast_frames_ok),
	ENIC_TX_STAT(tx_bytes_ok),
	ENIC_TX_STAT(tx_unicast_bytes_ok),
	ENIC_TX_STAT(tx_multicast_bytes_ok),
	ENIC_TX_STAT(tx_broadcast_bytes_ok),
	ENIC_TX_STAT(tx_drops),
	ENIC_TX_STAT(tx_errors),
	ENIC_TX_STAT(tx_tso),
	ENIC_TX_STAT(tx_tso_bytes_ok),
	ENIC_TX_STAT(tx_drops_spoofchk),
};

#define NUM_ENIC_TX_STATS	ARRAY_SIZE(enic_tx_stats)

/* to keep in sync with struct vnic_rx_stats
 */
static const struct enic_stat enic_rx_stats[] = {
	ENIC_RX_STAT(rx_frames_ok),
	ENIC_RX_STAT(rx_frames_total),
	ENIC_RX_STAT(rx_unicast_frames_ok),
	ENIC_RX_STAT(rx_multicast_frames_ok),
	ENIC_RX_STAT(rx_broadcast_frames_ok),
	ENIC_RX_STAT(rx_bytes_ok),
	ENIC_RX_STAT(rx_unicast_bytes_ok),
	ENIC_RX_STAT(rx_multicast_bytes_ok),
	ENIC_RX_STAT(rx_broadcast_bytes_ok),
	ENIC_RX_STAT(rx_drop),
	ENIC_RX_STAT(rx_no_bufs),
	ENIC_RX_STAT(rx_errors),
	ENIC_RX_STAT(rx_rss),
	ENIC_RX_STAT(rx_crc_errors),
	ENIC_RX_STAT(rx_frames_64),
	ENIC_RX_STAT(rx_frames_127),
	ENIC_RX_STAT(rx_frames_255),
	ENIC_RX_STAT(rx_frames_511),
	ENIC_RX_STAT(rx_frames_1023),
	ENIC_RX_STAT(rx_frames_1518),
	ENIC_RX_STAT(rx_frames_to_max),
};

#define NUM_ENIC_RX_STATS	ARRAY_SIZE(enic_rx_stats)

/* ADMIN QPs macros/data_structures
 */

#define ENIC_PER_ADMIN_RQ_STAT(stat) { \
	.name = "admin_rq[%d]_"#stat, \
	.offset = offsetof(struct enic_rq_stats, stat) / sizeof(u64) \
}

#define ENIC_PER_ADMIN_WQ_STAT(stat) { \
	.name = "admin_wq[%d]_"#stat, \
	.offset = offsetof(struct enic_wq_stats, stat) / sizeof(u64) \
}

static const struct enic_stat enic_per_admin_rq_stats[] = {
	ENIC_PER_ADMIN_RQ_STAT(packets),
	ENIC_PER_ADMIN_RQ_STAT(bytes),
	ENIC_PER_ADMIN_RQ_STAT(vlan_stripped),
	ENIC_PER_ADMIN_RQ_STAT(napi_complete),
	ENIC_PER_ADMIN_RQ_STAT(napi_repoll),
	ENIC_PER_ADMIN_RQ_STAT(dma_map_error),
	//ENIC_PER_ADMIN_RQ_STAT(bad_fcs),
	//ENIC_PER_ADMIN_RQ_STAT(pkt_truncated),
	ENIC_PER_ADMIN_RQ_STAT(no_skb),
	ENIC_PER_ADMIN_RQ_STAT(desc_skip),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_64),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_127),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_255),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_511),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_1023),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_1518),
	ENIC_PER_ADMIN_RQ_STAT(rx_frames_to_max),
};

#define NUM_ENIC_PER_ADMIN_RQ_STATS	ARRAY_SIZE(enic_per_admin_rq_stats)

static const struct enic_stat enic_per_admin_wq_stats[] = {
	ENIC_PER_ADMIN_WQ_STAT(packets),
	ENIC_PER_ADMIN_WQ_STAT(stopped),
	ENIC_PER_ADMIN_WQ_STAT(wake),
	ENIC_PER_ADMIN_WQ_STAT(bytes),
	ENIC_PER_ADMIN_WQ_STAT(add_vlan),
	ENIC_PER_ADMIN_WQ_STAT(cq_work),
	ENIC_PER_ADMIN_WQ_STAT(cq_bytes),
	ENIC_PER_ADMIN_WQ_STAT(null_pkt),
	ENIC_PER_ADMIN_WQ_STAT(skb_linear_fail),
	ENIC_PER_ADMIN_WQ_STAT(dma_map_error),
	ENIC_PER_ADMIN_WQ_STAT(desc_full_awake),
};

#define NUM_ENIC_PER_ADMIN_WQ_STATS	ARRAY_SIZE(enic_per_admin_wq_stats)

static const int enic_stats_block_size[ENIC_STATS_MAX] = {
	NUM_ENIC_TX_STATS,
	NUM_ENIC_RX_STATS,
	NUM_ENIC_PER_WQ_STATS,
	NUM_ENIC_PER_RQ_STATS,
	NUM_ENIC_PER_ADMIN_WQ_STATS,
	NUM_ENIC_PER_ADMIN_RQ_STATS,
};

int enic_get_num_stats(struct enic *enic, enum enic_stats_block stats_block)
{
	struct net_device *netdev = enic->netdev;

	if (stats_block >= ENIC_STATS_MAX) {
		netdev_err(netdev, "%s - Unknown stats block %d\n", __func__, stats_block);
		return 0;
	}

	return enic_stats_block_size[stats_block];
}

void enic_intr_coal_set(struct enic *enic, u32 timer)
{
	struct enic_qp *qp;
	u32 timer_hw;
	int i;

	timer_hw = vnic_dev_intr_coal_timer_usec_to_hw(enic->vdev, timer);
	/* Including all QP types
	 */
	for (i = 0; i < enic->qp_count; i++) {
		qp = &enic->qp[i];
		/* If ctrl pointer is not set, ethtool command on this interface which has
		 * not been opened yet. The timers will be set on the adapter when opened.
		 */
		if (qp->ctrl)
			iowrite32(timer_hw, &qp->ctrl->coalescing_timer);
	}
	return;
}

#if (ENIC_HAVE_GET_LINK_KSETTINGS)
static void
enic_get_supp_adv_media_type(struct net_device *netdev,
			      struct ethtool_link_ksettings *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	struct ethtool_link_settings *base = &ecmd->base;
	u16 sub_dev_id = 0;

	if (enic->pdev)
		sub_dev_id = enic->pdev->subsystem_device;

	switch (sub_dev_id) {
	case PCI_SUBDEV_ID_CISCO_VIC_1225:
	case PCI_SUBDEV_ID_CISCO_VIC_1227:
		/* adding closest one */
#if (ENIC_HAVE_LINK_MODE_10000_BASE_SR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseSR_Full);
#endif
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_1285:
		/* adding closest one */
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_1225T:
	case PCI_SUBDEV_ID_CISCO_VIC_1227T:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_1385:
	case PCI_SUBDEV_ID_CISCO_VIC_1387:
#if (ENIC_HAVE_LINK_MODE_10000_BASE_SR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseSR_Full);
#endif

#if (ENIC_HAVE_LINK_MODE_10000_BASE_LR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseLR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseLR_Full);
#endif

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseSR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		break;
	case PCI_SUBDEV_ID_CISCO_VIC_1477:
	case PCI_SUBDEV_ID_CISCO_VIC_1485:
	case PCI_SUBDEV_ID_CISCO_VIC_1487:
	case PCI_SUBDEV_ID_CISCO_VIC_1495:
	case PCI_SUBDEV_ID_CISCO_VIC_1497:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseCR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseSR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseLR4_Full);

#if (ENIC_HAVE_LINK_MODE_100000_BASE_SR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			100000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			100000baseSR4_Full);
#endif
#if (ENIC_HAVE_LINK_MODE_100000_BASE_CR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			100000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			100000baseCR4_Full);
#endif

		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		base->port = PORT_FIBRE;
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_15235:
	case PCI_SUBDEV_ID_CISCO_VIC_15237:
	case PCI_SUBDEV_ID_CISCO_VIC_15238:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseCR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseSR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseLR4_Full);

#if (ENIC_HAVE_LINK_MODE_100000_BASE_SR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			100000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			100000baseSR4_Full);
#endif

#if (ENIC_HAVE_LINK_MODE_100000_BASE_CR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			100000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			100000baseCR4_Full);
#endif
#if (ENIC_HAVE_LINK_MODE_200000_BASE_SR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			200000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			200000baseSR4_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			200000baseDR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			200000baseDR4_Full);
#endif
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		base->port = PORT_FIBRE;
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_1455:
	case PCI_SUBDEV_ID_CISCO_VIC_1457:
	case PCI_SUBDEV_ID_CISCO_VIC_1467:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseT_Full);

#if (ENIC_HAVE_LINK_MODE_25000_BASE_SR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			25000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			25000baseSR_Full);
#endif
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		base->port = PORT_FIBRE;
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_15428:
	case PCI_SUBDEV_ID_CISCO_VIC_15427:
	case PCI_SUBDEV_ID_CISCO_VIC_15425:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseT_Full);

#if (ENIC_HAVE_LINK_MODE_25000_BASE_SR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			25000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			25000baseSR_Full);
#endif
#if (ENIC_HAVE_LINK_MODE_50000_BASE_SR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			50000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			50000baseSR_Full);
#endif
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
		base->port = PORT_FIBRE;
		break;

	/* Do not mention port type as FIBRE for blade VICs */
	case PCI_SUBDEV_ID_CISCO_VIC_1240:
	case PCI_SUBDEV_ID_CISCO_VIC_1280:
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseKR_Full);
		break;
	case PCI_SUBDEV_ID_CISCO_VIC_1340:
	case PCI_SUBDEV_ID_CISCO_VIC_1380:
	case PCI_SUBDEV_ID_CISCO_VIC_1440: /* 10G/40G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_1480: /* 10G/40G KR */
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseKR_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			40000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			40000baseKR4_Full);

		break;

	case PCI_SUBDEV_ID_CISCO_VIC_14425: /* 25G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_14825: /* 25G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_15420: /* 25G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_15422: /* 25G KR */
#if (ENIC_HAVE_LINK_MODE_25000_BASE_KR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			25000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			25000baseKR_Full);
#endif
		break;

	case PCI_SUBDEV_ID_CISCO_VIC_15411: /* 10G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_15412: /* 10G KR */
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			10000baseKR_Full);
		break;


	case PCI_SUBDEV_ID_CISCO_VIC_15231: /* 25G/100G/200G KR */
	case PCI_SUBDEV_ID_CISCO_VIC_15230: /* 25G/100G/200G KR */
#if (ENIC_HAVE_LINK_MODE_25000_BASE_KR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			25000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			25000baseKR_Full);
#endif

#if (ENIC_HAVE_LINK_MODE_100000_BASE_KR_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			100000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			100000baseKR_Full);
#endif

#if (ENIC_HAVE_LINK_MODE_200000_BASE_SR4_SUPP)
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
			200000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
			200000baseKR4_Full);
#endif

		break;
	}
}

static int enic_get_ksettings(struct net_device *netdev,
			      struct ethtool_link_ksettings *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	struct ethtool_link_settings *base = &ecmd->base;

	enic_get_supp_adv_media_type(netdev, ecmd);

	if (netif_carrier_ok(netdev)) {
		base->speed = vnic_dev_port_speed(enic->vdev);
		base->duplex = DUPLEX_FULL;
	} else {
		base->speed = SPEED_UNKNOWN;
		base->duplex = DUPLEX_UNKNOWN;
	}

	base->autoneg = AUTONEG_DISABLE;

	return 0;
}
#endif /* ENIC_HAVE_GET_LINK_KSETTINGS */

#if (ENIC_HAVE_GET_SETTINGS)
static int enic_get_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	ecmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(netdev)) {
		ethtool_cmd_speed_set(ecmd, vnic_dev_port_speed(enic->vdev));
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ethtool_cmd_speed_set(ecmd, -1);
		ecmd->duplex = -1;
	}

	ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}
#endif /*ENIC_HAVE_GET_SETTINGS*/

static void enic_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *drvinfo)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_devcmd_fw_info *fw_info;
	int err;

	err = enic_dev_fw_info(enic, &fw_info);
	if (err == -ENOMEM)
		return;
#if (ENIC_HAVE_STRLCPY)
	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, fw_info->fw_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(enic->pdev),
		sizeof(drvinfo->bus_info));
#else
	strscpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strscpy(drvinfo->fw_version, fw_info->fw_version,
		sizeof(drvinfo->fw_version));
	strscpy(drvinfo->bus_info, pci_name(enic->pdev),
		sizeof(drvinfo->bus_info));
#endif
	enic_driver_encode_asic_info(drvinfo, fw_info);
}

static u32 enic_get_msglevel(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	return enic->msg_enable;
}

static void enic_set_msglevel(struct net_device *netdev, u32 value)
{
	struct enic *enic = netdev_priv(netdev);
	enic->msg_enable = value;
}

static void enic_get_strings(struct net_device *netdev, u32 stringset,
	u8 *data)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int i;
	unsigned int j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < NUM_ENIC_TX_STATS; i++) {
			memcpy(data, enic_tx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		for (i = 0; i < NUM_ENIC_RX_STATS; i++) {
			memcpy(data, enic_rx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}

		for (i = 0; i < enic->rq_count[ENIC_DATA_QP]; i++) {
			for (j = 0; j < NUM_ENIC_PER_RQ_STATS; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 enic_per_rq_stats[j].name, i);
				data += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < enic->wq_count[ENIC_DATA_QP]; i++) {
			for (j = 0; j < NUM_ENIC_PER_WQ_STATS; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 enic_per_wq_stats[j].name, i);
				data += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < enic->rq_count[ENIC_ADMIN_QP]; i++) {
			for (j = 0; j < NUM_ENIC_PER_ADMIN_RQ_STATS; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 enic_per_admin_rq_stats[j].name, i);
				data += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < enic->wq_count[ENIC_ADMIN_QP]; i++) {
			for (j = 0; j < NUM_ENIC_PER_ADMIN_WQ_STATS; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 enic_per_admin_wq_stats[j].name, i);
				data += ETH_GSTRING_LEN;
			}
		}

		break;
	}
}

static void enic_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_enet_config *c = &enic->config;

	ring->rx_max_pending = c->max_rq_ring;
	ring->tx_max_pending = c->max_wq_ring;
	ring->rx_pending = c->rq_desc_count;
	ring->tx_pending = c->wq_desc_count;
}

static int enic_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_enet_config *c = &enic->config;
	int running = netif_running(netdev);
	unsigned int rx_pending;
	unsigned int tx_pending;
	unsigned int rx_max;
	unsigned int tx_max;
	int err = 0;

	rx_max = c->max_rq_ring;
	tx_max = c->max_wq_ring;

	if (ring->rx_mini_max_pending || ring->rx_mini_pending) {
		netdev_info(netdev,
			    "modifying mini ring params is not supported");
		return -EINVAL;
	}
	if (ring->rx_jumbo_max_pending || ring->rx_jumbo_pending) {
		netdev_info(netdev,
			    "modifying jumbo ring params is not supported");
		return -EINVAL;
	}
	rx_pending = c->rq_desc_count;
	tx_pending = c->wq_desc_count;
	if (ring->rx_pending > rx_max ||
	    ring->rx_pending < ENIC_MIN_RQ_DESCS) {
		netdev_info(netdev, "rx pending (%u) not in range [%u,%u]",
			    ring->rx_pending, ENIC_MIN_RQ_DESCS,
			    rx_max);
		return -EINVAL;
	}
	if (ring->tx_pending > tx_max ||
	    ring->tx_pending < ENIC_MIN_WQ_DESCS) {
		netdev_info(netdev, "tx pending (%u) not in range [%u,%u]",
			    ring->tx_pending, ENIC_MIN_WQ_DESCS,
			    tx_max);
		return -EINVAL;
	}
	if (running)
		dev_close(netdev);
	c->rq_desc_count =
		ring->rx_pending & 0xffffffe0; /* must be aligned to groups of 32 */
	c->wq_desc_count =
		ring->tx_pending & 0xffffffe0; /* must be aligned to groups of 32 */
	if (running) {
		err = enic_kcompat_dev_open(netdev, NULL);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	c->rq_desc_count = rx_pending;
	c->wq_desc_count = tx_pending;
	return err;
}

#if (ENIC_HAVE_GET_STATS_COUNT)
int enic_get_stats_count(struct net_device *netdev)
{
	return NUM_ENIC_TX_STATS + NUM_ENIC_RX_STATS +
	       NUM_ENIC_PER_ADMIN_WQ_STATS + NUM_ENIC_PER_ADMIN_RQ_STATS;
}
#else
int enic_get_sset_count(struct net_device *netdev, int sset)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int n_stats;

	switch (sset) {
	case ETH_SS_STATS:
		n_stats = NUM_ENIC_TX_STATS + NUM_ENIC_RX_STATS;
		n_stats += enic->rq_count[ENIC_DATA_QP] *
			   NUM_ENIC_PER_RQ_STATS;
		n_stats += enic->wq_count[ENIC_DATA_QP] *
			   NUM_ENIC_PER_WQ_STATS;
		n_stats += enic->rq_count[ENIC_ADMIN_QP] *
			   NUM_ENIC_PER_ADMIN_RQ_STATS;
		n_stats += enic->wq_count[ENIC_ADMIN_QP] *
			   NUM_ENIC_PER_ADMIN_WQ_STATS;
		return n_stats;
	default:
		return -EOPNOTSUPP;
	}
}

/* See enic_get_strings() and enic_get_ethtool_stats() for the order the stats
 * blocks are returned by ethtool.
 */
int enic_get_sset_block_offset(struct net_device *netdev, enum enic_stats_block stats_block,
			       unsigned int queue_num, unsigned int *offset)
{
	switch (stats_block) {
	case ENIC_STATS_TX:
		*offset = 0;
		break;
	case ENIC_STATS_RX:
		*offset = NUM_ENIC_TX_STATS;
		break;
	case ENIC_STATS_DATA_RQ:
	case ENIC_STATS_DATA_WQ:
	case ENIC_STATS_ADMIN_RQ:
	case ENIC_STATS_ADMIN_WQ:
		/* There is currently no use case for these stats blocks
		 */
		netdev_warn(netdev, "%s - stats_block %u not supported\n",
			    __func__, stats_block);
		return -EOPNOTSUPP;
	default:
		netdev_err(netdev, "%s - unknown stats_block %u\n",
			   __func__, stats_block);

		return -EINVAL;
	}

	return 0;
}
#endif

void enic_get_ethtool_stats(struct net_device *netdev,
			    struct ethtool_stats *stats, u64 *data)
{
	u64 *tx_stats_offset, *rx_stats_offset, *rq_stats_offset;
	struct enic *enic = netdev_priv(netdev);
	unsigned int qp_from, qp_to;
	struct vnic_stats *vstats;
	int data_rq_count = 0;
	unsigned int count;
	unsigned int i;
	unsigned int j;
	int err;

	err = enic_dev_stats_dump(enic, &vstats);
	if (err == -ENOMEM)
		return;

	/* When the HW (global) RQ stats are not reliable, we need to use the
	 * SW per-RQ stats to compute in SW the global RQ stats.
	 * In order to do so, we need to first init/save the per-RQ stats
	 * (into the 'data' array) and later use them to compute the global RQ
	 * stats:
	 * - a1) per-RQ stats (data QPs + admin QP)
	 * - b1) per-WQ stats (data QPs + admin QP)
	 * - c1) global WK stats
	 * - d1) global RX stats
	 * In other words a1) must be init before d1).
	 * Since the strings returned by 'get_strings' follow the opposite
	 * order (ie, first the global stats and then per-RQ stats):
	 * - a2) global TX stats
	 * - b2) global RX stats
	 * - c2) per-RQ stats (data QPs + admin QP)
	 * - d2) per-WQ stats (data QPs + admin QP)
	 * here we will not be able to ini the 'data' stats array in the same
	 * order the strings are added to the array of strings.
	 * The values in 'data' will follow the a1/b1/c1/d1 order as the
	 * strings in the strings array, but their init will follow the
	 * a2/b2/c2/d2 order.
	 * In order to achieve that we save here the offsets to the four stats
	 * block to make it easier to initialize them following a non-sequential
	 * order (ie, a2/b2/c2/d2 instead of a1/b1/c1/d1).
	 *
	 * - base_offset = 'data': pointer to the input 'data' stats array
	 * - tx_stats_offset     : global TX stats
	 * - rx_stats_offset     : global RX stats
	 * - rq_stats_offset     : per-RQ stats
	 *			   (immediately followed by per-WQ stats)
	 */

	tx_stats_offset = data;
	rx_stats_offset = tx_stats_offset + NUM_ENIC_TX_STATS;
	rq_stats_offset = rx_stats_offset + NUM_ENIC_RX_STATS;

	/* DATA QPs - per-RQ stats
	 */

	err = enic_get_qps_range(enic, ENIC_DATA_QP, &qp_from, &qp_to);
	if (err)
		qp_from = qp_to = 0;

	count = enic->rq_count[ENIC_DATA_QP];
	data = rq_stats_offset;
	for (i = qp_from; i < qp_from + count; i++) {
		struct enic_qp *qp = &enic->qp[i];
		int index;

		for (j = 0; j < NUM_ENIC_PER_RQ_STATS; j++) {
			index = enic_per_rq_stats[j].offset;
			*(data++) = ((u64 *)&qp->rq.stats)[index];
		}
	}
	data_rq_count = count;

	/* DATA QPs - per-WQ stats
	 */

	count = enic->wq_count[ENIC_DATA_QP];
	if (err)
		count = 0;

	for (i = qp_from; i < qp_from + count; i++) {
		struct enic_qp *qp = &enic->qp[i];
		int index;

		for (j = 0; j < NUM_ENIC_PER_WQ_STATS; j++) {
			index = enic_per_wq_stats[j].offset;
			*(data++) = ((u64 *)&qp->wq.stats)[index];
		}
	}

	/* ADMIN QPs - per-RQ stats
	 */

	err = enic_get_qps_range(enic, ENIC_ADMIN_QP, &qp_from, &qp_to);
	if (err)
		qp_from = qp_to = 0;

	count = enic->rq_count[ENIC_ADMIN_QP];
	for (i = qp_from; i < qp_from + count; i++) {
		struct enic_qp *qp = &enic->qp[i];
		int index;

		for (j = 0; j < NUM_ENIC_PER_ADMIN_RQ_STATS; j++) {
			index = enic_per_admin_rq_stats[j].offset;
			*(data++) = ((u64 *)&qp->rq.stats)[index];
		}
	}

	/* ADMIN QPs - per-WQ stats
	 */

	count = enic->wq_count[ENIC_ADMIN_QP];
	for (i = qp_from; i < qp_from + count; i++) {
		struct enic_qp *qp = &enic->qp[i];
		int index;

		for (j = 0; j < NUM_ENIC_PER_ADMIN_WQ_STATS; j++) {
			index = enic_per_admin_wq_stats[j].offset;
			*(data++) = ((u64 *)&qp->wq.stats)[index];
		}
	}

	/* Global stats are init last because the global RX stats may require
	 * the per-RQ stats to be already init (if the former are updated in SW).
	 *
	 * Global TX stats
	 */

	data = tx_stats_offset;
	for (i = 0; i < NUM_ENIC_TX_STATS; i++)
		*(data++) = ((u64 *)&vstats->tx)[enic_tx_stats[i].offset];

	/* Global RX stats
	 */

	data = rx_stats_offset;
	if (enic_hw_rx_stats_ok(enic)) {
		for (i = 0; i < NUM_ENIC_RX_STATS; i++)
			*(data++) = ((u64 *)&vstats->rx)[enic_rx_stats[i].offset];
	} else {
		unsigned int rx_idx, rq_idx;
		u64 *offset;

		/* DATA QPs only (ie, not including ADMIN QP stats)
		 */
		for (i = 0; i < ARRAY_SIZE(enic_rx_to_rq_stats); i++) {
			/*
			 * rx_idx: Global RX stat to initialize
			 * rq_idx: Per-RQ stat to use to initialize the
			 *	   associated global stat
			 * offset: Pointer to RQ#j set of already initialized
			 *	   stats
			 */
			rx_idx = enic_rx_to_rq_stats[i].rx_index;
			rq_idx = enic_rx_to_rq_stats[i].rq_index;

			*(data + rx_idx) = 0;
			for (j = 0; j < data_rq_count; j++) {
				offset =  rq_stats_offset + (j * NUM_ENIC_PER_RQ_STATS);

				*(data + rx_idx) += *(offset + rq_idx);
			}
		}

		/* Good HW stats
		 */

		for (i = 0; i < ARRAY_SIZE(enic_rx_hw_stat_ok); i++) {
			rx_idx = enic_rx_hw_stat_ok[i];
			*(data + rx_idx) = ((u64 *)&vstats->rx)[rx_idx];
		}

		/* These global stats are computed here (by using other stats)
		 * to save some cycles in the data path.
		 */

		*(data + ENIC_RX_STAT_INDEX(rx_frames_total)) =
			*(data + ENIC_RX_STAT_INDEX(rx_frames_ok)) +
			*(data + ENIC_RX_STAT_INDEX(rx_drop)) +
			*(data + ENIC_RX_STAT_INDEX(rx_errors)) +
			*(data + ENIC_RX_STAT_INDEX(rx_no_bufs));

		*(data + ENIC_RX_STAT_INDEX(rx_unicast_frames_ok)) =
			*(data + ENIC_RX_STAT_INDEX(rx_frames_ok)) -
			*(data + ENIC_RX_STAT_INDEX(rx_multicast_frames_ok)) -
			*(data + ENIC_RX_STAT_INDEX(rx_broadcast_frames_ok));

		*(data + ENIC_RX_STAT_INDEX(rx_unicast_bytes_ok)) =
			*(data + ENIC_RX_STAT_INDEX(rx_bytes_ok)) -
			*(data + ENIC_RX_STAT_INDEX(rx_multicast_bytes_ok)) -
			*(data + ENIC_RX_STAT_INDEX(rx_broadcast_bytes_ok));
	}
}

#if (ENIC_HAVE_GET_RX_CSUM)
static u32 enic_get_rx_csum(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	return enic->csum_rx_enabled;
}
#endif

#if (ENIC_HAVE_SET_RX_CSUM)
static int enic_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);
	if (data && !ENIC_SETTING(enic, RXCSUM))
		return -EINVAL;

	enic->csum_rx_enabled = !!data;

	return 0;
}
#endif

#if (ENIC_HAVE_SET_TX_CSUM)
static int enic_set_tx_csum(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);

	if (data && !ENIC_SETTING(enic, TXCSUM))
		return -EINVAL;

	if (data)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;

	return 0;
}
#endif

#if (ENIC_HAVE_SET_TSO)
static int enic_set_tso(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);

	if (data && !ENIC_SETTING(enic, TSO))
		return -EINVAL;

	if (data)
		netdev->features |=
			NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN;
	else
		netdev->features &=
			~(NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN);

	return 0;
}
#endif

#if (ENIC_HAVE_GET_COALESCE)
static int enic_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->rx_coalesce_usecs = enic->rx_coal.coal_usecs;
	if (enic->rx_coal.use_adaptive_rx_coalesce)
		ecmd->use_adaptive_rx_coalesce = 1;
	ecmd->rx_coalesce_usecs_low = enic->rx_coal.acoal_low;
	ecmd->rx_coalesce_usecs_high = enic->rx_coal.acoal_high;

	return 0;
}
#endif

static int enic_coalesce_valid(struct enic *enic,
			       struct ethtool_coalesce *ec,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack)

{
	u32 coalesce_usecs_max = vnic_dev_get_intr_coal_timer_max(enic->vdev);
	u32 rx_coalesce_usecs_high = min_t(u32, coalesce_usecs_max,
					   ec->rx_coalesce_usecs_high);
	u32 rx_coalesce_usecs_low = min_t(u32, coalesce_usecs_max,
					  ec->rx_coalesce_usecs_low);

	if (ec->tx_coalesce_usecs) {
		netdev_info(enic->netdev, "Use rx-usecs instead of tx-usecs, this will set coalescing value for both rx & tx");
		ENIC_COAL_NL_SET_ERR_MSG_MOD(extack,
					     "Use rx-usecs instead of tx-usecs, this will set coalescing value for both rx & tx");
		return -EINVAL;
	}
	if (ec->rx_max_coalesced_frames		||
	    ec->rx_coalesce_usecs_irq		||
	    ec->rx_max_coalesced_frames_irq	||
	    ec->tx_max_coalesced_frames		||
	    ec->tx_coalesce_usecs_irq		||
	    ec->tx_max_coalesced_frames_irq	||
	    ec->stats_block_coalesce_usecs	||
	    ec->use_adaptive_tx_coalesce	||
	    ec->pkt_rate_low			||
	    ec->rx_max_coalesced_frames_low	||
	    ec->tx_coalesce_usecs_low		||
	    ec->tx_max_coalesced_frames_low	||
	    ec->pkt_rate_high			||
	    ec->rx_max_coalesced_frames_high	||
	    ec->tx_coalesce_usecs_high		||
	    ec->tx_max_coalesced_frames_high	||
	    ec->rate_sample_interval) {
		ENIC_COAL_NL_SET_ERR_MSG_MOD(extack, "Invalid coalesce option.");
		return -EINVAL;
	}

	if ((ec->rx_coalesce_usecs > coalesce_usecs_max)	||
	    (ec->rx_coalesce_usecs_high > coalesce_usecs_max)	||
	    (ec->rx_coalesce_usecs_low > coalesce_usecs_max))
		netdev_info(enic->netdev, "ethtool_set_coalesce: adaptor supports max coalesce value of %d. Setting max value.\n",
			    coalesce_usecs_max);

	if (ec->rx_coalesce_usecs_high &&
	    (rx_coalesce_usecs_high <
	     rx_coalesce_usecs_low)) {
		ENIC_COAL_NL_SET_ERR_MSG_MOD(extack, "Invalid coalescing values");
		return -EINVAL;
	}

	return 0;
}

#if (ENIC_HAVE_SET_COALESCE)
static int enic_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_qp *qp;
	u32 rx_coalesce_usecs;
        u32 rx_coalesce_usecs_low;
        u32 rx_coalesce_usecs_high;
	struct enic_rx_coal *rxcoal = &enic->rx_coal;
        u32 coalesce_usecs_max;
	int ret;
	int i;

	ret = enic_coalesce_valid(enic, ecmd, kernel_coal, extack);
	if (ret) {
		netdev_err(netdev, "enic_coalesce_valid error %d", ret);
		return ret;
	}

        coalesce_usecs_max = vnic_dev_get_intr_coal_timer_max(enic->vdev);
	rx_coalesce_usecs = min_t(u32, ecmd->rx_coalesce_usecs,
				  coalesce_usecs_max);
        rx_coalesce_usecs_low = min_t(u32, ecmd->rx_coalesce_usecs_low,
				      coalesce_usecs_max);
        rx_coalesce_usecs_high = min_t(u32, ecmd->rx_coalesce_usecs_high,
				       coalesce_usecs_max);
	rxcoal->use_adaptive_rx_coalesce = !!ecmd->use_adaptive_rx_coalesce;

	for (i = 0; i < enic->qp_count; i++) {
		qp = &enic->qp[i];

		qp->rq.adaptive_coal = rxcoal->use_adaptive_rx_coalesce;
	}
	rxcoal->coal_usecs = rx_coalesce_usecs;
	if (!rxcoal->use_adaptive_rx_coalesce) {
		enic_intr_coal_set(enic, rx_coalesce_usecs);
		if (enic->qp) {
			for (i = 0; i < enic->qp_count; i++) {
				qp = &enic->qp[i];
				qp->rq.coal_timer = rx_coalesce_usecs;
			}
		}
	}
	if (ecmd->rx_coalesce_usecs_high) {
		rxcoal->acoal_high = rx_coalesce_usecs_high;
		rxcoal->acoal_low = rx_coalesce_usecs_low;
		if (enic->qp) {
			for (i = 0; i < enic->qp_count; i++) {
				qp = &enic->qp[i];

				qp->rq.acoal_high = rx_coalesce_usecs_high;
				qp->rq.acoal_low = rx_coalesce_usecs_low;
			}
		}
	}

	return 0;
}
#endif

#if (ENIC_HAVE_GET_RXNFC)
static int enic_grxclsrlall(struct enic *enic, struct ethtool_rxnfc *cmd,
			    u32 *rule_locs)
{
	int j, ret = 0, cnt = 0;

	cmd->data = enic->rfs_h.max - enic->rfs_h.free;
	for (j = 0; j < (1 << ENIC_RFS_FLW_BITSHIFT); j++) {
		struct hlist_head *hhead;
#if (VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG)
		struct hlist_node *pos;
#endif
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;

		hhead = &enic->rfs_h.ht_head[j];
		enic_hlist_for_each_entry_safe(n, pos, tmp, hhead, node) {
			if (cnt == cmd->rule_cnt)
				return -EMSGSIZE;
			rule_locs[cnt] = n->fltr_id;
			cnt++;
		}
	}
	cmd->rule_cnt = cnt;

	return ret;
}

static int enic_grxclsrule(struct enic *enic, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
				(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct enic_rfs_fltr_node *n;

	n = htbl_fltr_search(enic, (u16)fsp->location);
	if (!n)
		return -EINVAL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)) || \
    (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4)))
	switch (n->keys.basic.ip_proto) {
#else
	switch (n->keys.ip_proto) {
#endif
	case IPPROTO_TCP:
		fsp->flow_type = TCP_V4_FLOW;
		break;
	case IPPROTO_UDP:
		fsp->flow_type = UDP_V4_FLOW;
		break;
	default:
		return -EINVAL;
		break;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)) || \
    (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4)))
	fsp->h_u.tcp_ip4_spec.ip4src = flow_get_u32_src(&n->keys);
	fsp->h_u.tcp_ip4_spec.ip4dst = flow_get_u32_dst(&n->keys);
	fsp->h_u.tcp_ip4_spec.psrc = n->keys.ports.src;
	fsp->h_u.tcp_ip4_spec.pdst = n->keys.ports.dst;
#else
	fsp->h_u.tcp_ip4_spec.ip4src = n->keys.src;
	fsp->h_u.tcp_ip4_spec.ip4dst = n->keys.dst;
	fsp->h_u.tcp_ip4_spec.psrc = n->keys.port16[0];
	fsp->h_u.tcp_ip4_spec.pdst = n->keys.port16[1];
#endif
	fsp->ring_cookie = n->rq_id;

	fsp->m_u.tcp_ip4_spec.ip4src = (__u32)~0;
	fsp->m_u.tcp_ip4_spec.ip4dst = (__u32)~0;
	fsp->m_u.tcp_ip4_spec.psrc = (__u16)~0;
	fsp->m_u.tcp_ip4_spec.pdst = (__u16)~0;

	return 0;
}

static int enic_get_rx_flow_hash(struct enic *enic, struct ethtool_rxnfc *cmd)
{
	u8 rss_hash_type = 0;
	cmd->data = 0;

	spin_lock_bh(&enic->devcmd_lock);
	(void)vnic_dev_capable_rss_hash_type(enic->vdev, &rss_hash_type);
	spin_unlock_bh(&enic->devcmd_lock);
	switch (cmd->flow_type) {
	case TCP_V6_FLOW:
	case TCP_V4_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3 |
			     RXH_IP_SRC | RXH_IP_DST;
		break;
	case UDP_V6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		if (rss_hash_type & NIC_CFG_RSS_HASH_TYPE_UDP_IPV6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		if (rss_hash_type & NIC_CFG_RSS_HASH_TYPE_UDP_IPV4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int enic_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
#if (ENIC_HAVE_GET_RXNFC_VOID_RULE_LOCKS)
			  void *rule_locs)
#else
			  u32 *rule_locs)
#endif
{
	struct enic *enic = netdev_priv(dev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = enic->rq_count[ENIC_DATA_QP];
		break;
	case ETHTOOL_GRXCLSRLCNT:
		spin_lock_bh(&enic->rfs_h.lock);
		cmd->rule_cnt = enic->rfs_h.max - enic->rfs_h.free;
		cmd->data = enic->rfs_h.max;
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXCLSRLALL:
		spin_lock_bh(&enic->rfs_h.lock);
		ret = enic_grxclsrlall(enic, cmd, rule_locs);
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXCLSRULE:
		spin_lock_bh(&enic->rfs_h.lock);
		ret = enic_grxclsrule(enic, cmd);
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXFH:
		ret = enic_get_rx_flow_hash(enic, cmd);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
#endif

#if (ENIC_HAVE_GET_TS_INFO)
static int enic_get_ts_info(struct net_device *netdev,
			    struct ethtool_ts_info *info)
{
	struct enic *enic;

	enic = netdev_priv(netdev);
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_ON) |
			 (1 << HWTSTAMP_TX_OFF);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	info->phc_index = enic->tstamp.ptp ? ptp_clock_index(enic->tstamp.ptp) :
			  -1;
	return 0;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32))
static struct ethtool_ops enic_ethtool_ops = {
#else
static const struct ethtool_ops enic_ethtool_ops = {
#endif
#if (ENIC_HAVE_SUPP_COAL_PARAMS)
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX |
				     ETHTOOL_COALESCE_RX_USECS_LOW |
				     ETHTOOL_COALESCE_RX_USECS_HIGH,
#endif
#if (ENIC_HAVE_GET_LINK_KSETTINGS)
	.get_link_ksettings = enic_get_ksettings,
#endif
#if (ENIC_HAVE_GET_SETTINGS)
	.get_settings = enic_get_settings,
#endif
	.get_drvinfo = enic_get_drvinfo,
	.get_msglevel = enic_get_msglevel,
	.set_msglevel = enic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = enic_get_strings,
	.get_ringparam = enic_get_ringparam,
	.set_ringparam = enic_set_ringparam,
#if (ENIC_HAVE_GET_RXFH_OPS)
	.get_rxfh_key_size = enic_get_rxfh_key_size,
	.get_rxfh = enic_get_rxfh,
	.set_rxfh = enic_set_rxfh,
#endif
#if (ENIC_HAVE_GET_STATS_COUNT)
	.get_stats_count = enic_get_stats_count,
#else
	.get_sset_count = enic_get_sset_count,
#endif
	.get_ethtool_stats = enic_get_ethtool_stats,
#if (ENIC_HAVE_GET_RX_CSUM)
	.get_rx_csum = enic_get_rx_csum,
#endif
#if (ENIC_HAVE_SET_RX_CSUM)
	.set_rx_csum = enic_set_rx_csum,
#endif
#if (ENIC_HAVE_GET_TX_CSUM)
	.get_tx_csum = ethtool_op_get_tx_csum,
#endif
#if (ENIC_HAVE_SET_TX_CSUM)
	.set_tx_csum = enic_set_tx_csum,
#endif
#if (ENIC_HAVE_GET_SG)
	.get_sg = ethtool_op_get_sg,
#endif
#if (ENIC_HAVE_SET_SG)
	.set_sg = ethtool_op_set_sg,
#endif
#if (ENIC_HAVE_GET_TSO)
	.get_tso = ethtool_op_get_tso,
#endif
#if (ENIC_HAVE_SET_TSO)
	.set_tso = enic_set_tso,
#endif
#if (ENIC_HAVE_GET_COALESCE)
	.get_coalesce = enic_get_coalesce,
#endif
#if (ENIC_HAVE_SET_COALESCE)
	.set_coalesce = enic_set_coalesce,
#endif
#if (ENIC_HAVE_GET_FLAGS)
	.get_flags = ethtool_op_get_flags,
#endif
#if (ENIC_HAVE_SET_FLAGS)
	.set_flags = ethtool_op_set_flags,
#endif
#if (ENIC_HAVE_GET_RXNFC)
	.get_rxnfc = enic_get_rxnfc,
#endif
#if (!ENIC_HAVE_ETHTOOL_OPS_EXT && ENIC_HAVE_GET_TS_INFO)
	.get_ts_info = enic_get_ts_info,
#endif
};

#if (ENIC_HAVE_ETHTOOL_OPS_EXT)
static struct ethtool_ops_ext enic_ethtool_ops_ext = {
	.size		= sizeof(struct ethtool_ops_ext),
#if (ENIC_HAVE_GET_TS_INFO)
	.get_ts_info	= enic_get_ts_info,
#endif
#if (ENIC_HAVE_GET_RXFH_OPS_EXT)
	.get_rxfh_key_size = enic_get_rxfh_key_size,
	.get_rxfh = enic_get_rxfh,
	.set_rxfh = enic_set_rxfh,
#endif
};
#endif

void enic_set_ethtool_ops(struct enic *enic)
{
	enic->netdev->ethtool_ops = &enic_ethtool_ops;
	set_ethtool_ops_ext(enic->netdev, &enic_ethtool_ops_ext);
}
