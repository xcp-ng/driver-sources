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

#ifndef _ENIC_SRIOV_H_
#define _ENIC_SRIOV_H_

#include "enic_mbox.h"
#include "vnic_devcmd.h"

struct enic;

#define PCI_DEVICE_ID_CISCO_VIC_ENET		0x0043 /* ethernet vnic */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_DYN	0x0044 /* enet dynamic vnic */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF		0x0071 /* enet SRIOV VMFEX VF */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V1	PCI_DEVICE_ID_CISCO_VIC_ENET_VF
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V2	0x02b7 /* enet SRIOV VF */
/* Same as PCI_DEVICE_ID_CISCO_VIC_USPACE_NIC in usnic.h */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF_USNIC	0x00cf /* usnic VF */


enum enic_vnic_type {
	ENIC_UNKNOWN,
	ENIC_PF,
	ENIC_DYN,	/* Obsolete */
	ENIC_VF_V1,	/* VF to be used with 802.1Qbh */
	ENIC_VF_USNIC,
	ENIC_VF_V2,
	ENIC_TYPE_MAX
};

struct enic_mbox_msg_status {
	u64	tstamp;	/* TX timestamp (jiffies) */
	u64	seq;	/* sequence number */
};

/* 256 is the size of the input/output bitmap from CMD_VF_ALLOWED_LIST devcmd
 */
#define ENIC_ALLOWED_DEVCMD_BITMAP_SIZE 256

/* Used by the PF */
struct enic_vf {
	struct enic *enic_pf;
	u32 registered;
	u16 vf_id;

	/* Flags requested by the VF */
	u16 pkt_fltr_req_flags;
	/* Flags applied by the PF. They  may not include all requested flags */
	u16 pkt_fltr_flags;

	/* The params below are configured by the PF.
	 * Exception: the_mac_addr can be init by the VF too if trusted.
	 */
	u16 vlan_id;
	u8 mac_addr[ETH_ALEN];
	u8 qos;
	u8 trusted;
	u8 spoofchk;
	u8 rss_query;
	u8 link_state_mode;
	u32 min_tx_rate;
	u32 max_tx_rate;

	DECLARE_BITMAP(dflt_allowed_devcmds, ENIC_ALLOWED_DEVCMD_BITMAP_SIZE);
	DECLARE_BITMAP(allowed_devcmds, ENIC_ALLOWED_DEVCMD_BITMAP_SIZE);

	/* Copy of ucast/mcast addresses configured by the VF
	 */
	struct list_head mac_addrs;

	struct u64_stats_sync syncp;
	struct vnic_stats vnic_stats;

	struct enic_mbox_msg_stat_t *mbox_stats;
	struct enic_mbox_msg_status msg_status[ENIC_MBOX_MAX];

	/* Last seq number used on egreess/ingress mbox request/notification
	 * messages.
	 * The seq number used in reply/acks messages are copied from the
	 * associated request/notification messages.
	 */
	u32 mbox_tx_seq;
	u32 mbox_rx_seq;
};

/* Flags for enic_vf_v2
 */
#define ENIC_VF_REGISTERED	BIT(0)

struct enic_vf_v2 {
	u16 vf_id;

	u16 flags;
	struct enic_mbox_msg_status msg_status[ENIC_MBOX_MAX];

	struct mutex pending_lists_lock;
	struct list_head pending_macs;

	/* Last seq number used on egreess/ingress mbox request/notification
	 * messages.
	 * The seq number used in reply/acks messages are copied from the
	 * associated request/notification messages.
	 */
	u32 mbox_tx_seq;
	u32 mbox_rx_seq;
	u16 ethtool_sset_count;
	u64 *ethtool_stats;
	struct completion unregister_comp;

	/* enic_mbox_vf_capability_reply_msg.version, used to check if
	 * PF and VF drivers are using compatible admin channel protocol.
	 */
	u32 pf_cap_version;
	struct completion pf_cap_comp;
};

struct enic_pf {
	spinlock_t vfs_lock;
	u16 num_vfs;
	u16 num_vfs_registered;
	u16 total_num_vfs;
	u16 vf_offset;
	u16 vf_stride;
	u16 misconfig_detected;
	u16 vfs_type;
	u16 _unused;
	struct work_struct vfs_link_state_updt_work;
	struct delayed_work vfs_stats_poller_work;
	struct enic_vf *vfs;
};

/* enic_sriov flags
 */
#define ENIC_SRIOV_LOCAL_FWD	BIT(0)

struct enic_sriov {
	union {
		struct enic_vf_v2 vf_v2;
		struct enic_pf pf;
	};

	struct enic_mbox_msg_stat_t mbox_stats[ENIC_MBOX_MAX];
	struct enic_mbox_gen_stat_t mbox_gen_stats;

	/* MBOX RX */
	struct work_struct mbox_work; /* Used by PFs and VFs V2 */
	spinlock_t mbox_msg_list_lock;
	struct list_head mbox_msg_list;

	u32 flags;
	u64 supp_ver;	/* sriov supported versions */
	u64 feat;	/* sriov features */
};

static const u16 enic_vf_v2_devcmds[] = {
	_CMD_N(CMD_INIT),
	_CMD_N(CMD_INITIALIZE_DEVCMD2),
	_CMD_N(CMD_NOTIFY),

	_CMD_N(CMD_OPEN),
	_CMD_N(CMD_OPEN_STATUS),
	_CMD_N(CMD_CLOSE),

	_CMD_N(CMD_ENABLE),
	_CMD_N(CMD_ENABLE_WAIT),
	_CMD_N(CMD_DISABLE),

	_CMD_N(CMD_DEV_SPEC),
	_CMD_N(CMD_INTR_COAL_CONVERT),
	_CMD_N(CMD_MCPU_FW_INFO),
	_CMD_N(CMD_GET_MAC_ADDR),
	_CMD_N(CMD_CAPABILITY),
	_CMD_N(CMD_GET_SUPP_FEATURE_VER),

	/* TODO: >=Beverly only */
	_CMD_N(CMD_RSS_KEY),
	_CMD_N(CMD_RSS_CPU),
	_CMD_N(CMD_NIC_CFG),
	_CMD_N(CMD_NIC_CFG_CHK),

	_CMD_N(CMD_QUEUE_ERROR),
	_CMD_N(CMD_STATS_DUMP),

	_CMD_N(CMD_HANG_NOTIFY),
	_CMD_N(CMD_HANG_RESET),
	_CMD_N(CMD_HANG_RESET_STATUS),

	_CMD_N(CMD_CQ_ENTRY_SIZE_SET),
	_CMD_N(CMD_QP_TYPE_SET),
	_CMD_N(CMD_SRIOV_STATS_GET),
};

#define NUM_ENIC_VF_V2_ALLOWED_DEVCMDS ARRAY_SIZE(enic_vf_v2_devcmds)

int enic_sriov_init(struct enic *enic);
void enic_sriov_deinit(struct enic *enic);
int enic_sriov_configure(struct pci_dev *pdev, int num_vfs);
int enic_is_pf(struct enic *enic);
int enic_is_sriov_pf(struct enic *enic);
int enic_is_vf_v1(struct enic *enic);
int enic_is_vf_v2(struct enic *enic);
int enic_is_dynamic(struct enic *enic);
const char *enic_sriov_vnic_type_to_str(struct enic *enic);
int enic_dev_is_vf_v1(u16 dev_id);
int enic_dev_is_vf_v2(u16 dev_id);
enum enic_vnic_type enic_get_vnic_type(struct enic *enic);
int enic_sriov_register(struct enic *enic);
int enic_sriov_unregister(struct enic *enic);
int enic_is_sriov_v1_enabled(struct enic *enic);
int enic_is_sriov_v2_enabled(struct enic *enic);
int enic_sriov_stats_capability(struct enic *enic);
int enic_is_valid_vf(struct enic *enic, int vf);
bool enic_sriov_requirements_ok(struct enic *enic, bool log_err, int *err);
int enic_sriov_check_capability(struct enic *enic);

int enic_is_vf_registered_locked(struct enic *enic, u16 vf_id);
int enic_vf_registered(struct enic *enic);

void enic_sriov_lock_init(struct enic *enic);
int enic_sriov_trylock_bh(struct enic *enic);
void enic_sriov_lock_bh(struct enic *enic);
void enic_sriov_unlock_bh(struct enic *enic);
int enic_sriov_ndo_check_and_lock(struct net_device *netdev, int vf);
int enic_sriov_enforce_vf_link_state(struct enic *enic, int vf,
				     bool enforce_down);

bool enic_is_vf_admin_mac_configured(struct enic *enic, u16 vf_id);
bool enic_is_vf_admin_mac_matching(struct enic *enic, u16 vf_id, u8 *mac);
bool enic_is_vf_admin_configured(struct enic *enic, u16 vf_id);

void enic_sriov_misconfig_detected(struct enic *enic);
bool enic_is_sriov_misconfig_detected(struct enic *enic);

u16 enic_sriov_vfs_type(struct enic *enic);

#define ENIC_ASSERT_SRIOV_LOCK(enic) \
	{							\
		ENIC_ASSERT_SRIOV_PF(enic);			\
		lockdep_assert_held(&enic->sriov.pf.vfs_lock);	\
	}

#define ENIC_ASSERT_SRIOV_PF(enic)					\
	WARN(!enic_is_sriov_pf(enic),					\
	     "enic - BUG: PF-only op issued on a !PF vnic at %s (%d)\n",\
	     __FILE__, __LINE__)

#endif /* _ENIC_SRIOV_H_ */
