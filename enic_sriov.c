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
#include "enic.h"
#include "enic_sriov.h"
#include "enic_dev.h"
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>

static int enic_disable_sriov_v2(struct pci_dev *pdev);
static void enic_vfs_link_state_updt_work(struct work_struct *work);

static inline struct enic *enic_pf_to_enic(struct enic_pf *enic_pf)
{
	struct enic_sriov *enic_sriov;
	struct enic *enic;

	enic_sriov = container_of(enic_pf, struct enic_sriov, pf);
	enic = container_of(enic_sriov, struct enic, sriov);

	return enic;
}

u16 enic_sriov_vfs_type(struct enic *enic)
{
	if (enic_is_sriov_pf(enic))
		return enic->sriov.pf.vfs_type;
	return 0;
}

inline int enic_is_pf(struct enic *enic)
{
	return enic_dev_id(enic) == PCI_DEVICE_ID_CISCO_VIC_ENET;
}

/* Check for a PF configured with any kind of VF
 */
inline int enic_is_sriov_pf(struct enic *enic)
{
	return ((enic_is_pf(enic)) && (enic->sriov.pf.total_num_vfs > 0));
}


/* Check for a PF configured with VF_V2 VFs
 */
inline int enic_is_sriov_v2_pf(struct enic *enic)
{
	return ((enic_is_sriov_pf(enic)) &&
		(enic_sriov_vfs_type(enic) == ENIC_VF_V2));
}

/* V1 VFs can be used only with VMFEX style SRIOV: they require a port
 *  profile (802.1Qbh) to be enabled
 */
inline int enic_is_vf_v1(struct enic *enic)
{
	return enic_dev_id(enic) == PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V1;
}

/* V2 VFs can be used with standard SRIOV and do not require 802.1Qbh
 */
inline int enic_is_vf_v2(struct enic *enic)
{
	return enic_dev_id(enic) == PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V2;
}

inline int enic_is_dynamic(struct enic *enic)
{
	return enic_dev_id(enic) == PCI_DEVICE_ID_CISCO_VIC_ENET_DYN;
}

inline enum enic_vnic_type enic_get_vnic_type_from_id(u16 dev_id)
{
	switch (dev_id) {
	case PCI_DEVICE_ID_CISCO_VIC_ENET:
		return ENIC_PF;
	case PCI_DEVICE_ID_CISCO_VIC_ENET_DYN:
		return ENIC_DYN;
	case PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V1:
		return ENIC_VF_V1;
	case PCI_DEVICE_ID_CISCO_VIC_ENET_VF_USNIC:
		return ENIC_VF_USNIC;
	case PCI_DEVICE_ID_CISCO_VIC_ENET_VF_V2:
		return ENIC_VF_V2;
	default:
		return ENIC_UNKNOWN;
	}
}

inline enum enic_vnic_type enic_get_vnic_type(struct enic *enic)
{
	return enic_get_vnic_type_from_id(enic_dev_id(enic));
}

static const char enic_vnic_types_str[ENIC_TYPE_MAX][8] = {
	"UNKNOWN",
	"PF",
	"DYN",
	"VF_V1",
	"VF_USNIC",
	"VF_V2",
};

const char *enic_sriov_vnic_type_to_str(struct enic *enic)
{
	return enic_vnic_types_str[enic_get_vnic_type(enic)];
}

int enic_sriov_register(struct enic *enic)
{
	struct net_device *netdev =  enic->netdev;
	int err = 0;

	if (!enic_is_vf_v2(enic))
		return 0;

	err = enic_mbox_tx_msg_vf_register(enic, ENIC_DST_PARENT_PF);
	if (err)
		netdev_err(netdev, "%s - failed, err = %d\n",
			__func__, err);

	return err;
}

int enic_sriov_unregister(struct enic *enic)
{
	struct net_device *netdev =  enic->netdev;
	int err = 0;
	int i;

	if (!enic_is_vf_v2(enic))
		return 0;

	err = enic_mbox_tx_msg_vf_unregister(enic, ENIC_DST_PARENT_PF);
	if (err) {
		netdev_err(netdev, "%s - Unable to properly unregister SRIOV\n",
			   __func__);
		return err;
	}

	for (i = 0; i < 5; i++) {
		if (wait_for_completion_timeout(&enic->sriov.vf_v2.unregister_comp,
						2 * HZ))
			goto out;

		netdev_warn(netdev, "%s - timeout(%d/5): PF has not replied to VF_UNREGISTER request\n",
			    __func__, i + 1);
		cond_resched();
	}
	err = -ETIMEDOUT;

out:
	return err;
}

inline int enic_is_sriov_v1_enabled(struct enic *enic)
{
	return (enic->priv_flags & ENIC_SRIOV_ENABLED) ? 1 : 0;
}

/* While enic_is_sriov_pf() checks whether a vnic is a PF with SRIOV configured
 * in HW (regardless of software configuration), enic_is_sriov_v2_enabled()
 * checks whether SRIOV has been configured in SW (ie, whether any VF has been
 * created in the host).
 */
inline int enic_is_sriov_v2_enabled(struct enic *enic)
{
	return (enic->priv_flags & ENIC_SRIOV_ENABLED_V2) ? 1 : 0;
}

inline int enic_is_sriov_usnic_enabled(struct enic *enic)
{
	return (enic->priv_flags & ENIC_SRIOV_ENABLED_USNIC) ? 1 : 0;
}


inline int enic_is_valid_vf(struct enic *enic, int vf)
{
#ifdef CONFIG_PCI_IOV
	return vf >= 0 && vf < enic->sriov.pf.num_vfs;
#else
	return 0;
#endif
}

static int enic_enable_sriov_legacy_modes(struct pci_dev *pdev, u16 en_flag)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic = netdev_priv(netdev);
	int err;

	ENIC_ASSERT_SRIOV_PF(enic);

	err = pci_enable_sriov(pdev, enic->sriov.pf.total_num_vfs);
	if (err) {
		dev_err(&pdev->dev, "SRIOV enable failed, aborting. pci_enable_sriov() returned %d\n",
			err);
		return err;
	}

	enic->priv_flags |= en_flag;
	return 0;
}

static void enic_disable_sriov_legacy_modes(struct pci_dev *pdev, u16 en_flag)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic = netdev_priv(netdev);

	ENIC_ASSERT_SRIOV_PF(enic);

	pci_disable_sriov(pdev);
	enic->priv_flags &= ~en_flag;
}

static int enic_enable_sriov_v1(struct pci_dev *pdev)
{
	return enic_enable_sriov_legacy_modes(pdev, ENIC_SRIOV_ENABLED);
}

static void enic_disable_sriov_v1(struct pci_dev *pdev)
{
	enic_disable_sriov_legacy_modes(pdev, ENIC_SRIOV_ENABLED);
}

static int enic_enable_sriov_usnic(struct pci_dev *pdev)
{
	return enic_enable_sriov_legacy_modes(pdev, ENIC_SRIOV_ENABLED_USNIC);
}

static void enic_disable_sriov_usnic(struct pci_dev *pdev)
{
	enic_disable_sriov_legacy_modes(pdev, ENIC_SRIOV_ENABLED_USNIC);
}

/* RTNL not needed */
static void enic_vfs_stats_poller_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct enic_pf *enic_pf = container_of(delayed_work, struct enic_pf,
					       vfs_stats_poller_work);
	struct enic *enic = enic_pf_to_enic(enic_pf);
	struct net_device *netdev = enic->netdev;
	int num_vfs = enic->sriov.pf.num_vfs;
	int err = 0;
	int i;

	enic_sriov_lock_bh(enic);

	if (enic->sriov.pf.num_vfs_registered == 0) {
		/* DEBUG ONLY */
		netdev_info(netdev, "%s - Stopping the VFs stats poller\n",
			    __func__);

		enic_sriov_unlock_bh(enic);
		return;
	}

	for (i = 0; i < num_vfs; i++) {
		if (enic_is_vf_registered_locked(enic, i)) {
			err = enic_mbox_tx_pf_get_stats_request_locked(enic, i);
			if (err)
				netdev_err(netdev, "%s - Unable to fetch stats from VF%d\n",
					   __func__, i);
		}
	}

	schedule_delayed_work(&enic->sriov.pf.vfs_stats_poller_work, 1 * HZ);

	enic_sriov_unlock_bh(enic);
}

static void enic_sriov_uninit_admin_res_count(struct enic *enic)
{
	enic->rq_count[ENIC_ADMIN_QP] = 0;
	enic->wq_count[ENIC_ADMIN_QP] = 0;
	enic->cq_count[ENIC_ADMIN_QP] = 0;
}

static void enic_sriov_init_admin_res_count(struct enic *enic)
{
	if (enic_is_sriov_v2_pf(enic) || enic_is_vf_v2(enic)) {
		enic->rq_count[ENIC_ADMIN_QP] = 1;
		enic->wq_count[ENIC_ADMIN_QP] = 1;
		enic->cq_count[ENIC_ADMIN_QP] = 2;
	} else
		enic_sriov_uninit_admin_res_count(enic);
}

static int enic_alloc_pp_array(struct enic *enic, u16 size)
{
#ifdef IFLA_VF_PORT_MAX
	enic->pp = kcalloc(size, sizeof(*enic->pp), GFP_KERNEL);
	if (!enic->pp)
		return -ENOMEM;
#endif
	return 0;
}

static int enic_sriov_init_vfs_core(struct enic *enic)
{
	struct pci_dev *pdev = enic->pdev;
	struct device *dev = &pdev->dev;
	u16 vfs_type;
	int err = 0;

	ENIC_ASSERT_SRIOV_PF(enic);

	/* VF_V1 and VF_USNIC devices are created at probe time and their
	 * number can not be changed later at run time.
	 * VF_V2 devices can be created (and changed) at run time via /sys
	 * interface.
	 */

	vfs_type = enic->sriov.pf.vfs_type;
	switch (vfs_type) {
	case ENIC_DYN:
		/* Dynamic vnics are not a kind of VF, but they need the
		 * same pp alloc/init as VF_V1 at probe time.
		 */
		err = enic_alloc_pp_array(enic, 1);
		if (err)
			return err;
		break;
	case ENIC_VF_V1:
		if (enic->sriov.pf.total_num_vfs) {
			err = enic_enable_sriov_v1(pdev);
			if (err)
				return err;
			err = enic_alloc_pp_array(enic,
						  enic->sriov.pf.total_num_vfs);
			if (err) {
				enic_disable_sriov_v1(pdev);
				return err;
			}
		}
		enic->sriov.pf.num_vfs = enic->sriov.pf.total_num_vfs;
		break;
	case ENIC_VF_USNIC:
		if (enic->sriov.pf.total_num_vfs) {
			err = enic_enable_sriov_usnic(pdev);
			if (err) {
				enic_disable_sriov_usnic(pdev);
				return err;
			}
		}
		enic->sriov.pf.num_vfs = enic->sriov.pf.total_num_vfs;
		break;
	case ENIC_VF_V2:
		INIT_WORK(&enic->sriov.pf.vfs_link_state_updt_work,
			  enic_vfs_link_state_updt_work);
		INIT_DELAYED_WORK(&enic->sriov.pf.vfs_stats_poller_work,
				  enic_vfs_stats_poller_work);
		enic_dev_mbox_init(enic);
		break;
	default:
		dev_warn(dev, "%s = unknown VF type %u\n",
			 __func__, vfs_type);
	}

	return 0;
}

int enic_sriov_init(struct enic *enic)
{
#ifdef CONFIG_PCI_IOV
	struct pci_dev *pdev = enic->pdev;
	int pos = 0, err = 0;
	u16 vf_dev_id;

	switch (enic_get_vnic_type(enic)) {
	case ENIC_PF:
		pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
		if (!pos)
			return 0;

		pci_read_config_word(pdev, pos + PCI_SRIOV_VF_DID, &vf_dev_id);
		enic->sriov.pf.vfs_type = enic_get_vnic_type_from_id(vf_dev_id);

		/* Get SRIOV features for VF type V2 */
		if (enic->sriov.pf.vfs_type == ENIC_VF_V2) {
			spin_lock_bh(&enic->devcmd_lock);
			err = vnic_dev_get_supported_feature_ver(enic->vdev,
							VIC_FEATURE_SRIOV,
							&enic->sriov.supp_ver,
							&enic->sriov.feat);
			spin_unlock_bh(&enic->devcmd_lock);

			if (err == -ERR_EINVAL) {
				dev_info(enic_get_dev(enic),
					 "Current VIC FW version doesn't have VIC_FEATURE_SRIOV set in FW.\n");

				return 0;
			}

			if (err) { /* devcmd error status other than -EINVAL */
				enic->sriov.pf.vfs_type = 0;
				err = enic_dev_status_to_errno(err);
				dev_err(enic_get_dev(enic),
					"Get Supp SRIOV Feature devcmd failed w/ err %d\n",
					err);

				return err;
			}

			dev_info(enic_get_dev(enic),
				 "SRIOV version %llu configured with features %llx.\n",
				 enic->sriov.supp_ver, enic->sriov.feat);
		}

		pci_read_config_word(pdev, pos + PCI_SRIOV_TOTAL_VF,
				     &enic->sriov.pf.total_num_vfs);
		pci_read_config_word(pdev, pos + PCI_SRIOV_VF_OFFSET,
				     &enic->sriov.pf.vf_offset);
		pci_read_config_word(pdev, pos + PCI_SRIOV_VF_STRIDE,
				     &enic->sriov.pf.vf_stride);
		pci_read_config_word(pdev, pos + PCI_SRIOV_VF_DID,
				     &vf_dev_id);

		enic_sriov_lock_init(enic);

		err = enic_sriov_init_vfs_core(enic);

		break;

	case ENIC_VF_V2:
		init_completion(&enic->sriov.vf_v2.unregister_comp);
		enic_dev_mbox_init(enic);
		break;
	case ENIC_VF_V1:
#ifdef IFLA_VF_PORT_MAX
		enic->pp = kzalloc(sizeof(*enic->pp), GFP_KERNEL);
		/* Allocate structure for port profile */
		if (!enic->pp) {
			err = -ENOMEM;
			goto out;
		}
#endif
		break;
	case ENIC_DYN:
	default:
		/* Nothing to do */
		break;
	}
out:
	if (!err)
		enic_sriov_init_admin_res_count(enic);

	return err;

#else /* CONFIG_PCI_IOV */
	return 0;
#endif
}

void enic_sriov_deinit(struct enic *enic)
{
#ifdef CONFIG_PCI_IOV
	struct pci_dev *pdev = enic->pdev;
	int err;

	switch (enic_get_vnic_type(enic)) {
	case ENIC_PF:
		if (enic_is_sriov_v2_enabled(enic)) {
#if (ENIC_HAVE_CANCEL_WORK_SYNC)
			cancel_work_sync(&enic->sriov.pf.vfs_link_state_updt_work);
			cancel_delayed_work_sync(&enic->sriov.pf.vfs_stats_poller_work);
			cancel_work_sync(&enic->sriov.mbox_work);
#else
			flush_scheduled_work();
#endif
			/* err value ignored
			 */
			err = enic_disable_sriov_v2(pdev);
			if (err < 0)
				netdev_err(enic->netdev,
					"enic_disable_sriov_v2 : failed %d\n",
					err);

			enic_dev_mbox_deinit(enic);
			enic_mbox_flush_messages(enic);
		} else if (enic_is_sriov_usnic_enabled(enic)) {
			enic_disable_sriov_usnic(pdev);
		} else if (enic_is_sriov_v1_enabled(enic)) {
			enic_disable_sriov_v1(pdev);
			kfree(enic->pp);
		}
		break;
	case ENIC_VF_V2:
#if (ENIC_HAVE_CANCEL_WORK_SYNC)
		cancel_work_sync(&enic->sriov.mbox_work);
#else
		flush_scheduled_work();
#endif
		enic_stop_admin_qp(enic);
		enic_dev_mbox_deinit(enic);
		enic_mbox_flush_messages(enic);

		kfree(enic->sriov.vf_v2.ethtool_stats);
		enic->sriov.vf_v2.ethtool_stats = NULL;
		enic->sriov.vf_v2.ethtool_sset_count = 0;

		break;
#ifdef IFLA_VF_PORT_MAX
	case ENIC_VF_V1:
		kfree(enic->pp);
		break;
#endif
	default:
		break;
	}
#endif /* CONFIG_PCI_IOV */
}

int enic_is_vf_registered_locked(struct enic *enic, u16 vf_id)
{
	ENIC_ASSERT_SRIOV_LOCK(enic);

	if (!enic->sriov.pf.vfs || !enic_is_valid_vf(enic, vf_id)) {
		netdev_err(enic->netdev, "%s - Invalid vf_id %u\n",
			   __func__, vf_id);
		return 0;
	}

	return enic->sriov.pf.vfs[vf_id].registered;
}

/* Used by VFs to check if their registration with the PF completed successfully
 */
int enic_vf_registered(struct enic *enic)
{
	if (enic_is_vf_v2(enic))
		return !!(enic->sriov.vf_v2.flags & ENIC_VF_REGISTERED);
	return 0;
}

static int enic_sriov_alloc_vfs(struct enic *enic, int num_vfs)
{
	int i;

	ENIC_ASSERT_SRIOV_PF(enic);

	enic->sriov.pf.vfs = kcalloc(num_vfs, sizeof(struct enic_vf),
				     GFP_KERNEL);
	if (!enic->sriov.pf.vfs)
		return -ENOMEM;

	for (i = 0; i < num_vfs; i++) {
		INIT_LIST_HEAD(&enic->sriov.pf.vfs[i].mac_addrs);
		enic->sriov.pf.vfs[i].enic_pf = enic;
	}

	return 0;
}

static void enic_sriov_free_vfs(struct enic *enic)
{
	kfree(enic->sriov.pf.vfs);
	enic->sriov.pf.vfs = NULL;
}

static void enic_sriov_free_vfs_mbox_stats(struct enic *enic)
{
	int num_vfs, i;

	num_vfs = enic->sriov.pf.num_vfs;

	for (i = 0; i < num_vfs; i++) {
		kfree(enic->sriov.pf.vfs[i].mbox_stats);
		enic->sriov.pf.vfs[i].mbox_stats = NULL;
	}

	memset(&enic->sriov.mbox_stats, 0,
	       sizeof(enic->sriov.mbox_stats));
	memset(&enic->sriov.mbox_gen_stats, 0,
	       sizeof(enic->sriov.mbox_gen_stats));
}

static int enic_sriov_alloc_vfs_mbox_stats(struct enic *enic, int num_vfs)
{
	int i;

	for (i = 0; i < num_vfs; i++) {
		enic->sriov.pf.vfs[i].mbox_stats = kcalloc(ENIC_MBOX_MAX,
							   sizeof(struct enic_mbox_msg_stat_t),
							   GFP_KERNEL);
		if (!enic->sriov.pf.vfs[i].mbox_stats) {
			enic_sriov_free_vfs_mbox_stats(enic);
			return -ENOMEM;
		}
	}

	return 0;
}

static int enic_get_vfs_spoofchk_config(struct enic *enic, int num_vfs)
{
	int err = 0, i;
	bool enable;

	ENIC_ASSERT_SRIOV_PF(enic);

	for (i = 0; i < enic->sriov.pf.num_vfs; i++) {
		err = enic_spoofchk_op(enic, i, VNIC_DEVCMD_OP_GET, &enable);
		if (err) {
			netdev_err(enic->netdev,
				   "%s - Unable to get spoofchk config for vf_id %d (err = %d)\n",
				   __func__, i, err);
			goto out;
		}
		enic->sriov.pf.vfs[i].spoofchk = (u8)enable;
	}

out:
	return err;
}

static int enic_get_allowed_vfs_devcmds(struct enic *enic, int num_vfs,
					u64 *bitmap)
{
	int err = 0, i;

	ENIC_ASSERT_SRIOV_PF(enic);

	for (i = 0; i < num_vfs; i++) {
		err =  enic_vf_allowed_list_op(
			enic, i, VNIC_DEVCMD_OP_GET,
			(u64 *) &enic->sriov.pf.vfs[i].dflt_allowed_devcmds,
			4);
		if (err) {
			netdev_err(enic->netdev,
				   "%s - Could not get allowed VF devcmds for vf %d (err = %d)\n",
				   __func__, i, err);
			return err;
		}
	}

	for (i = 0; i < num_vfs; i++)
		bitmap_copy((unsigned long *)&enic->sriov.pf.vfs[i].allowed_devcmds,
			(unsigned long *)&enic->sriov.pf.vfs[i].dflt_allowed_devcmds,
			(unsigned int)ENIC_ALLOWED_DEVCMD_BITMAP_SIZE);

	return err;
}

static void enic_init_allowed_vfs_devcmds(struct enic *enic, int num_vfs)
{
	int i, j;

	ENIC_ASSERT_SRIOV_PF(enic);

	for (i = 0; i < num_vfs; i++) {
		bitmap_zero((unsigned long *)&enic->sriov.pf.vfs[i].allowed_devcmds,
			    (unsigned int)ENIC_ALLOWED_DEVCMD_BITMAP_SIZE);

		for (j = 0; j < NUM_ENIC_VF_V2_ALLOWED_DEVCMDS; j++)
			set_bit(enic_vf_v2_devcmds[j],
				(unsigned long *)&enic->sriov.pf.vfs[i].allowed_devcmds);
	}
}

static int enic_set_allowed_vf_devcmds(struct enic *enic, int vf_id)
{
	ENIC_ASSERT_SRIOV_PF(enic);

	return enic_vf_allowed_list_op(enic, vf_id, VNIC_DEVCMD_OP_SET,
				       (u64 *)&enic->sriov.pf.vfs[vf_id].allowed_devcmds,
				       4);
}

static int enic_set_allowed_vfs_devcmds(struct enic *enic, int num_vfs)
{
	int err = 0, i;

	ENIC_ASSERT_SRIOV_PF(enic);

	for (i = 0; i < num_vfs; i++) {

		err = enic_set_allowed_vf_devcmds(enic, i);
		if (err) {
			netdev_err(enic->netdev,
				   "%s - Could not set allowed VF devcmds for vf %d (err = %d)\n",
				   __func__, i, err);
			return err;
		}
	}

	return err;
}

static int enic_get_local_fwd_config(struct enic *enic, bool *enable)
{
	ENIC_ASSERT_SRIOV_PF(enic);

	return enic_local_fwd_op(enic, VNIC_DEVCMD_OP_GET, enable);
}

static void enic_vfs_link_state_updt_work(struct work_struct *work)
{
	struct enic_pf *enic_pf = container_of(work, struct enic_pf,
					       vfs_link_state_updt_work);
	struct enic *enic = enic_pf_to_enic(enic_pf);
	struct net_device *netdev = enic->netdev;
	int num_failed = 0;
	int num_vfs, i;
	int err = 0;

	rtnl_lock();
	enic_sriov_lock_bh(enic);

	if (!enic_is_sriov_v2_enabled(enic))
		goto out;

	num_vfs = enic->sriov.pf.num_vfs;
	for (i = 0; i < num_vfs; i++) {
		if (enic->sriov.pf.vfs[i].link_state_mode == IFLA_VF_LINK_STATE_AUTO) {
			netdev_info(netdev, "%s - updating link for VF#%u\n",
				    __func__, i);
			err = enic_mbox_tx_link_state_notif_locked(enic, i);
			if (err)
				num_failed++;
		}
	}

out:
	enic_sriov_unlock_bh(enic);
	rtnl_unlock();

	if (num_failed)
		netdev_warn(netdev, "%s - link state update failed for %d VFs\n",
			    __func__, num_failed);
}

static int enic_sriov_read_vf_macs(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	int num_vfs = enic->sriov.pf.num_vfs;
	int err, num_err = 0, i;

	ENIC_ASSERT_SRIOV_PF(enic);

	netdev_info(netdev, "VFs station addresses at PF probe time:\n");

	for (i = 0; i < num_vfs; i++) {
		ENIC_DEVCMD_PROXY_BY_INDEX(i, err, enic,
					   vnic_dev_get_mac_addr,
					   enic->sriov.pf.vfs[i].mac_addr);
		if (err) {
			netdev_err(netdev, "%s - Error getting mac for vf %d (err = %d)\n",
				   __func__, i, err);
			num_err++;
		} else
			/* No need to add this address to the VF's address list
			 * because it will happen later when the VF will
			 * register this MAC at probe time.
			 */
			netdev_info(netdev, "- VF %2d Default MAC = %pM\n",
				    i, enic->sriov.pf.vfs[i].mac_addr);
	}

	return num_err;
}

#define ENIC_NO_VLAN 0xFFFF
static int enic_get_vfs_vlan_mode(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	int num_errs = 0, err = 0;
	u16 vlan_id;
	int i;

	ENIC_ASSERT_SRIOV_PF(enic);

	for (i = 0; i < enic->sriov.pf.num_vfs; i++) {
		err = enic_vf_access_vlan_op(enic, VNIC_DEVCMD_OP_GET, i,
					     &vlan_id);
		if (err) {
			netdev_err(netdev, "%s - Error getting vlan mode for vf %d (err = %d)\n",
				   __func__, i, err);
			num_errs++;
		} else {
			if (vlan_id != (u16)ENIC_NO_VLAN)
				enic->sriov.pf.vfs[i].vlan_id = vlan_id;
		}
	}

	return num_errs;
}

int enic_sriov_stats_capability(struct enic *enic)
{
	struct vnic_sriov_stats *stats = NULL;
	int err = 0;

	if (enic_is_vf_v2(enic) || enic_is_sriov_v2_pf(enic)) {

		err = enic_dev_sriov_stats(enic, &stats);
		if (err)
			goto out;

		if (enic_is_vf_v2(enic))
			enic->sriov.vf_v2.vf_id = stats->vf_index;

		if (stats->sriov_host_rx_stats)
			enic->priv_flags |= ENIC_SW_RX_STATS;
		else
			enic->priv_flags &= ~ENIC_SW_RX_STATS;
	}

out:
	return err;
}

bool enic_sriov_requirements_ok(struct enic *enic, bool log_err, int *err)
{
	struct net_device *netdev = enic->netdev;
	struct pci_dev *pdev = enic->pdev;
	enum vnic_dev_intr_mode intr_mode;

	intr_mode = vnic_dev_get_intr_mode(enic->vdev);
	if (intr_mode != VNIC_DEV_INTR_MODE_MSIX) {
		if (log_err)
			netdev_err(netdev, "%s - MSIx required (current intr mode = %u)\n",
				   __func__, intr_mode);
		*err = -EOPNOTSUPP;
		return false;
	}

	if ((enic_get_vnic_type(enic) == ENIC_PF) &&
	    (!pci_ari_enabled(pdev->bus))) {
		if (log_err)
			netdev_err(netdev, "%s - Error: Missing ARI support\n",
				   __func__);
		*err = -EPERM;
		return false;
	}

	if (enic_is_sriov_misconfig_detected(enic)) {
		netdev_err(netdev, "%s - Misconfig detected: SRIOV can not be configured\n",
			   __func__);
		*err = -EOPNOTSUPP;
		return false;
	}

	return true;
}

static int enic_enable_sriov_v2(struct pci_dev *pdev, int num_vfs)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic = netdev_priv(netdev);
	bool pf_only = true;
	bool enable;
	int err = 0;
	int i;

	if (enic_is_sriov_v2_enabled(enic)) {
		netdev_err(netdev, "%s - SRIOV already enabled\n", __func__);
		return -1;
	}

	if ((num_vfs < 0) || (num_vfs > enic->sriov.pf.total_num_vfs)) {
		netdev_err(netdev, "%s - Error: num_vfs=%d not in the allowed range 0-%d\n",
			   __func__, num_vfs, enic->sriov.pf.total_num_vfs - 1);
		return -EINVAL;
	}

	if (!enic_sriov_requirements_ok(enic, true, &err))
		return -1;

	err = pci_vfs_assigned(pdev);
	if (err) {
		/* This is not supposed to be possible given that SRIOV is
		 * disabled
		 */
		netdev_err(netdev, "%s - BUG: %d VFs are assigned to VMs\n",
			   __func__, err);
		return -EBUSY;
	}

	err = enic_sriov_alloc_vfs(enic, num_vfs);
	if (err) {
		netdev_err(netdev, "%s - Error: could not allocate VFs array\n",
			   __func__);
		return err;
	}

	err = enic_sriov_alloc_vfs_mbox_stats(enic, num_vfs);
	if (err) {
		netdev_err(netdev, "%s - Could not enable SRIOV mbox stats (err = %d)\n",
			   __func__, err);
		goto free_vfs;
	}

	if (!enic_hw_rx_stats_ok(enic)) {
		for (i = 0; i < num_vfs; i++)
			u64_stats_init(&enic->sriov.pf.vfs[i].syncp);
	}

	err = enic_get_allowed_vfs_devcmds(enic, num_vfs, NULL);
	if (err) {
		netdev_err(netdev, "%s - Could not get default allowed VFs devcmds (err = %d)\n",
			   __func__, err);
		goto free_vfs_mbox_stats;
	}

	enic_init_allowed_vfs_devcmds(enic, num_vfs);
	err = enic_set_allowed_vfs_devcmds(enic, num_vfs);
	if (err) {
		netdev_err(netdev, "%s - Could not set default allowed VFs devcmds (err = %d)\n",
			   __func__, err);
		goto free_vfs_mbox_stats;
	}

	err = enic_get_vfs_spoofchk_config(enic, num_vfs);
	if (err) {
		netdev_err(netdev, "%s - Unable to get spoofchk config for all VFs (err = %d)\n",
				__func__, err);
		goto free_vfs_mbox_stats;
	}

	err = enic_get_local_fwd_config(enic, &enable);
	if (err) {
		netdev_err(netdev, "%s - Unable to get local fwd config (err = %d)\n",
			   __func__, err);
		goto free_vfs_mbox_stats;
	}
	if (!enable) {
		enable = true;
		err = enic_local_fwd_op(enic, VNIC_DEVCMD_OP_SET, &enable);
		if (err) {
			netdev_err(netdev, "%s - unable to %s local fwd config (err = %d)\n",
				   __func__, enable ? "enable" : "disable",
				   err);
			goto free_vfs_mbox_stats;
		}
	}
	enic->sriov.flags |= ENIC_SRIOV_LOCAL_FWD;

	err = enic_sriov_read_vf_macs(enic);
	if (err)
		goto free_vfs_mbox_stats;

	err = enic_get_vfs_vlan_mode(enic);
	if (err)
		goto free_vfs_mbox_stats;

	/* Tell FW that we want the link states updates UP/DOWN to only
	 * be delivered to the PF.
	 */
	err = enic_link_status_notify_mode(enic, VNIC_DEVCMD_OP_SET,
					   ENIC_DST_PARENT_PF, &pf_only);
	if (err) {
		netdev_err(netdev, "%s - unable to set link state notifcation mode to PF-only\n",
			   __func__);
		goto free_vfs_mbox_stats;
	}

	enic->sriov.pf.num_vfs = num_vfs;

	/* Setting the flag here (instead of at the end of the fn)
	 * because checked by enic_open_admin_qp()
	 */
	enic->priv_flags |= ENIC_SRIOV_ENABLED_V2;

	err = enic_open_admin_qp(enic);
	if (err) {
		netdev_err(netdev, "%s - Unable to open the ADMIN QP (err = %d)\n",
			   __func__, err);
		goto clear_sriov_flag;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		netdev_err(netdev, "%s - Error: pci_enable_sriov() failed (err = %d)\n",
			   __func__, err);
		goto close_admin_qp;
	}

	return 0;

close_admin_qp:
	enic_stop_admin_qp(enic); /* ignoring ret value */
clear_sriov_flag:
	enic->priv_flags &= ~ENIC_SRIOV_ENABLED_V2;
free_vfs_mbox_stats:
	enic_sriov_free_vfs_mbox_stats(enic);
free_vfs:
	enic_sriov_free_vfs(enic);
	return err;
}

static int enic_disable_sriov_v2(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic = netdev_priv(netdev);
	int err;

	if (!enic_is_sriov_v2_enabled(enic)) {
		/* DEBUG ONLY */
		netdev_info(netdev, "%s - SRIOV already disabled\n", __func__);
		return 0;
	}

	err = pci_vfs_assigned(pdev);
	if (err) {
		netdev_err(netdev, "%s - %d VFs are assigned\n",
			   __func__, err);
		return -EBUSY;
	}

	pci_disable_sriov(pdev);
	enic_stop_admin_qp(enic);
	enic_sriov_free_vfs_mbox_stats(enic);
	enic_sriov_free_vfs(enic);

	enic->priv_flags &= ~ENIC_SRIOV_ENABLED_V2;

	return err;
}

/* out: < 0  -> Error code
 *      >= 0 -> num_vfs created (0 when disabling SRIOV)
 */
int enic_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct enic *enic = netdev_priv(netdev);
	int err = 0;

	/* Only VF_V2 VFs can be configured at run-time via /sys.
	 * VF_V1 and usnic VFs are statically created at probe time.
	 */
	if (!enic_is_sriov_v2_pf(enic))
		return -EOPNOTSUPP;

	if (num_vfs == enic->sriov.pf.num_vfs)
		goto out;

	/* Going from num_vfs=0 to num_vfs!=0
	 */
	if (num_vfs) {
		if (enic_is_sriov_v2_enabled(enic)) {
			dev_err(&pdev->dev, "%s - Can't create %d VFs: SRIOV already enabled  with %d VFs\n",
				__func__, num_vfs, enic->sriov.pf.num_vfs);
			err = -EBUSY;
			goto out;
		}

		enic->sriov.pf.num_vfs = num_vfs;

		err = enic_enable_sriov_v2(pdev, num_vfs);
		if (err) {
			dev_err(&pdev->dev, "%s - err = %d\n", __func__, err);
			goto out;
		}
	} else {
		/* Going from num_vfs>0 to num_vfs=0
		 */
		err = enic_disable_sriov_v2(pdev);
		if (err) {
			dev_err(&pdev->dev, "%s - err = %d\n", __func__, err);
			goto out;
		}

		enic->sriov.pf.num_vfs = 0;
	}
out:
	return ((err) ? err : num_vfs);
}

void enic_sriov_lock_init(struct enic *enic)
{
	spin_lock_init(&(enic->sriov.pf.vfs_lock));
}

int enic_sriov_trylock_bh(struct enic *enic)
{
	if (enic_is_sriov_pf(enic))
		return spin_trylock_bh(&enic->sriov.pf.vfs_lock);

	/* VF_V2 vnics do not use/need this lock */
	return 1;
}

void enic_sriov_lock_bh(struct enic *enic)
{
	if (enic_is_sriov_pf(enic))
		spin_lock_bh(&enic->sriov.pf.vfs_lock);
}

void enic_sriov_unlock_bh(struct enic *enic)
{
	if (enic_is_sriov_pf(enic))
		spin_unlock_bh(&enic->sriov.pf.vfs_lock);
}

int enic_sriov_ndo_check_and_lock(struct net_device *netdev, int vf)
{
	struct enic *enic = netdev_priv(netdev);
	int err = 0;

	if (atomic_read(&enic->remove_in_progress))
		return -ENODEV;

	if (!enic_is_sriov_v2_pf(enic))
		return -EOPNOTSUPP;

	if (!enic_sriov_trylock_bh(enic)) {
		netdev_info(netdev, "%s VF %d - sriov config momentarily busy\n",
			    __func__, vf);
		return -EAGAIN;
	}

	if (!enic_is_sriov_v2_enabled(enic)) {
		netdev_err(netdev, "%s - SRIOV is not enabled. Please create the VFs first\n",
			   __func__);
		err = -EINVAL;
		goto out;
	}

	if (!enic_is_valid_vf(enic, vf)) {
		netdev_err(netdev, "%s - Invalid VF ID %d (allowed range: 0 - %u)\n",
			   __func__, vf, enic->sriov.pf.num_vfs - 1);
		err = -EINVAL;
		goto out;
	}

	return 0;

out:
	enic_sriov_unlock_bh(enic);
	return err;
}

#if (VIC_HAVE_NDO_SET_VF_LINK_STATE) || (VIC_HAVE_EXT_NDO_SET_VF_LINK_STATE)

/* In the link down case, the PF does enforce it by disabling the VF and
 * making sure the VF can not re-enable itself.
 * In the link up case, there is no real enforcement, meaning that a non
 * cooperating VF can ignore the MBOX link-up notification and shoot itself
 * into the foot, but it won't be able to do anything harmfull with the device.
 */
int enic_sriov_enforce_vf_link_state(struct enic *enic, int vf,
				     bool enforce_down)
{
	struct net_device *netdev = enic->netdev;
	unsigned long *vf_devcmds;
	int err = 0;

	ENIC_ASSERT_SRIOV_PF(enic);

	vf_devcmds = (unsigned long *)&enic->sriov.pf.vfs[vf].allowed_devcmds;

	/* _CMD_N(CMD_ENABLE) = _CMD_N(CMD_ENABLE_WAIT) = 28.
	 * Doing it twice just for correctness.
	 */
	if (!enforce_down) {
		/* Add back CMD_ENABLE/CMD_ENABLE_WAIT to VF devcmd whitelist
		 */
		set_bit(_CMD_N(CMD_ENABLE), vf_devcmds);
		set_bit(_CMD_N(CMD_ENABLE_WAIT), vf_devcmds);
	/* enforcement needed */
	} else {
		/* Remove CMD_ENABLE/CMD_ENABLE_WAIT from VF devcmd whitelist
		 */
		clear_bit(_CMD_N(CMD_ENABLE), vf_devcmds);
		clear_bit(_CMD_N(CMD_ENABLE_WAIT), vf_devcmds);
	}
	err = enic_vf_allowed_list_op(enic, vf, VNIC_DEVCMD_OP_SET,
				      (u64 *)&enic->sriov.pf.vfs[vf].allowed_devcmds,
				      4);
	if (err) {
		netdev_err(netdev, "%s - unable to modify VF %d devcmd whitelist (err = %d)\n",
			   __func__, vf, err);
		goto out;
	}

	/* When the enforcement is not needed (ie, for the link UP case), the
	 * mbox notification to the VF is sufficient.
	 */
	ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic,
				  ((enforce_down) ? vnic_dev_disable :
						    vnic_dev_enable));
	if (err)
		netdev_err(netdev, "%s - unable to %s VF %d (devcmd err = %d)\n",
			   __func__, (enforce_down) ? "disable" : "enable",
			    vf, err);
	else
		netdev_info(netdev, "%s - %s VF %d\n", __func__,
			    (enforce_down) ? "enabled" : "disabled", vf);
out:
	return err;
}

#endif

bool enic_is_vf_admin_mac_matching(struct enic *enic, u16 vf_id, u8 *mac)
{
	u8 *admin_mac_addr;

	admin_mac_addr = enic->sriov.pf.vfs[vf_id].mac_addr;
	return ether_addr_equal(admin_mac_addr, mac);
}

bool enic_is_vf_admin_mac_configured(struct enic *enic, u16 vf_id)
{
	u8 *admin_mac_addr;

	admin_mac_addr = enic->sriov.pf.vfs[vf_id].mac_addr;
	if (!is_zero_ether_addr(admin_mac_addr))
		return true;

	return false;
}

/* This will later be extended to include other configs
 */
bool enic_is_vf_admin_configured(struct enic *enic, u16 vf_id)
{
	return enic_is_vf_admin_mac_configured(enic, vf_id);
}

void enic_sriov_misconfig_detected(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;

	if (enic_is_sriov_pf(enic))
		enic->sriov.pf.misconfig_detected = 1;
	else
		netdev_err(netdev, "%s - %s is not a PF\n",
			   __func__, netdev->name);
}

bool enic_is_sriov_misconfig_detected(struct enic *enic)
{
	if (enic_is_sriov_pf(enic))
		return (enic->sriov.pf.misconfig_detected & 1);
	else
		return 0;
}
