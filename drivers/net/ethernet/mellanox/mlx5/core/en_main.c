/*
 *
 * Copyright (c) 2015-2016, Mellanox Technologies. All rights reserved.
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

#ifdef CONFIG_MLX5_CLS_ACT
#include <net/tc_act/tc_gact.h>
#endif
#include <linux/mlx5/fs.h>
#include <net/switchdev.h>
#if defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON)
#include <net/vxlan.h>
#endif
#include <net/geneve.h>
#include <linux/bpf.h>
#include <linux/if_bridge.h>
#ifdef HAVE_BASECODE_EXTRAS
#include <linux/irq.h>
#endif
#include <linux/filter.h>
#ifdef HAVE_NET_PAGE_POOL_H
#include <net/page_pool.h>
#endif
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_XDP_SOCK_DRV_H
#include <net/xdp_sock_drv.h>
#else
#include <net/xdp_sock.h>
#endif
#endif
#include "eswitch.h"
#include "en.h"
#include "en/txrx.h"
#include "en_tc.h"
#include "en_rep.h"
#include "en_accel/ipsec.h"
#include "en_accel/macsec.h"
#include "en_accel/en_accel.h"
#include "en_accel/ktls.h"
#if defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON)
#include "lib/vxlan.h"
#endif
#include "lib/clock.h"
#include "en/port.h"
#include "en/xdp.h"
#include "lib/eq.h"
#include "en/monitor_stats.h"
#include "en/health.h"
#include "en/params.h"
#include "en/xsk/pool.h"
#include "en/xsk/setup.h"
#include "en/xsk/rx.h"
#include "en/xsk/tx.h"
#include "en/hv_vhca_stats.h"
#include "en/devlink.h"
#include "lib/mlx5.h"
#include "en/ptp.h"
#include "en/htb.h"
#include "qos.h"
#include "en/trap.h"
#include "compat.h"

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev, u8 page_shift,
					    enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u16 umr_wqebbs, max_wqebbs;
	bool striding_rq_umr;

	striding_rq_umr = MLX5_CAP_GEN(mdev, striding_rq) && MLX5_CAP_GEN(mdev, umr_ptr_rlky) &&
			  MLX5_CAP_ETH(mdev, reg_umr_sq);
	if (!striding_rq_umr)
		return false;

	umr_wqebbs = mlx5e_mpwrq_umr_wqebbs(mdev, page_shift, umr_mode);
	max_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);
	/* Sanity check; should never happen, because mlx5e_mpwrq_umr_wqebbs is
	 * calculated from mlx5e_get_max_sq_aligned_wqebbs.
	 */
	if (WARN_ON(umr_wqebbs > max_wqebbs))
		return false;

	return true;
}

void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;
#ifdef HAVE_NETIF_CARRIER_EVENT
	bool up;
#endif

	port_state = mlx5_query_vport_state(mdev,
					    MLX5_VPORT_STATE_OP_MOD_VNIC_VPORT,
					    0);
#ifdef HAVE_NETIF_CARRIER_EVENT
	up = port_state == VPORT_STATE_UP;
	if (up == netif_carrier_ok(priv->netdev))
		netif_carrier_event(priv->netdev);
	if (up) {
#else
	if (port_state == VPORT_STATE_UP) {
#endif
		netdev_info(priv->netdev, "Link up\n");
		netif_carrier_on(priv->netdev);
	} else {
		netdev_info(priv->netdev, "Link down\n");
		netif_carrier_off(priv->netdev);
	}
}

static void mlx5e_update_carrier_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       update_carrier_work);

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		if (priv->profile->update_carrier)
			priv->profile->update_carrier(priv);
	mutex_unlock(&priv->state_lock);
}

static void mlx5e_update_stats_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       update_stats_work);

	mutex_lock(&priv->state_lock);
	priv->profile->update_stats(priv);
	mutex_unlock(&priv->state_lock);
}

static int mlx5_core_set_delay_drop(struct mlx5_core_dev *dev,
		u32 timeout_usec)
{
	u32 in[MLX5_ST_SZ_DW(set_delay_drop_params_in)] = {};

	MLX5_SET(set_delay_drop_params_in, in, opcode,
			MLX5_CMD_OP_SET_DELAY_DROP_PARAMS);
	MLX5_SET(set_delay_drop_params_in, in, delay_drop_timeout,
			timeout_usec / 100);
	return mlx5_cmd_exec_in(dev, set_delay_drop_params, in);
}

static void mlx5e_delay_drop_handler(struct work_struct *work)
{
	struct mlx5e_delay_drop *delay_drop =
		container_of(work, struct mlx5e_delay_drop, work);
	struct mlx5e_priv *priv = container_of(delay_drop, struct mlx5e_priv,
					       delay_drop);
	int err;

	mutex_lock(&delay_drop->lock);
	err = mlx5_core_set_delay_drop(priv->mdev,
				       delay_drop->usec_timeout);
	if (err) {
		mlx5_core_warn(priv->mdev, "Failed to enable delay drop err=%d\n",
			       err);
		delay_drop->activate = false;
	}
	mutex_unlock(&delay_drop->lock);
}

void mlx5e_queue_update_stats(struct mlx5e_priv *priv)
{
	if (!priv->profile->update_stats)
		return;

	if (unlikely(test_bit(MLX5E_STATE_DESTROYING, &priv->state)))
		return;

	queue_work(priv->wq, &priv->update_stats_work);
}

static int async_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, events_nb);
	struct mlx5_eqe   *eqe = data;

	if (event != MLX5_EVENT_TYPE_PORT_CHANGE &&
	    event != MLX5_EVENT_TYPE_GENERAL_EVENT)
		return NOTIFY_DONE;

	switch (event) {
	case MLX5_EVENT_TYPE_PORT_CHANGE:
		switch (eqe->sub_type) {
		case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
		case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
			queue_work(priv->wq, &priv->update_carrier_work);
			break;
		default:
			return NOTIFY_DONE;
		}
	break;
	case MLX5_EVENT_TYPE_GENERAL_EVENT:
		switch (eqe->sub_type) {
		case MLX5_GENERAL_SUBTYPE_DELAY_DROP_TIMEOUT:
			if (MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_DROPLESS_RQ))
				queue_work(priv->wq, &priv->delay_drop.work);
			break;
		default:
			return NOTIFY_DONE;
		}
	break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static void mlx5e_enable_async_events(struct mlx5e_priv *priv)
{
	priv->events_nb.notifier_call = async_event;
	mlx5_notifier_register(priv->mdev, &priv->events_nb);
}

static void mlx5e_disable_async_events(struct mlx5e_priv *priv)
{
	mlx5_notifier_unregister(priv->mdev, &priv->events_nb);
}

static int blocking_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, blocking_events_nb);
	struct mlx5_devlink_trap_event_ctx *trap_event_ctx = data;
	int err;

	switch (event) {
	case MLX5_DRIVER_EVENT_TYPE_TRAP:
		err = mlx5e_handle_trap_event(priv, trap_event_ctx->trap);
		if (err) {
			trap_event_ctx->err = err;
			return NOTIFY_BAD;
		}
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static void mlx5e_enable_blocking_events(struct mlx5e_priv *priv)
{
	priv->blocking_events_nb.notifier_call = blocking_event;
	mlx5_blocking_notifier_register(priv->mdev, &priv->blocking_events_nb);
}

static void mlx5e_disable_blocking_events(struct mlx5e_priv *priv)
{
	mlx5_blocking_notifier_unregister(priv->mdev, &priv->blocking_events_nb);
}

static u16 mlx5e_mpwrq_umr_octowords(u32 entries, enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 umr_entry_size = mlx5e_mpwrq_umr_entry_size(umr_mode);
	u32 sz;

	sz = ALIGN(entries * umr_entry_size, MLX5_UMR_MTT_ALIGNMENT);

	return sz / MLX5_OCTWORD;
}

static inline void mlx5e_build_umr_wqe(struct mlx5e_rq *rq,
				       struct mlx5e_icosq *sq,
				       struct mlx5e_umr_wqe *wqe)
{
	struct mlx5_wqe_ctrl_seg      *cseg = &wqe->ctrl;
	struct mlx5_wqe_umr_ctrl_seg *ucseg = &wqe->uctrl;
	u16 octowords;
	u8 ds_cnt;

	ds_cnt = DIV_ROUND_UP(mlx5e_mpwrq_umr_wqe_sz(rq->mdev, rq->mpwqe.page_shift,
						     rq->mpwqe.umr_mode),
			      MLX5_SEND_WQE_DS);

	cseg->qpn_ds    = cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
				      ds_cnt);
	cseg->umr_mkey  = rq->mpwqe.umr_mkey_be;

	ucseg->flags = MLX5_UMR_TRANSLATION_OFFSET_EN | MLX5_UMR_INLINE;
	octowords = mlx5e_mpwrq_umr_octowords(rq->mpwqe.pages_per_wqe, rq->mpwqe.umr_mode);
	ucseg->xlt_octowords = cpu_to_be16(octowords);
	ucseg->mkey_mask     = cpu_to_be64(MLX5_MKEY_MASK_FREE);
}

#ifdef HAVE_SHAMPO_SUPPORT
static int mlx5e_rq_shampo_hd_alloc(struct mlx5e_rq *rq, int node)
{
	rq->mpwqe.shampo = kvzalloc_node(sizeof(*rq->mpwqe.shampo),
					 GFP_KERNEL, node);
	if (!rq->mpwqe.shampo)
		return -ENOMEM;
	return 0;
}

static void mlx5e_rq_shampo_hd_free(struct mlx5e_rq *rq)
{
	kvfree(rq->mpwqe.shampo);
}

static int mlx5e_rq_shampo_hd_info_alloc(struct mlx5e_rq *rq, int node)
{
	struct mlx5e_shampo_hd *shampo = rq->mpwqe.shampo;

#ifdef HAVE_BITMAP_ZALLOC_NODE
	shampo->bitmap = bitmap_zalloc_node(shampo->hd_per_wq, GFP_KERNEL,
					    node);
#else
	shampo->bitmap = bitmap_zalloc(shampo->hd_per_wq, GFP_KERNEL);
#endif
	if (!shampo->bitmap)
		return -ENOMEM;

	shampo->info = kvzalloc_node(array_size(shampo->hd_per_wq,
						sizeof(*shampo->info)),
				     GFP_KERNEL, node);
	if (!shampo->info) {
		kvfree(shampo->bitmap);
		return -ENOMEM;
	}
	return 0;
}

static void mlx5e_rq_shampo_hd_info_free(struct mlx5e_rq *rq)
{
	kvfree(rq->mpwqe.shampo->bitmap);
	kvfree(rq->mpwqe.shampo->info);
}
#endif

static int mlx5e_rq_alloc_mpwqe_info(struct mlx5e_rq *rq, int node)
{
	int wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);
	size_t alloc_size;

	alloc_size = array_size(wq_sz, struct_size(rq->mpwqe.info, alloc_units,
						   rq->mpwqe.pages_per_wqe));

	rq->mpwqe.info = kvzalloc_node(alloc_size, GFP_KERNEL, node);
	if (!rq->mpwqe.info)
		return -ENOMEM;

	mlx5e_build_umr_wqe(rq, rq->icosq, &rq->mpwqe.umr_wqe);

	return 0;
}


static u8 mlx5e_mpwrq_access_mode(enum mlx5e_mpwrq_umr_mode umr_mode)
{
	switch (umr_mode) {
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		return MLX5_MKC_ACCESS_MODE_MTT;
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		return MLX5_MKC_ACCESS_MODE_KSM;
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		return MLX5_MKC_ACCESS_MODE_KLMS;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		return MLX5_MKC_ACCESS_MODE_KSM;
	}
	WARN_ONCE(1, "MPWRQ UMR mode %d is not known\n", umr_mode);
	return 0;
}

static int mlx5e_create_umr_mkey(struct mlx5_core_dev *mdev,
				 u32 npages, u8 page_shift, u32 *umr_mkey,
				 dma_addr_t filler_addr,
				 enum mlx5e_mpwrq_umr_mode umr_mode
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				 , u32 xsk_chunk_size
#endif
				)
{
	struct mlx5_mtt *mtt;
	struct mlx5_ksm *ksm;
	struct mlx5_klm *klm;
	u32 octwords;
	int inlen;
	void *mkc;
	u32 *in;
	int err;
	int i;

	if ((umr_mode == MLX5E_MPWRQ_UMR_MODE_UNALIGNED ||
	     umr_mode == MLX5E_MPWRQ_UMR_MODE_TRIPLE) &&
	    !MLX5_CAP_GEN(mdev, fixed_buffer_size)) {
		mlx5_core_warn(mdev, "Unaligned AF_XDP requires fixed_buffer_size capability\n");
		return -EINVAL;
	}

	octwords = mlx5e_mpwrq_umr_octowords(npages, umr_mode);

	inlen = MLX5_FLEXIBLE_INLEN(mdev, MLX5_ST_SZ_BYTES(create_mkey_in),
				    MLX5_OCTWORD, octwords);
	if (inlen < 0)
		return inlen;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, mlx5e_mpwrq_access_mode(umr_mode));
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET64(mkc, mkc, len, npages << page_shift);
	MLX5_SET(mkc, mkc, translations_octword_size, octwords);
	if (umr_mode == MLX5E_MPWRQ_UMR_MODE_TRIPLE)
		MLX5_SET(mkc, mkc, log_page_size, page_shift - 2);
	else if (umr_mode != MLX5E_MPWRQ_UMR_MODE_OVERSIZED)
		MLX5_SET(mkc, mkc, log_page_size, page_shift);
	MLX5_SET(create_mkey_in, in, translations_octword_actual_size, octwords);

	/* Initialize the mkey with all MTTs pointing to a default
	 * page (filler_addr). When the channels are activated, UMR
	 * WQEs will redirect the RX WQEs to the actual memory from
	 * the RQ's pool, while the gaps (wqe_overflow) remain mapped
	 * to the default page.
	 */
	switch (umr_mode) {
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		klm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++) {
			klm[i << 1] = (struct mlx5_klm) {
				.va = cpu_to_be64(filler_addr),
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				.bcount = cpu_to_be32(xsk_chunk_size),
#endif
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
			};
			klm[(i << 1) + 1] = (struct mlx5_klm) {
				.va = cpu_to_be64(filler_addr),
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				.bcount = cpu_to_be32((1 << page_shift) - xsk_chunk_size),
#endif
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
			};
		}
		break;
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		ksm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++)
			ksm[i] = (struct mlx5_ksm) {
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
				.va = cpu_to_be64(filler_addr),
			};
		break;
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		mtt = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++)
			mtt[i] = (struct mlx5_mtt) {
				.ptag = cpu_to_be64(filler_addr),
			};
		break;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		ksm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages * 4; i++) {
			ksm[i] = (struct mlx5_ksm) {
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
				.va = cpu_to_be64(filler_addr),
			};
		}
		break;
	}

	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}

#ifdef HAVE_SHAMPO_SUPPORT
static int mlx5e_create_umr_klm_mkey(struct mlx5_core_dev *mdev,
				     u64 nentries,
				     u32 *umr_mkey)
{
	int inlen;
	void *mkc;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in);

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_KLMS);
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET(mkc, mkc, translations_octword_size, nentries);
	MLX5_SET(mkc, mkc, length64, 1);
	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}
#endif

static int mlx5e_create_rq_umr_mkey(struct mlx5_core_dev *mdev, struct mlx5e_rq *rq)
{
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
	u32 xsk_chunk_size = rq->xsk_pool ? rq->xsk_pool->chunk_size : 0;
#else
#ifdef HAVE_XDP_UMEM_CHUNK_SIZE
	u32 xsk_chunk_size = rq->umem ? rq->umem->chunk_size : 0;
#else
	u32 xsk_chunk_size = rq->umem ? rq->umem->chunk_size_nohr + rq->umem->headroom : 0;
#endif /*HAVE_XDP_UMEM_CHUNK_SIZE */
#endif /* HAVE_NETDEV_BF_XSK_BUFF_POOL*/
#endif /* HAVE_XSK_COPY_SUPPORT*/
	u32 wq_size = mlx5_wq_ll_get_size(&rq->mpwqe.wq);
	u32 num_entries, max_num_entries;
	u32 umr_mkey;
	int err;

	max_num_entries = mlx5e_mpwrq_max_num_entries(mdev, rq->mpwqe.umr_mode);

	/* Shouldn't overflow, the result is at most MLX5E_MAX_RQ_NUM_MTTS. */
	if (WARN_ON_ONCE(check_mul_overflow(wq_size, (u32)rq->mpwqe.mtts_per_wqe,
					    &num_entries) ||
			 num_entries > max_num_entries))
		mlx5_core_err(mdev, "%s: multiplication overflow: %u * %u > %u\n",
			      __func__, wq_size, rq->mpwqe.mtts_per_wqe,
			      max_num_entries);

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	err = mlx5e_create_umr_mkey(mdev, num_entries, rq->mpwqe.page_shift,
				    &umr_mkey, rq->wqe_overflow.addr,
				    rq->mpwqe.umr_mode, xsk_chunk_size);
#else
	err = mlx5e_create_umr_mkey(mdev, num_entries, rq->mpwqe.page_shift,
				    &umr_mkey, rq->wqe_overflow.addr,
				    rq->mpwqe.umr_mode);
#endif

	rq->mpwqe.umr_mkey_be = cpu_to_be32(umr_mkey);
	return err;
}

#ifdef HAVE_SHAMPO_SUPPORT
static int mlx5e_create_rq_hd_umr_mkey(struct mlx5_core_dev *mdev,
				       struct mlx5e_rq *rq)
{
	u32 max_klm_size = BIT(MLX5_CAP_GEN(mdev, log_max_klm_list_size));

	if (max_klm_size < rq->mpwqe.shampo->hd_per_wq) {
		mlx5_core_err(mdev, "max klm list size 0x%x is smaller than shampo header buffer list size 0x%x\n",
			      max_klm_size, rq->mpwqe.shampo->hd_per_wq);
		return -EINVAL;
	}
	return mlx5e_create_umr_klm_mkey(mdev, rq->mpwqe.shampo->hd_per_wq,
					 &rq->mpwqe.shampo->mkey);
}
#endif

static void mlx5e_init_frags_partition(struct mlx5e_rq *rq)
{
	struct mlx5e_wqe_frag_info next_frag = {};
	struct mlx5e_wqe_frag_info *prev = NULL;
	int i;

#ifdef HAVE_NO_REFCNT_BIAS
	if (rq->xsk_pool) {
		/* Assumptions used by XSK batched allocator. */
		WARN_ON(rq->wqe.info.num_frags != 1);
		WARN_ON(rq->wqe.info.log_num_frags != 0);
		WARN_ON(rq->wqe.info.arr[0].frag_stride != PAGE_SIZE);
	}
#endif
	next_frag.au = &rq->wqe.alloc_units[0];

	for (i = 0; i < mlx5_wq_cyc_get_size(&rq->wqe.wq); i++) {
		struct mlx5e_rq_frag_info *frag_info = &rq->wqe.info.arr[0];
		struct mlx5e_wqe_frag_info *frag =
			&rq->wqe.frags[i << rq->wqe.info.log_num_frags];
		int f;

		for (f = 0; f < rq->wqe.info.num_frags; f++, frag++) {
			if (next_frag.offset + frag_info[f].frag_stride > PAGE_SIZE) {
				next_frag.au++;
				next_frag.offset = 0;
				if (prev)
					prev->last_in_page = true;
			}
			*frag = next_frag;

			/* prepare next */
			next_frag.offset += frag_info[f].frag_stride;
			prev = frag;
		}
	}

	if (prev)
		prev->last_in_page = true;
}

static int mlx5e_init_di_list(struct mlx5e_rq *rq, int wq_sz, int node)
{
	int len = wq_sz << rq->wqe.info.log_num_frags;

	rq->wqe.alloc_units = kvzalloc_node(array_size(len, sizeof(*rq->wqe.alloc_units)),
					    GFP_KERNEL, node);
	if (!rq->wqe.alloc_units)
		return -ENOMEM;

	mlx5e_init_frags_partition(rq);

	return 0;
}

static void mlx5e_free_di_list(struct mlx5e_rq *rq)
{
	kvfree(rq->wqe.alloc_units);
}

static void mlx5e_rq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_rq *rq = container_of(recover_work, struct mlx5e_rq, recover_work);

	mlx5e_reporter_rq_cqe_err(rq);
}

static int mlx5e_alloc_mpwqe_rq_drop_page(struct mlx5e_rq *rq)
{
	rq->wqe_overflow.page = alloc_page(GFP_KERNEL);
	if (!rq->wqe_overflow.page)
		return -ENOMEM;

	rq->wqe_overflow.addr = dma_map_page(rq->pdev, rq->wqe_overflow.page, 0,
					     PAGE_SIZE, rq->buff.map_dir);
	if (dma_mapping_error(rq->pdev, rq->wqe_overflow.addr)) {
		__free_page(rq->wqe_overflow.page);
		return -ENOMEM;
	}
	return 0;
}

static void mlx5e_free_mpwqe_rq_drop_page(struct mlx5e_rq *rq)
{
	 dma_unmap_page(rq->pdev, rq->wqe_overflow.addr, PAGE_SIZE,
			rq->buff.map_dir);
	 __free_page(rq->wqe_overflow.page);
}

static int mlx5e_init_rxq_rq(struct mlx5e_channel *c, struct mlx5e_params *params,
			     struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	rq->wq_type      = params->rq_wq_type;
	rq->pdev         = c->pdev;
	rq->netdev       = c->netdev;
	rq->priv         = c->priv;
	rq->tstamp       = c->tstamp;
	rq->clock        = &mdev->clock;
	rq->icosq        = &c->icosq;
	rq->ix           = c->ix;
	rq->channel      = c;
	rq->mdev         = mdev;
	rq->hw_mtu       = MLX5E_SW2HW_MTU(params, params->sw_mtu);
#ifdef HAVE_XDP_SUPPORT
	rq->xdpsq        = &c->rq_xdpsq;
#endif
	rq->stats        = &c->priv->channel_stats[c->ix]->rq;
	rq->ptp_cyc2time = mlx5_rq_ts_translator(mdev);
	if (mlx5_eswitch_mode(mdev) == MLX5_ESWITCH_OFFLOADS &&
	    mlx5e_esw_offloads_pet_enabled(mdev->priv.eswitch)) {
		rq->pet_hdr_size = 8;
	}

	err = mlx5e_rq_set_handlers(rq, params, NULL);
	if (err)
		return err;

#if defined(HAVE_XDP_SUPPORT) && defined(HAVE_XDP_RXQ_INFO)
#ifdef HAVE_XDP_RXQ_INFO_REG_4_PARAMS
	return xdp_rxq_info_reg(&rq->xdp_rxq, rq->netdev, rq->ix, c->napi.napi_id);
#else
	err = xdp_rxq_info_reg(&rq->xdp_rxq, rq->netdev, rq->ix);
#endif
#endif /* HAVE_XDP_SUPPORT && HAVE_XDP_RXQ_INFO*/
	return err;
}

#ifdef HAVE_SHAMPO_SUPPORT
static int mlx5_rq_shampo_alloc(struct mlx5_core_dev *mdev,
				struct mlx5e_params *params,
				struct mlx5e_rq_param *rqp,
				struct mlx5e_rq *rq,
				u32 *pool_size,
				int node)
{
	void *wqc = MLX5_ADDR_OF(rqc, rqp->rqc, wq);
	int wq_size;
	int err;

	if (!test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
		return 0;
	err = mlx5e_rq_shampo_hd_alloc(rq, node);
	if (err)
		goto out;
	rq->mpwqe.shampo->hd_per_wq =
		mlx5e_shampo_hd_per_wq(mdev, params, rqp);
	err = mlx5e_create_rq_hd_umr_mkey(mdev, rq);
	if (err)
		goto err_shampo_hd;
	err = mlx5e_rq_shampo_hd_info_alloc(rq, node);
	if (err)
		goto err_shampo_info;
	rq->hw_gro_data = kvzalloc_node(sizeof(*rq->hw_gro_data), GFP_KERNEL, node);
	if (!rq->hw_gro_data) {
		err = -ENOMEM;
		goto err_hw_gro_data;
	}
	rq->mpwqe.shampo->key =
		cpu_to_be32(rq->mpwqe.shampo->mkey);
	rq->mpwqe.shampo->hd_per_wqe =
		mlx5e_shampo_hd_per_wqe(mdev, params, rqp);
	wq_size = BIT(MLX5_GET(wq, wqc, log_wq_sz));
	*pool_size += (rq->mpwqe.shampo->hd_per_wqe * wq_size) /
		     MLX5E_SHAMPO_WQ_HEADER_PER_PAGE;
	return 0;

err_hw_gro_data:
	mlx5e_rq_shampo_hd_info_free(rq);
err_shampo_info:
	mlx5_core_destroy_mkey(mdev, rq->mpwqe.shampo->mkey);
err_shampo_hd:
	mlx5e_rq_shampo_hd_free(rq);
out:
	return err;
}

static void mlx5e_rq_free_shampo(struct mlx5e_rq *rq)
{
	if (!test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
		return;

	kvfree(rq->hw_gro_data);
	mlx5e_rq_shampo_hd_info_free(rq);
	mlx5_core_destroy_mkey(rq->mdev, rq->mpwqe.shampo->mkey);
	mlx5e_rq_shampo_hd_free(rq);
}
#endif /* HAVE_SHAMPO_SUPPORT */

static void mlx5e_rx_cache_reduce_clean_pending(struct mlx5e_rq *rq)
{
	struct mlx5e_page_cache_reduce *reduce = &rq->page_cache.reduce;
	int i;

	if (!test_bit(MLX5E_RQ_STATE_CACHE_REDUCE_PENDING, &rq->state))
		return;

	for (i = 0; i < reduce->npages; i++)
		mlx5e_page_release_dynamic(rq, &reduce->pending[i], false);

	clear_bit(MLX5E_RQ_STATE_CACHE_REDUCE_PENDING, &rq->state);
}

static void mlx5e_rx_cache_reduce_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlx5e_page_cache_reduce *reduce =
		container_of(dwork, struct mlx5e_page_cache_reduce, reduce_work);
	struct mlx5e_page_cache *cache =
		container_of(reduce, struct mlx5e_page_cache, reduce);
	struct mlx5e_rq *rq = container_of(cache, struct mlx5e_rq, page_cache);

	local_bh_disable();
	napi_schedule(rq->cq.napi);
	local_bh_enable();
	mlx5e_rx_cache_reduce_clean_pending(rq);

	if (ilog2(cache->sz) > cache->log_min_sz)
		schedule_delayed_work_on(smp_processor_id(),
					 dwork, reduce->delay);
}

static int mlx5e_rx_alloc_page_cache(struct mlx5e_rq *rq,
				     int node, u8 log_init_sz)
{
	struct mlx5e_page_cache *cache = &rq->page_cache;
	struct mlx5e_page_cache_reduce *reduce = &cache->reduce;
	u32 max_sz;

	cache->log_max_sz = log_init_sz + MLX5E_PAGE_CACHE_LOG_MAX_RQ_MULT;
	cache->log_min_sz = log_init_sz;
	max_sz = 1 << cache->log_max_sz;

	cache->page_cache = kvzalloc_node(max_sz * sizeof(*cache->page_cache),
					  GFP_KERNEL, node);
	if (!cache->page_cache)
		return -ENOMEM;

	reduce->pending = kvzalloc_node(max_sz * sizeof(*reduce->pending),
					GFP_KERNEL, node);
	if (!reduce->pending)
		goto err_free_cache;

	cache->sz = 1 << cache->log_min_sz;
	cache->head = -1;
	INIT_DELAYED_WORK(&reduce->reduce_work, mlx5e_rx_cache_reduce_work);
	reduce->delay = msecs_to_jiffies(MLX5E_PAGE_CACHE_REDUCE_WORK_INTERVAL);
	reduce->graceful_period = msecs_to_jiffies(MLX5E_PAGE_CACHE_REDUCE_GRACE_PERIOD);
	reduce->next_ts = MAX_JIFFY_OFFSET; /* in init, no reduce is needed */

	return 0;

err_free_cache:
	kvfree(cache->page_cache);

	return -ENOMEM;
}

static void mlx5e_rx_free_page_cache(struct mlx5e_rq *rq)
{
	struct mlx5e_page_cache *cache = &rq->page_cache;
	struct mlx5e_page_cache_reduce *reduce = &cache->reduce;
	int i;

	cancel_delayed_work_sync(&reduce->reduce_work);
	mlx5e_rx_cache_reduce_clean_pending(rq);
	kvfree(reduce->pending);

	for (i = 0; i <= cache->head; i++) {
		struct mlx5e_alloc_unit *au = &cache->page_cache[i];

		mlx5e_page_release_dynamic(rq, au, false);
	}
	kvfree(cache->page_cache);
}

static int mlx5e_alloc_rq(struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk,
			  struct mlx5e_rq_param *rqp,
			  int node, struct mlx5e_rq *rq)
{
#ifdef HAVE_NET_PAGE_POOL_H
	struct page_pool_params pp_params = { 0 };
#endif
	struct mlx5_core_dev *mdev = rq->mdev;
	void *rqc = rqp->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
#if defined(HAVE_XSK_ZERO_COPY_SUPPORT) && !defined(HAVE_XSK_BUFF_ALLOC)
	u32 num_xsk_frames = 0;
#endif
#ifdef HAVE_NET_PAGE_POOL_H
	u32 pool_size;
#endif
	u32 cache_init_sz;
	int wq_sz;
	int err;
	int i;

	rqp->wq.db_numa_node = node;
	INIT_WORK(&rq->recover_work, mlx5e_rq_err_cqe_work);

#ifdef HAVE_XDP_SUPPORT
	if (params->xdp_prog)
#ifndef HAVE_BPF_PROG_ADD_RET_STRUCT
		bpf_prog_inc(params->xdp_prog);
#else
	{
		struct bpf_prog *prog = bpf_prog_inc(params->xdp_prog);
		if (IS_ERR(prog)) {
			err = PTR_ERR(prog);
			goto err_rq_xdp_prog;
		}
	}
#endif /* HAVE_BPF_PROG_ADD_RET_STRUCT */
	RCU_INIT_POINTER(rq->xdp_prog, params->xdp_prog);

	rq->buff.map_dir = params->xdp_prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
#else
	rq->buff.map_dir = DMA_FROM_DEVICE;
#endif
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	rq->buff.headroom = mlx5e_get_rq_headroom(mdev, params, xsk);
#ifndef HAVE_XSK_BUFF_ALLOC
	rq->buff.umem_headroom = xsk ? xsk->headroom : 0;
#endif
#else
	rq->buff.headroom = mlx5e_get_rq_headroom(mdev, params, NULL);
#endif /* HAVE_XSK_ZERO_COPY_SUPPORT */
#ifdef HAVE_NET_PAGE_POOL_H
	pool_size = 1 << params->log_rq_mtu_frames;
#endif

	rq->mkey_be = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey);

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		err = mlx5_wq_ll_create(mdev, &rqp->wq, rqc_wq, &rq->mpwqe.wq,
					&rq->wq_ctrl);
		if (err)
			goto err_rq_xdp_prog;

		err = mlx5e_alloc_mpwqe_rq_drop_page(rq);
		if (err)
			goto err_rq_wq_destroy;

		rq->mpwqe.wq.db = &rq->mpwqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);
#if defined(HAVE_XSK_ZERO_COPY_SUPPORT) && !defined(HAVE_XSK_BUFF_ALLOC)
		if (xsk)
			num_xsk_frames = wq_sz <<
				mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk);
#endif

		cache_init_sz = wq_sz * MLX5_MPWRQ_MAX_PAGES_PER_WQE;

		rq->mpwqe.page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
		rq->mpwqe.umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
		rq->mpwqe.pages_per_wqe =
			mlx5e_mpwrq_pages_per_wqe(mdev, rq->mpwqe.page_shift,
						  rq->mpwqe.umr_mode);
		rq->mpwqe.umr_wqebbs =
			mlx5e_mpwrq_umr_wqebbs(mdev, rq->mpwqe.page_shift,
					       rq->mpwqe.umr_mode);
		rq->mpwqe.mtts_per_wqe =
			mlx5e_mpwrq_mtts_per_wqe(mdev, rq->mpwqe.page_shift,
						 rq->mpwqe.umr_mode);

#ifdef HAVE_NET_PAGE_POOL_H
		pool_size = rq->mpwqe.pages_per_wqe <<
			mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk);
#endif

		rq->mpwqe.log_stride_sz = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
		rq->mpwqe.num_strides =
			BIT(mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk));
		rq->mpwqe.min_wqe_bulk = mlx5e_mpwqe_get_min_wqe_bulk(wq_sz);

		rq->buff.frame0_sz = (1 << rq->mpwqe.log_stride_sz);

		err = mlx5e_create_rq_umr_mkey(mdev, rq);
		if (err)
			goto err_rq_drop_page;

		err = mlx5e_rq_alloc_mpwqe_info(rq, node);
		if (err)
			goto err_rq_mkey;

#ifdef HAVE_SHAMPO_SUPPORT
		err = mlx5_rq_shampo_alloc(mdev, params, rqp, rq, &pool_size, node);
		if (err)
			goto err_free_mpwqe_info;
#endif

		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		err = mlx5_wq_cyc_create(mdev, &rqp->wq, rqc_wq, &rq->wqe.wq,
					 &rq->wq_ctrl);
		if (err)
			goto err_rq_xdp_prog;

		rq->wqe.wq.db = &rq->wqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_cyc_get_size(&rq->wqe.wq);

#if defined(HAVE_XSK_ZERO_COPY_SUPPORT) && !defined(HAVE_XSK_BUFF_ALLOC)
		if (xsk)
			num_xsk_frames = wq_sz << rq->wqe.info.log_num_frags;
#endif

		cache_init_sz = wq_sz;
		rq->wqe.info = rqp->frags_info;
		rq->buff.frame0_sz = rq->wqe.info.arr[0].frag_stride;

		rq->wqe.frags =
			kvzalloc_node(array_size(sizeof(*rq->wqe.frags),
					(wq_sz << rq->wqe.info.log_num_frags)),
				      GFP_KERNEL, node);
		if (!rq->wqe.frags) {
			err = -ENOMEM;
			goto err_rq_wq_destroy;
		}

		err = mlx5e_init_di_list(rq, wq_sz, node);
		if (err)
			goto err_rq_frags;
	}

	err = 0;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (xsk) {
#ifdef HAVE_XSK_BUFF_ALLOC
		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL, NULL);
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		xsk_pool_set_rxq_info(rq->xsk_pool, &rq->xdp_rxq);
#else
		xsk_buff_set_rxq_info(rq->umem, &rq->xdp_rxq);
#endif /* HAVE_NETDEV_BPF_XSK_BUFF_POOL*/
#else
		err = mlx5e_xsk_resize_reuseq(rq->umem, num_xsk_frames);
		if (unlikely(err)) {
			mlx5_core_err(mdev, "Unable to allocate the Reuse Ring for %u frames\n",
					num_xsk_frames);
			goto err_free_by_rq_type;
		}
		rq->zca.free = mlx5e_xsk_zca_free;
		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_ZERO_COPY,
						 &rq->zca);

#endif /* HAVE_XSK_BUFF_ALLOC */
	} else {
#endif /* HAVE_XSK_ZERO_COPY_SUPPORT */
		err = mlx5e_rx_alloc_page_cache(rq, node,
				ilog2(cache_init_sz));
		if (err)
			goto err_free_by_rq_type;

#ifdef HAVE_NET_PAGE_POOL_H
		/* Create a page_pool and register it with rxq */
		pp_params.order     = 0;
		pp_params.flags     = 0; /* No-internal DMA mapping in page_pool */
		pp_params.pool_size = pool_size;
		pp_params.nid       = node;
		pp_params.dev       = rq->pdev;
		pp_params.dma_dir   = rq->buff.map_dir;

		/* page_pool can be used even when there is no rq->xdp_prog,
		 * given page_pool does not handle DMA mapping there is no
		 * required state to clear. And page_pool gracefully handle
		 * elevated refcnt.
		 */
		rq->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rq->page_pool)) {
			err = PTR_ERR(rq->page_pool);
			rq->page_pool = NULL;
			goto err_free_by_rq_type;
		}
#endif /* HAVE_NET_PAGE_POOL_H */

#if defined(HAVE_XDP_SUPPORT) && defined(HAVE_XDP_RXQ_INFO_REG_MEM_MODEL)
		if (xdp_rxq_info_is_reg(&rq->xdp_rxq))
			err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
#ifdef HAVE_NET_PAGE_POOL_H
							 MEM_TYPE_PAGE_POOL, rq->page_pool);
#else
							 MEM_TYPE_PAGE_ORDER0, NULL);
#endif /* HAVE_NET_PAGE_POOL_H*/
	if (err)
#ifdef HAVE_NET_PAGE_POOL_H
		goto err_destroy_page_pool;
#else
		goto err_free_by_rq_type;
#endif

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	}
#endif
#endif /* HAVE_XDP_RXQ_INFO_REG_MEM_MODEL && HAVE_XDP_SUPPORT */

	for (i = 0; i < wq_sz; i++) {
		if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			struct mlx5e_rx_wqe_ll *wqe =
				mlx5_wq_ll_get_wqe(&rq->mpwqe.wq, i);
			u32 byte_count =
				rq->mpwqe.num_strides << rq->mpwqe.log_stride_sz;
#ifndef HAVE_MUL_U32_U32
			u64 dma_offset = MLX5E_REQUIRED_MTTS(i) << PAGE_SHIFT;
#else
			u64 dma_offset = mul_u32_u32(i, rq->mpwqe.mtts_per_wqe) <<
				rq->mpwqe.page_shift;
#endif
			u16 headroom = test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state) ?
				       0 : rq->buff.headroom;

			wqe->data[0].addr = cpu_to_be64(dma_offset + headroom);
			wqe->data[0].byte_count = cpu_to_be32(byte_count);
			wqe->data[0].lkey = rq->mpwqe.umr_mkey_be;
		} else {
			struct mlx5e_rx_wqe_cyc *wqe =
				mlx5_wq_cyc_get_wqe(&rq->wqe.wq, i);
			int f;

			for (f = 0; f < rq->wqe.info.num_frags; f++) {
				u32 frag_size = rq->wqe.info.arr[f].frag_size |
					MLX5_HW_START_PADDING;

				wqe->data[f].byte_count = cpu_to_be32(frag_size);
				wqe->data[f].lkey = rq->mkey_be;
			}
			/* check if num_frags is not a pow of two */
			if (rq->wqe.info.num_frags < (1 << rq->wqe.info.log_num_frags)) {
				wqe->data[f].byte_count = 0;
				wqe->data[f].lkey = cpu_to_be32(MLX5_INVALID_LKEY);
				wqe->data[f].addr = 0;
			}
		}
	}

	INIT_WORK(&rq->dim_obj.dim.work, mlx5e_rx_dim_work);

	switch (params->rx_cq_moderation.cq_period_mode) {
	case MLX5_CQ_PERIOD_MODE_START_FROM_CQE:
		rq->dim_obj.dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_CQE;
		break;
	case MLX5_CQ_PERIOD_MODE_START_FROM_EQE:
	default:
		rq->dim_obj.dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	}

	return 0;

#if defined(HAVE_XDP_SUPPORT) && defined(HAVE_XDP_RXQ_INFO_REG_MEM_MODEL) && defined(HAVE_NET_PAGE_POOL_H)
err_destroy_page_pool:
	page_pool_destroy(rq->page_pool);
#endif
err_free_by_rq_type:
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
#ifdef HAVE_SHAMPO_SUPPORT
		mlx5e_rq_free_shampo(rq);
err_free_mpwqe_info:
#endif
		kvfree(rq->mpwqe.info);
err_rq_mkey:
		mlx5_core_destroy_mkey(mdev, be32_to_cpu(rq->mpwqe.umr_mkey_be));
err_rq_drop_page:
		mlx5e_free_mpwqe_rq_drop_page(rq);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		mlx5e_free_di_list(rq);
err_rq_frags:
		kvfree(rq->wqe.frags);
	}
err_rq_wq_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);
err_rq_xdp_prog:
#ifdef HAVE_XDP_SUPPORT
	if (params->xdp_prog)
		bpf_prog_put(params->xdp_prog);
#endif
	return err;
}

static void mlx5e_free_rq(struct mlx5e_rq *rq)
{
#ifdef HAVE_XDP_SUPPORT
	struct bpf_prog *old_prog;

#ifdef HAVE_XDP_RXQ_INFO
	if (xdp_rxq_info_is_reg(&rq->xdp_rxq)) {
#endif
		old_prog = rcu_dereference_protected(rq->xdp_prog,
						     lockdep_is_held(&rq->priv->state_lock));
		if (old_prog)
			bpf_prog_put(old_prog);
#ifdef HAVE_XDP_RXQ_INFO
	}
#endif
#endif /* HAVE_XDP_SUPPORT */

	if (rq->page_cache.page_cache)
		mlx5e_rx_free_page_cache(rq);

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		kvfree(rq->mpwqe.info);
		mlx5_core_destroy_mkey(rq->mdev, be32_to_cpu(rq->mpwqe.umr_mkey_be));
		mlx5e_free_mpwqe_rq_drop_page(rq);
#ifdef HAVE_SHAMPO_SUPPORT
		mlx5e_rq_free_shampo(rq);
#endif
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		kvfree(rq->wqe.frags);
		mlx5e_free_di_list(rq);
	}

#if defined(HAVE_XDP_SUPPORT) && defined(HAVE_XDP_RXQ_INFO)
	xdp_rxq_info_unreg(&rq->xdp_rxq);
#endif

#ifdef HAVE_NET_PAGE_POOL_H
#ifdef HAVE_BASECODE_EXTRAS
        if (rq->page_pool)
#endif /* HAVE_BASECODE_EXTRAS */
		page_pool_destroy(rq->page_pool);
#endif
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_set_delay_drop(struct mlx5e_priv *priv,
				struct mlx5e_params *params)
{
	struct mlx5e_delay_drop *delay_drop = &priv->delay_drop;
	int err = 0;

	if (!MLX5E_GET_PFLAG(params, MLX5E_PFLAG_DROPLESS_RQ)) {
		delay_drop->activate = false;
		return 0;
	}

	mutex_lock(&delay_drop->lock);
	if (delay_drop->activate)
		goto out;

	err = mlx5_core_set_delay_drop(priv->mdev, delay_drop->usec_timeout);
	if (err)
		goto out;

	delay_drop->activate = true;
out:
	mutex_unlock(&delay_drop->lock);
	return err;
}

int mlx5e_create_rq(struct mlx5e_rq *rq, struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	u8 ts_format;
	void *in;
	void *rqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_rq_in) +
		sizeof(u64) * rq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	ts_format = mlx5_is_real_time_rq(mdev) ?
			    MLX5_TIMESTAMP_FORMAT_REAL_TIME :
			    MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	memcpy(rqc, param->rqc, sizeof(param->rqc));

	MLX5_SET(rqc,  rqc, cqn,		rq->cq.mcq.cqn);
	MLX5_SET(rqc,  rqc, state,		MLX5_RQC_STATE_RST);
	MLX5_SET(rqc,  rqc, ts_format,		ts_format);
	MLX5_SET(wq,   wq,  log_wq_pg_sz,	rq->wq_ctrl.buf.page_shift -
						MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq,  dbr_addr,		rq->wq_ctrl.db.dma);

#ifdef HAVE_SHAMPO_SUPPORT
	if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state)) {
		MLX5_SET(wq, wq, log_headers_buffer_entry_num,
			 order_base_2(rq->mpwqe.shampo->hd_per_wq));
		MLX5_SET(wq, wq, headers_mkey, rq->mpwqe.shampo->mkey);
	}
#endif

	mlx5_fill_page_frag_array(&rq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_rq(mdev, in, inlen, &rq->rqn);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_state(struct mlx5e_rq *rq, int curr_state, int next_state)
{
	struct mlx5_core_dev *mdev = rq->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	if (curr_state == MLX5_RQC_STATE_RST && next_state == MLX5_RQC_STATE_RDY)
		mlx5e_rqwq_reset(rq);

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, curr_state);
	MLX5_SET(rqc, rqc, state, next_state);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

static int mlx5e_rq_to_ready(struct mlx5e_rq *rq, int curr_state)
{
	struct net_device *dev = rq->netdev;
	int err;

	err = mlx5e_modify_rq_state(rq, curr_state, MLX5_RQC_STATE_RST);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to reset\n", rq->rqn);
		return err;
	}
	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to ready\n", rq->rqn);
		return err;
	}

	return 0;
}

int mlx5e_flush_rq(struct mlx5e_rq *rq, int curr_state)
{
	mlx5e_free_rx_descs(rq);

	return mlx5e_rq_to_ready(rq, curr_state);
}

static int mlx5e_modify_rq_scatter_fcs(struct mlx5e_rq *rq, bool enable)
{
	struct mlx5_core_dev *mdev = rq->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, MLX5_RQC_STATE_RDY);
	MLX5_SET64(modify_rq_in, in, modify_bitmask,
		   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_SCATTER_FCS);
	MLX5_SET(rqc, rqc, scatter_fcs, enable);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_vsd(struct mlx5e_rq *rq, bool vsd)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, MLX5_RQC_STATE_RDY);
	MLX5_SET64(modify_rq_in, in, modify_bitmask,
		   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_VSD);
	MLX5_SET(rqc, rqc, vsd, vsd);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

void mlx5e_destroy_rq(struct mlx5e_rq *rq)
{
	mlx5_core_destroy_rq(rq->mdev, rq->rqn);
}

int mlx5e_wait_for_min_rx_wqes(struct mlx5e_rq *rq, int wait_time)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(wait_time);

	u16 min_wqes = mlx5_min_rx_wqes(rq->wq_type, mlx5e_rqwq_get_size(rq));

	do {
		if (mlx5e_rqwq_get_cur_sz(rq) >= min_wqes)
			return 0;

		msleep(20);
	} while (time_before(jiffies, exp_time));

	netdev_warn(rq->netdev, "Failed to get min RX wqes on Channel[%d] RQN[0x%x] wq cur_sz(%d) min_rx_wqes(%d)\n",
		    rq->ix, rq->rqn, mlx5e_rqwq_get_cur_sz(rq), min_wqes);

	mlx5e_reporter_rx_timeout(rq);
	return -ETIMEDOUT;
}

void mlx5e_free_rx_in_progress_descs(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq;
	u16 head;
	int i;

	if (rq->wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return;

	wq = &rq->mpwqe.wq;
	head = wq->head;

	/* Outstanding UMR WQEs (in progress) start at wq->head */
	for (i = 0; i < rq->mpwqe.umr_in_progress; i++) {
		rq->dealloc_wqe(rq, head);
		head = mlx5_wq_ll_get_wqe_next_ix(wq, head);
	}

#ifdef HAVE_SHAMPO_SUPPORT
	if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state)) {
		u16 len;

		len = (rq->mpwqe.shampo->pi - rq->mpwqe.shampo->ci) &
		      (rq->mpwqe.shampo->hd_per_wq - 1);
		mlx5e_shampo_dealloc_hd(rq, len, rq->mpwqe.shampo->ci, false);
		rq->mpwqe.shampo->pi = rq->mpwqe.shampo->ci;
	}
#endif

	rq->mpwqe.actual_wq_head = wq->head;
	rq->mpwqe.umr_in_progress = 0;
	rq->mpwqe.umr_completed = 0;
}

void mlx5e_free_rx_descs(struct mlx5e_rq *rq)
{
	__be16 wqe_ix_be;
	u16 wqe_ix;

	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
		struct mlx5_wq_ll *wq = &rq->mpwqe.wq;

		mlx5e_free_rx_in_progress_descs(rq);

		while (!mlx5_wq_ll_is_empty(wq)) {
			struct mlx5e_rx_wqe_ll *wqe;

			wqe_ix_be = *wq->tail_next;
			wqe_ix    = be16_to_cpu(wqe_ix_be);
			wqe       = mlx5_wq_ll_get_wqe(wq, wqe_ix);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_ll_pop(wq, wqe_ix_be,
				       &wqe->next.next_wqe_index);
		}

#ifdef HAVE_SHAMPO_SUPPORT
		if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
			mlx5e_shampo_dealloc_hd(rq, rq->mpwqe.shampo->hd_per_wq,
						0, true);
#endif
	} else {
		struct mlx5_wq_cyc *wq = &rq->wqe.wq;

		while (!mlx5_wq_cyc_is_empty(wq)) {
			wqe_ix = mlx5_wq_cyc_get_tail(wq);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_cyc_pop(wq);
		}
	}

}

#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
static int get_skb_hdr(struct sk_buff *skb, void **iphdr,
			void **tcph, u64 *hdr_flags, void *priv)
{
	unsigned int ip_len;
	struct iphdr *iph;

	if (unlikely(skb->protocol != htons(ETH_P_IP)))
		return -1;

	/*
	* In the future we may add an else clause that verifies the
	* checksum and allows devices which do not calculate checksum
	* to use LRO.
	*/
	if (unlikely(skb->ip_summed != CHECKSUM_UNNECESSARY))
		return -1;

	/* Check for non-TCP packet */
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_TCP)
		return -1;

	ip_len = ip_hdrlen(skb);
	skb_set_transport_header(skb, ip_len);
	*tcph = tcp_hdr(skb);

	/* check if IP header and TCP header are complete */
	if (ntohs(iph->tot_len) < ip_len + tcp_hdrlen(skb))
		return -1;

	*hdr_flags = LRO_IPV4 | LRO_TCP;
	*iphdr = iph;

	return 0;
}

static void mlx5e_rq_sw_lro_init(struct mlx5e_rq *rq)
{
	rq->sw_lro = &rq->priv->sw_lro[rq->ix];
	rq->sw_lro->lro_mgr.max_aggr 		= 64;
	rq->sw_lro->lro_mgr.max_desc		= MLX5E_LRO_MAX_DESC;
	rq->sw_lro->lro_mgr.lro_arr		= rq->sw_lro->lro_desc;
	rq->sw_lro->lro_mgr.get_skb_header	= get_skb_hdr;
	rq->sw_lro->lro_mgr.features		= LRO_F_NAPI;
	rq->sw_lro->lro_mgr.frag_align_pad	= NET_IP_ALIGN;
	rq->sw_lro->lro_mgr.dev			= rq->netdev;
	rq->sw_lro->lro_mgr.ip_summed		= CHECKSUM_UNNECESSARY;
	rq->sw_lro->lro_mgr.ip_summed_aggr	= CHECKSUM_UNNECESSARY;
}
#endif

int mlx5e_open_rq(struct mlx5e_params *params, struct mlx5e_rq_param *param,
		  struct mlx5e_xsk_param *xsk, int node, struct mlx5e_rq *rq,
		  struct mlx5e_create_cq_param *ccp)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	int err;

	if (rq->priv->shared_rq) {
		/* Init post_wqes function handler for non-existent
		 * RQ so we don't need extra checks in datapath
		 */
		mlx5e_rq_init_handler(rq);
		return 0;
	}

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
		__set_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state);

	err = mlx5e_open_cq(rq->priv, params->rx_cq_moderation, &param->cqp, ccp, &rq->cq);
	if (err)
		return err;

	err = mlx5e_alloc_rq(params, xsk, param, node, rq);
	if (err)
		goto err_dealloc_rq;

	err = mlx5e_create_rq(rq, param);
	if (err)
		goto err_free_rq;

	err = mlx5e_set_delay_drop(rq->priv, params);
	if (err)
		mlx5_core_warn(mdev, "Failed to enable delay drop err=%d\n",
			       err);

#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
	mlx5e_rq_sw_lro_init(rq);
#endif

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_destroy_rq;

	if (MLX5_CAP_ETH(mdev, cqe_checksum_full))
		__set_bit(MLX5E_RQ_STATE_CSUM_FULL, &rq->state);

	if (params->rx_dim_enabled)
		__set_bit(MLX5E_RQ_STATE_AM, &rq->state);

	/* We disable csum_complete when XDP is enabled since
	 * XDP programs might manipulate packets which will render
	 * skb->checksum incorrect.
	 */
#ifdef HAVE_XDP_SUPPORT
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE) || params->xdp_prog)
#else
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE))
#endif
		__set_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &rq->state);

	/* For CQE compression on striding RQ, use stride index provided by
	 * HW if capability is supported.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ) &&
	    MLX5_CAP_GEN(mdev, mini_cqe_resp_stride_index))
		__set_bit(MLX5E_RQ_STATE_MINI_CQE_HW_STRIDX, &rq->state);

#ifdef HAVE_BASECODE_EXTRAS
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_XMIT_MORE))
		__set_bit(MLX5E_RQ_STATE_SKB_XMIT_MORE, &rq->state);
#endif

	return 0;

err_destroy_rq:
	mlx5e_destroy_rq(rq);
err_free_rq:
	mlx5e_free_rq(rq);
err_dealloc_rq:
	mlx5e_close_cq(&rq->cq);
	return err;
}

void mlx5e_activate_rq(struct mlx5e_rq *rq)
{
	set_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
}

void mlx5e_deactivate_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	synchronize_net(); /* Sync with NAPI to prevent mlx5e_post_rx_wqes. */
}

void mlx5e_close_rq(struct mlx5e_priv *priv, struct mlx5e_rq *rq)
{
	if (priv->shared_rq)
		return;

	cancel_work_sync(&rq->dim_obj.dim.work);
	cancel_work_sync(&rq->recover_work);
	mlx5e_destroy_rq(rq);
	mlx5e_free_rx_descs(rq);
	mlx5e_free_rq(rq);
	mlx5e_close_cq(&rq->cq);
	memset(rq, 0, sizeof(*rq));
}

#ifdef HAVE_XDP_SUPPORT
static void mlx5e_free_xdpsq_db(struct mlx5e_xdpsq *sq)
{
	kvfree(sq->db.xdpi_fifo.xi);
	kvfree(sq->db.wqe_info);
}

static int mlx5e_alloc_xdpsq_fifo(struct mlx5e_xdpsq *sq, int numa)
{
	struct mlx5e_xdp_info_fifo *xdpi_fifo = &sq->db.xdpi_fifo;
	int wq_sz        = mlx5_wq_cyc_get_size(&sq->wq);
	int dsegs_per_wq = wq_sz * MLX5_SEND_WQEBB_NUM_DS;
	size_t size;

	size = array_size(sizeof(*xdpi_fifo->xi), dsegs_per_wq);
	xdpi_fifo->xi = kvzalloc_node(size, GFP_KERNEL, numa);
	if (!xdpi_fifo->xi)
		return -ENOMEM;

	xdpi_fifo->pc   = &sq->xdpi_fifo_pc;
	xdpi_fifo->cc   = &sq->xdpi_fifo_cc;
	xdpi_fifo->mask = dsegs_per_wq - 1;

	return 0;
}

static int mlx5e_alloc_xdpsq_db(struct mlx5e_xdpsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	size_t size;
	int err;

	size = array_size(sizeof(*sq->db.wqe_info), wq_sz);
	sq->db.wqe_info = kvzalloc_node(size, GFP_KERNEL, numa);
	if (!sq->db.wqe_info)
		return -ENOMEM;

	err = mlx5e_alloc_xdpsq_fifo(sq, numa);
	if (err) {
		mlx5e_free_xdpsq_db(sq);
		return err;
	}

	return 0;
}

static int mlx5e_alloc_xdpsq(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
			     struct xsk_buff_pool *xsk_pool,
#else
			     struct xdp_umem *xsk_pool,
#endif
#endif
			     struct mlx5e_sq_param *param,
			     struct mlx5e_xdpsq *sq,
			     bool is_redirect)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->pdev      = c->pdev;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu) - ETH_FCS_LEN;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
	sq->xsk_pool  = xsk_pool;

	sq->stats = sq->xsk_pool ?
#else
	sq->umem  = xsk_pool;
	sq->stats = sq->umem ?
#endif
		&c->priv->channel_stats[c->ix]->xsksq :
		is_redirect ?
#else
	sq->stats = is_redirect ?
#endif
			&c->priv->channel_stats[c->ix]->xdpsq :
			&c->priv->channel_stats[c->ix]->rq_xdpsq;
	sq->stop_room = param->is_mpw ? mlx5e_stop_room_for_mpwqe(mdev) :
					mlx5e_stop_room_for_max_wqe(mdev);
	sq->max_sq_mpw_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_xdpsq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_free_xdpsq(struct mlx5e_xdpsq *sq)
{
	mlx5e_free_xdpsq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}
#endif /* HAVE_XDP_SUPPORT */

static void mlx5e_free_icosq_db(struct mlx5e_icosq *sq)
{
	kvfree(sq->db.wqe_info);
}

static int mlx5e_alloc_icosq_db(struct mlx5e_icosq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	size_t size;

	size = array_size(wq_sz, sizeof(*sq->db.wqe_info));
	sq->db.wqe_info = kvzalloc_node(size, GFP_KERNEL, numa);
	if (!sq->db.wqe_info)
		return -ENOMEM;

	return 0;
}

static void mlx5e_icosq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_icosq *sq = container_of(recover_work, struct mlx5e_icosq,
					      recover_work);

	mlx5e_reporter_icosq_cqe_err(sq);
}

static void mlx5e_async_icosq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_icosq *sq = container_of(recover_work, struct mlx5e_icosq,
					      recover_work);

	/* Not implemented yet. */

	netdev_warn(sq->channel->netdev, "async_icosq recovery is not implemented\n");
}

static int mlx5e_alloc_icosq(struct mlx5e_channel *c,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_icosq *sq,
			     work_func_t recover_work_func)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->reserved_room = param->stop_room;

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_icosq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	INIT_WORK(&sq->recover_work, recover_work_func);

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_free_icosq(struct mlx5e_icosq *sq)
{
	mlx5e_free_icosq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

void mlx5e_free_txqsq_db(struct mlx5e_txqsq *sq)
{
	kvfree(sq->db.wqe_info);
	kvfree(sq->db.skb_fifo.fifo);
	kvfree(sq->db.dma_fifo);
}

int mlx5e_alloc_txqsq_db(struct mlx5e_txqsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	int df_sz = wq_sz * MLX5_SEND_WQEBB_NUM_DS;

	sq->db.dma_fifo = kvzalloc_node(array_size(df_sz,
						   sizeof(*sq->db.dma_fifo)),
					GFP_KERNEL, numa);
	sq->db.skb_fifo.fifo = kvzalloc_node(array_size(df_sz,
							sizeof(*sq->db.skb_fifo.fifo)),
					GFP_KERNEL, numa);
	sq->db.wqe_info = kvzalloc_node(array_size(wq_sz,
						   sizeof(*sq->db.wqe_info)),
					GFP_KERNEL, numa);
	if (!sq->db.dma_fifo || !sq->db.skb_fifo.fifo || !sq->db.wqe_info) {
		mlx5e_free_txqsq_db(sq);
		return -ENOMEM;
	}

	sq->dma_fifo_mask = df_sz - 1;

	sq->db.skb_fifo.pc   = &sq->skb_fifo_pc;
	sq->db.skb_fifo.cc   = &sq->skb_fifo_cc;
	sq->db.skb_fifo.mask = df_sz - 1;

	return 0;
}

static int mlx5e_alloc_txqsq(struct mlx5e_channel *c,
			     int txq_ix,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_txqsq *sq,
			     int tc)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->pdev      = c->pdev;
	sq->clock     = &mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->mdev      = c->mdev;
	sq->priv      = c->priv;
	sq->ch_ix     = c->ix;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->max_sq_mpw_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
	if (!MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		set_bit(MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE, &sq->state);
	if (mlx5_ipsec_device_caps(c->priv->mdev))
		set_bit(MLX5E_SQ_STATE_IPSEC, &sq->state);
	if (param->is_mpw)
		set_bit(MLX5E_SQ_STATE_MPWQE, &sq->state);
	sq->stop_room = param->stop_room;
	sq->ptp_cyc2time = mlx5_sq_ts_translator(mdev);

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db    = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_txqsq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	INIT_WORK(&sq->dim_obj.dim.work, mlx5e_tx_dim_work);
	sq->dim_obj.dim.mode = params->tx_cq_moderation.cq_period_mode;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

void mlx5e_free_txqsq(struct mlx5e_txqsq *sq)
{
	mlx5e_free_txqsq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static int mlx5e_create_sq(struct mlx5_core_dev *mdev,
			   struct mlx5e_sq_param *param,
			   struct mlx5e_create_sq_param *csp,
			   u32 *sqn)
{
	u8 ts_format;
	void *in;
	void *sqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) * csp->wq_ctrl->buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	ts_format = mlx5_is_real_time_sq(mdev) ?
			    MLX5_TIMESTAMP_FORMAT_REAL_TIME :
			    MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, param->sqc, sizeof(param->sqc));
	MLX5_SET(sqc,  sqc, tis_lst_sz, csp->tis_lst_sz);
	MLX5_SET(sqc,  sqc, tis_num_0, csp->tisn);
	MLX5_SET(sqc,  sqc, cqn, csp->cqn);
	MLX5_SET(sqc,  sqc, ts_cqe_to_dest_cqn, csp->ts_cqe_to_dest_cqn);
	MLX5_SET(sqc,  sqc, ts_format, ts_format);


	if (MLX5_CAP_ETH(mdev, wqe_inline_mode) == MLX5_CAP_INLINE_MODE_VPORT_CONTEXT)
		MLX5_SET(sqc,  sqc, min_wqe_inline_mode, csp->min_inline_mode);

	MLX5_SET(sqc,  sqc, state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc,  sqc, flush_in_error_en, 1);

	MLX5_SET(wq,   wq, wq_type,       MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq,   wq, uar_page,      mdev->mlx5e_res.hw_objs.bfreg.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  csp->wq_ctrl->buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      csp->wq_ctrl->db.dma);

	mlx5_fill_page_frag_array(&csp->wq_ctrl->buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, sqn);

	kvfree(in);

	return err;
}

int mlx5e_modify_sq(struct mlx5_core_dev *mdev, u32 sqn,
		    struct mlx5e_modify_sq_param *p)
{
	u64 bitmask = 0;
	void *in;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);

	MLX5_SET(modify_sq_in, in, sq_state, p->curr_state);
	MLX5_SET(sqc, sqc, state, p->next_state);
	if (p->rl_update && p->next_state == MLX5_SQC_STATE_RDY) {
		bitmask |= 1;
		MLX5_SET(sqc, sqc, packet_pacing_rate_limit_index, p->rl_index);
	}
	if (p->qos_update && p->next_state == MLX5_SQC_STATE_RDY) {
		bitmask |= 1 << 2;
		MLX5_SET(sqc, sqc, qos_queue_group_id, p->qos_queue_group_id);
	}
	MLX5_SET64(modify_sq_in, in, modify_bitmask, bitmask);

	err = mlx5_core_modify_sq(mdev, sqn, in);

	kvfree(in);

	return err;
}

void mlx5e_destroy_sq(struct mlx5_core_dev *mdev, u32 sqn)
{
	mlx5_core_destroy_sq(mdev, sqn);
}

int mlx5e_create_sq_rdy(struct mlx5_core_dev *mdev,
			struct mlx5e_sq_param *param,
			struct mlx5e_create_sq_param *csp,
			u16 qos_queue_group_id,
			u32 *sqn)
{
	struct mlx5e_modify_sq_param msp = {0};
	int err;

	err = mlx5e_create_sq(mdev, param, csp, sqn);
	if (err)
		return err;

	msp.curr_state = MLX5_SQC_STATE_RST;
	msp.next_state = MLX5_SQC_STATE_RDY;
	if (qos_queue_group_id) {
		msp.qos_update = true;
		msp.qos_queue_group_id = qos_queue_group_id;
	}
	err = mlx5e_modify_sq(mdev, *sqn, &msp);
	if (err)
		mlx5e_destroy_sq(mdev, *sqn);

	return err;
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_txqsq *sq, u32 rate);

int mlx5e_open_txqsq(struct mlx5e_channel *c, u32 tisn, int txq_ix,
		     struct mlx5e_params *params, struct mlx5e_sq_param *param,
		     struct mlx5e_txqsq *sq, int tc, u16 qos_queue_group_id,
		     struct mlx5e_sq_stats *sq_stats)
{
	struct mlx5e_create_sq_param csp = {};
	u32 tx_rate;
	int err;

	err = mlx5e_alloc_txqsq(c, txq_ix, params, param, sq, tc);
	if (err)
		return err;

	sq->stats = sq_stats;

	csp.tisn            = tisn;
	csp.tis_lst_sz      = 1;
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, qos_queue_group_id, &sq->sqn);
	if (err)
		goto err_free_txqsq;

	tx_rate = c->priv->tx_rates[sq->txq_ix];
	if (tx_rate)
		mlx5e_set_sq_maxrate(c->netdev, sq, tx_rate);

	if (params->tx_dim_enabled)
		sq->state |= BIT(MLX5E_SQ_STATE_AM);

#ifdef HAVE_BASECODE_EXTRAS
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_XMIT_MORE))
		set_bit(MLX5E_SQ_STATE_SKB_XMIT_MORE, &sq->state);
#endif

	return 0;

err_free_txqsq:
	mlx5e_free_txqsq(sq);

	return err;
}

void mlx5e_activate_txqsq(struct mlx5e_txqsq *sq)
{
	sq->txq = netdev_get_tx_queue(sq->netdev, sq->txq_ix);
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	netdev_tx_reset_queue(sq->txq);
	netif_tx_start_queue(sq->txq);
}

void mlx5e_tx_disable_queue(struct netdev_queue *txq)
{
	__netif_tx_lock_bh(txq);
	netif_tx_stop_queue(txq);
	__netif_tx_unlock_bh(txq);
}

void mlx5e_deactivate_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5_wq_cyc *wq = &sq->wq;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	synchronize_net(); /* Sync with NAPI to prevent netif_tx_wake_queue. */

	mlx5e_tx_disable_queue(sq->txq);

	/* last doorbell out, godspeed .. */
	if (mlx5e_wqc_has_room_for(wq, sq->cc, sq->pc, 1)) {
		u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
		struct mlx5e_tx_wqe *nop;

		sq->db.wqe_info[pi] = (struct mlx5e_tx_wqe_info) {
			.num_wqebbs = 1,
		};

		nop = mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		mlx5e_notify_hw(wq, sq->pc, sq->uar_map, &nop->ctrl);
	}
}

void mlx5e_close_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5_core_dev *mdev = sq->mdev;
	struct mlx5_rate_limit rl = {0};

	cancel_work_sync(&sq->dim_obj.dim.work);
	cancel_work_sync(&sq->recover_work);
	mlx5e_destroy_sq(mdev, sq->sqn);
	if (sq->rate_limit) {
		rl.rate = sq->rate_limit;
		mlx5_rl_remove_rate(mdev, &rl);
	}
	mlx5e_free_txqsq_descs(sq);
	mlx5e_free_txqsq(sq);
}

void mlx5e_tx_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_txqsq *sq = container_of(recover_work, struct mlx5e_txqsq,
					      recover_work);

	mlx5e_reporter_tx_err_cqe(sq);
}

static int mlx5e_open_icosq(struct mlx5e_channel *c, struct mlx5e_params *params,
			    struct mlx5e_sq_param *param, struct mlx5e_icosq *sq,
			    work_func_t recover_work_func)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_icosq(c, param, sq, recover_work_func);
	if (err)
		return err;

	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = params->tx_min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_icosq;

#if defined(CONFIG_MLX5_EN_TLS) && defined(HAVE_KTLS_RX_SUPPORT)
	if (param->is_tls) {
		sq->ktls_resync = mlx5e_ktls_rx_resync_create_resp_list();
		if (IS_ERR(sq->ktls_resync)) {
			err = PTR_ERR(sq->ktls_resync);
			goto err_destroy_icosq;
		}
	}
#endif
	return 0;

#if defined(CONFIG_MLX5_EN_TLS) && defined(HAVE_KTLS_RX_SUPPORT)
err_destroy_icosq:
	mlx5e_destroy_sq(c->mdev, sq->sqn);
#endif
err_free_icosq:
	mlx5e_free_icosq(sq);

	return err;
}

void mlx5e_activate_icosq(struct mlx5e_icosq *icosq)
{
	set_bit(MLX5E_SQ_STATE_ENABLED, &icosq->state);
}

void mlx5e_deactivate_icosq(struct mlx5e_icosq *icosq)
{
	clear_bit(MLX5E_SQ_STATE_ENABLED, &icosq->state);
	synchronize_net(); /* Sync with NAPI. */
}

static void mlx5e_close_icosq(struct mlx5e_icosq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	if (sq->ktls_resync)
		mlx5e_ktls_rx_resync_destroy_resp_list(sq->ktls_resync);
	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_icosq_descs(sq);
	mlx5e_free_icosq(sq);
}

#ifdef HAVE_XDP_SUPPORT
int mlx5e_open_xdpsq(struct mlx5e_channel *c, struct mlx5e_params *params,
		     struct mlx5e_sq_param *param,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		     struct xsk_buff_pool *xsk_pool,
#else
		     struct xdp_umem *xsk_pool,
#endif
#endif
		     struct mlx5e_xdpsq *sq, bool is_redirect)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_xdpsq(c, params,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				xsk_pool,
#endif
				param, sq, is_redirect);
	if (err)
		return err;

	csp.tis_lst_sz      = 1;
	csp.tisn            = c->priv->tisn[c->lag_port][0]; /* tc = 0 */
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);

	/* Don't enable multi buffer on XDP_REDIRECT SQ, as it's not yet
	 * supported by upstream, and there is no defined trigger to allow
	 * transmitting redirected multi-buffer frames.
	 */
	if (param->is_xdp_mb && !is_redirect)
		set_bit(MLX5E_SQ_STATE_XDP_MULTIBUF, &sq->state);

	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_xdpsq;

	mlx5e_set_xmit_fp(sq, param->is_mpw);
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_TX_XDP_CSUM))
		set_bit(MLX5E_SQ_STATE_TX_XDP_CSUM, &sq->state);

#ifdef HAVE_BASECODE_EXTRAS
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_XMIT_MORE))
		set_bit(MLX5E_SQ_STATE_SKB_XMIT_MORE, &sq->state);
#endif

	if (!param->is_mpw && !test_bit(MLX5E_SQ_STATE_XDP_MULTIBUF, &sq->state)) {
		unsigned int ds_cnt = MLX5E_TX_WQE_EMPTY_DS_COUNT + 1;
		unsigned int inline_hdr_sz = 0;
		int i;

		if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE) {
			inline_hdr_sz = MLX5E_XDP_MIN_INLINE;
			ds_cnt++;
		}

		/* Pre initialize fixed WQE fields */
		for (i = 0; i < mlx5_wq_cyc_get_size(&sq->wq); i++) {
			struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(&sq->wq, i);
			struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
			struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
			struct mlx5_wqe_data_seg *dseg;

			sq->db.wqe_info[i] = (struct mlx5e_xdp_wqe_info) {
				.num_wqebbs = 1,
				.num_pkts   = 1,
			};

			cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
			eseg->inline_hdr.sz = cpu_to_be16(inline_hdr_sz);

			dseg = (struct mlx5_wqe_data_seg *)cseg + (ds_cnt - 1);
			dseg->lkey = sq->mkey_be;
		}
	}

	return 0;

err_free_xdpsq:
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	mlx5e_free_xdpsq(sq);

	return err;
}

void mlx5e_close_xdpsq(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	synchronize_net(); /* Sync with NAPI. */

	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_xdpsq_descs(sq);
	mlx5e_free_xdpsq(sq);
}
#endif

int mlx5e_alloc_cq_common(struct mlx5e_priv *priv,
			  struct mlx5e_cq_param *param,
			  struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	int err;
	u32 i;

	err = mlx5_cqwq_create(mdev, &param->wq, param->cqc, &cq->wq,
			       &cq->wq_ctrl);
	if (err)
		return err;

	mcq->cqe_sz     = 64;
	mcq->set_ci_db  = cq->wq_ctrl.db.db;
	mcq->arm_db     = cq->wq_ctrl.db.db + 1;
	*mcq->set_ci_db = 0;
	*mcq->arm_db    = 0;
	mcq->vector     = param->eq_ix;
	mcq->comp       = mlx5e_completion_event;
	mcq->event      = mlx5e_cq_error_event;

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(&cq->wq, i);

		cqe->op_own = 0xf1;
	}

	cq->mdev = mdev;
	cq->netdev = priv->netdev;
	cq->priv = priv;

	return 0;
}

static int mlx5e_alloc_cq(struct mlx5e_priv *priv,
			  struct mlx5e_cq_param *param,
			  struct mlx5e_create_cq_param *ccp,
			  struct mlx5e_cq *cq)
{
	int err;

	param->wq.buf_numa_node = ccp->node;
	param->wq.db_numa_node  = ccp->node;
	param->eq_ix            = ccp->ix;

	err = mlx5e_alloc_cq_common(priv, param, cq);

	cq->napi     = ccp->napi;
	cq->ch_stats = ccp->ch_stats;
#ifndef HAVE_NAPI_STATE_MISSED
	cq->ch_flags = ccp->ch_flags;
#endif

	return err;
}

void mlx5e_free_cq(struct mlx5e_cq *cq)
{
	mlx5_wq_destroy(&cq->wq_ctrl);
}

int mlx5e_create_cq(struct mlx5e_cq *cq, struct mlx5e_cq_param *param)
{
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_core_dev *mdev = cq->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;

	void *in;
	void *cqc;
	int inlen;
	int eqn;
	int err;

	err = mlx5_vector2eqn(mdev, param->eq_ix, &eqn);
	if (err)
		return err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);

	memcpy(cqc, param->cqc, sizeof(param->cqc));

	mlx5_fill_page_frag_array(&cq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	MLX5_SET(cqc,   cqc, cq_period_mode, param->cq_period_mode);
	MLX5_SET(cqc,   cqc, c_eqn_or_apu_element, eqn);
	MLX5_SET(cqc,   cqc, uar_page,      mdev->priv.uar->index);
	MLX5_SET(cqc,   cqc, log_page_size, cq->wq_ctrl.buf.page_shift -
					    MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr,      cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen, out, sizeof(out));

	kvfree(in);

	if (err)
		return err;

	if (!cq->no_arm)
		mlx5e_cq_arm(cq);

	return 0;
}

static void mlx5e_destroy_cq(struct mlx5e_cq *cq)
{
	mlx5_core_destroy_cq(cq->mdev, &cq->mcq);
}

int mlx5e_open_cq(struct mlx5e_priv *priv, struct dim_cq_moder moder,
		  struct mlx5e_cq_param *param, struct mlx5e_create_cq_param *ccp,
		  struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5e_alloc_cq(priv, param, ccp, cq);
	if (err)
		return err;

	err = mlx5e_create_cq(cq, param);
	if (err)
		goto err_free_cq;

	if (MLX5_CAP_GEN(mdev, cq_moderation))
		mlx5_core_modify_cq_moderation(mdev, &cq->mcq, moder.usec, moder.pkts);
	return 0;

err_free_cq:
	mlx5e_free_cq(cq);

	return err;
}

void mlx5e_close_cq(struct mlx5e_cq *cq)
{
	mlx5e_destroy_cq(cq);
	mlx5e_free_cq(cq);
}

static int mlx5e_open_tx_cqs(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_create_cq_param *ccp,
			     struct mlx5e_channel_param *cparam)
{
	int err;
	int tc;

	for (tc = 0; tc < c->num_tc; tc++) {
		err = mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->txq_sq.cqp,
				    ccp, &c->sq[tc].cq);
		if (err)
			goto err_close_tx_cqs;
	}

	return 0;

err_close_tx_cqs:
	for (tc--; tc >= 0; tc--)
		mlx5e_close_cq(&c->sq[tc].cq);

	return err;
}

static void mlx5e_close_tx_cqs(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->sq[tc].cq);
}

#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
static int mlx5e_mqprio_txq_to_tc(struct netdev_tc_txq *tc_to_txq, unsigned int txq)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++)
		if (txq - tc_to_txq[tc].offset < tc_to_txq[tc].count)
			return tc;

	WARN(1, "Unexpected TCs configuration. No match found for txq %u", txq);
	return -ENOENT;
}

static int mlx5e_txq_get_qos_node_hw_id(struct mlx5e_params *params, int txq_ix,
					u32 *hw_id)
{
	int tc;

	if (params->mqprio.mode != TC_MQPRIO_MODE_CHANNEL) {
		*hw_id = 0;
		return 0;
	}

	tc = mlx5e_mqprio_txq_to_tc(params->mqprio.tc_to_txq, txq_ix);
	if (tc < 0)
		return tc;

	if (tc >= params->mqprio.num_tc) {
		WARN(1, "Unexpected TCs configuration. tc %d is out of range of %u",
		     tc, params->mqprio.num_tc);
		return -EINVAL;
	}

	*hw_id = params->mqprio.channel.hw_id[tc];
	return 0;
}
#endif
static int mlx5e_open_sqs(struct mlx5e_channel *c,
			  struct mlx5e_params *params,
			  struct mlx5e_channel_param *cparam)
{
	int err, tc;

	for (tc = 0; tc < mlx5e_get_dcb_num_tc(params); tc++) {
		int txq_ix = c->ix + tc * params->num_channels;
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
		u32 qos_queue_group_id;

		err = mlx5e_txq_get_qos_node_hw_id(params, txq_ix, &qos_queue_group_id);
		if (err)
			goto err_close_sqs;
#endif

		err = mlx5e_open_txqsq(c, c->priv->tisn[c->lag_port][tc], txq_ix,
				       params, &cparam->txq_sq, &c->sq[tc], tc,
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
				       qos_queue_group_id,
#else
					0,
#endif
				       &c->priv->channel_stats[c->ix]->sq[tc]);
		if (err)
			goto err_close_sqs;
	}

	return 0;

err_close_sqs:
	for (tc--; tc >= 0; tc--)
		mlx5e_close_txqsq(&c->sq[tc]);

	return err;
}

static void mlx5e_close_sqs(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_txqsq(&c->sq[tc]);
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_txqsq *sq, u32 rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_modify_sq_param msp = {0};
	struct mlx5_rate_limit rl = {0};
	u16 rl_index = 0;
	int err;

	if (rate == sq->rate_limit)
		/* nothing to do */
		return 0;

	if (sq->rate_limit) {
		rl.rate = sq->rate_limit;
		/* remove current rl index to free space to next ones */
		mlx5_rl_remove_rate(mdev, &rl);
	}

	sq->rate_limit = 0;

	if (rate) {
		rl.rate = rate;
		err = mlx5_rl_add_rate(mdev, &rl_index, &rl);
		if (err) {
			netdev_err(dev, "Failed configuring rate %u: %d\n",
				   rate, err);
			return err;
		}
	}

	msp.curr_state = MLX5_SQC_STATE_RDY;
	msp.next_state = MLX5_SQC_STATE_RDY;
	msp.rl_index   = rl_index;
	msp.rl_update  = true;
	err = mlx5e_modify_sq(mdev, sq->sqn, &msp);
	if (err) {
		netdev_err(dev, "Failed configuring rate %u: %d\n",
			   rate, err);
		/* remove the rate from the table */
		if (rate)
			mlx5_rl_remove_rate(mdev, &rl);
		return err;
	}

	sq->rate_limit = rate;
	return 0;
}

#if defined(HAVE_NDO_SET_TX_MAXRATE) || defined(HAVE_NDO_SET_TX_MAXRATE_EXTENDED)
static int mlx5e_set_tx_maxrate(struct net_device *dev, int index, u32 rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_txqsq *sq = priv->txq2sq[index];
	int err = 0;

	if (!mlx5_rl_is_supported(mdev)) {
		netdev_err(dev, "Rate limiting is not supported on this device\n");
		return -EINVAL;
	}

	/* rate is given in Mb/sec, HW config is in Kb/sec */
	rate = rate << 10;

	/* Check whether rate in valid range, 0 is always valid */
	if (rate && !mlx5_rl_is_in_range(mdev, rate)) {
		netdev_err(dev, "TX rate %u, is not in range\n", rate);
		return -ERANGE;
	}

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		err = mlx5e_set_sq_maxrate(dev, sq, rate);
	if (!err)
		priv->tx_rates[index] = rate;
	mutex_unlock(&priv->state_lock);

	return err;
}
#endif

static int mlx5e_open_rxq_rq(struct mlx5e_channel *c, struct mlx5e_params *params,
			     struct mlx5e_create_cq_param *ccp, struct mlx5e_rq_param *rq_params)
{
	int err;

	err = mlx5e_init_rxq_rq(c, params, &c->rq);
	if (err)
		return err;

	return mlx5e_open_rq(params, rq_params, NULL, cpu_to_node(c->cpu), &c->rq, ccp);
}

static int mlx5e_open_queues(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_channel_param *cparam)
{
	struct dim_cq_moder icocq_moder = {0, 0};
	struct mlx5e_create_cq_param ccp;
	int err;

	mlx5e_build_create_cq_param(&ccp, c);
	err = mlx5e_open_cq(c->priv, icocq_moder, &cparam->async_icosq.cqp, &ccp,
			    &c->async_icosq.cq);
	if (err)
		return err;
	err = mlx5e_open_cq(c->priv, icocq_moder, &cparam->icosq.cqp, &ccp,
			    &c->icosq.cq);
	if (err)
		goto err_close_async_icosq_cq;

	err = mlx5e_open_tx_cqs(c, params, &ccp, cparam);
	if (err)
		goto err_close_icosq_cq;

#ifdef HAVE_XDP_SUPPORT
	err = mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->xdp_sq.cqp, &ccp,
			    &c->xdpsq.cq);
	if (err)
		goto err_close_tx_cqs;

	err = c->xdp ? mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->xdp_sq.cqp,
				     &ccp, &c->rq_xdpsq.cq) : 0;
	if (err)
		goto err_close_xdp_tx_cqs;
#endif

	spin_lock_init(&c->async_icosq_lock);

	err = mlx5e_open_icosq(c, params, &cparam->async_icosq, &c->async_icosq,
			       mlx5e_async_icosq_err_cqe_work);
	if (err)
		goto err_close_xdpsq_cq;

	mutex_init(&c->icosq_recovery_lock);

	err = mlx5e_open_icosq(c, params, &cparam->icosq, &c->icosq,
			       mlx5e_icosq_err_cqe_work);
	if (err)
		goto err_close_async_icosq;

	err = mlx5e_open_sqs(c, params, cparam);
	if (err)
		goto err_close_icosq;

	err = mlx5e_open_rxq_rq(c, params, &ccp, &cparam->rq);
	if (err)
		goto err_close_sqs;

#ifdef HAVE_XDP_SUPPORT
	if (c->xdp) {
		err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				       NULL,
#endif
				       &c->rq_xdpsq, false);
		if (err)
			goto err_close_rq;
	}

	err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
			      NULL,
#endif
			      &c->xdpsq, true);
	if (err)
		goto err_close_xdp_sq;
#endif

	return 0;

#ifdef HAVE_XDP_SUPPORT
err_close_xdp_sq:
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);

err_close_rq:
	mlx5e_close_rq(c->priv, &c->rq);
#endif

err_close_sqs:
	mlx5e_close_sqs(c);

err_close_icosq:
	mlx5e_close_icosq(&c->icosq);

err_close_async_icosq:
	mlx5e_close_icosq(&c->async_icosq);

err_close_xdpsq_cq:
#ifdef HAVE_XDP_SUPPORT
	if (c->xdp)
		mlx5e_close_cq(&c->rq_xdpsq.cq);

err_close_xdp_tx_cqs:
	mlx5e_close_cq(&c->xdpsq.cq);

err_close_tx_cqs:
#endif
	mlx5e_close_tx_cqs(c);

err_close_icosq_cq:
	mlx5e_close_cq(&c->icosq.cq);

err_close_async_icosq_cq:
	mlx5e_close_cq(&c->async_icosq.cq);

	return err;
}

static void mlx5e_close_queues(struct mlx5e_channel *c)
{
#ifdef HAVE_XDP_SUPPORT
	mlx5e_close_xdpsq(&c->xdpsq);
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);
#endif
	/* The same ICOSQ is used for UMRs for both RQ and XSKRQ. */
	cancel_work_sync(&c->icosq.recover_work);
	mlx5e_close_rq(c->priv, &c->rq);
	mlx5e_close_sqs(c);
	mlx5e_close_icosq(&c->icosq);
	mutex_destroy(&c->icosq_recovery_lock);
	mlx5e_close_icosq(&c->async_icosq);
#ifdef HAVE_XDP_SUPPORT
	if (c->xdp)
		mlx5e_close_cq(&c->rq_xdpsq.cq);
	mlx5e_close_cq(&c->xdpsq.cq);
#endif
	mlx5e_close_tx_cqs(c);
	mlx5e_close_cq(&c->icosq.cq);
	mlx5e_close_cq(&c->async_icosq.cq);
}

static u8 mlx5e_enumerate_lag_port(struct mlx5_core_dev *mdev, int ix)
{
	u16 port_aff_bias = mlx5_core_is_pf(mdev) ? 0 : MLX5_CAP_GEN(mdev, vhca_id);

	return (ix + port_aff_bias) % mlx5e_get_num_lag_ports(mdev);
}

static int mlx5e_channel_stats_alloc(struct mlx5e_priv *priv, int ix, int cpu)
{
	if (ix > priv->stats_nch)  {
		netdev_warn(priv->netdev, "Unexpected channel stats index %d > %d\n", ix,
			    priv->stats_nch);
		return -EINVAL;
	}

	if (priv->channel_stats[ix])
		return 0;

	/* Asymmetric dynamic memory allocation.
	 * Freed in mlx5e_priv_arrays_free, not on channel closure.
	 */
	mlx5e_dbg(DRV, priv, "Creating channel stats %d\n", ix);
	priv->channel_stats[ix] = kvzalloc_node(sizeof(**priv->channel_stats),
						GFP_KERNEL, cpu_to_node(cpu));
	if (!priv->channel_stats[ix])
		return -ENOMEM;
	priv->stats_nch++;

	return 0;
}

void mlx5e_trigger_napi_icosq(struct mlx5e_channel *c)
{
	spin_lock_bh(&c->async_icosq_lock);
	mlx5e_trigger_irq(&c->async_icosq);
	spin_unlock_bh(&c->async_icosq_lock);
}

void mlx5e_trigger_napi_sched(struct napi_struct *napi)
{
	local_bh_disable();
	napi_schedule(napi);
	local_bh_enable();
}

static int mlx5e_open_channel(struct mlx5e_priv *priv, int ix,
			      struct mlx5e_params *params,
			      struct mlx5e_channel_param *cparam,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
			      struct xsk_buff_pool *xsk_pool,
#else
			      struct xdp_umem *xsk_pool,
#endif
#endif
			      struct mlx5e_channel **cp)
{
	const struct cpumask *aff;
	struct net_device *netdev = priv->netdev;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	struct mlx5e_xsk_param xsk;
#endif
	struct mlx5e_channel *c;
	unsigned int irq;
	int err;
	int cpu = 0;

	err = mlx5_vector2irqn(priv->mdev, ix, &irq);
	if (err)
		return err;

	err = mlx5e_channel_stats_alloc(priv, ix, cpu);
	if (err)
		return err;

#ifdef HAVE_IRQ_GET_EFFECTIVE_AFFINITY_MASK
	aff = irq_get_effective_affinity_mask(irq);
#elif defined(HAVE_IRQ_GET_AFFINITY_MASK)
	aff = irq_get_affinity_mask(irq);
#else
#ifndef HAVE_IRQ_DATA_AFFINITY
	aff = irq_data_get_affinity_mask(irq_desc_get_irq_data(irq_to_desc(irq)));
#else
	aff = irq_desc_get_irq_data(irq_to_desc(irq))->affinity;
#endif
#endif
 	cpu = cpumask_first(aff);
	c = kvzalloc_node(sizeof(*c), GFP_KERNEL, cpu_to_node(cpu));
	if (!c)
		return -ENOMEM;

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->ix       = ix;
	c->cpu      = cpu;
	c->pdev     = mlx5_core_dma_dev(priv->mdev);
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey);
	c->num_tc   = mlx5e_get_dcb_num_tc(params);
#ifdef HAVE_XDP_SUPPORT
	c->xdp      = !!params->xdp_prog;
#endif
	c->stats    = &priv->channel_stats[ix]->ch;
	c->aff_mask = aff;
	c->lag_port = mlx5e_enumerate_lag_port(priv->mdev, ix);

#ifdef HAVE_NETIF_NAPI_ADD_GET_3_PARAMS //forwardport
	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll);
#else
	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll, 64);
#endif

	err = mlx5e_open_queues(c, params, cparam);
	if (unlikely(err))
		goto err_napi_del;

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (xsk_pool) {
		mlx5e_build_xsk_param(xsk_pool, &xsk);
		err = mlx5e_open_xsk(priv, params, &xsk, xsk_pool, c);
		if (unlikely(err))
			goto err_close_queues;
	}
#endif

	*cp = c;

	return 0;

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
err_close_queues:
	mlx5e_close_queues(c);
#endif

err_napi_del:
	netif_napi_del(&c->napi);

	kvfree(c);

	return err;
}

static void mlx5e_rq_channel_activate(struct mlx5e_channel *c)
{
	if (c->priv->shared_rq)
		return;


#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_activate_xsk(c);
	else
#endif
		mlx5e_activate_rq(&c->rq);
}

static void mlx5e_activate_channel(struct mlx5e_channel *c)
{
	int tc;

	napi_enable(&c->napi);

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_activate_txqsq(&c->sq[tc]);
	mlx5e_activate_icosq(&c->icosq);
	mlx5e_activate_icosq(&c->async_icosq);
	mlx5e_rq_channel_activate(c);

	mlx5e_trigger_napi_icosq(c);
}

static void mlx5e_rq_channel_deactivate(struct mlx5e_channel *c)
{
	if (c->priv->shared_rq)
		return;

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_deactivate_xsk(c);
	else
#endif
		mlx5e_deactivate_rq(&c->rq);
}

static void mlx5e_deactivate_channel(struct mlx5e_channel *c)
{
	int tc;

	mlx5_rename_comp_eq(c->priv->mdev, c->ix, NULL);
	mlx5e_rq_channel_deactivate(c);
	mlx5e_deactivate_icosq(&c->async_icosq);
	mlx5e_deactivate_icosq(&c->icosq);
	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_deactivate_txqsq(&c->sq[tc]);
	mlx5e_qos_deactivate_queues(c);

	napi_disable(&c->napi);
}

static void mlx5e_close_channel(struct mlx5e_channel *c)
{
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_close_xsk(c);
#endif
	mlx5e_close_queues(c);
	mlx5e_qos_close_queues(c);
	netif_napi_del(&c->napi);

	kvfree(c);
}

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs)
{
	struct mlx5e_channel_param *cparam;
	int err = -ENOMEM;
	int i;

	chs->num = chs->params.num_channels;

	chs->c = kcalloc(chs->num, sizeof(struct mlx5e_channel *), GFP_KERNEL);
	cparam = kvzalloc(sizeof(struct mlx5e_channel_param), GFP_KERNEL);
	if (!chs->c || !cparam)
		goto err_free;

	err = mlx5e_build_channel_param(priv->mdev, &chs->params, priv->q_counter, cparam);
	if (err)
		goto err_free;

	for (i = 0; i < chs->num; i++) {
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		struct xsk_buff_pool *xsk_pool = NULL;
#else
		struct xdp_umem *xsk_pool = NULL;
#endif
		if (chs->params.xdp_prog)
			xsk_pool = mlx5e_xsk_get_pool(&chs->params, chs->params.xsk, i);
#endif
		err = mlx5e_open_channel(priv, i, &chs->params, cparam,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
					xsk_pool,
#endif
					&chs->c[i]);
		if (err)
			goto err_close_channels;
	}

	if (MLX5E_GET_PFLAG(&chs->params, MLX5E_PFLAG_TX_PORT_TS) || chs->params.ptp_rx) {
		err = mlx5e_ptp_open(priv, &chs->params, chs->c[0]->lag_port, &chs->ptp);
		if (err)
			goto err_close_channels;
	}

	if (priv->htb) {
		err = mlx5e_qos_open_queues(priv, chs);
		if (err)
			goto err_close_ptp;
	}

	mlx5e_health_channels_update(priv);
	kvfree(cparam);
	return 0;

err_close_ptp:
	if (chs->ptp)
		mlx5e_ptp_close(chs->ptp);

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(chs->c[i]);

err_free:
	kfree(chs->c);
	kvfree(cparam);
	chs->num = 0;
	return err;
}

static void mlx5e_activate_channels(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_activate_channel(chs->c[i]);

	if (chs->ptp)
		mlx5e_ptp_activate_channel(chs->ptp);
}

static int mlx5e_wait_channels_min_rx_wqes(struct mlx5e_channels *chs)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		int timeout = err ? 0 : MLX5E_RQ_WQES_TIMEOUT;
		struct mlx5e_channel *c = chs->c[i];

		if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
			continue;

		err |= mlx5e_wait_for_min_rx_wqes(&c->rq, timeout);

		/* Don't wait on the XSK RQ, because the newer xdpsock sample
		 * doesn't provide any Fill Ring entries at the setup stage.
		 */
	}

	return err ? -ETIMEDOUT : 0;
}

static void mlx5e_deactivate_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->ptp)
		mlx5e_ptp_deactivate_channel(chs->ptp);

	for (i = 0; i < chs->num; i++)
		mlx5e_deactivate_channel(chs->c[i]);
}

void mlx5e_close_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->ptp) {
		mlx5e_ptp_close(chs->ptp);
		chs->ptp = NULL;
	}
	for (i = 0; i < chs->num; i++)
		mlx5e_close_channel(chs->c[i]);

	kfree(chs->c);
	chs->num = 0;
}

int mlx5e_modify_tirs_packet_merge(struct mlx5e_priv *priv)
{
	struct mlx5e_rx_res *res = priv->rx_res;

	return mlx5e_rx_res_packet_merge_set_param(res, &priv->channels.params.packet_merge);
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_modify_tirs_packet_merge);

static int mlx5e_set_mtu(struct mlx5_core_dev *mdev,
			 struct mlx5e_params *params, u16 mtu)
{
	u16 hw_mtu = MLX5E_SW2HW_MTU(params, mtu);
	int err;

	err = mlx5_set_port_mtu(mdev, hw_mtu, 1);
	if (err)
		return err;

	/* Update vport context MTU */
	mlx5_modify_nic_vport_mtu(mdev, hw_mtu);
	return 0;
}

static void mlx5e_query_mtu(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params, u16 *mtu)
{
	u16 hw_mtu = 0;
	int err;

	err = mlx5_query_nic_vport_mtu(mdev, &hw_mtu);
	if (err || !hw_mtu) /* fallback to port oper mtu */
		mlx5_query_port_oper_mtu(mdev, &hw_mtu, 1);

	*mtu = MLX5E_HW2SW_MTU(params, hw_mtu);
}

int mlx5e_set_dev_port_mtu(struct mlx5e_priv *priv)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 mtu;
	int err;

	err = mlx5e_set_mtu(mdev, params, params->sw_mtu);
	if (err)
		return err;

	mlx5e_query_mtu(mdev, params, &mtu);
	if (mtu != params->sw_mtu)
		netdev_warn(netdev, "%s: VPort MTU %d is different than netdev mtu %d\n",
			    __func__, mtu, params->sw_mtu);

	params->sw_mtu = mtu;
	return 0;
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_set_dev_port_mtu);

void mlx5e_set_netdev_mtu_boundaries(struct mlx5e_priv *priv)
{
#if defined(HAVE_NET_DEVICE_MIN_MAX_MTU) || defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	struct mlx5e_params *params = &priv->channels.params;
	struct net_device *netdev   = priv->netdev;
	struct mlx5_core_dev *mdev  = priv->mdev;
	u16 max_mtu;
#endif

#ifdef HAVE_NET_DEVICE_MIN_MAX_MTU

	/* MTU range: 68 - hw-specific max */
	netdev->min_mtu = ETH_MIN_MTU;

	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);
	netdev->max_mtu = min_t(unsigned int, MLX5E_HW2SW_MTU(params, max_mtu),
				ETH_MAX_MTU);
#elif defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	netdev->extended->min_mtu = ETH_MIN_MTU;
	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);
	netdev->extended->max_mtu = min_t(unsigned int, MLX5E_HW2SW_MTU(params, max_mtu),
			ETH_MAX_MTU);
#endif
}

static int mlx5e_netdev_set_tcs(struct net_device *netdev, u16 nch, u8 ntc,
				struct netdev_tc_txq *tc_to_txq)
{
	int tc, err;

	netdev_reset_tc(netdev);

	if (ntc == 1)
		return 0;

	err = netdev_set_num_tc(netdev, ntc);
	if (err) {
		netdev_WARN(netdev, "netdev_set_num_tc failed (%d), ntc = %d\n", err, ntc);
		return err;
	}

	for (tc = 0; tc < ntc; tc++) {
		u16 count, offset;

		count = tc_to_txq[tc].count;
		offset = tc_to_txq[tc].offset;
		netdev_set_tc_queue(netdev, tc, count, offset);
	}

	return 0;
}

int mlx5e_update_tx_netdev_queues(struct mlx5e_priv *priv)
{
	int nch, ntc, num_txqs, err;
	int qos_queues = 0;
#ifndef HAVE_NET_SYNCHRONIZE_IN_SET_REAL_NUM_TX_QUEUES
        struct net_device *netdev = priv->netdev;
        bool disabling;
#endif

	if (priv->htb)
		qos_queues = mlx5e_htb_cur_leaf_nodes(priv->htb);

	nch = priv->channels.params.num_channels;
	ntc = mlx5e_get_dcb_num_tc(&priv->channels.params);
	num_txqs = nch * ntc + qos_queues;
	if (MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_TX_PORT_TS))
		num_txqs += ntc;
#ifndef HAVE_NET_SYNCHRONIZE_IN_SET_REAL_NUM_TX_QUEUES
        disabling = num_txqs < netdev->real_num_tx_queues;
#endif

	mlx5e_dbg(DRV, priv, "Setting num_txqs %d\n", num_txqs);
	err = netif_set_real_num_tx_queues(priv->netdev, num_txqs);
	if (err)
		netdev_warn(priv->netdev, "netif_set_real_num_tx_queues failed (%d > %d), %d\n",
			    num_txqs, priv->netdev->num_tx_queues, err);

#ifndef HAVE_NET_SYNCHRONIZE_IN_SET_REAL_NUM_TX_QUEUES
	if (disabling)
		synchronize_net();
#endif

	return err;
}

static int mlx5e_update_netdev_queues(struct mlx5e_priv *priv)
{
	struct netdev_tc_txq old_tc_to_txq[TC_MAX_QUEUE], *tc_to_txq;
	struct net_device *netdev = priv->netdev;
	int old_num_txqs, old_ntc;
	int nch, ntc;
	int err;
	int i;

	old_num_txqs = netdev->real_num_tx_queues;
	old_ntc = netdev->num_tc ? : 1;
	for (i = 0; i < ARRAY_SIZE(old_tc_to_txq); i++)
		old_tc_to_txq[i] = netdev->tc_to_txq[i];

	nch = priv->channels.params.num_channels;
	ntc = priv->channels.params.mqprio.num_tc;
	tc_to_txq = priv->channels.params.mqprio.tc_to_txq;

	err = mlx5e_netdev_set_tcs(netdev, nch, ntc, tc_to_txq);
	if (err)
		goto err_out;
	err = mlx5e_update_tx_netdev_queues(priv);
	if (err)
		goto err_tcs;
	err = netif_set_real_num_rx_queues(netdev, nch);
	if (err) {
		netdev_warn(netdev, "netif_set_real_num_rx_queues failed, %d\n", err);
		goto err_txqs;
	}

	return 0;

err_txqs:
	/* netif_set_real_num_rx_queues could fail only when nch increased. Only
	 * one of nch and ntc is changed in this function. That means, the call
	 * to netif_set_real_num_tx_queues below should not fail, because it
	 * decreases the number of TX queues.
	 */
	WARN_ON_ONCE(netif_set_real_num_tx_queues(netdev, old_num_txqs));

err_tcs:
	WARN_ON_ONCE(mlx5e_netdev_set_tcs(netdev, old_num_txqs / old_ntc, old_ntc,
					  old_tc_to_txq));
err_out:
	return err;
}

#if defined(HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE) || defined(HAVE_NDO_SETUP_TC_RH_EXTENDED)
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
static MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_update_netdev_queues);
#endif
#endif
static void mlx5e_set_default_xps_cpumasks(struct mlx5e_priv *priv,
					   struct mlx5e_params *params)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int num_comp_vectors, ix, irq;

	num_comp_vectors = mlx5_comp_vectors_count(mdev);

	for (ix = 0; ix < params->num_channels; ix++) {
		cpumask_clear(priv->scratchpad.cpumask);

		for (irq = ix; irq < num_comp_vectors; irq += params->num_channels) {
			int cpu = cpumask_first(mlx5_comp_irq_get_affinity_mask(mdev, irq));

			cpumask_set_cpu(cpu, priv->scratchpad.cpumask);
		}

		netif_set_xps_queue(priv->netdev, priv->scratchpad.cpumask, ix);
	}
}

static int mlx5e_num_channels_changed(struct mlx5e_priv *priv)
{
	u16 count = priv->channels.params.num_channels;
	int err;

	err = mlx5e_update_netdev_queues(priv);
	if (err)
		return err;

	mlx5e_set_default_xps_cpumasks(priv, &priv->channels.params);

	/* This function may be called on attach, before priv->rx_res is created. */
#ifdef HAVE_NETIF_IS_RXFH_CONFIGURED
	if (!netif_is_rxfh_configured(priv->netdev) && priv->rx_res)
#else
	if (priv->rx_res)
#endif
		mlx5e_rx_res_rss_set_indir_uniform(priv->rx_res, count);

	return 0;
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_num_channels_changed);

static void mlx5e_build_txq_maps(struct mlx5e_priv *priv)
{
	int i, ch, tc, num_tc;

	ch = priv->channels.num;
	num_tc = mlx5e_get_dcb_num_tc(&priv->channels.params);

	for (i = 0; i < ch; i++) {
		for (tc = 0; tc < num_tc; tc++) {
			struct mlx5e_channel *c = priv->channels.c[i];
			struct mlx5e_txqsq *sq = &c->sq[tc];

			priv->txq2sq[sq->txq_ix] = sq;
		}
	}

	if (!priv->channels.ptp)
		goto out;

	if (!test_bit(MLX5E_PTP_STATE_TX, priv->channels.ptp->state))
		goto out;

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_ptp *c = priv->channels.ptp;
		struct mlx5e_txqsq *sq = &c->ptpsq[tc].txqsq;

		priv->txq2sq[sq->txq_ix] = sq;
	}

out:
	/* Make the change to txq2sq visible before the queue is started.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();
}

static void mlx5e_activate_priv_channels_rx(struct mlx5e_priv *priv)
{
	if (priv->shared_rq)
		return;

	mlx5e_wait_channels_min_rx_wqes(&priv->channels);

	if (priv->rx_res)
		mlx5e_rx_res_channels_activate(priv->rx_res, &priv->channels);

}

void mlx5e_activate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_build_txq_maps(priv);
	mlx5e_activate_channels(&priv->channels);
	if (priv->htb)
		mlx5e_qos_activate_queues(priv);
#ifdef HAVE_XDP_SUPPORT
	mlx5e_xdp_tx_enable(priv);
#endif

	/* dev_watchdog() wants all TX queues to be started when the carrier is
	 * OK, including the ones in range real_num_tx_queues..num_tx_queues-1.
	 * Make it happy to avoid TX timeout false alarms.
	 */
	netif_tx_start_all_queues(priv->netdev);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_rep_activate_channels(priv);

	mlx5e_activate_priv_channels_rx(priv);
}

static void mlx5e_deactivate_priv_channels_rx(struct mlx5e_priv *priv)
{
	if (priv->shared_rq)
		return;

	if (priv->rx_res)
		mlx5e_rx_res_channels_deactivate(priv->rx_res);
}

void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_deactivate_priv_channels_rx(priv);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_rep_deactivate_channels(priv);

	/* The results of ndo_select_queue are unreliable, while netdev config
	 * is being changed (real_num_tx_queues, num_tc). Stop all queues to
	 * prevent ndo_start_xmit from being called, so that it can assume that
	 * the selected queue is always valid.
	 */
	netif_tx_disable(priv->netdev);

#ifdef HAVE_XDP_SUPPORT
	mlx5e_xdp_tx_disable(priv);
#endif
	mlx5e_deactivate_channels(&priv->channels);
}

static int mlx5e_switch_priv_params(struct mlx5e_priv *priv,
				    struct mlx5e_params *new_params,
				    mlx5e_fp_preactivate preactivate,
				    void *context)
{
	struct mlx5e_params old_params;

	old_params = priv->channels.params;
	priv->channels.params = *new_params;

	if (preactivate) {
		int err;

		err = preactivate(priv, context);
		if (err) {
			priv->channels.params = old_params;
			return err;
		}
	}

	return 0;
}

static int mlx5e_switch_priv_channels(struct mlx5e_priv *priv,
				      struct mlx5e_channels *new_chs,
				      mlx5e_fp_preactivate preactivate,
				      void *context)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_channels old_chs;
	int carrier_ok;
	int err = 0;

	carrier_ok = netif_carrier_ok(netdev);
	netif_carrier_off(netdev);

	mlx5e_deactivate_priv_channels(priv);

	old_chs = priv->channels;
	priv->channels = *new_chs;

	/* New channels are ready to roll, call the preactivate hook if needed
	 * to modify HW settings or update kernel parameters.
	 */
	if (preactivate) {
		err = preactivate(priv, context);
		if (err) {
			priv->channels = old_chs;
			goto out;
		}
	}

	mlx5e_close_channels(&old_chs);
	priv->profile->update_rx(priv);

	mlx5e_selq_apply(&priv->selq);
out:
	mlx5e_activate_priv_channels(priv);

	/* return carrier back if needed */
	if (carrier_ok)
		netif_carrier_on(netdev);

	return err;
}

int mlx5e_safe_switch_params(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     mlx5e_fp_preactivate preactivate,
			     void *context, bool reset)
{
	struct mlx5e_channels new_chs = {};
	int err;

	reset &= test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (!reset)
		return mlx5e_switch_priv_params(priv, params, preactivate, context);

	new_chs.params = *params;

	mlx5e_selq_prepare_params(&priv->selq, &new_chs.params);

	err = mlx5e_open_channels(priv, &new_chs);
	if (err)
		goto err_cancel_selq;

	err = mlx5e_switch_priv_channels(priv, &new_chs, preactivate, context);
	if (err)
		goto err_close;

	return 0;

err_close:
	mlx5e_close_channels(&new_chs);

err_cancel_selq:
	mlx5e_selq_cancel(&priv->selq);
	return err;
}

int mlx5e_safe_reopen_channels(struct mlx5e_priv *priv)
{
	return mlx5e_safe_switch_params(priv, &priv->channels.params, NULL, NULL, true);
}

void mlx5e_timestamp_init(struct mlx5e_priv *priv)
{
	priv->tstamp.tx_type   = HWTSTAMP_TX_OFF;
	priv->tstamp.rx_filter = HWTSTAMP_FILTER_NONE;
}

static void mlx5e_modify_admin_state(struct mlx5_core_dev *mdev,
				     enum mlx5_port_status state)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int vport_admin_state;

	mlx5_set_port_admin_status(mdev, state);

	if (mlx5_eswitch_mode(mdev) == MLX5_ESWITCH_OFFLOADS ||
	    !MLX5_CAP_GEN(mdev, uplink_follow))
		return;

	if (state == MLX5_PORT_UP)
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_AUTO;
	else
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_DOWN;

	mlx5_eswitch_set_vport_state(esw, MLX5_VPORT_UPLINK, vport_admin_state);
}

int mlx5e_open_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mlx5e_selq_prepare_params(&priv->selq, &priv->channels.params);

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	err = mlx5e_open_channels(priv, &priv->channels);
	if (err)
		goto err_clear_state_opened_flag;

	err = priv->profile->update_rx(priv);
	if (err)
		goto err_close_channels;

	mlx5e_selq_apply(&priv->selq);
	mlx5e_activate_priv_channels(priv);
	if (!mlx5e_is_uplink_rep(priv) && !mlx5e_is_vport_rep(priv))
		mlx5e_create_debugfs(priv);
	mlx5e_apply_traps(priv, true);
	if (priv->profile->update_carrier)
		priv->profile->update_carrier(priv);

	mlx5e_queue_update_stats(priv);
	return 0;

err_close_channels:
	mlx5e_close_channels(&priv->channels);
err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &priv->state);
	mlx5e_selq_cancel(&priv->selq);
	return err;
}

int mlx5e_open(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_open_locked(netdev);
	if (!err)
		mlx5e_modify_admin_state(priv->mdev, MLX5_PORT_UP);
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_close_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	/* May already be CLOSED in case a previous configuration operation
	 * (e.g RX/TX queue size change) that involves close&open failed.
	 */
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	mlx5e_apply_traps(priv, false);
	clear_bit(MLX5E_STATE_OPENED, &priv->state);

	netif_carrier_off(priv->netdev);
	if (!mlx5e_is_uplink_rep(priv) && !mlx5e_is_vport_rep(priv))
		mlx5e_destroy_debugfs(priv);
	mlx5e_deactivate_priv_channels(priv);
	mlx5e_close_channels(&priv->channels);

	return 0;
}

int mlx5e_close(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (!netif_device_present(netdev))
		return -ENODEV;

	mutex_lock(&priv->state_lock);
	mlx5e_modify_admin_state(priv->mdev, MLX5_PORT_DOWN);
	err = mlx5e_close_locked(netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

static void mlx5e_free_drop_rq(struct mlx5e_rq *rq)
{
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_alloc_drop_rq(struct mlx5_core_dev *mdev,
			       struct mlx5e_rq *rq,
			       struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int err;

	param->wq.db_numa_node = param->wq.buf_numa_node;

	err = mlx5_wq_cyc_create(mdev, &param->wq, rqc_wq, &rq->wqe.wq,
				 &rq->wq_ctrl);
	if (err)
		return err;

	/* Mark as unused given "Drop-RQ" packets never reach XDP */
#ifdef HAVE_XDP_SUPPORT
#ifdef HAVE_XDP_RXQ_INFO
	xdp_rxq_info_unused(&rq->xdp_rxq);
#endif
#endif
	rq->mdev = mdev;

	return 0;
}

static int mlx5e_alloc_drop_cq(struct mlx5e_priv *priv,
			       struct mlx5e_cq *cq,
			       struct mlx5e_cq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
	param->wq.db_numa_node  = dev_to_node(mlx5_core_dma_dev(mdev));

	return mlx5e_alloc_cq_common(priv, param, cq);
}

int mlx5e_open_drop_rq(struct mlx5e_priv *priv,
		       struct mlx5e_rq *drop_rq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_cq_param cq_param = {};
	struct mlx5e_rq_param rq_param = {};
	struct mlx5e_cq *cq = &drop_rq->cq;
	int err;

	mlx5e_build_drop_rq_param(mdev, priv->drop_rq_q_counter, &rq_param);

	err = mlx5e_alloc_drop_cq(priv, cq, &cq_param);
	if (err)
		return err;

	err = mlx5e_create_cq(cq, &cq_param);
	if (err)
		goto err_free_cq;

	err = mlx5e_alloc_drop_rq(mdev, drop_rq, &rq_param);
	if (err)
		goto err_destroy_cq;

	err = mlx5e_create_rq(drop_rq, &rq_param);
	if (err)
		goto err_free_rq;

	err = mlx5e_modify_rq_state(drop_rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		mlx5_core_warn(priv->mdev, "modify_rq_state failed, rx_if_down_packets won't be counted %d\n", err);

	return 0;

err_free_rq:
	mlx5e_free_drop_rq(drop_rq);

err_destroy_cq:
	mlx5e_destroy_cq(cq);

err_free_cq:
	mlx5e_free_cq(cq);

	return err;
}

void mlx5e_close_drop_rq(struct mlx5e_rq *drop_rq)
{
	mlx5e_destroy_rq(drop_rq);
	mlx5e_free_drop_rq(drop_rq);
	mlx5e_destroy_cq(&drop_rq->cq);
	mlx5e_free_cq(&drop_rq->cq);
}

int mlx5e_create_tis(struct mlx5_core_dev *mdev, void *in, u32 *tisn)
{
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, transport_domain, mdev->mlx5e_res.hw_objs.td.tdn);

	if (MLX5_GET(tisc, tisc, tls_en))
		MLX5_SET(tisc, tisc, pd, mdev->mlx5e_res.hw_objs.pdn);

	if (mlx5_lag_is_lacp_owner(mdev))
		MLX5_SET(tisc, tisc, strict_lag_tx_port_affinity, 1);

	return mlx5_core_create_tis(mdev, in, tisn);
}

void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn)
{
	mlx5_core_destroy_tis(mdev, tisn);
}

void mlx5e_destroy_tises(struct mlx5e_priv *priv)
{
	int tc, i;

	for (i = 0; i < mlx5e_get_num_lag_ports(priv->mdev); i++)
		for (tc = 0; tc < priv->profile->max_tc; tc++)
			mlx5e_destroy_tis(priv->mdev, priv->tisn[i][tc]);
}

static bool mlx5e_lag_should_assign_affinity(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, lag_tx_port_affinity) && mlx5e_get_num_lag_ports(mdev) > 1;
}

int mlx5e_create_tises(struct mlx5e_priv *priv)
{
	int tc, i;
	int err;

	for (i = 0; i < mlx5e_get_num_lag_ports(priv->mdev); i++) {
		for (tc = 0; tc < priv->profile->max_tc; tc++) {
			u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
			void *tisc;

			tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

			MLX5_SET(tisc, tisc, prio, tc << 1);

			if (mlx5e_lag_should_assign_affinity(priv->mdev))
				MLX5_SET(tisc, tisc, lag_tx_port_affinity, i + 1);

			err = mlx5e_create_tis(priv->mdev, in, &priv->tisn[i][tc]);
			if (err)
				goto err_close_tises;
		}
	}

	return 0;

err_close_tises:
	for (; i >= 0; i--) {
		for (tc--; tc >= 0; tc--)
			mlx5e_destroy_tis(priv->mdev, priv->tisn[i][tc]);
		tc = priv->profile->max_tc;
	}

	return err;
}

static void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv)
{
	if (priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
		priv->mqprio_rl = NULL;
	}
	mlx5e_accel_cleanup_tx(priv);
	mlx5e_destroy_tises(priv);
}

static int mlx5e_modify_channels_scatter_fcs(struct mlx5e_channels *chs, bool enable)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_modify_rq_scatter_fcs(&chs->c[i]->rq, enable);
		if (err)
			return err;
	}

	return 0;
}

#ifndef LEGACY_ETHTOOL_OPS
static
#endif
int mlx5e_modify_channels_vsd(struct mlx5e_channels *chs, bool vsd)
{
	int err;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_modify_rq_vsd(&chs->c[i]->rq, vsd);
		if (err)
			return err;
	}
	if (chs->ptp && test_bit(MLX5E_PTP_STATE_RX, chs->ptp->state))
		return mlx5e_modify_rq_vsd(&chs->ptp->rq, vsd);

	return 0;
}

static void mlx5e_mqprio_build_default_tc_to_txq(struct netdev_tc_txq *tc_to_txq,
						 int ntc, int nch)
{
	int tc;

	memset(tc_to_txq, 0, sizeof(*tc_to_txq) * TC_MAX_QUEUE);

	/* Map netdev TCs to offset 0.
	 * We have our own UP to TXQ mapping for DCB mode of QoS
	 */
	for (tc = 0; tc < ntc; tc++) {
		tc_to_txq[tc] = (struct netdev_tc_txq) {
			.count = nch,
			.offset = 0,
		};
	}
}

static void mlx5e_params_mqprio_dcb_set(struct mlx5e_params *params, u8 num_tc)
{
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
	params->mqprio.mode = TC_MQPRIO_MODE_DCB;
#endif
	params->mqprio.num_tc = num_tc;
	mlx5e_mqprio_build_default_tc_to_txq(params->mqprio.tc_to_txq, num_tc,
					     params->num_channels);
}

static void mlx5e_params_mqprio_reset(struct mlx5e_params *params)
{
	mlx5e_params_mqprio_dcb_set(params, 1);
}

#if defined(HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE) || defined(HAVE_NDO_SETUP_TC_RH_EXTENDED)
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
static void mlx5e_mqprio_build_tc_to_txq(struct netdev_tc_txq *tc_to_txq,
					 struct tc_mqprio_qopt *qopt)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++) {
		tc_to_txq[tc] = (struct netdev_tc_txq) {
			.count = qopt->count[tc],
			.offset = qopt->offset[tc],
		};
	}
}

static void mlx5e_mqprio_rl_update_params(struct mlx5e_params *params,
					  struct mlx5e_mqprio_rl *rl)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++) {
		u32 hw_id = 0;

		if (rl)
			mlx5e_mqprio_rl_get_node_hw_id(rl, tc, &hw_id);
		params->mqprio.channel.hw_id[tc] = hw_id;
	}
}

static void mlx5e_params_mqprio_channel_set(struct mlx5e_params *params,
					    struct tc_mqprio_qopt_offload *mqprio,
					    struct mlx5e_mqprio_rl *rl)
{
	int tc;

	params->mqprio.mode = TC_MQPRIO_MODE_CHANNEL;
	params->mqprio.num_tc = mqprio->qopt.num_tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++)
		params->mqprio.channel.max_rate[tc] = mqprio->max_rate[tc];

	mlx5e_mqprio_rl_update_params(params, rl);
	mlx5e_mqprio_build_tc_to_txq(params->mqprio.tc_to_txq, &mqprio->qopt);
}
#endif

static int mlx5e_setup_tc_mqprio_dcb(struct mlx5e_priv *priv,
				     struct tc_mqprio_qopt *mqprio)
{
	struct mlx5e_params new_params;
	u8 tc = mqprio->num_tc;
	int err;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
	if (tc && tc != MLX5E_MAX_NUM_TC
#ifdef CONFIG_MLX5_CORE_EN_DCB
			&& priv->dcbx_dp.trust_state != MLX5_QPTS_TRUST_PCP
#endif
			)
		return -EINVAL;

	new_params = priv->channels.params;
	mlx5e_params_mqprio_dcb_set(&new_params, tc ? tc : 1);

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_PCP)
		priv->pcp_tc_num = tc;
#endif

	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_num_channels_changed_ctx, NULL, true);

	if (!err && priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
		priv->mqprio_rl = NULL;
	}

	priv->max_opened_tc = max_t(u8, priv->max_opened_tc,
				    mlx5e_get_dcb_num_tc(&priv->channels.params));
	return err;
}

#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
static int mlx5e_mqprio_channel_validate(struct mlx5e_priv *priv,
					 struct tc_mqprio_qopt_offload *mqprio)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_ptp *ptp_channel;
	int agg_count = 0;
	int i;

	ptp_channel = priv->channels.ptp;
	if (ptp_channel && test_bit(MLX5E_PTP_STATE_TX, ptp_channel->state)) {
		netdev_err(netdev,
			   "Cannot activate MQPRIO mode channel since it conflicts with TX port TS\n");
		return -EINVAL;
	}

	if (mqprio->qopt.offset[0] != 0 || mqprio->qopt.num_tc < 1 ||
	    mqprio->qopt.num_tc > MLX5E_MAX_NUM_MQPRIO_CH_TC)
		return -EINVAL;

	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		if (!mqprio->qopt.count[i]) {
			netdev_err(netdev, "Zero size for queue-group (%d) is not supported\n", i);
			return -EINVAL;
		}
		if (mqprio->min_rate[i]) {
			netdev_err(netdev, "Min tx rate is not supported\n");
			return -EINVAL;
		}

		if (mqprio->max_rate[i]) {
			int err;

			err = mlx5e_qos_bytes_rate_check(priv->mdev, mqprio->max_rate[i]);
			if (err)
				return err;
		}

		if (mqprio->qopt.offset[i] != agg_count) {
			netdev_err(netdev, "Discontinuous queues config is not supported\n");
			return -EINVAL;
		}
		agg_count += mqprio->qopt.count[i];
	}

	if (priv->channels.params.num_channels != agg_count) {
		netdev_err(netdev, "Num of queues (%d) does not match available (%d)\n",
			   agg_count, priv->channels.params.num_channels);
		return -EINVAL;
	}

	return 0;
}

static bool mlx5e_mqprio_rate_limit(u8 num_tc, u64 max_rate[])
{
	int tc;

	for (tc = 0; tc < num_tc; tc++)
		if (max_rate[tc])
			return true;
	return false;
}

static struct mlx5e_mqprio_rl *mlx5e_mqprio_rl_create(struct mlx5_core_dev *mdev,
						      u8 num_tc, u64 max_rate[])
{
	struct mlx5e_mqprio_rl *rl;
	int err;

	if (!mlx5e_mqprio_rate_limit(num_tc, max_rate))
		return NULL;

	rl = mlx5e_mqprio_rl_alloc();
	if (!rl)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_mqprio_rl_init(rl, mdev, num_tc, max_rate);
	if (err) {
		mlx5e_mqprio_rl_free(rl);
		return ERR_PTR(err);
	}

	return rl;
}

static int mlx5e_setup_tc_mqprio_channel(struct mlx5e_priv *priv,
					 struct tc_mqprio_qopt_offload *mqprio)
{
	mlx5e_fp_preactivate preactivate;
	struct mlx5e_params new_params;
	struct mlx5e_mqprio_rl *rl;
	bool nch_changed;
	int err;

	err = mlx5e_mqprio_channel_validate(priv, mqprio);
	if (err)
		return err;

	rl = mlx5e_mqprio_rl_create(priv->mdev, mqprio->qopt.num_tc, mqprio->max_rate);
	if (IS_ERR(rl))
		return PTR_ERR(rl);

	new_params = priv->channels.params;
	mlx5e_params_mqprio_channel_set(&new_params, mqprio, rl);

	nch_changed = mlx5e_get_dcb_num_tc(&priv->channels.params) > 1;
	preactivate = nch_changed ? mlx5e_num_channels_changed_ctx :
		mlx5e_update_netdev_queues_ctx;
	err = mlx5e_safe_switch_params(priv, &new_params, preactivate, NULL, true);
	if (err) {
		if (rl) {
			mlx5e_mqprio_rl_cleanup(rl);
			mlx5e_mqprio_rl_free(rl);
		}
		return err;
	}

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (!err && priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_PCP)
		priv->pcp_tc_num = mqprio->qopt.num_tc;
#endif

	if (priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
	}
	priv->mqprio_rl = rl;

	return 0;
}
#endif
#endif

#if defined(HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE) || defined(HAVE_NDO_SETUP_TC_RH_EXTENDED)
int mlx5e_setup_tc_mqprio(struct mlx5e_priv *priv,
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
				struct tc_mqprio_qopt_offload *mqprio
#else
				struct tc_mqprio_qopt *mqprio
#endif
)
{
	/* MQPRIO is another toplevel qdisc that can't be attached
	 * simultaneously with the offloaded HTB.
	 */
	if (WARN_ON(mlx5e_selq_is_htb_enabled(&priv->selq)))
		return -EINVAL;

#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
	switch (mqprio->mode) {
	case TC_MQPRIO_MODE_DCB:
		return mlx5e_setup_tc_mqprio_dcb(priv, &mqprio->qopt);
	case TC_MQPRIO_MODE_CHANNEL:
		return mlx5e_setup_tc_mqprio_channel(priv, mqprio);
	default:
		return -EOPNOTSUPP;
	}
#else
	return mlx5e_setup_tc_mqprio_dcb(priv, mqprio);
#endif

}
#else
int mlx5e_setup_tc(struct net_device *netdev, u8 tc)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	int err = 0;

	if (tc && tc != MLX5E_MAX_NUM_TC
#ifdef CONFIG_MLX5_CORE_EN_DCB
			&& priv->dcbx_dp.trust_state != MLX5_QPTS_TRUST_PCP
#endif
			)
                return -EINVAL;

        mutex_lock(&priv->state_lock);

        /* MQPRIO is another toplevel qdisc that can't be attached
 *          * simultaneously with the offloaded HTB.
 *                   */
        if (WARN_ON(mlx5e_selq_is_htb_enabled(&priv->selq))) {
                err = -EINVAL;
                goto out;
        }

        new_params = priv->channels.params;
        mlx5e_params_mqprio_dcb_set(&new_params, tc ? tc : 1);

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_PCP)
		priv->pcp_tc_num = tc;
#endif

        err = mlx5e_safe_switch_params(priv, &new_params,
                                       mlx5e_num_channels_changed_ctx, NULL, true);

out:
	priv->max_opened_tc = max_t(u8, priv->max_opened_tc,
				    mlx5e_get_dcb_num_tc(&priv->channels.params));
        mutex_unlock(&priv->state_lock);
        return err;
}
#endif

#if defined(HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE) || defined(HAVE_NDO_SETUP_TC_RH_EXTENDED)
#ifdef HAVE_FLOW_CLS_OFFLOAD
static LIST_HEAD(mlx5e_block_cb_list);
#endif

#ifdef HAVE_TC_SETUP_CB_EGDEV_REGISTER
int mlx5e_setup_tc(struct net_device *dev, enum tc_setup_type type,
		   void *type_data)
#else
static int mlx5e_setup_tc(struct net_device *dev, enum tc_setup_type type,
			  void *type_data)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	bool tc_unbind = false;
	int err;

#if defined(HAVE_TC_BLOCK_OFFLOAD) || defined(HAVE_FLOW_BLOCK_OFFLOAD)
	if (type == TC_SETUP_BLOCK &&
	    ((struct flow_block_offload *)type_data)->command == FLOW_BLOCK_UNBIND)
		tc_unbind = true;
#endif

	if (!netif_device_present(dev) && !tc_unbind)
		return -ENODEV;

	switch (type) {
#ifdef CONFIG_MLX5_ESWITCH
#if defined(HAVE_TC_BLOCK_OFFLOAD) || defined(HAVE_FLOW_BLOCK_OFFLOAD)
#ifdef HAVE_FLOW_BLOCK_CB_SETUP_SIMPLE
	case TC_SETUP_BLOCK: {
#ifdef HAVE_UNLOCKED_DRIVER_CB
		struct flow_block_offload *f = type_data;

		f->unlocked_driver_cb = true;
#endif
		return flow_block_cb_setup_simple(type_data,
						  &mlx5e_block_cb_list,
						  mlx5e_setup_tc_block_cb,
						  priv, priv, true);
	}
#else /* HAVE_FLOW_BLOCK_CB_SETUP_SIMPLE */
	case TC_SETUP_BLOCK:
		return mlx5e_setup_tc_block(dev, type_data);
#endif /* HAVE_FLOW_BLOCK_CB_SETUP_SIMPLE */
#else
	case TC_SETUP_CLSFLOWER:
#ifdef CONFIG_MLX5_CLS_ACT
		return mlx5e_setup_tc_cls_flower(dev, type_data, MLX5_TC_FLAG(INGRESS));
#endif
#endif /* HAVE_TC_BLOCK_OFFLOAD || HAVE_FLOW_BLOCK_OFFLOAD */
#endif /* CONFIG_MLX5_ESWITCH */
	case TC_SETUP_QDISC_MQPRIO:
		mutex_lock(&priv->state_lock);
		err = mlx5e_setup_tc_mqprio(priv, type_data);
		mutex_unlock(&priv->state_lock);
		return err;
#ifdef HAVE_ENUM_TC_HTB_COMMAND
	case TC_SETUP_QDISC_HTB:
		mutex_lock(&priv->state_lock);
		err = mlx5e_htb_setup_tc(priv, type_data);
		mutex_unlock(&priv->state_lock);
		return err;
#endif
	default:
		return -EOPNOTSUPP;
	}
}
#else /* HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE || HAVE_NDO_SETUP_TC_RH_EXTENDED */
#if defined(HAVE_NDO_SETUP_TC_4_PARAMS) || defined(HAVE_NDO_SETUP_TC_TAKES_CHAIN_INDEX)
static int mlx5e_ndo_setup_tc(struct net_device *dev, u32 handle,
#ifdef HAVE_NDO_SETUP_TC_TAKES_CHAIN_INDEX
			      u32 chain_index, __be16 proto,
#else
			      __be16 proto,
#endif
			      struct tc_to_netdev *tc)
{
#ifdef HAVE_TC_FLOWER_OFFLOAD
#ifdef CONFIG_MLX5_CLS_ACT
	struct mlx5e_priv *priv = netdev_priv(dev);
#endif /*CONFIG_MLX5_CLS_ACT*/

	if (!netif_device_present(dev))
		return -EOPNOTSUPP;

	if (TC_H_MAJ(handle) != TC_H_MAJ(TC_H_INGRESS))
		goto mqprio;

#ifdef HAVE_NDO_SETUP_TC_TAKES_CHAIN_INDEX
	if (chain_index)
		return -EOPNOTSUPP;
#endif

	switch (tc->type) {
#ifdef CONFIG_MLX5_CLS_ACT
	case TC_SETUP_CLSFLOWER:
		switch (tc->cls_flower->command) {
		case TC_CLSFLOWER_REPLACE:
			return mlx5e_configure_flower(priv->netdev, priv, tc->cls_flower,
						      MLX5_TC_FLAG(INGRESS));
		case TC_CLSFLOWER_DESTROY:
			return mlx5e_delete_flower(priv->netdev, priv, tc->cls_flower,
						   MLX5_TC_FLAG(INGRESS));
#ifdef HAVE_TC_CLSFLOWER_STATS
		case TC_CLSFLOWER_STATS:
			return mlx5e_stats_flower(priv->netdev, priv, tc->cls_flower,
						  MLX5_TC_FLAG(INGRESS));
#endif
		}
#endif /*CONFIG_MLX5_CLS_ACT*/
	default:
		return -EOPNOTSUPP;
	}

mqprio:
#endif /* HAVE_TC_FLOWER_OFFLOAD */
	if (tc->type != TC_SETUP_MQPRIO)
		return -EINVAL;

#ifdef HAVE_TC_TO_NETDEV_TC
	return mlx5e_setup_tc(dev, tc->tc);
#else
	tc->mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	return mlx5e_setup_tc(dev, tc->mqprio->num_tc);
#endif /* HAVE_TC_TO_NETDEV_TC */
}
#endif /* HAVE_NDO_SETUP_TC_4_PARAMS || HAVE_NDO_SETUP_TC_TAKES_CHAIN_INDEX */
#endif /* HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE || HAVE_NDO_SETUP_TC_RH_EXTENDED */

void mlx5e_fold_sw_stats64(struct mlx5e_priv *priv, struct rtnl_link_stats64 *s)
{
	int i;

	for (i = 0; i < priv->stats_nch; i++) {
		struct mlx5e_channel_stats *channel_stats = priv->channel_stats[i];
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		struct mlx5e_rq_stats *xskrq_stats = &channel_stats->xskrq;
#endif
		struct mlx5e_rq_stats *rq_stats = &channel_stats->rq;
		int j;

		s->rx_packets   += rq_stats->packets
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				+ xskrq_stats->packets
#endif
				;
		s->rx_bytes     += rq_stats->bytes
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				+ xskrq_stats->bytes
#endif
				;
		s->multicast    += rq_stats->mcast_packets
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				+ xskrq_stats->mcast_packets
#endif
				;

		for (j = 0; j < priv->max_opened_tc; j++) {
			struct mlx5e_sq_stats *sq_stats = &channel_stats->sq[j];

			s->tx_packets    += sq_stats->packets;
			s->tx_bytes      += sq_stats->bytes;
			s->tx_dropped    += sq_stats->dropped;
		}
	}
	if (priv->tx_ptp_opened) {
		for (i = 0; i < priv->max_opened_tc; i++) {
			struct mlx5e_sq_stats *sq_stats = &priv->ptp_stats.sq[i];

			s->tx_packets    += sq_stats->packets;
			s->tx_bytes      += sq_stats->bytes;
			s->tx_dropped    += sq_stats->dropped;
		}
	}
	if (priv->rx_ptp_opened) {
		struct mlx5e_rq_stats *rq_stats = &priv->ptp_stats.rq;

		s->rx_packets   += rq_stats->packets;
		s->rx_bytes     += rq_stats->bytes;
		s->multicast    += rq_stats->mcast_packets;
	}
}

#ifdef HAVE_NDO_GET_STATS64_RET_VOID
void mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
#elif defined(HAVE_NDO_GET_STATS64)
struct rtnl_link_stats64 * mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
#else
struct net_device_stats * mlx5e_get_stats(struct net_device *dev)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
#if !defined(HAVE_NDO_GET_STATS64) && !defined(HAVE_NDO_GET_STATS64_RET_VOID)
	struct net_device_stats *stats = &priv->netdev_stats;
#endif

	if (!netif_device_present(dev))
#ifdef HAVE_NDO_GET_STATS64_RET_VOID
		return;
#else
		return stats;
#endif

	/* In switchdev mode, monitor counters doesn't monitor
	 * rx/tx stats of 802_3. The update stats mechanism
	 * should keep the 802_3 layout counters updated
	 */
	if (!mlx5e_monitor_counter_supported(priv) ||
	    mlx5e_is_uplink_rep(priv)) {
		/* update HW stats in background for next time */
		mlx5e_queue_update_stats(priv);
	}

	if (mlx5e_is_uplink_rep(priv)) {
		struct mlx5e_vport_stats *vstats = &priv->stats.vport;

		stats->rx_packets = PPORT_802_3_GET(pstats, a_frames_received_ok);
		stats->rx_bytes   = PPORT_802_3_GET(pstats, a_octets_received_ok);
		stats->tx_packets = PPORT_802_3_GET(pstats, a_frames_transmitted_ok);
		stats->tx_bytes   = PPORT_802_3_GET(pstats, a_octets_transmitted_ok);

		/* vport multicast also counts packets that are dropped due to steering
		 * or rx out of buffer
		 */
		stats->multicast = VPORT_COUNTER_GET(vstats, received_eth_multicast.packets);
	} else {
		mlx5e_fold_sw_stats64(priv, stats);
	}

	stats->rx_dropped = priv->stats.qcnt.rx_out_of_buffer;

	stats->rx_length_errors =
		PPORT_802_3_GET(pstats, a_in_range_length_errors) +
		PPORT_802_3_GET(pstats, a_out_of_range_length_field) +
		PPORT_802_3_GET(pstats, a_frame_too_long_errors) +
		VNIC_ENV_GET(&priv->stats.vnic, eth_wqe_too_small);
	stats->rx_crc_errors =
		PPORT_802_3_GET(pstats, a_frame_check_sequence_errors);
	stats->rx_frame_errors = PPORT_802_3_GET(pstats, a_alignment_errors);
	stats->tx_aborted_errors = PPORT_2863_GET(pstats, if_out_discards);
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
			   stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_carrier_errors;

#ifndef HAVE_NDO_GET_STATS64_RET_VOID
	return stats;
#endif
}

static void mlx5e_nic_set_rx_mode(struct mlx5e_priv *priv)
{
	if (mlx5e_is_uplink_rep(priv))
		return; /* no rx mode for uplink rep */

	queue_work(priv->wq, &priv->set_rx_mode_work);
}

static void mlx5e_set_rx_mode(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_nic_set_rx_mode(priv);
}

static int mlx5e_set_mac(struct net_device *netdev, void *addr)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	netif_addr_lock_bh(netdev);
#ifdef HAVE_DEV_ADDR_MOD
	eth_hw_addr_set(netdev, saddr->sa_data);
#else
	ether_addr_copy(netdev->dev_addr, saddr->sa_data);
#endif
	netif_addr_unlock_bh(netdev);

	mlx5e_nic_set_rx_mode(priv);

	return 0;
}

#define MLX5E_SET_FEATURE(features, feature, enable)	\
	do {						\
		if (enable)				\
			*features |= feature;		\
		else					\
			*features &= ~feature;		\
	} while (0)

typedef int (*mlx5e_feature_handler)(struct net_device *netdev, bool enable);

static int set_feature_lro(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params *cur_params;
	struct mlx5e_params new_params;
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);

	cur_params = &priv->channels.params;
	new_params = *cur_params;

	if (enable)
#if defined(CONFIG_COMPAT_LRO_ENABLED_IPOIB)
	{
		new_params.lro_en = true;
		if (IS_HW_LRO(&new_params))
			new_params.packet_merge.type = MLX5E_PACKET_MERGE_LRO;
	}
	else if (new_params.packet_merge.type == MLX5E_PACKET_MERGE_LRO) {
		new_params.lro_en = false;
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_NONE;

	} else {
		new_params.lro_en = false;
		goto update_params;
	}
#else
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_LRO;
	else if (new_params.packet_merge.type == MLX5E_PACKET_MERGE_LRO)
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_NONE;
	else
		goto out;
#endif

	if (!(cur_params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO &&
	      new_params.packet_merge.type == MLX5E_PACKET_MERGE_LRO)) {
		if (cur_params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			if (mlx5e_rx_mpwqe_is_linear_skb(mdev, cur_params, NULL) ==
			    mlx5e_rx_mpwqe_is_linear_skb(mdev, &new_params, NULL))
				reset = false;
		}
	}

#ifdef HAVE_BASECODE_EXTRAS
	if ((new_params.packet_merge.type != MLX5E_PACKET_MERGE_NONE) &&
	    !MLX5E_GET_PFLAG(cur_params, MLX5E_PFLAG_RX_STRIDING_RQ)) {
		netdev_warn(netdev, "can't set HW LRO with legacy RQ\n");
		err = -EINVAL;
		goto out;
	}
#endif

#if defined(CONFIG_COMPAT_LRO_ENABLED_IPOIB)
update_params:
#endif
	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_modify_tirs_packet_merge_ctx, NULL, reset);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}

#ifdef HAVE_NETIF_F_GRO_HW
static int set_feature_hw_gro(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);
	new_params = priv->channels.params;

	if (enable) {
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_SHAMPO;
		new_params.packet_merge.shampo.match_criteria_type =
			MLX5_RQC_SHAMPO_MATCH_CRITERIA_TYPE_EXTENDED;
		new_params.packet_merge.shampo.alignment_granularity =
			MLX5_RQC_SHAMPO_NO_MATCH_ALIGNMENT_GRANULARITY_STRIDE;
	} else if (new_params.packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO) {
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_NONE;
	} else {
		goto out;
	}

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, reset);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}
#endif

static int set_feature_cvlan_filter(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (enable)
		mlx5e_enable_cvlan_filter(priv);
	else
		mlx5e_disable_cvlan_filter(priv);

	return 0;
}

static int set_feature_hw_tc(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	int tc_flag = mlx5e_is_uplink_rep(priv) ? MLX5_TC_FLAG(ESW_OFFLOAD) :
						  MLX5_TC_FLAG(NIC_OFFLOAD);
	if (!enable && mlx5e_tc_num_filters(priv, tc_flag)) {
		netdev_err(netdev,
			   "Active offloaded tc filters, can't turn hw_tc_offload off\n");
		return -EINVAL;
	}
#endif

	mutex_lock(&priv->state_lock);
	if (!enable && mlx5e_selq_is_htb_enabled(&priv->selq)) {
		netdev_err(netdev, "Active HTB offload, can't turn hw_tc_offload off\n");
		err = -EINVAL;
	}
	mutex_unlock(&priv->state_lock);

	return err;
}

static int set_feature_rx_all(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_set_port_fcs(mdev, !enable);
}

static int mlx5e_set_rx_port_ts(struct mlx5_core_dev *mdev, bool enable)
{
	u32 in[MLX5_ST_SZ_DW(pcmr_reg)] = {};
	bool supported, curr_state;
	int err;

	if (!MLX5_CAP_GEN(mdev, ports_check))
		return 0;

	err = mlx5_query_ports_check(mdev, in, sizeof(in));
	if (err)
		return err;

	supported = MLX5_GET(pcmr_reg, in, rx_ts_over_crc_cap);
	curr_state = MLX5_GET(pcmr_reg, in, rx_ts_over_crc);

	if (!supported || enable == curr_state)
		return 0;

	MLX5_SET(pcmr_reg, in, local_port, 1);
	MLX5_SET(pcmr_reg, in, rx_ts_over_crc, enable);

	return mlx5_set_ports_check(mdev, in, sizeof(in));
}

static int set_feature_rx_fcs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels *chs = &priv->channels;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	mutex_lock(&priv->state_lock);

	if (enable) {
		err = mlx5e_set_rx_port_ts(mdev, false);
		if (err)
			goto out;

		chs->params.scatter_fcs_en = true;
		err = mlx5e_modify_channels_scatter_fcs(chs, true);
		if (err) {
			chs->params.scatter_fcs_en = false;
			mlx5e_set_rx_port_ts(mdev, true);
		}
	} else {
		chs->params.scatter_fcs_en = false;
		err = mlx5e_modify_channels_scatter_fcs(chs, false);
		if (err) {
			chs->params.scatter_fcs_en = true;
			goto out;
		}
		err = mlx5e_set_rx_port_ts(mdev, true);
		if (err) {
			mlx5_core_warn(mdev, "Failed to set RX port timestamp %d\n", err);
			err = 0;
		}
	}

out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int set_feature_rx_vlan(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

	mutex_lock(&priv->state_lock);

	priv->fs->vlan_strip_disable = !enable;
	priv->channels.params.vlan_strip_disable = !enable;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	err = mlx5e_modify_channels_vsd(&priv->channels, !enable);
	if (err) {
		priv->fs->vlan_strip_disable = enable;
		priv->channels.params.vlan_strip_disable = enable;
	}
unlock:
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_flow_steering *fs = priv->fs;

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	return mlx5e_fs_vlan_rx_add_vid(fs, dev, proto, vid);
}

int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_flow_steering *fs = priv->fs;

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	return mlx5e_fs_vlan_rx_kill_vid(fs, dev, proto, vid);
}

#ifdef CONFIG_MLX5_EN_ARFS
#ifndef HAVE_NET_FLOW_KEYS_H
static int set_feature_arfs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (enable)
		err = mlx5e_arfs_enable(priv);
	else
		err = mlx5e_arfs_disable(priv);

	return err;
}
#endif
#endif

static int mlx5e_handle_feature(struct net_device *netdev,
				netdev_features_t *features,
				netdev_features_t feature,
				mlx5e_feature_handler feature_handler)
{
	netdev_features_t changes = *features ^ netdev->features;
	bool enable = !!(*features & feature);
	int err;

	if (!(changes & feature))
		return 0;

	err = feature_handler(netdev, enable);
	if (err) {
		MLX5E_SET_FEATURE(features, feature, !enable);
		netdev_err(netdev, "%s feature %pNF failed, err %d\n",
			   enable ? "Enable" : "Disable", &feature, err);
		return err;
	}

	return 0;
}

int mlx5e_set_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t oper_features = features;
	int err = 0;

#define MLX5E_HANDLE_FEATURE(feature, handler) \
	mlx5e_handle_feature(netdev, &oper_features, feature, handler)

	err |= MLX5E_HANDLE_FEATURE(NETIF_F_LRO, set_feature_lro);
#ifdef HAVE_NETIF_F_GRO_HW
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_GRO_HW, set_feature_hw_gro);
#endif
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_VLAN_CTAG_FILTER,
				    set_feature_cvlan_filter);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_TC, set_feature_hw_tc);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_RXALL, set_feature_rx_all);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_RXFCS, set_feature_rx_fcs);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_VLAN_CTAG_RX, set_feature_rx_vlan);
#ifdef CONFIG_MLX5_EN_ARFS
#ifndef HAVE_NET_FLOW_KEYS_H
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_NTUPLE, set_feature_arfs);
#endif
#endif
#ifdef HAVE_NETIF_F_HW_TLS_RX
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_TLS_RX, mlx5e_ktls_set_feature_rx);
#endif

	if (err) {
		netdev->features = oper_features;
		return -EINVAL;
	}

	return 0;
}

static netdev_features_t mlx5e_fix_uplink_rep_features(struct net_device *netdev,
						       netdev_features_t features)
{
#ifdef HAVE_NETIF_F_HW_TLS_RX
	features &= ~NETIF_F_HW_TLS_RX;
	if (netdev->features & NETIF_F_HW_TLS_RX)
		netdev_warn(netdev, "Disabling hw_tls_rx, not supported in switchdev mode\n");

	features &= ~NETIF_F_HW_TLS_TX;
	if (netdev->features & NETIF_F_HW_TLS_TX)
		netdev_warn(netdev, "Disabling hw_tls_tx, not supported in switchdev mode\n");

#endif
#ifdef CONFIG_MLX5_EN_ARFS
#ifndef HAVE_NET_FLOW_KEYS_H
	features &= ~NETIF_F_NTUPLE;
	if (netdev->features & NETIF_F_NTUPLE)
		netdev_warn(netdev, "Disabling ntuple, not supported in switchdev mode\n");
#endif
#endif

#ifdef HAVE_NETIF_F_GRO_HW
	features &= ~NETIF_F_GRO_HW;
	if (netdev->features & NETIF_F_GRO_HW)
		netdev_warn(netdev, "Disabling HW_GRO, not supported in switchdev mode\n");
#endif

	return features;
}

static netdev_features_t mlx5e_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params *params;

	if (!netif_device_present(netdev))
		return features;

	mutex_lock(&priv->state_lock);
	params = &priv->channels.params;
	if (!priv->fs->vlan ||
	    !bitmap_empty(mlx5e_vlan_get_active_svlans(priv->fs->vlan), VLAN_N_VID)) {
		/* HW strips the outer C-tag header, this is a problem
		 * for S-tag traffic.
		 */
		features &= ~NETIF_F_HW_VLAN_CTAG_RX;
		if (!params->vlan_strip_disable)
			netdev_warn(netdev, "Dropping C-tag vlan stripping offload due to S-tag vlan\n");
	}

	if (!MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ)
#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
			&& IS_HW_LRO(&priv->channels.params)
#endif
	) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "Disabling LRO, not supported in legacy RQ\n");
			features &= ~NETIF_F_LRO;
		}
#ifdef HAVE_NETIF_F_GRO_HW
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "Disabling HW-GRO, not supported in legacy RQ\n");
			features &= ~NETIF_F_GRO_HW;
		}
#endif
	}

#ifdef HAVE_XDP_SUPPORT
	if (params->xdp_prog) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "LRO is incompatible with XDP\n");
			features &= ~NETIF_F_LRO;
		}
#ifdef HAVE_NETIF_F_GRO_HW
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "HW GRO is incompatible with XDP\n");
			features &= ~NETIF_F_GRO_HW;
		}
#endif
	}
#endif

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (priv->xsk.refcnt) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "LRO is incompatible with AF_XDP (%u XSKs are active)\n",
				    priv->xsk.refcnt);
			features &= ~NETIF_F_LRO;
		}
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "HW GRO is incompatible with AF_XDP (%u XSKs are active)\n",
				    priv->xsk.refcnt);
			features &= ~NETIF_F_GRO_HW;
		}
	}
#endif

	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		features &= ~NETIF_F_RXHASH;
		if (netdev->features & NETIF_F_RXHASH)
			netdev_warn(netdev, "Disabling rxhash, not supported when CQE compress is active\n");

#ifdef HAVE_NETIF_F_GRO_HW
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "Disabling HW-GRO, not supported when CQE compress is active\n");
			features &= ~NETIF_F_GRO_HW;
		}
#endif
	}

	/* LRO/HW-GRO features cannot be combined with RX-FCS */
	if (features & NETIF_F_RXFCS) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "Dropping LRO feature since RX-FCS is requested\n");
			features &= ~NETIF_F_LRO;
		}
#ifdef HAVE_NETIF_F_GRO_HW
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "Dropping HW-GRO feature since RX-FCS is requested\n");
			features &= ~NETIF_F_GRO_HW;
		}
#endif
	}

#ifdef HAVE_NETIF_F_HW_TLS_RX
	if ((features & NETIF_F_HW_TLS_RX) && !(features & NETIF_F_RXCSUM)) {
		netdev_warn(netdev, "Dropping TLS RX HW offload feature since no RXCSUM feature.\n");
		features &= ~NETIF_F_HW_TLS_RX;
	}
#endif

	if (mlx5e_is_uplink_rep(priv))
		features = mlx5e_fix_uplink_rep_features(netdev, features);

	mutex_unlock(&priv->state_lock);

	return features;
}

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
static bool mlx5e_xsk_validate_mtu(struct net_device *netdev,
				   struct mlx5e_channels *chs,
				   struct mlx5e_params *new_params,
				   struct mlx5_core_dev *mdev)
{
	u16 ix;

	for (ix = 0; ix < chs->params.num_channels; ix++) {
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		struct xsk_buff_pool *xsk_pool =
#else
		struct xdp_umem *xsk_pool =
#endif
			mlx5e_xsk_get_pool(&chs->params, chs->params.xsk, ix);

		struct mlx5e_xsk_param xsk;

		if (!xsk_pool)
			continue;

		mlx5e_build_xsk_param(xsk_pool, &xsk);

		if (!mlx5e_validate_xsk_param(new_params, &xsk, mdev)) {
			u32 hr = mlx5e_get_linear_rq_headroom(new_params, &xsk);
			int max_mtu_frame, max_mtu_page, max_mtu;

			/* Two criteria must be met:
			 * 1. HW MTU + all headrooms <= XSK frame size.
			 * 2. Size of SKBs allocated on XDP_PASS <= PAGE_SIZE.
			 */
			max_mtu_frame = MLX5E_HW2SW_MTU(new_params, xsk.chunk_size - hr);
			max_mtu_page = MLX5E_HW2SW_MTU(new_params, SKB_MAX_HEAD(0));
			max_mtu = min(max_mtu_frame, max_mtu_page);

			netdev_err(netdev, "MTU %d is too big for an XSK running on channel %u. Try MTU <= %d\n",
				   new_params->sw_mtu, ix, max_mtu);
			return false;
		}
	}

	return true;
}
#endif /* HAVE_XSK_ZERO_COPY_SUPPORT */
#ifdef HAVE_XDP_SUPPORT
static bool mlx5e_params_validate_xdp(struct net_device *netdev,
				      struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params)
{
	bool is_linear;

	/* No XSK params: AF_XDP can't be enabled yet at the point of setting
	 * the XDP program.
	 */
	is_linear = mlx5e_rx_is_linear_skb(mdev, params, NULL);

	if (!is_linear && params->rq_wq_type != MLX5_WQ_TYPE_CYCLIC) {
		netdev_warn(netdev, "XDP is not allowed with striding RQ and MTU(%d) > %d\n",
			    params->sw_mtu,
			    mlx5e_xdp_max_mtu(params, NULL));
		return false;
	}
#ifdef HAVE_XDP_HAS_FRAGS
	if (!is_linear && !params->xdp_prog->aux->xdp_has_frags) {
		netdev_warn(netdev, "MTU(%d) > %d, too big for an XDP program not aware of multi buffer\n",
			    params->sw_mtu,
			    mlx5e_xdp_max_mtu(params, NULL));
		return false;
	}
#endif
	return true;
}
#endif /* HAVE_XDP_SUPPORT */

int mlx5e_change_mtu(struct net_device *netdev, int new_mtu,
		     mlx5e_fp_preactivate preactivate)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	struct mlx5e_params *params;
#if !defined(HAVE_NET_DEVICE_MIN_MAX_MTU) && !defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 max_mtu;
	u16 min_mtu;
#endif
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);

	params = &priv->channels.params;
#if !defined(HAVE_NET_DEVICE_MIN_MAX_MTU) && !defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);
	max_mtu = min_t(unsigned int, MLX5E_HW2SW_MTU(params, max_mtu),
			ETH_MAX_MTU);
	min_mtu = ETH_MIN_MTU;

	if (new_mtu > max_mtu || new_mtu < min_mtu) {
		netdev_err(netdev,
			   "%s: Bad MTU (%d), valid range is: [%d..%d]\n",
			   __func__, new_mtu, min_mtu, max_mtu);
		mutex_unlock(&priv->state_lock);
		return -EINVAL;
	}
#endif

	new_params = *params;
	new_params.sw_mtu = new_mtu;
	err = mlx5e_validate_params(priv->mdev, &new_params);
	if (err)
		goto out;

#ifdef HAVE_XDP_SUPPORT
	if (new_params.xdp_prog && !mlx5e_params_validate_xdp(netdev, priv->mdev,
							      &new_params)) {
		err = -EINVAL;
		goto out;
	}

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (priv->xsk.refcnt &&
	    !mlx5e_xsk_validate_mtu(netdev, &priv->channels,
				    &new_params, priv->mdev)) {
		err = -EINVAL;
		goto out;
	}
#endif /* HAVE_XSK_ZERO_COPY_SUPPORT */
#endif /* HAVE_XDP_SUPPORT */

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_LRO)
		reset = false;

	if (params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
	    params->packet_merge.type != MLX5E_PACKET_MERGE_SHAMPO) {
		bool is_linear_old = mlx5e_rx_mpwqe_is_linear_skb(priv->mdev, params, NULL);
		bool is_linear_new = mlx5e_rx_mpwqe_is_linear_skb(priv->mdev,
								  &new_params, NULL);
		u8 sz_old = mlx5e_mpwqe_get_log_rq_size(priv->mdev, params, NULL);
		u8 sz_new = mlx5e_mpwqe_get_log_rq_size(priv->mdev, &new_params, NULL);

		/* Always reset in linear mode - hw_mtu is used in data path.
		 * Check that the mode was non-linear and didn't change.
		 * If XSK is active, XSK RQs are linear.
		 * Reset if the RQ size changed, even if it's non-linear.
		 */
		if (!is_linear_old && !is_linear_new &&
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		    !priv->xsk.refcnt &&
#endif
		    sz_old == sz_new)
			reset = false;
	}

	err = mlx5e_safe_switch_params(priv, &new_params, preactivate, NULL, reset);

out:
	netdev->mtu = params->sw_mtu;
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_change_nic_mtu(struct net_device *netdev, int new_mtu)
{
	return mlx5e_change_mtu(netdev, new_mtu, mlx5e_set_dev_port_mtu_ctx);
}

int mlx5e_ptp_rx_manage_fs_ctx(struct mlx5e_priv *priv, void *ctx)
{
	bool set  = *(bool *)ctx;

	return mlx5e_ptp_rx_manage_fs(priv, set);
}

static int mlx5e_hwstamp_config_no_ptp_rx(struct mlx5e_priv *priv, bool rx_filter)
{
	bool rx_cqe_compress_def = priv->channels.params.rx_cqe_compress_def;
	int err;

	if (!rx_filter)
		/* Reset CQE compression to Admin default */
		return mlx5e_modify_rx_cqe_compression_locked(priv, rx_cqe_compress_def, false);

	if (!MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_RX_CQE_COMPRESS))
		return 0;

	/* Disable CQE compression */
	netdev_warn(priv->netdev, "Disabling RX cqe compression\n");
	err = mlx5e_modify_rx_cqe_compression_locked(priv, false, true);
	if (err)
		netdev_err(priv->netdev, "Failed disabling cqe compression err=%d\n", err);

	return err;
}

static int mlx5e_hwstamp_config_ptp_rx(struct mlx5e_priv *priv, bool ptp_rx)
{
	struct mlx5e_params new_params;

	if (ptp_rx == priv->channels.params.ptp_rx)
		return 0;

	new_params = priv->channels.params;
	new_params.ptp_rx = ptp_rx;
	return mlx5e_safe_switch_params(priv, &new_params, mlx5e_ptp_rx_manage_fs_ctx,
					&new_params.ptp_rx, true);
}

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	bool rx_cqe_compress_def;
	bool ptp_rx;
	int err;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz) ||
	    (mlx5_clock_get_ptp_index(priv->mdev) == -1))
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* TX HW timestamp */
	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	mutex_lock(&priv->state_lock);
	rx_cqe_compress_def = priv->channels.params.rx_cqe_compress_def;

	/* RX HW timestamp */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ptp_rx = false;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		/* ptp_rx is set if both HW TS is set and CQE
		 * compression is set
		 */
		ptp_rx = rx_cqe_compress_def;
		break;
	default:
		err = -ERANGE;
		goto err_unlock;
	}

	if (!mlx5e_profile_feature_cap(priv->profile, PTP_RX))
		err = mlx5e_hwstamp_config_no_ptp_rx(priv,
						     config.rx_filter != HWTSTAMP_FILTER_NONE);
	else
		err = mlx5e_hwstamp_config_ptp_rx(priv, ptp_rx);
	if (err)
		goto err_unlock;

	memcpy(&priv->tstamp, &config, sizeof(config));
	mutex_unlock(&priv->state_lock);

	netdev_update_features(priv->netdev);

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
err_unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

int mlx5e_hwstamp_get(struct mlx5e_priv *priv, struct ifreq *ifr)
{
	struct hwtstamp_config *cfg = &priv->tstamp;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, cfg, sizeof(*cfg)) ? -EFAULT : 0;
}

static int mlx5e_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlx5e_hwstamp_set(priv, ifr);
	case SIOCGHWTSTAMP:
		return mlx5e_hwstamp_get(priv, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

#ifdef CONFIG_MLX5_ESWITCH
int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_mac(mdev->priv.eswitch, vf + 1, mac);
}

#if defined(HAVE_NDO_SET_VF_VLAN) || defined(HAVE_NDO_SET_VF_VLAN_EXTENDED)
#ifdef HAVE_VF_VLAN_PROTO
static int mlx5e_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos,
			     __be16 vlan_proto)
#else
static int mlx5e_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
#ifndef HAVE_VF_VLAN_PROTO
	__be16 vlan_proto = htons(ETH_P_8021Q);
#endif

	return mlx5_eswitch_set_vport_vlan(mdev->priv.eswitch, vf + 1,
					   vlan, qos, vlan_proto);
}
#endif /* HAVE_NDO_SET_VF_VLAN */

#ifdef HAVE_NETDEV_OPS_NDO_SET_VF_TRUNK_RANGE
static int mlx5e_add_vf_vlan_trunk_range(struct net_device *dev, int vf,
					 u16 start_vid, u16 end_vid,
					 __be16 vlan_proto)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	return mlx5_eswitch_add_vport_trunk_range(mdev->priv.eswitch, vf + 1,
						  start_vid, end_vid);
}

static int mlx5e_del_vf_vlan_trunk_range(struct net_device *dev, int vf,
					 u16 start_vid, u16 end_vid,
					 __be16 vlan_proto)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	return mlx5_eswitch_del_vport_trunk_range(mdev->priv.eswitch, vf + 1,
						  start_vid, end_vid);
}
#endif

static int mlx5e_set_vf_spoofchk(struct net_device *dev, int vf, bool setting)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_spoofchk(mdev->priv.eswitch, vf + 1, setting);
}

#if defined(HAVE_NETDEV_OPS_NDO_SET_VF_TRUST) || defined(HAVE_NETDEV_OPS_NDO_SET_VF_TRUST_EXTENDED)
static int mlx5e_set_vf_trust(struct net_device *dev, int vf, bool setting)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_trust(mdev->priv.eswitch, vf + 1, setting);
}
#endif

int mlx5e_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate,
		      int max_tx_rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int vport = (vf == 0xffff) ? 0 : vf + 1;

#ifdef HAVE_BASECODE_EXTRAS
	/* MLNX OFED only -??
	 * Allow to set eswitch min rate for the PF.
	 * In order to avoid bottlenecks on the slow-path arising from
	 * VF->PF packet transitions consuming a high amount of HW BW,
	 * resulting in drops of packets destined from PF->WIRE.
	 * This essentially assigns PF->WIRE a higher priority than VF->PF
	 * packet processing. */
	if (vport == 0) {
		min_tx_rate = max_tx_rate;
		max_tx_rate = 0;
	}
#endif

	return mlx5_eswitch_set_vport_rate(mdev->priv.eswitch, vport,
					   max_tx_rate, min_tx_rate);
}

static int mlx5_vport_link2ifla(u8 esw_link)
{
	switch (esw_link) {
	case MLX5_VPORT_ADMIN_STATE_DOWN:
		return IFLA_VF_LINK_STATE_DISABLE;
	case MLX5_VPORT_ADMIN_STATE_UP:
		return IFLA_VF_LINK_STATE_ENABLE;
	}
	return IFLA_VF_LINK_STATE_AUTO;
}

static int mlx5_ifla_link2vport(u8 ifla_link)
{
	switch (ifla_link) {
	case IFLA_VF_LINK_STATE_DISABLE:
		return MLX5_VPORT_ADMIN_STATE_DOWN;
	case IFLA_VF_LINK_STATE_ENABLE:
		return MLX5_VPORT_ADMIN_STATE_UP;
	}
	return MLX5_VPORT_ADMIN_STATE_AUTO;
}

static int mlx5e_set_vf_link_state(struct net_device *dev, int vf,
				   int link_state)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (mlx5e_is_uplink_rep(priv))
		return -EOPNOTSUPP;

	return mlx5_eswitch_set_vport_state(mdev->priv.eswitch, vf + 1,
					    mlx5_ifla_link2vport(link_state));
}

int mlx5e_get_vf_config(struct net_device *dev,
			int vf, struct ifla_vf_info *ivi)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!netif_device_present(dev))
		return -EOPNOTSUPP;

	err = mlx5_eswitch_get_vport_config(mdev->priv.eswitch, vf + 1, ivi);
	if (err)
		return err;
	ivi->linkstate = mlx5_vport_link2ifla(ivi->linkstate);
	return 0;
}

#ifdef HAVE_NDO_GET_VF_STATS
int mlx5e_get_vf_stats(struct net_device *dev,
		       int vf, struct ifla_vf_stats *vf_stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_get_vport_stats(mdev->priv.eswitch, vf + 1,
					    vf_stats);
}
#endif

#if defined(HAVE_NDO_HAS_OFFLOAD_STATS_GETS_NET_DEVICE) || defined(HAVE_NDO_HAS_OFFLOAD_STATS_EXTENDED)
static bool
mlx5e_has_offload_stats(const struct net_device *dev, int attr_id)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

#ifdef HAVE_NETIF_DEVICE_PRESENT_GET_CONST
	if (!netif_device_present(dev))
#else
	if (!netif_device_present_const(dev))
#endif
		return false;

	if (!mlx5e_is_uplink_rep(priv))
		return false;

	return mlx5e_rep_has_offload_stats(dev, attr_id);
}
#endif

#if defined(HAVE_NDO_GET_OFFLOAD_STATS) || defined(HAVE_NDO_GET_OFFLOAD_STATS_EXTENDED)
static int
mlx5e_get_offload_stats(int attr_id, const struct net_device *dev,
			void *sp)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (!mlx5e_is_uplink_rep(priv))
		return -EOPNOTSUPP;

	return mlx5e_rep_get_offload_stats(attr_id, dev, sp);
}
#endif
#endif /*CONFIG_MLX5_ESWITCH*/

static bool mlx5e_tunnel_proto_supported_tx(struct mlx5_core_dev *mdev, u8 proto_type)
{
	switch (proto_type) {
	case IPPROTO_GRE:
		return MLX5_CAP_ETH(mdev, tunnel_stateless_gre);
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		return (MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip) ||
			MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip_tx));
	default:
		return false;
	}
}


#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
struct mlx5e_vxlan_work {
	struct work_struct      work;
	struct mlx5e_priv       *priv;
	u16                     port;
};

#if defined(HAVE_NDO_UDP_TUNNEL_ADD) || defined(HAVE_NDO_UDP_TUNNEL_ADD_EXTENDED) || defined(HAVE_NDO_ADD_VXLAN_PORT)
static void mlx5e_vxlan_add_work(struct work_struct *work)
{
	struct mlx5e_vxlan_work *vxlan_work =
		container_of(work, struct mlx5e_vxlan_work, work);
	struct mlx5e_priv *priv = vxlan_work->priv;
	u16 port = vxlan_work->port;

	mutex_lock(&priv->state_lock);
	mlx5_vxlan_add_port(priv->mdev->vxlan, port);
	mutex_unlock(&priv->state_lock);

	kfree(vxlan_work);
}

static void mlx5e_vxlan_del_work(struct work_struct *work)
{
	struct mlx5e_vxlan_work *vxlan_work =
		container_of(work, struct mlx5e_vxlan_work, work);
	struct mlx5e_priv *priv         = vxlan_work->priv;
	u16 port = vxlan_work->port;

	mutex_lock(&priv->state_lock);
	mlx5_vxlan_del_port(priv->mdev->vxlan, port);
	mutex_unlock(&priv->state_lock);
	kfree(vxlan_work);
}

static void mlx5e_vxlan_queue_work(struct mlx5e_priv *priv, u16 port, int add)
{
	struct mlx5e_vxlan_work *vxlan_work;

	vxlan_work = kmalloc(sizeof(*vxlan_work), GFP_ATOMIC);
	if (!vxlan_work)
		return;

	if (add)
		INIT_WORK(&vxlan_work->work, mlx5e_vxlan_add_work);
	else
		INIT_WORK(&vxlan_work->work, mlx5e_vxlan_del_work);

	vxlan_work->priv = priv;
	vxlan_work->port = port;
	queue_work(priv->wq, &vxlan_work->work);
}
#endif

#if defined(HAVE_NDO_UDP_TUNNEL_ADD) || defined(HAVE_NDO_UDP_TUNNEL_ADD_EXTENDED)
void mlx5e_add_vxlan_port(struct net_device *netdev, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	mlx5e_vxlan_queue_work(priv, be16_to_cpu(ti->port), 1);
}

void mlx5e_del_vxlan_port(struct net_device *netdev, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	mlx5e_vxlan_queue_work(priv, be16_to_cpu(ti->port), 0);
}
#elif defined(HAVE_NDO_ADD_VXLAN_PORT)
void mlx5e_add_vxlan_port(struct net_device *netdev,
		sa_family_t sa_family, __be16 port)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	mlx5e_vxlan_queue_work(priv, be16_to_cpu(port), 1);
}

void mlx5e_del_vxlan_port(struct net_device *netdev,
		sa_family_t sa_family, __be16 port)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	mlx5e_vxlan_queue_work(priv, be16_to_cpu(port), 0);
}
#endif
#endif /* HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON */

static bool mlx5e_gre_tunnel_inner_proto_offload_supported(struct mlx5_core_dev *mdev,
							   struct sk_buff *skb)
{
	switch (skb->inner_protocol) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_TEB):
		return true;
	case htons(ETH_P_MPLS_UC):
	case htons(ETH_P_MPLS_MC):
		return MLX5_CAP_ETH(mdev, tunnel_stateless_mpls_over_gre);
	}
	return false;
}

static netdev_features_t mlx5e_tunnel_features_check(struct mlx5e_priv *priv,
						     struct sk_buff *skb,
						     netdev_features_t features)
{
	unsigned int offset = 0;
#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	struct udphdr *udph;
#endif
	u8 proto;
#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	u16 port;
#endif

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		proto = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		proto = ipv6_find_hdr(skb, &offset, -1, NULL, NULL);
		break;
	default:
		goto out;
	}

	switch (proto) {
	case IPPROTO_GRE:
		if (mlx5e_gre_tunnel_inner_proto_offload_supported(priv->mdev, skb))
			return features;
		break;
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		if (mlx5e_tunnel_proto_supported_tx(priv->mdev, IPPROTO_IPIP))
			return features;
		break;
#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	case IPPROTO_UDP:
		udph = udp_hdr(skb);
		port = be16_to_cpu(udph->dest);

		/* Verify if UDP port is being offloaded by HW */
		if (mlx5_vxlan_lookup_port(priv->mdev->vxlan, port))
			return features;
#endif

#if IS_ENABLED(CONFIG_GENEVE)
		/* Support Geneve offload for default UDP port */
		if (port == GENEVE_UDP_PORT && mlx5_geneve_tx_allowed(priv->mdev))
			return features;
#endif
		break;
#ifdef CONFIG_MLX5_EN_IPSEC
	case IPPROTO_ESP:
		return mlx5e_ipsec_feature_check(skb, features);
#endif
	}

out:
	/* Disable CSUM and GSO if the udp dport is not offloaded by HW */
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

netdev_features_t mlx5e_features_check(struct sk_buff *skb,
				       struct net_device *netdev,
				       netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	features = vlan_features_check(skb, features);
#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	features = vxlan_features_check(skb, features);
#endif /* HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON */

	/* Validate if the tunneled packet is being offloaded by HW */
	if (skb->encapsulation &&
	    (features & NETIF_F_CSUM_MASK || features & NETIF_F_GSO_MASK))
		return mlx5e_tunnel_features_check(priv, skb, features);

	return features;
}

static void mlx5e_tx_timeout_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       tx_timeout_work);
	struct net_device *netdev = priv->netdev;
	int i;

	rtnl_lock();
	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		struct netdev_queue *dev_queue =
			netdev_get_tx_queue(netdev, i);
		struct mlx5e_txqsq *sq = priv->txq2sq[i];

		if (!netif_xmit_stopped(dev_queue))
			continue;

		if (mlx5e_reporter_tx_timeout(sq))
		/* break if tried to reopened channels */
			break;
	}

unlock:
	mutex_unlock(&priv->state_lock);
	rtnl_unlock();
}

#ifdef HAVE_NDO_TX_TIMEOUT_GET_2_PARAMS
static void mlx5e_tx_timeout(struct net_device *dev, unsigned int txqueue)
#else
static void mlx5e_tx_timeout(struct net_device *dev)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	netdev_err(dev, "TX timeout detected\n");
	queue_work(priv->wq, &priv->tx_timeout_work);
}

#ifdef HAVE_XDP_SUPPORT
static int mlx5e_xdp_allowed(struct mlx5e_priv *priv, struct bpf_prog *prog)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_params new_params;

#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
	if (IS_HW_LRO(&priv->channels.params)) {
#else
	if (priv->channels.params.packet_merge.type != MLX5E_PACKET_MERGE_NONE) {
#endif
		netdev_warn(netdev, "can't set XDP while HW-GRO/LRO is on, disable them first\n");
		return -EINVAL;
	}

	new_params = priv->channels.params;
	new_params.xdp_prog = prog;

	if (!mlx5e_params_validate_xdp(netdev, priv->mdev, &new_params))
		return -EINVAL;

	return 0;
}

static void mlx5e_rq_replace_xdp_prog(struct mlx5e_rq *rq, struct bpf_prog *prog)
{
	struct bpf_prog *old_prog;

	old_prog = rcu_replace_pointer(rq->xdp_prog, prog,
				       lockdep_is_held(&rq->priv->state_lock));
	if (old_prog)
		bpf_prog_put(old_prog);
}

static int mlx5e_xdp_set(struct net_device *netdev, struct bpf_prog *prog)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	struct bpf_prog *old_prog;
	int err = 0;
	bool reset;
	int i;

	mutex_lock(&priv->state_lock);

	if (prog) {
		err = mlx5e_xdp_allowed(priv, prog);
		if (err)
			goto unlock;
	}

	/* no need for full reset when exchanging programs */
	reset = (!priv->channels.params.xdp_prog || !prog);

	new_params = priv->channels.params;
	new_params.xdp_prog = prog;

	/* XDP affects striding RQ parameters. Block XDP if striding RQ won't be
	 * supported with the new parameters: if PAGE_SIZE is bigger than
	 * MLX5_MPWQE_LOG_STRIDE_SZ_MAX, striding RQ can't be used, even though
	 * the MTU is small enough for the linear mode, because XDP uses strides
	 * of PAGE_SIZE on regular RQs.
	 */
	if (reset && MLX5E_GET_PFLAG(&new_params, MLX5E_PFLAG_RX_STRIDING_RQ)) {
		/* Checking for regular RQs here; XSK RQs were checked on XSK bind. */
		err = mlx5e_mpwrq_validate_regular(priv->mdev, &new_params);
		if (err)
			goto unlock;
	}

	old_prog = priv->channels.params.xdp_prog;

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, reset);
	if (err)
		goto unlock;

	if (old_prog)
		bpf_prog_put(old_prog);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state) || reset)
		goto unlock;

	/* exchanging programs w/o reset, we update ref counts on behalf
	 * of the channels RQs here.
	 */
#ifndef HAVE_BPF_PROG_ADD_RET_STRUCT
	bpf_prog_add(prog, priv->channels.num);
#else
		prog = bpf_prog_add(prog, priv->channels.num);
		if (IS_ERR(prog)) {
			err = PTR_ERR(prog);
			goto unlock;
		}
#endif
	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		mlx5e_rq_replace_xdp_prog(&c->rq, prog);
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state)) {
#ifndef HAVE_BPF_PROG_ADD_RET_STRUCT
			bpf_prog_inc(prog);
#else
			prog = bpf_prog_inc(prog);
			if (IS_ERR(prog)) {
				err = PTR_ERR(prog);
				goto unlock;
			}
#endif
			mlx5e_rq_replace_xdp_prog(&c->xskrq, prog);
		}
#endif
	}

unlock:
	mutex_unlock(&priv->state_lock);

	/* Need to fix some features. */
	if (!err)
		netdev_update_features(netdev);

	return err;
}

#ifndef HAVE_DEV_XDP_PROG_ID
static u32 mlx5e_xdp_query(struct net_device *dev)
{
       struct mlx5e_priv *priv = netdev_priv(dev);
       const struct bpf_prog *xdp_prog;
       u32 prog_id = 0;

       if (!netif_device_present(dev))
	       goto out;

       mutex_lock(&priv->state_lock);
       xdp_prog = priv->channels.params.xdp_prog;
       if (xdp_prog)
              prog_id = xdp_prog->aux->id;
       mutex_unlock(&priv->state_lock);

out:
       return prog_id;
}
#endif

static int mlx5e_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return mlx5e_xdp_set(dev, xdp->prog);
#ifndef HAVE_DEV_XDP_PROG_ID
	case XDP_QUERY_PROG:
		xdp->prog_id = mlx5e_xdp_query(dev);
		return 0;
#endif
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
	case XDP_SETUP_XSK_POOL:
		return mlx5e_xsk_setup_pool(dev, xdp->xsk.pool,
					    xdp->xsk.queue_id);
#else
	case XDP_SETUP_XSK_UMEM:
		return mlx5e_xsk_setup_pool(dev, xdp->xsk.umem,
					    xdp->xsk.queue_id);
#endif
#endif
	default:
		return -EINVAL;
	}
}
#endif

#ifndef HAVE_NETPOLL_POLL_DEV_EXPORTED
#ifdef CONFIG_NET_POLL_CONTROLLER
/* Fake "interrupt" called by netpoll (eg netconsole) to send skbs without
 * reenabling interrupts.
 */
static void mlx5e_netpoll(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_channels *chs = &priv->channels;

	int i;

	for (i = 0; i < chs->num; i++)
		napi_schedule(&chs->c[i]->napi);
}
#endif
#endif/*HAVE_NETPOLL_POLL_DEV__EXPORTED*/

#ifdef CONFIG_MLX5_ESWITCH
#if defined(HAVE_NDO_BRIDGE_GETLINK) || defined(HAVE_NDO_BRIDGE_GETLINK_NLFLAGS)
static int mlx5e_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				struct net_device *dev, u32 filter_mask
#if defined(HAVE_NDO_BRIDGE_GETLINK_NLFLAGS)
				, int nlflags)
#elif defined(HAVE_NDO_BRIDGE_GETLINK)
				)
#endif
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 mode, setting;
	int err;

	err = mlx5_eswitch_get_vepa(mdev->priv.eswitch, &setting);
	if (err)
		return err;
	mode = setting ? BRIDGE_MODE_VEPA : BRIDGE_MODE_VEB;
	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, mode
#if defined(HAVE_NDO_DFLT_BRIDGE_GETLINK_FLAG_MASK)
				       , 0, 0);
#endif
#if defined(HAVE_NDO_DFLT_BRIDGE_GETLINK_FLAG_MASK_NFLAGS) && defined(HAVE_NDO_BRIDGE_GETLINK)
				       , 0, 0, 0);
#endif
#if defined(HAVE_NDO_DFLT_BRIDGE_GETLINK_FLAG_MASK_NFLAGS) && defined(HAVE_NDO_BRIDGE_GETLINK_NLFLAGS)
				       , 0, 0, nlflags);
#endif
#if defined(HAVE_NDO_DFLT_BRIDGE_GETLINK_FLAG_MASK_NFLAGS_FILTER) && defined(HAVE_NDO_BRIDGE_GETLINK)
				       , 0, 0, 0, filter_mask, NULL);
#endif
#if defined(HAVE_NDO_DFLT_BRIDGE_GETLINK_FLAG_MASK_NFLAGS_FILTER) && defined(HAVE_NDO_BRIDGE_GETLINK_NLFLAGS)
				       , 0, 0, nlflags, filter_mask, NULL);
#endif
}
#endif

#if defined(HAVE_NDO_BRIDGE_SETLINK) || defined(HAVE_NDO_BRIDGE_SETLINK_EXTACK)
#ifdef HAVE_NDO_BRIDGE_SETLINK_EXTACK
static int mlx5e_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
				u16 flags, struct netlink_ext_ack *extack)
#endif
#ifdef HAVE_NDO_BRIDGE_SETLINK
static int mlx5e_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
				u16 flags)
#endif

{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct nlattr *attr, *br_spec;
	u16 mode = BRIDGE_MODE_UNDEF;
	u8 setting;
	int rem;

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested(attr, br_spec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		if (nla_len(attr) < sizeof(mode))
			return -EINVAL;

		mode = nla_get_u16(attr);
		if (mode > BRIDGE_MODE_VEPA)
			return -EINVAL;

		break;
	}

	if (mode == BRIDGE_MODE_UNDEF)
		return -EINVAL;

	setting = (mode == BRIDGE_MODE_VEPA) ?  1 : 0;
	return mlx5_eswitch_set_vepa(mdev->priv.eswitch, setting);
}
#endif

#ifndef HAVE_DEVLINK_PORT_ATTRS_PCI_PF_SET
#if defined(HAVE_NDO_GET_PHYS_PORT_NAME) || defined(HAVE_NDO_GET_PHYS_PORT_NAME_EXTENDED)
int mlx5e_get_phys_port_name(struct net_device *dev,
			     char *buf, size_t len)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	unsigned int fn;
	int ret;

	if (!netif_device_present(dev))
		return -EOPNOTSUPP;

	if (mlx5e_is_uplink_rep(priv))
		return mlx5e_rep_get_phys_port_name(dev, buf, len);

	/* Only rename ecpf, don't rename non-smartnic PF/VF/SF */
	if (!mlx5_core_is_pf(priv->mdev) &&
	    !mlx5_core_is_ecpf(priv->mdev))
		return -EOPNOTSUPP;

	fn = mlx5_get_dev_index(priv->mdev);
	ret = snprintf(buf, len, "p%d", fn);
	if (ret >= len)
		return -EOPNOTSUPP;

	return 0;
}
#endif
#endif

#if defined(HAVE_NDO_GET_PORT_PARENT_ID)
#ifdef HAVE_BASECODE_EXTRAS
#ifdef HAVE_DEVLINK_PORT_ATTRS_PCI_PF_SET
void
#else
int
#endif
mlx5e_get_port_parent_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (!netif_device_present(dev))
#ifndef HAVE_DEVLINK_PORT_ATTRS_PCI_PF_SET
		return -EOPNOTSUPP;
#else
	return;
#endif

	if (!mlx5e_is_uplink_rep(priv))
#ifndef HAVE_DEVLINK_PORT_ATTRS_PCI_PF_SET
		return -EOPNOTSUPP;
#else
		return;
#endif

#ifndef HAVE_DEVLINK_PORT_ATTRS_PCI_PF_SET
	return mlx5e_rep_get_port_parent_id(dev, ppid);
#else
	mlx5e_rep_get_port_parent_id(dev, ppid);
#endif
}
#endif
#endif
#endif /* CONFIG_MLX5_ESWITCH */

const struct net_device_ops mlx5e_netdev_ops = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
#ifdef HAVE_NDO_SETUP_TC_RH_EXTENDED
	.extended.ndo_setup_tc_rh = mlx5e_setup_tc,
#else
#ifdef HAVE_NDO_SETUP_TC
#ifdef HAVE_NDO_SETUP_TC_TAKES_TC_SETUP_TYPE
	.ndo_setup_tc            = mlx5e_setup_tc,
#else
#if defined(HAVE_NDO_SETUP_TC_4_PARAMS) || defined(HAVE_NDO_SETUP_TC_TAKES_CHAIN_INDEX)
	.ndo_setup_tc		 = mlx5e_ndo_setup_tc,
#else
	.ndo_setup_tc		 = mlx5e_setup_tc,
#endif
#endif
#endif
#endif
	.ndo_select_queue        = mlx5e_select_queue,
#if defined(HAVE_NDO_GET_STATS64) || defined(HAVE_NDO_GET_STATS64_RET_VOID)
	.ndo_get_stats64         = mlx5e_get_stats,
#else
	.ndo_get_stats		 = mlx5e_get_stats,
#endif
	.ndo_set_rx_mode         = mlx5e_set_rx_mode,
	.ndo_set_mac_address     = mlx5e_set_mac,
	.ndo_vlan_rx_add_vid     = mlx5e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid    = mlx5e_vlan_rx_kill_vid,
	.ndo_set_features        = mlx5e_set_features,
	.ndo_fix_features        = mlx5e_fix_features,
#ifdef HAVE_NDO_CHANGE_MTU_EXTENDED
	.extended.ndo_change_mtu = mlx5e_change_nic_mtu,
#else
       .ndo_change_mtu          = mlx5e_change_nic_mtu,
#endif

#ifdef HAVE_NDO_ETH_IOCTL
	.ndo_eth_ioctl            = mlx5e_ioctl,
#else
	.ndo_do_ioctl		  = mlx5e_ioctl,
#endif

#ifdef HAVE_NDO_SET_TX_MAXRATE
	.ndo_set_tx_maxrate      = mlx5e_set_tx_maxrate,
#elif defined(HAVE_NDO_SET_TX_MAXRATE_EXTENDED)
	.extended.ndo_set_tx_maxrate      = mlx5e_set_tx_maxrate,
#endif

#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
#if defined(HAVE_UDP_TUNNEL_NIC_INFO) && defined(HAVE_NDO_UDP_TUNNEL_ADD)
	.ndo_udp_tunnel_add      = udp_tunnel_nic_add_port,
	.ndo_udp_tunnel_del      = udp_tunnel_nic_del_port,
#elif defined(HAVE_NDO_UDP_TUNNEL_ADD)
	.ndo_udp_tunnel_add      = mlx5e_add_vxlan_port,
	.ndo_udp_tunnel_del      = mlx5e_del_vxlan_port,
#elif defined(HAVE_NDO_UDP_TUNNEL_ADD_EXTENDED)
	.extended.ndo_udp_tunnel_add	  = mlx5e_add_vxlan_port,
	.extended.ndo_udp_tunnel_del	  = mlx5e_del_vxlan_port,
#elif defined(HAVE_NDO_ADD_VXLAN_PORT)
	.ndo_add_vxlan_port	 = mlx5e_add_vxlan_port,
	.ndo_del_vxlan_port	 = mlx5e_del_vxlan_port,
#endif /* HAVE_UDP_TUNNEL_NIC_INFO */
#endif
	.ndo_features_check      = mlx5e_features_check,
	.ndo_tx_timeout          = mlx5e_tx_timeout,
#ifdef HAVE_XDP_SUPPORT
#ifdef HAVE_NDO_XDP_EXTENDED
	.extended.ndo_xdp        = mlx5e_xdp,
#else
	.ndo_bpf                 = mlx5e_xdp,
#endif
#ifdef HAVE_NDO_XDP_XMIT
	.ndo_xdp_xmit            = mlx5e_xdp_xmit,
#endif
#ifdef HAVE_NDO_XDP_FLUSH
        .ndo_xdp_flush           = mlx5e_xdp_flush,
#endif
#endif /* HAVE_XDP_SUPPORT */
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifdef HAVE_NDO_XSK_WAKEUP
	.ndo_xsk_wakeup          = mlx5e_xsk_wakeup,
#else
	.ndo_xsk_async_xmit	 = mlx5e_xsk_wakeup,
#endif
#endif
#ifdef CONFIG_MLX5_EN_ARFS
#ifndef HAVE_NET_FLOW_KEYS_H
	.ndo_rx_flow_steer	 = mlx5e_rx_flow_steer,
#endif
#endif
#ifndef HAVE_NETPOLL_POLL_DEV_EXPORTED
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = mlx5e_netpoll,
#endif
#endif
#ifdef HAVE_NET_DEVICE_OPS_EXTENDED
	.ndo_size = sizeof(struct net_device_ops),
#endif
#ifdef CONFIG_MLX5_ESWITCH
#if defined(HAVE_NDO_BRIDGE_SETLINK) || defined(HAVE_NDO_BRIDGE_SETLINK_EXTACK)
	.ndo_bridge_setlink      = mlx5e_bridge_setlink,
#endif
#if defined(HAVE_NDO_BRIDGE_GETLINK) || defined(HAVE_NDO_BRIDGE_GETLINK_NLFLAGS)
	.ndo_bridge_getlink      = mlx5e_bridge_getlink,
#endif

	/* SRIOV E-Switch NDOs */
	.ndo_set_vf_mac          = mlx5e_set_vf_mac,
#if defined(HAVE_NDO_SET_VF_VLAN)
	.ndo_set_vf_vlan         = mlx5e_set_vf_vlan,
#elif defined(HAVE_NDO_SET_VF_VLAN_EXTENDED)
	.extended.ndo_set_vf_vlan  = mlx5e_set_vf_vlan,
#endif

	/* these ndo's are not upstream yet */
#ifdef HAVE_NETDEV_OPS_NDO_SET_VF_TRUNK_RANGE
	.ndo_add_vf_vlan_trunk_range = mlx5e_add_vf_vlan_trunk_range,
	.ndo_del_vf_vlan_trunk_range = mlx5e_del_vf_vlan_trunk_range,
#endif

	.ndo_set_vf_spoofchk     = mlx5e_set_vf_spoofchk,
#ifdef HAVE_NETDEV_OPS_NDO_SET_VF_TRUST
	.ndo_set_vf_trust        = mlx5e_set_vf_trust,
#elif defined(HAVE_NETDEV_OPS_NDO_SET_VF_TRUST_EXTENDED)
	.extended.ndo_set_vf_trust        = mlx5e_set_vf_trust,
#endif
	.ndo_set_vf_rate         = mlx5e_set_vf_rate,
	.ndo_set_vf_link_state	 = mlx5e_set_vf_link_state,
	.ndo_get_vf_config       = mlx5e_get_vf_config,
#ifdef HAVE_NDO_GET_VF_STATS
	.ndo_get_vf_stats	 = mlx5e_get_vf_stats,
#endif
#ifdef HAVE_NDO_GET_DEVLINK_PORT
	.ndo_get_devlink_port    = mlx5e_get_devlink_port,
#else
#ifdef HAVE_NDO_GET_PHYS_PORT_NAME
	.ndo_get_phys_port_name  = mlx5e_get_phys_port_name,
#elif defined(HAVE_NDO_GET_PHYS_PORT_NAME_EXTENDED)
	.extended.ndo_get_phys_port_name = mlx5e_get_phys_port_name,
#endif
#ifdef HAVE_NDO_GET_PORT_PARENT_ID
	.ndo_get_port_parent_id  = mlx5e_get_port_parent_id,
#endif
#endif
#ifdef HAVE_NDO_HAS_OFFLOAD_STATS_GETS_NET_DEVICE
	.ndo_has_offload_stats   = mlx5e_has_offload_stats,
#elif defined(HAVE_NDO_HAS_OFFLOAD_STATS_EXTENDED)
	.extended.ndo_has_offload_stats   = mlx5e_has_offload_stats,
#endif
#ifdef HAVE_NDO_GET_OFFLOAD_STATS
	.ndo_get_offload_stats   = mlx5e_get_offload_stats,
#elif defined(HAVE_NDO_GET_OFFLOAD_STATS_EXTENDED)
	.extended.ndo_get_offload_stats   = mlx5e_get_offload_stats,
#endif
#endif /* CONFIG_MLX5_ESWITCH */
};

u32 mlx5e_choose_lro_timeout(struct mlx5_core_dev *mdev, u32 wanted_timeout)
{
	int i;

	/* The supported periods are organized in ascending order */
	for (i = 0; i < MLX5E_LRO_TIMEOUT_ARR_SIZE - 1; i++)
		if (MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]) >= wanted_timeout)
			break;

	return MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]);
}

static void mlx5e_init_delay_drop(struct mlx5e_priv *priv,
				  struct mlx5e_params *params)
{
	if (!mlx5e_dropless_rq_supported(priv->mdev))
		return;

	mutex_init(&priv->delay_drop.lock);
	priv->delay_drop.activate = false;
	priv->delay_drop.usec_timeout = MLX5_MAX_DELAY_DROP_TIMEOUT_MS * 1000;
	INIT_WORK(&priv->delay_drop.work, mlx5e_delay_drop_handler);
}

void mlx5e_build_nic_params(struct mlx5e_priv *priv,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
			   struct mlx5e_xsk *xsk,
#endif
			   u16 mtu)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 rx_cq_period_mode;

	params->sw_mtu = mtu;
	params->hard_mtu = MLX5E_ETH_HARD_MTU;
	params->num_channels = min_t(unsigned int, MLX5E_MAX_NUM_CHANNELS / 2,
				     priv->max_nch);
	params->log_rx_page_cache_mult = MLX5E_PAGE_CACHE_LOG_MAX_RQ_MULT;
	mlx5e_params_mqprio_reset(params);

	/* SQ */
	params->log_sq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE, mlx5e_tx_mpwqe_supported(mdev));

	/* XDP SQ */
#ifdef HAVE_XDP_SUPPORT
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_XDP_TX_MPWQE, mlx5e_tx_mpwqe_supported(mdev));
#endif

	/* set CQE compression */
	params->rx_cqe_compress_def = false;
	if (MLX5_CAP_GEN(mdev, cqe_compression) &&
	    MLX5_CAP_GEN(mdev, vport_group_manager))
		params->rx_cqe_compress_def = slow_pci_heuristic(mdev);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS, params->rx_cqe_compress_def);
#ifdef HAVE_BASECODE_EXTRAS
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_TX_CQE_COMPRESS, false);
#endif
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE, false);

	/* RQ */
	mlx5e_build_rq_params(mdev, params);

	params->packet_merge.timeout = mlx5e_choose_lro_timeout(mdev, MLX5E_DEFAULT_LRO_TIMEOUT);

	/* CQ moderation params */
	rx_cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
			MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
			MLX5_CQ_PERIOD_MODE_START_FROM_EQE;

#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_HWLRO, MLX5_CAP_ETH(mdev, lro_cap) &&
			MLX5_CAP_GEN(mdev, striding_rq));
#endif

	params->rx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	params->tx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, rx_cq_period_mode);
	mlx5e_set_tx_cq_mode_params(params, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);

	/* TX inline */
	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);

	params->tunneled_offload_en = mlx5_tunnel_inner_ft_supported(mdev);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_PER_CH_STATS, true);
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	/* AF_XDP */
	params->xsk = xsk;
#endif

	/* TX HW checksum offload for XDP is off by default */
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_TX_XDP_CSUM, 0);

#ifdef HAVE_BASECODE_EXTRAS
	/* skb xmit_more in driver is off by default */
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_SKB_XMIT_MORE, 0);
#endif

	/* Do not update netdev->features directly in here
	 * on mlx5e_attach_netdev() we will call mlx5e_update_features()
	 * To update netdev->features please modify mlx5e_fix_features()
	 */
}

static void mlx5e_set_netdev_dev_addr(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
#ifdef HAVE_DEV_ADDR_MOD
       u8 addr[ETH_ALEN];

       mlx5_query_mac_address(priv->mdev, addr);
       if (is_zero_ether_addr(addr) &&
#else
	mlx5_query_mac_address(priv->mdev, netdev->dev_addr);
	if (is_zero_ether_addr(netdev->dev_addr) &&
#endif
	    !MLX5_CAP_GEN(priv->mdev, vport_group_manager)) {
		eth_hw_addr_random(netdev);
		mlx5_core_info(priv->mdev, "Assigned random MAC address %pM\n", netdev->dev_addr);
#ifdef HAVE_DEV_ADDR_MOD
		return;
#endif
	}
#ifdef HAVE_DEV_ADDR_MOD
	eth_hw_addr_set(netdev, addr);
#endif
}

#if defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON) && defined(HAVE_UDP_TUNNEL_NIC_INFO)
static int mlx5e_vxlan_set_port(struct net_device *netdev, unsigned int table,
				unsigned int entry, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5_vxlan_add_port(priv->mdev->vxlan, ntohs(ti->port));
}

static int mlx5e_vxlan_unset_port(struct net_device *netdev, unsigned int table,
				  unsigned int entry, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5_vxlan_del_port(priv->mdev->vxlan, ntohs(ti->port));
}

void mlx5e_vxlan_set_netdev_info(struct mlx5e_priv *priv)
{
	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	priv->nic_info.set_port = mlx5e_vxlan_set_port;
	priv->nic_info.unset_port = mlx5e_vxlan_unset_port;
	priv->nic_info.flags = UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
				UDP_TUNNEL_NIC_INFO_STATIC_IANA_VXLAN;
	priv->nic_info.tables[0].tunnel_types = UDP_TUNNEL_TYPE_VXLAN;
	/* Don't count the space hard-coded to the IANA port */
	priv->nic_info.tables[0].n_entries =
		mlx5_vxlan_max_udp_ports(priv->mdev) - 1;

	priv->netdev->udp_tunnel_nic_info = &priv->nic_info;
}
#endif

#if defined(CONFIG_MLX5_ESWITCH) && defined(HAVE_SWITCHDEV_OPS)
static const struct switchdev_ops mlx5e_switchdev_ops = {
		.switchdev_port_attr_get	= mlx5e_attr_get,
};
#endif

static bool mlx5e_tunnel_any_tx_proto_supported(struct mlx5_core_dev *mdev)
{
	int tt;

	for (tt = 0; tt < MLX5_NUM_TUNNEL_TT; tt++) {
		if (mlx5e_tunnel_proto_supported_tx(mdev, mlx5_get_proto_by_tunnel_type(tt)))
			return true;
	}
#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	return (mlx5_vxlan_allowed(mdev->vxlan) || mlx5_geneve_tx_allowed(mdev));
#else
	return false;
#endif
}

static void mlx5e_build_nic_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool fcs_supported;
	bool fcs_enabled;

	SET_NETDEV_DEV(netdev, mdev->device);

	netdev->netdev_ops = &mlx5e_netdev_ops;

	mlx5e_dcbnl_build_netdev(netdev);

#if defined(CONFIG_MLX5_ESWITCH) && defined(HAVE_SWITCHDEV_OPS)
        netdev->switchdev_ops = &mlx5e_switchdev_ops;
#endif
	netdev->watchdog_timeo    = 15 * HZ;

	netdev->ethtool_ops	  = &mlx5e_ethtool_ops;

	netdev->vlan_features    |= NETIF_F_SG;
	netdev->vlan_features    |= NETIF_F_HW_CSUM;
	netdev->vlan_features    |= NETIF_F_GRO;
	netdev->vlan_features    |= NETIF_F_TSO;
	netdev->vlan_features    |= NETIF_F_TSO6;
	netdev->vlan_features    |= NETIF_F_RXCSUM;
	netdev->vlan_features    |= NETIF_F_RXHASH;
#ifdef HAVE_NETIF_F_GSO_PARTIAL
	netdev->vlan_features    |= NETIF_F_GSO_PARTIAL;
#endif

	netdev->mpls_features    |= NETIF_F_SG;
	netdev->mpls_features    |= NETIF_F_HW_CSUM;
	netdev->mpls_features    |= NETIF_F_TSO;
	netdev->mpls_features    |= NETIF_F_TSO6;

	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_RX;

	/* Tunneled LRO is not supported in the driver, and the same RQs are
	 * shared between inner and outer TIRs, so the driver can't disable LRO
	 * for inner TIRs while having it enabled for outer TIRs. Due to this,
	 * block LRO altogether if the firmware declares tunneled LRO support.
	 */
	/* If SW LRO is supported turn on LRO Primary flags*/
#ifdef CONFIG_COMPAT_LRO_ENABLED_IPOIB
	netdev->vlan_features    |= NETIF_F_LRO;
#else
	if (!!MLX5_CAP_ETH(mdev, lro_cap) &&
	    !MLX5_CAP_ETH(mdev, tunnel_lro_vxlan) &&
	    !MLX5_CAP_ETH(mdev, tunnel_lro_gre) &&
	    mlx5e_check_fragmented_striding_rq_cap(mdev, PAGE_SHIFT,
						   MLX5E_MPWRQ_UMR_MODE_ALIGNED))
		netdev->vlan_features    |= NETIF_F_LRO;
#endif

	netdev->hw_features       = netdev->vlan_features;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_RX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_FILTER;
	netdev->hw_features      |= NETIF_F_HW_VLAN_STAG_TX;

	if (mlx5e_tunnel_any_tx_proto_supported(mdev)) {
		netdev->hw_enc_features |= NETIF_F_HW_CSUM;
		netdev->hw_enc_features |= NETIF_F_TSO;
		netdev->hw_enc_features |= NETIF_F_TSO6;
#ifdef HAVE_NETIF_F_GSO_PARTIAL
		netdev->hw_enc_features |= NETIF_F_GSO_PARTIAL;
#endif
	}

#ifdef HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON
	if (mlx5_vxlan_allowed(mdev->vxlan) || mlx5_geneve_tx_allowed(mdev)) {
		netdev->hw_features     |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
#ifdef HAVE_NETIF_F_GSO_PARTIAL
		netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
#endif
		netdev->vlan_features |= NETIF_F_GSO_UDP_TUNNEL |
					 NETIF_F_GSO_UDP_TUNNEL_CSUM;
	}
#endif

	if (mlx5e_tunnel_proto_supported_tx(mdev, IPPROTO_GRE)) {
		netdev->hw_features     |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
#ifdef HAVE_NETIF_F_GSO_PARTIAL
		netdev->gso_partial_features |= NETIF_F_GSO_GRE |
						NETIF_F_GSO_GRE_CSUM;
#endif
	}

	if (mlx5e_tunnel_proto_supported_tx(mdev, IPPROTO_IPIP)) {
#ifdef HAVE_NETIF_F_GSO_IPXIP6
		netdev->hw_features |= NETIF_F_GSO_IPXIP4 |
				       NETIF_F_GSO_IPXIP6;
		netdev->hw_enc_features |= NETIF_F_GSO_IPXIP4 |
					   NETIF_F_GSO_IPXIP6;
		netdev->gso_partial_features |= NETIF_F_GSO_IPXIP4 |
						NETIF_F_GSO_IPXIP6;
#endif
	}

#ifdef HAVE_NETIF_F_GSO_UDP_L4
	netdev->gso_partial_features             |= NETIF_F_GSO_UDP_L4;
	netdev->hw_features                      |= NETIF_F_GSO_UDP_L4;
	netdev->features                         |= NETIF_F_GSO_UDP_L4;
#endif

	mlx5_query_port_fcs(mdev, &fcs_supported, &fcs_enabled);

	if (fcs_supported)
		netdev->hw_features |= NETIF_F_RXALL;

	if (MLX5_CAP_ETH(mdev, scatter_fcs))
		netdev->hw_features |= NETIF_F_RXFCS;

#ifdef CONFIG_COMPAT_CLS_FLOWER_MOD
#if !defined(CONFIG_NET_SCHED_NEW) && !defined(CONFIG_COMPAT_KERNEL_4_14)
	if (mlx5_qos_is_supported(mdev))
		netdev->hw_features |= NETIF_F_HW_TC;
#endif
#endif

	netdev->features          = netdev->hw_features;

	/* Defaults */
	if (fcs_enabled)
		netdev->features  &= ~NETIF_F_RXALL;
	netdev->features  &= ~NETIF_F_LRO;
#ifdef HAVE_NETIF_F_GRO_HW
	netdev->features  &= ~NETIF_F_GRO_HW;
#endif
	netdev->features  &= ~NETIF_F_RXFCS;

#define FT_CAP(f) MLX5_CAP_FLOWTABLE(mdev, flow_table_properties_nic_receive.f)
	if (FT_CAP(flow_modify_en) &&
	    FT_CAP(modify_root) &&
	    FT_CAP(identified_miss_table_mode) &&
	    FT_CAP(flow_table_modify)) {
#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
		netdev->hw_features      |= NETIF_F_HW_TC;
#endif
#ifdef CONFIG_MLX5_EN_ARFS
#ifndef HAVE_NET_FLOW_KEYS_H
		netdev->hw_features	 |= NETIF_F_NTUPLE;
#endif
#endif
	}

	netdev->features         |= NETIF_F_HIGHDMA;
	netdev->features         |= NETIF_F_HW_VLAN_STAG_FILTER;

	netdev->priv_flags       |= IFF_UNICAST_FLT;

#ifdef HAVE_STRUCT_HOP_JUMBO_HDR
	netif_set_tso_max_size(netdev, GSO_MAX_SIZE);
#endif
	mlx5e_set_netdev_dev_addr(netdev);
	mlx5e_macsec_build_netdev(priv);
	mlx5e_ipsec_build_netdev(priv);
	mlx5e_ktls_build_netdev(priv);
}

void mlx5e_create_q_counters(struct mlx5e_priv *priv)
{
	u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)] = {};
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	MLX5_SET(alloc_q_counter_in, in, opcode, MLX5_CMD_OP_ALLOC_Q_COUNTER);
	err = mlx5_cmd_exec_inout(mdev, alloc_q_counter, in, out);
	if (!err)
		priv->q_counter =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);

	err = mlx5_cmd_exec_inout(mdev, alloc_q_counter, in, out);
	if (!err)
		priv->drop_rq_q_counter =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);
}

void mlx5e_destroy_q_counters(struct mlx5e_priv *priv)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_q_counter_in)] = {};

	MLX5_SET(dealloc_q_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
	if (priv->q_counter) {
		MLX5_SET(dealloc_q_counter_in, in, counter_set_id,
			 priv->q_counter);
		mlx5_cmd_exec_in(priv->mdev, dealloc_q_counter, in);
	}

	if (priv->drop_rq_q_counter) {
		MLX5_SET(dealloc_q_counter_in, in, counter_set_id,
			 priv->drop_rq_q_counter);
		mlx5_cmd_exec_in(priv->mdev, dealloc_q_counter, in);
	}
}

static int mlx5e_nic_init(struct mlx5_core_dev *mdev,
			  struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_flow_steering *fs;
	int err;

	mlx5e_build_nic_params(priv,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
			      &priv->xsk,
#endif
			      netdev->mtu);
#if defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON) && defined(HAVE_UDP_TUNNEL_NIC_INFO)
	mlx5e_vxlan_set_netdev_info(priv);
#endif

	mlx5e_init_delay_drop(priv, &priv->channels.params);

	mlx5e_timestamp_init(priv);

	fs = mlx5e_fs_init(priv->profile, mdev,
			   !test_bit(MLX5E_STATE_DESTROYING, &priv->state));
	if (!fs) {
		err = -ENOMEM;
		mlx5_core_err(mdev, "FS initialization failed, %d\n", err);
		return err;
	}
	priv->fs = fs;

	err = mlx5e_ktls_init(priv);
	if (err)
		mlx5_core_err(mdev, "TLS initialization failed, %d\n", err);

	mlx5e_health_create_reporters(priv);
	return 0;
}

static void mlx5e_nic_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_health_destroy_reporters(priv);
	mlx5e_ktls_cleanup(priv);
	mlx5e_fs_cleanup(priv->fs);
}

static int mlx5e_init_nic_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	enum mlx5e_rx_res_features features;
	int err;

	priv->rx_res = mlx5e_rx_res_alloc();
	if (!priv->rx_res)
		return -ENOMEM;

	/* Update tunneled offloads cap which might be updated during re-attach */
	priv->channels.params.tunneled_offload_en = mlx5_tunnel_inner_ft_supported(mdev);

	mlx5e_create_q_counters(priv);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_q_counters;
	}

	features = MLX5E_RX_RES_FEATURE_PTP;
	if (priv->channels.params.tunneled_offload_en)
		features |= MLX5E_RX_RES_FEATURE_INNER_FT;
	err = mlx5e_rx_res_init(priv->rx_res, priv->mdev, features,
				priv->max_nch, priv->drop_rq.rqn,
				&priv->channels.params.packet_merge,
				priv->channels.params.num_channels);
	if (err)
		goto err_close_drop_rq;

	err = mlx5e_create_flow_steering(priv);
	if (err) {
		mlx5_core_warn(mdev, "create flow steering failed, %d\n", err);
		goto err_destroy_rx_res;
	}

	err = mlx5e_tc_nic_init(priv);
	if (err)
		goto err_destroy_flow_steering;

	err = mlx5e_accel_init_rx(priv);
	if (err)
		goto err_tc_nic_cleanup;

#ifdef CONFIG_MLX5_EN_ARFS
	priv->netdev->rx_cpu_rmap =  mlx5_eq_table_get_rmap(priv->mdev);
#endif

	return 0;

err_tc_nic_cleanup:
	mlx5e_tc_nic_cleanup(priv);
err_destroy_flow_steering:
	mlx5e_destroy_flow_steering(priv);
err_destroy_rx_res:
	mlx5e_rx_res_destroy(priv->rx_res);
err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_destroy_q_counters:
	mlx5e_destroy_q_counters(priv);
	mlx5e_rx_res_free(priv->rx_res);
	priv->rx_res = NULL;
	return err;
}

static void mlx5e_cleanup_nic_rx(struct mlx5e_priv *priv)
{
	mlx5e_accel_cleanup_rx(priv);
	mlx5e_tc_nic_cleanup(priv);
	mlx5e_destroy_flow_steering(priv);
	mlx5e_rx_res_destroy(priv->rx_res);
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5e_destroy_q_counters(priv);
	mlx5e_rx_res_free(priv->rx_res);
	priv->rx_res = NULL;
}

#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
static void mlx5e_set_mqprio_rl(struct mlx5e_priv *priv)
{
	struct mlx5e_params *params;
	struct mlx5e_mqprio_rl *rl;

	params = &priv->channels.params;
	if (params->mqprio.mode != TC_MQPRIO_MODE_CHANNEL)
		return;

	rl = mlx5e_mqprio_rl_create(priv->mdev, params->mqprio.num_tc,
				    params->mqprio.channel.max_rate);
	if (IS_ERR(rl))
		rl = NULL;
	priv->mqprio_rl = rl;
	mlx5e_mqprio_rl_update_params(params, rl);
}
#endif

static int mlx5e_init_nic_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}

	err = mlx5e_accel_init_tx(priv);
	if (err)
		goto err_destroy_tises;

#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
	mlx5e_set_mqprio_rl(priv);
#endif
	mlx5e_dcbnl_initialize(priv);
	return 0;

err_destroy_tises:
	mlx5e_destroy_tises(priv);
	return err;
}

static void mlx5e_nic_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;
#if defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	u16 max_mtu;
#endif

	mlx5e_fs_init_l2_addr(priv->fs, netdev);
	mlx5e_ipsec_init(priv);

	err = mlx5e_macsec_init(priv);
	if (err)
		mlx5_core_err(mdev, "MACsec initialization failed, %d\n", err);

	/* Marking the link as currently not needed by the Driver */
	if (!netif_running(netdev))
		mlx5e_modify_admin_state(mdev, MLX5_PORT_DOWN);

#ifdef HAVE_NET_DEVICE_MIN_MAX_MTU
	mlx5e_set_netdev_mtu_boundaries(priv);
#elif defined(HAVE_NET_DEVICE_MIN_MAX_MTU_EXTENDED)
	netdev->extended->min_mtu = ETH_MIN_MTU;
	mlx5_query_port_max_mtu(priv->mdev, &max_mtu, 1);
	netdev->extended->max_mtu = MLX5E_HW2SW_MTU(&priv->channels.params, max_mtu);
#endif
	mlx5e_set_dev_port_mtu(priv);

	mlx5_lag_add_netdev(mdev, netdev);

#ifdef HAVE_BASECODE_EXTRAS
	if (!is_valid_ether_addr(netdev->perm_addr))
		memcpy(netdev->perm_addr, netdev->dev_addr, netdev->addr_len);
#endif

	mlx5e_enable_async_events(priv);
	mlx5e_enable_blocking_events(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_init(priv);

	mlx5e_hv_vhca_stats_create(priv);
	if (netdev->reg_state != NETREG_REGISTERED)
		return;
	mlx5e_dcbnl_init_app(priv);

	mlx5e_nic_set_rx_mode(priv);

	rtnl_lock();
	if (netif_running(netdev))
		mlx5e_open(netdev);
#ifdef HAVE_UDP_TUNNEL_NIC_INFO
	udp_tunnel_nic_reset_ntf(priv->netdev);
#elif defined(HAVE_UDP_TUNNEL_RX_INFO) && defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON) && defined(HAVE_DEVLINK_HAS_RELOAD_UP_DOWN)
	if (mlx5_vxlan_allowed(priv->mdev->vxlan))
		udp_tunnel_get_rx_info(priv->netdev);
#endif
	netif_device_attach(netdev);
	rtnl_unlock();
}

static void mlx5e_nic_disable(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (priv->netdev->reg_state == NETREG_REGISTERED)
		mlx5e_dcbnl_delete_app(priv);

	rtnl_lock();
	if (netif_running(priv->netdev))
		mlx5e_close(priv->netdev);
#ifndef HAVE_UDP_TUNNEL_NIC_INFO
#if defined(HAVE_UDP_TUNNEL_RX_INFO) && defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON) && defined(HAVE_DEVLINK_HAS_RELOAD_UP_DOWN)
	if (mlx5_vxlan_allowed(priv->mdev->vxlan))
		udp_tunnel_drop_rx_info(priv->netdev);

#endif
#endif
	netif_device_detach(priv->netdev);
	rtnl_unlock();

	mlx5e_nic_set_rx_mode(priv);

	mlx5e_hv_vhca_stats_destroy(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_cleanup(priv);

	mlx5e_disable_blocking_events(priv);
	if (priv->en_trap) {
		mlx5e_deactivate_trap(priv);
		mlx5e_close_trap(priv->en_trap);
		priv->en_trap = NULL;
	}
	mlx5e_disable_async_events(priv);
	mlx5_lag_remove_netdev(mdev, priv->netdev);
#if defined(HAVE_KERNEL_WITH_VXLAN_SUPPORT_ON) && defined(HAVE_DEVLINK_HAS_RELOAD_UP_DOWN)
	mlx5_vxlan_reset_to_default(mdev->vxlan);
#endif
	mlx5e_macsec_cleanup(priv);
	mlx5e_ipsec_cleanup(priv);
}

int mlx5e_update_nic_rx(struct mlx5e_priv *priv)
{
	return mlx5e_refresh_tirs(priv, false, false);
}

static const struct mlx5e_profile mlx5e_nic_profile = {
	.init		   = mlx5e_nic_init,
	.cleanup	   = mlx5e_nic_cleanup,
	.init_rx	   = mlx5e_init_nic_rx,
	.cleanup_rx	   = mlx5e_cleanup_nic_rx,
	.init_tx	   = mlx5e_init_nic_tx,
	.cleanup_tx	   = mlx5e_cleanup_nic_tx,
	.enable		   = mlx5e_nic_enable,
	.disable	   = mlx5e_nic_disable,
	.update_rx	   = mlx5e_update_nic_rx,
	.update_stats	   = mlx5e_stats_update_ndo_stats,
	.update_carrier	   = mlx5e_update_carrier,
	.rx_handlers       = &mlx5e_rx_handlers_nic,
	.max_tc		   = MLX5E_MAX_NUM_TC,
	.stats_grps	   = mlx5e_nic_stats_grps,
	.stats_grps_num	   = mlx5e_nic_stats_grps_num,
	.features          = BIT(MLX5E_PROFILE_FEATURE_PTP_RX) |
		BIT(MLX5E_PROFILE_FEATURE_PTP_TX) |
		BIT(MLX5E_PROFILE_FEATURE_QOS_HTB) |
		BIT(MLX5E_PROFILE_FEATURE_FS_VLAN) |
		BIT(MLX5E_PROFILE_FEATURE_FS_TC),
};

static int mlx5e_profile_max_num_channels(struct mlx5_core_dev *mdev,
					  const struct mlx5e_profile *profile)
{
	int nch;

	nch = mlx5e_get_max_num_channels(mdev);

	if (profile->max_nch_limit)
		nch = min_t(int, nch, profile->max_nch_limit(mdev));
	return nch;
}

static unsigned int
mlx5e_calc_max_nch(struct mlx5_core_dev *mdev, struct net_device *netdev,
		   const struct mlx5e_profile *profile)

{
	unsigned int max_nch, tmp;

	/* core resources */
	max_nch = mlx5e_profile_max_num_channels(mdev, profile);

	/* netdev rx queues */
	max_nch = min_t(unsigned int, max_nch, netdev->num_rx_queues);

	/* netdev tx queues */
	tmp = netdev->num_tx_queues;
	if (mlx5_qos_is_supported(mdev))
		tmp -= mlx5e_qos_max_leaf_nodes(mdev);
	if (MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn))
		tmp -= profile->max_tc;
	tmp = tmp / profile->max_tc;
	max_nch = min_t(unsigned int, max_nch, tmp);

	return max_nch;
}

int mlx5e_get_pf_num_tirs(struct mlx5_core_dev *mdev)
{
	/* Indirect TIRS: 2 sets of TTCs (inner + outer steering)
	 * and 1 set of direct TIRS
	 */
	return 2 * MLX5E_NUM_INDIR_TIRS
		+ mlx5e_profile_max_num_channels(mdev, &mlx5e_nic_profile);
}

void mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       set_rx_mode_work);

	return mlx5e_fs_set_rx_mode_work(priv->fs, priv->netdev);
}

/* mlx5e generic netdev management API (move to en_common.c) */
int mlx5e_priv_init(struct mlx5e_priv *priv,
		    const struct mlx5e_profile *profile,
		    struct net_device *netdev,
		    struct mlx5_core_dev *mdev)
{
	int nch, num_txqs, node;
	int err;

	num_txqs = netdev->num_tx_queues;
	nch = mlx5e_calc_max_nch(mdev, netdev, profile);
	node = dev_to_node(mlx5_core_dma_dev(mdev));

	/* priv init */
	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->msglevel    = MLX5E_MSG_LEVEL;
	priv->max_nch     = nch;
	priv->max_opened_tc = 1;
	priv->pcp_tc_num = 1;

	if (!alloc_cpumask_var(&priv->scratchpad.cpumask, GFP_KERNEL))
		return -ENOMEM;

	mutex_init(&priv->state_lock);

	err = mlx5e_selq_init(&priv->selq, &priv->state_lock);
	if (err)
		goto err_free_cpumask;

	INIT_WORK(&priv->update_carrier_work, mlx5e_update_carrier_work);
	INIT_WORK(&priv->set_rx_mode_work, mlx5e_set_rx_mode_work);
	INIT_WORK(&priv->tx_timeout_work, mlx5e_tx_timeout_work);
	INIT_WORK(&priv->update_stats_work, mlx5e_update_stats_work);

	priv->wq = create_singlethread_workqueue("mlx5e");
	if (!priv->wq)
		goto err_free_selq;

	priv->txq2sq = kcalloc_node(num_txqs, sizeof(*priv->txq2sq), GFP_KERNEL, node);
	if (!priv->txq2sq)
		goto err_destroy_workqueue;

	priv->tx_rates = kcalloc_node(num_txqs, sizeof(*priv->tx_rates), GFP_KERNEL, node);
	if (!priv->tx_rates)
		goto err_free_txq2sq;

	priv->channel_stats =
		kcalloc_node(nch, sizeof(*priv->channel_stats), GFP_KERNEL, node);
	if (!priv->channel_stats)
		goto err_free_tx_rates;

	return 0;

err_free_tx_rates:
	kfree(priv->tx_rates);
err_free_txq2sq:
	kfree(priv->txq2sq);
err_destroy_workqueue:
	destroy_workqueue(priv->wq);
err_free_selq:
	mlx5e_selq_cleanup(&priv->selq);
err_free_cpumask:
	free_cpumask_var(priv->scratchpad.cpumask);
	return -ENOMEM;
}

void mlx5e_priv_cleanup(struct mlx5e_priv *priv)
{
	int i;

	/* bail if change profile failed and also rollback failed */
	if (!priv->mdev)
		return;

	for (i = 0; i < priv->stats_nch; i++)
		kvfree(priv->channel_stats[i]);
	kfree(priv->channel_stats);
	kfree(priv->tx_rates);
	kfree(priv->txq2sq);
	destroy_workqueue(priv->wq);
	mutex_lock(&priv->state_lock);
	mlx5e_selq_cleanup(&priv->selq);
	mutex_unlock(&priv->state_lock);
	free_cpumask_var(priv->scratchpad.cpumask);

	for (i = 0; i < priv->htb_max_qos_sqs; i++)
		kfree(priv->htb_qos_sq_stats[i]);
	kvfree(priv->htb_qos_sq_stats);

	memset(priv, 0, sizeof(*priv));
}

static unsigned int mlx5e_get_max_num_txqs(struct mlx5_core_dev *mdev,
					   const struct mlx5e_profile *profile)
{
	unsigned int nch, ptp_txqs, qos_txqs;

	nch = mlx5e_profile_max_num_channels(mdev, profile);

	ptp_txqs = MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn) &&
		mlx5e_profile_feature_cap(profile, PTP_TX) ?
		profile->max_tc : 0;

	qos_txqs = mlx5_qos_is_supported(mdev) &&
		mlx5e_profile_feature_cap(profile, QOS_HTB) ?
		mlx5e_qos_max_leaf_nodes(mdev) : 0;

	return nch * profile->max_tc + ptp_txqs + qos_txqs;
}

static unsigned int mlx5e_get_max_num_rxqs(struct mlx5_core_dev *mdev,
					   const struct mlx5e_profile *profile)
{
	return mlx5e_profile_max_num_channels(mdev, profile);
}

struct net_device *
mlx5e_create_netdev(struct mlx5_core_dev *mdev, const struct mlx5e_profile *profile)
{
	struct net_device *netdev;
	unsigned int txqs, rxqs;
	int err;

	txqs = mlx5e_get_max_num_txqs(mdev, profile);
	rxqs = mlx5e_get_max_num_rxqs(mdev, profile);

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv), txqs, rxqs);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

	err = mlx5e_priv_init(netdev_priv(netdev), profile, netdev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_priv_init failed, err=%d\n", err);
		goto err_free_netdev;
	}

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
	dev_net_set(netdev, mlx5_core_net(mdev));

	return netdev;

err_free_netdev:
	free_netdev(netdev);

	return NULL;
}

static void mlx5e_update_features(struct net_device *netdev)
{
	if (netdev->reg_state != NETREG_REGISTERED)
		return; /* features will be updated on netdev registration */

	rtnl_lock();
	netdev_update_features(netdev);
	rtnl_unlock();
}

static void mlx5e_reset_channels(struct net_device *netdev)
{
	netdev_reset_tc(netdev);
}

int mlx5e_attach_netdev(struct mlx5e_priv *priv)
{
	const bool take_rtnl = priv->netdev->reg_state == NETREG_REGISTERED;
	const struct mlx5e_profile *profile = priv->profile;
	int max_nch;
	int err;

	clear_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		priv->fs->state_destroy = !test_bit(MLX5E_STATE_DESTROYING, &priv->state);

	/* Validate the max_wqe_size_sq capability. */
	if (WARN_ON_ONCE(mlx5e_get_max_sq_wqebbs(priv->mdev) < MLX5E_MAX_TX_WQEBBS)) {
		mlx5_core_warn(priv->mdev, "MLX5E: Max SQ WQEBBs firmware capability: %u, needed %lu\n",
			       mlx5e_get_max_sq_wqebbs(priv->mdev), MLX5E_MAX_TX_WQEBBS);
		return -EIO;
	}

	/* max number of channels may have changed */
	max_nch = mlx5e_calc_max_nch(priv->mdev, priv->netdev, profile);
	if (priv->channels.params.num_channels > max_nch) {
		mlx5_core_warn(priv->mdev, "MLX5E: Reducing number of channels to %d\n", max_nch);
		/* Reducing the number of channels - RXFH has to be reset, and
		 * mlx5e_num_channels_changed below will build the RQT.
		 */
#ifdef HAVE_NETDEV_IFF_RXFH_CONFIGURED
		priv->netdev->priv_flags &= ~IFF_RXFH_CONFIGURED;
#endif
		priv->channels.params.num_channels = max_nch;
#ifdef HAVE_TC_MQPRIO_QOPT_OFFLOAD
		if (priv->channels.params.mqprio.mode == TC_MQPRIO_MODE_CHANNEL) {
			mlx5_core_warn(priv->mdev, "MLX5E: Disabling MQPRIO channel mode\n");
			mlx5e_params_mqprio_reset(&priv->channels.params);
		}
#endif
	}
	if (max_nch != priv->max_nch) {
		mlx5_core_warn(priv->mdev,
			       "MLX5E: Updating max number of channels from %u to %u\n",
			       priv->max_nch, max_nch);
		priv->max_nch = max_nch;
	}

	/* 1. Set the real number of queues in the kernel the first time.
	 * 2. Set our default XPS cpumask.
	 * 3. Build the RQT.
	 *
	 * rtnl_lock is required by netif_set_real_num_*_queues in case the
	 * netdev has been registered by this point (if this function was called
	 * in the reload or resume flow).
	 */
	if (take_rtnl)
		rtnl_lock();
	err = mlx5e_num_channels_changed(priv);
	if (take_rtnl)
		rtnl_unlock();
	if (err)
		goto out;

	err = profile->init_tx(priv);
	if (err)
		goto out;

	err = profile->init_rx(priv);
	if (err)
		goto err_cleanup_tx;

	if (profile->enable)
		profile->enable(priv);

	mlx5e_update_features(priv->netdev);

	return 0;

err_cleanup_tx:
	profile->cleanup_tx(priv);

out:
	mlx5e_reset_channels(priv->netdev);
	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		priv->fs->state_destroy = !test_bit(MLX5E_STATE_DESTROYING, &priv->state);
	cancel_work_sync(&priv->update_stats_work);
	return err;
}

void mlx5e_detach_netdev(struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		priv->fs->state_destroy = !test_bit(MLX5E_STATE_DESTROYING, &priv->state);

	if (profile->disable)
		profile->disable(priv);
	flush_workqueue(priv->wq);

	profile->cleanup_rx(priv);
	profile->cleanup_tx(priv);
	mlx5e_reset_channels(priv->netdev);
	cancel_work_sync(&priv->update_stats_work);
}

static int
mlx5e_netdev_init_profile(struct net_device *netdev, struct mlx5_core_dev *mdev,
			  const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	err = mlx5e_priv_init(priv, new_profile, netdev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_priv_init failed, err=%d\n", err);
		return err;
	}
	netif_carrier_off(netdev);
	priv->profile = new_profile;
	priv->ppriv = new_ppriv;
	err = new_profile->init(priv->mdev, priv->netdev);
	if (err)
		goto priv_cleanup;

	return 0;

priv_cleanup:
	mlx5e_priv_cleanup(priv);
	return err;
}

static int
mlx5e_netdev_attach_profile(struct net_device *netdev, struct mlx5_core_dev *mdev,
			    const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	err = mlx5e_netdev_init_profile(netdev, mdev, new_profile, new_ppriv);
	if (err)
		return err;

	err = mlx5e_attach_netdev(priv);
	if (err)
		goto profile_cleanup;
	return err;

profile_cleanup:
	new_profile->cleanup(priv);
	mlx5e_priv_cleanup(priv);
	return err;
}

int mlx5e_netdev_change_profile(struct mlx5e_priv *priv,
				const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	const struct mlx5e_profile *orig_profile = priv->profile;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	void *orig_ppriv = priv->ppriv;
	int err, rollback_err;

	/* cleanup old profile */
	mlx5e_detach_netdev(priv);
	priv->profile->cleanup(priv);
	mlx5e_priv_cleanup(priv);

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		mlx5e_netdev_init_profile(netdev, mdev, new_profile, new_ppriv);
		set_bit(MLX5E_STATE_DESTROYING, &priv->state);
		return -EIO;
	}

	err = mlx5e_netdev_attach_profile(netdev, mdev, new_profile, new_ppriv);
	if (err) { /* roll back to original profile */
		netdev_warn(netdev, "%s: new profile init failed, %d\n", __func__, err);
		goto rollback;
	}

	return 0;

rollback:
	rollback_err = mlx5e_netdev_attach_profile(netdev, mdev, orig_profile, orig_ppriv);
	if (rollback_err)
		netdev_err(netdev, "%s: failed to rollback to orig profile, %d\n",
			   __func__, rollback_err);
	return err;
}

void mlx5e_netdev_attach_nic_profile(struct mlx5e_priv *priv)
{
	mlx5e_netdev_change_profile(priv, &mlx5e_nic_profile, NULL);
}

void mlx5e_destroy_netdev(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;

	mlx5e_priv_cleanup(priv);
	free_netdev(netdev);
}

static int mlx5e_resume(struct auxiliary_device *adev)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5e_priv *priv = auxiliary_get_drvdata(adev);
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = edev->mdev;
	int err;

	if (netif_device_present(netdev))
		return 0;

	err = mlx5e_create_mdev_resources(mdev);
	if (err)
		return err;

	err = mlx5e_attach_netdev(priv);
	if (err) {
		mlx5e_destroy_mdev_resources(mdev);
		return err;
	}

	return 0;
}

static int mlx5e_suspend(struct auxiliary_device *adev, pm_message_t state)
{
	struct mlx5e_priv *priv = auxiliary_get_drvdata(adev);
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!netif_device_present(netdev)) {
		if (test_bit(MLX5E_STATE_DESTROYING, &priv->state) &&
		    !test_bit(MLX5_BREAK_FW_WAIT, &mdev->intf_state))
			mlx5e_destroy_mdev_resources(mdev);
		return -ENODEV;
	}

	mlx5e_detach_netdev(priv);
	mlx5e_destroy_mdev_resources(mdev);
	return 0;
}

static int mlx5e_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	const struct mlx5e_profile *profile = &mlx5e_nic_profile;
	struct mlx5_core_dev *mdev = edev->mdev;
	struct net_device *netdev;
	pm_message_t state = {};
	struct mlx5e_priv *priv;
	int err;

	netdev = mlx5e_create_netdev(mdev, profile);
	if (!netdev) {
		mlx5_core_err(mdev, "mlx5e_create_netdev failed\n");
		return -ENOMEM;
	}

	mlx5e_build_nic_netdev(netdev);

	priv = netdev_priv(netdev);
	auxiliary_set_drvdata(adev, priv);

	priv->profile = profile;
	priv->ppriv = NULL;

#ifdef HAVE_DEVLINK_PORT_ATRRS_SET_GET_SUPPORT
	err = mlx5e_devlink_port_register(priv);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_devlink_port_register failed, %d\n", err);
		goto err_destroy_netdev;
	}
#endif

	err = profile->init(mdev, netdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_nic_profile init failed, %d\n", err);
		goto err_devlink_cleanup;
	}

	err = mlx5e_resume(adev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_resume failed, %d\n", err);
		goto err_profile_cleanup;
	}

#ifdef HAVE_BASECODE_EXTRAS
	mlx5e_rep_set_sysfs_attr(netdev);
#endif

	err = register_netdev(netdev);
	if (err) {
		mlx5_core_err(mdev, "register_netdev failed, %d\n", err);
		goto err_resume;
	}

#ifdef HAVE_DEVLINK_PORT_ATRRS_SET_GET_SUPPORT
	mlx5e_devlink_port_type_eth_set(priv);
#endif

	err = mlx5e_sysfs_create(netdev);
	if (err)
		goto err_unregister_netdev;

	mlx5e_dcbnl_init_app(priv);
	mlx5_core_uplink_netdev_set(mdev, netdev);
	return 0;

err_unregister_netdev:
	unregister_netdev(netdev);
err_resume:
	mlx5e_suspend(adev, state);
err_profile_cleanup:
	profile->cleanup(priv);
err_devlink_cleanup:
#ifdef HAVE_DEVLINK_PORT_ATRRS_SET_GET_SUPPORT
	mlx5e_devlink_port_unregister(priv);
err_destroy_netdev:
#endif
	mlx5e_destroy_netdev(priv);
	return err;
}

static void mlx5e_remove(struct auxiliary_device *adev)
{
	struct mlx5e_priv *priv = auxiliary_get_drvdata(adev);
	pm_message_t state = {};

	mlx5_core_uplink_netdev_set(priv->mdev, NULL);
	mlx5e_dcbnl_delete_app(priv);
	mlx5e_sysfs_remove(priv->netdev);
	unregister_netdev(priv->netdev);
	mlx5e_suspend(adev, state);
	priv->profile->cleanup(priv);
#ifdef HAVE_DEVLINK_PORT_ATRRS_SET_GET_SUPPORT
	mlx5e_devlink_port_unregister(priv);
#endif
	mlx5e_destroy_netdev(priv);
}

static const struct auxiliary_device_id mlx5e_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".eth", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary_mlx5e_id_table, mlx5e_id_table);

static struct auxiliary_driver mlx5e_driver = {
	.name = "eth",
	.probe = mlx5e_probe,
	.remove = mlx5e_remove,
	.suspend = mlx5e_suspend,
	.resume = mlx5e_resume,
	.id_table = mlx5e_id_table,
};

int mlx5e_init(void)
{
	int ret;

#ifdef __ETHTOOL_DECLARE_LINK_MODE_MASK
       mlx5e_build_ptys2ethtool_map();
#endif
	ret = auxiliary_driver_register(&mlx5e_driver);
	if (ret)
		return ret;

	ret = mlx5e_rep_init();
	if (ret)
		auxiliary_driver_unregister(&mlx5e_driver);
	return ret;
}

void mlx5e_cleanup(void)
{
	mlx5e_rep_cleanup();
	auxiliary_driver_unregister(&mlx5e_driver);
}

bool mlx5e_is_rep_shared_rq(const struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!mlx5e_is_vport_rep(priv))
		return false;

	if (mlx5e_is_uplink_rep(priv))
		return false;

	if (!mlx5e_esw_offloads_pet_enabled(mdev->priv.eswitch))
		return false;

	return true;
}

