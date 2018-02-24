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

#ifndef _ENIC_DEV_H_
#define _ENIC_DEV_H_

#include "vnic_dev.h"
#include "vnic_vic.h"

/*
 * Calls the devcmd function given by argument vnicdevcmdfn.
 * If vf argument is valid, it proxies the devcmd
 */
#define ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnicdevcmdfn, ...) \
	do { \
		spin_lock_bh(&enic->devcmd_lock); \
		if (enic_is_valid_vf(enic, vf)) { \
			vnic_dev_cmd_proxy_by_index_start (enic->vdev, vf); \
			err = vnicdevcmdfn(enic->vdev, ##__VA_ARGS__); \
			vnic_dev_cmd_proxy_end(enic->vdev); \
		} else { \
			err = vnicdevcmdfn(enic->vdev, ##__VA_ARGS__); \
		} \
		spin_unlock_bh(&enic->devcmd_lock); \
	} while (0)

void enic_ext_cq(struct enic *enic);
int enic_dev_fw_info(struct enic *enic, struct vnic_devcmd_fw_info **fw_info);
int enic_dev_stats_dump(struct enic *enic, struct vnic_stats **vstats);
int enic_dev_add_station_addr(struct enic *enic);
int enic_dev_del_station_addr(struct enic *enic);
int enic_dev_packet_filter(struct enic *enic, int directed, int multicast,
	int broadcast, int promisc, int allmulti);
int enic_dev_add_addr(struct enic *enic, u8 *addr);
int enic_dev_del_addr(struct enic *enic, u8 *addr);
#if (ENIC_HAVE_VLAN_RX_ADD_VID_RET_TYPE_INT)
#if (ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_ADD_VID)
int enic_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid);
#else
int enic_vlan_rx_add_vid(struct net_device *netdev, u16 vid);
#endif
#else
void enic_vlan_rx_add_vid(struct net_device *netdev, u16 vid);
#endif

#if (ENIC_HAVE_VLAN_RX_KILL_VID_RET_TYPE_INT)
#if (ENIC_HAVE_PROTO_IN_NDO_VLAN_RX_KILL_VID)
int enic_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid);
#else
int enic_vlan_rx_kill_vid(struct net_device *netdev, u16 vid);
#endif
#else
void enic_vlan_rx_kill_vid(struct net_device *netdev, u16 vid);
#endif
int enic_dev_notify_set(struct enic *enic);
int enic_dev_notify_unset(struct enic *enic);
int enic_dev_hang_notify(struct enic *enic);
int enic_dev_set_ig_vlan_rewrite_mode(struct enic *enic);
int enic_dev_enable(struct enic *enic);
int enic_dev_disable(struct enic *enic);
int enic_dev_intr_coal_timer_info(struct enic *enic);
int enic_dev_asic_info(struct enic *enic, u16 *asic_type, u16 *asic_rev);
#ifdef IFLA_VF_PORT_MAX
struct vic_provinfo;
int enic_vnic_dev_deinit(struct enic *enic);
int enic_dev_init_prov2(struct enic *enic, struct vic_provinfo *vp);
int enic_dev_deinit_done(struct enic *enic, int *status);
#endif
int enic_dev_enable2(struct enic *enic, int arg);
int enic_dev_enable2_done(struct enic *enic, int *status);
int enic_dev_status_to_errno(int devcmd_status);
int enic_dev_log_qerror_capable(struct enic *enic);
void enic_dev_log_qerror(struct enic *enic, u64 qerror_handle, u32 size);
int enic_spoofchk_op(struct enic *enic, int vf_id,
		     enum vnic_devcmd_get_set_op op, bool *enable);
int enic_local_fwd_op(struct enic *enic, enum vnic_devcmd_get_set_op op,
		      bool *enable);
int enic_vf_allowed_list_op(struct enic *enic, int vf_id,
			    enum vnic_devcmd_get_set_op op,
			    u64 *allowed_bitmap, int size);
int enic_vf_access_vlan_op(struct enic *enic, enum vnic_devcmd_get_set_op op,
			   u16 vf_id, u16 *vlan_id);
int enic_link_status_notify_mode(struct enic *enic,
				 enum vnic_devcmd_get_set_op op, u16 vf_id,
				 bool *pf_only);
int enic_qp_type_set(struct enic *enic, u32 qp_type, u32 enable);
int enic_dev_sriov_stats(struct enic *enic, struct vnic_sriov_stats **stats);
int enic_hw_rx_stats_ok(struct enic *enic);
int enic_dev_add_addr_vlan(struct enic *enic, u8 *addr, u16 vid);
int enic_dev_del_addr_vlan(struct enic *enic, u8 *addr, u16 vid);
#endif /* _ENIC_DEV_H_ */
