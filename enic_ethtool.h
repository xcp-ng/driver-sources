/*
 * Copyright 2011 Cisco Systems, Inc.  All rights reserved.
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

#ifndef _ENIC_ETHTOOL_H_
#define _ENIC_ETHTOOL_H_

#include <linux/netdevice.h>
#include <linux/ethtool.h>

enum enic_stats_block {
	ENIC_STATS_TX,
	ENIC_STATS_RX,
	ENIC_STATS_DATA_WQ,
	ENIC_STATS_DATA_RQ,
	ENIC_STATS_ADMIN_WQ,
	ENIC_STATS_ADMIN_RQ,
	ENIC_STATS_MAX
};

void enic_set_ethtool_ops(struct enic *enic);

#if (ENIC_HAVE_GET_STATS_COUNT)
int enic_get_stats_count(struct net_device *netdev);
#else
int enic_get_sset_count(struct net_device *netdev, int sset);
#endif

void enic_get_ethtool_stats(struct net_device *netdev,
			    struct ethtool_stats *stats, u64 *data);
int enic_get_num_stats(struct enic *enic, enum enic_stats_block stats_block);
int enic_get_sset_block_offset(struct net_device *netdev,
			       enum enic_stats_block stats_block,
			       unsigned int queue_num, unsigned int *offset);
void enic_intr_coal_set(struct enic *enic, u32 timer);

#endif	/* _ENIC_ETHTOOL_H_ */
