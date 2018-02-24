/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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

#ifdef HAVE_XDP_SUPPORT
#include <linux/bpf_trace.h>
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
#include "en/xdp.h"
#include "en/params.h"

int mlx5e_xdp_max_mtu(struct mlx5e_params *params, struct mlx5e_xsk_param *xsk)
{
	int hr = mlx5e_get_linear_rq_headroom(params, xsk);

	/* Let S := SKB_DATA_ALIGN(sizeof(struct skb_shared_info)).
	 * The condition checked in mlx5e_rx_is_linear_skb is:
	 *   SKB_DATA_ALIGN(sw_mtu + hard_mtu + hr) + S <= PAGE_SIZE         (1)
	 *   (Note that hw_mtu == sw_mtu + hard_mtu.)
	 * What is returned from this function is:
	 *   max_mtu = PAGE_SIZE - S - hr - hard_mtu                         (2)
	 * After assigning sw_mtu := max_mtu, the left side of (1) turns to
	 * SKB_DATA_ALIGN(PAGE_SIZE - S) + S, which is equal to PAGE_SIZE,
	 * because both PAGE_SIZE and S are already aligned. Any number greater
	 * than max_mtu would make the left side of (1) greater than PAGE_SIZE,
	 * so max_mtu is the maximum MTU allowed.
	 */

	return MLX5E_HW2SW_MTU(params, SKB_MAX_HEAD(hr));
}

static inline bool
mlx5e_xmit_xdp_buff(struct mlx5e_xdpsq *sq, struct mlx5e_rq *rq,
		    struct mlx5e_alloc_unit *au, struct xdp_buff *xdp)
{
	struct skb_shared_info *sinfo = NULL;
	struct mlx5e_xmit_data xdptxd;
	struct mlx5e_xdp_info xdpi;
	struct xdp_frame *xdpf;
	dma_addr_t dma_addr;
	struct page *page;
#ifdef HAVE_XDP_HAS_FRAGS
	int i;
#endif
	page = !au ? NULL : au->page;
#ifdef HAVE_XDP_CONVERT_BUFF_TO_FRAME
	xdpf = xdp_convert_buff_to_frame(xdp);
#else
	xdpf = convert_to_xdp_frame(xdp);
#endif
	if (unlikely(!xdpf))
		return false;

	xdptxd.data = xdpf->data;
	xdptxd.len  = xdpf->len;

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (xdp->rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL) {
		/* The xdp_buff was in the UMEM and was copied into a newly
		 * allocated page. The UMEM page was returned via the ZCA, and
		 * this new page has to be mapped at this point and has to be
		 * unmapped and returned via xdp_return_frame on completion.
		 */

		/* Prevent double recycling of the UMEM page. Even in case this
		 * function returns false, the xdp_buff shouldn't be recycled,
		 * as it was already done in xdp_convert_zc_to_xdp_frame.
		 */
		__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags); /* non-atomic */

		xdpi.mode = MLX5E_XDP_XMIT_MODE_FRAME;

		dma_addr = dma_map_single(sq->pdev, xdptxd.data, xdptxd.len,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(sq->pdev, dma_addr)) {
			xdp_return_frame(xdpf);
			return false;
		}

		xdptxd.dma_addr     = dma_addr;
		xdpi.frame.xdpf     = xdpf;
		xdpi.frame.dma_addr = dma_addr;

		if (unlikely(!INDIRECT_CALL_2(sq->xmit_xdp_frame, mlx5e_xmit_xdp_frame_mpwqe,
					      mlx5e_xmit_xdp_frame, sq, &xdptxd, NULL, 0)))
			return false;

		mlx5e_xdpi_fifo_push(&sq->db.xdpi_fifo, &xdpi);
		return true;
	}
#endif

	/* Driver assumes that xdp_convert_buff_to_frame returns an xdp_frame
	 * that points to the same memory region as the original xdp_buff. It
	 * allows to map the memory only once and to use the DMA_BIDIRECTIONAL
	 * mode.
	 */

	xdpi.mode = MLX5E_XDP_XMIT_MODE_PAGE;
	xdpi.page.rq = rq;

#ifdef HAVE_PAGE_POOL_GET_DMA_ADDR
	dma_addr = page_pool_get_dma_addr(page) + (xdpf->data - (void *)xdpf);
#elif defined(HAVE_PAGE_DMA_ADDR_ARRAY)
	dma_addr = page->dma_addr[0] + (xdpf->data - (void *)xdpf);
#elif defined(HAVE_PAGE_DMA_ADDR)
	dma_addr = page->dma_addr + (xdpf->data - (void *)xdpf);
#else
	dma_addr = au->addr + (xdpf->data - (void *)xdpf);
#endif
	dma_sync_single_for_device(sq->pdev, dma_addr, xdptxd.len, DMA_BIDIRECTIONAL);

#ifdef HAVE_XDP_HAS_FRAGS
	if (unlikely(xdp_frame_has_frags(xdpf))) {
		sinfo = xdp_get_shared_info_from_frame(xdpf);

		for (i = 0; i < sinfo->nr_frags; i++) {
			skb_frag_t *frag = &sinfo->frags[i];
			dma_addr_t addr;
			u32 len;

			addr = page_pool_get_dma_addr(skb_frag_page(frag)) +
				skb_frag_off(frag);
			len = skb_frag_size(frag);
			dma_sync_single_for_device(sq->pdev, addr, len,
						   DMA_BIDIRECTIONAL);
		}
	}
#endif

	xdptxd.dma_addr = dma_addr;

	if (unlikely(!INDIRECT_CALL_2(sq->xmit_xdp_frame, mlx5e_xmit_xdp_frame_mpwqe,
				      mlx5e_xmit_xdp_frame, sq, &xdptxd, sinfo, 0)))
		return false;

	xdpi.page.au = *au;
	mlx5e_xdpi_fifo_push(&sq->db.xdpi_fifo, &xdpi);

#ifdef HAVE_XDP_HAS_FRAGS
	if (unlikely(xdp_frame_has_frags(xdpf))) {
		for (i = 0; i < sinfo->nr_frags; i++) {
			skb_frag_t *frag = &sinfo->frags[i];

			xdpi.page.au.page = skb_frag_page(frag);
			mlx5e_xdpi_fifo_push(&sq->db.xdpi_fifo, &xdpi);
		}
	}
#endif

	return true;
}

/* returns true if packet was consumed by xdp */
bool mlx5e_xdp_handle(struct mlx5e_rq *rq, struct mlx5e_alloc_unit *au,
		      struct bpf_prog *prog, struct xdp_buff *xdp)
{
	struct page *page;
	u32 act;
#ifdef HAVE_XDP_SUPPORT
	int err;
#endif

	if (!au)
		page = NULL;
	else
		page = au->page;

	act = bpf_prog_run_xdp(prog, xdp);
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
#ifndef HAVE_XSK_BUFF_ALLOC
	if (xdp->rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL) {
		u64 off = xdp->data - xdp->data_hard_start;

#ifdef HAVE_XSK_UMEM_ADJUST_OFFSET
		xdp->handle = xsk_umem_adjust_offset(rq->umem, xdp->handle, off);
#else
		xdp->handle = xdp->handle + off;
#endif
	}
#endif
#endif
	switch (act) {
	case XDP_PASS:
		return false;
	case XDP_TX:
		if (unlikely(!mlx5e_xmit_xdp_buff(rq->xdpsq, rq, au, xdp)))
			goto xdp_abort;
		__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags); /* non-atomic */
		return true;
#ifdef HAVE_XDP_SUPPORT
	case XDP_REDIRECT:
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		if (xdp->rxq->mem.type != MEM_TYPE_XSK_BUFF_POOL) {
#endif
			page_ref_sub(page, au->refcnt_bias);
			au->refcnt_bias = 0;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		}
#endif
		/* When XDP enabled then page-refcnt==1 here */
		err = xdp_do_redirect(rq->netdev, xdp, prog);
		if (unlikely(err)) 
			goto xdp_abort;
		__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags);
		__set_bit(MLX5E_RQ_FLAG_XDP_REDIRECT, rq->flags);
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		if (xdp->rxq->mem.type != MEM_TYPE_XSK_BUFF_POOL)
#endif
#ifdef HAVE_PAGE_DMA_ADDR
			mlx5e_page_dma_unmap(rq, page);
#else
			mlx5e_page_dma_unmap(rq, au);
#endif
		rq->stats->xdp_redirect++;
		return true;
#endif
	default:
#ifdef HAVE_BPF_WARN_IVALID_XDP_ACTION_GET_3_PARAMS
		bpf_warn_invalid_xdp_action(rq->netdev, prog, act);
#else
		bpf_warn_invalid_xdp_action(act);
#endif
		fallthrough;
	case XDP_ABORTED:
xdp_abort:
#if defined(HAVE_TRACE_XDP_EXCEPTION) && !defined(MLX_DISABLE_TRACEPOINTS)
		trace_xdp_exception(rq->netdev, prog, act);
		fallthrough;
#endif
	case XDP_DROP:
		rq->stats->xdp_drop++;
		return true;
	}
}

static u16 mlx5e_xdpsq_get_next_pi(struct mlx5e_xdpsq *sq, u16 size)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi, contig_wqebbs;

	pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	contig_wqebbs = mlx5_wq_cyc_get_contig_wqebbs(wq, pi);
	if (unlikely(contig_wqebbs < size)) {
		struct mlx5e_xdp_wqe_info *wi, *edge_wi;

		wi = &sq->db.wqe_info[pi];
		edge_wi = wi + contig_wqebbs;

		/* Fill SQ frag edge with NOPs to avoid WQE wrapping two pages. */
		for (; wi < edge_wi; wi++) {
			*wi = (struct mlx5e_xdp_wqe_info) {
				.num_wqebbs = 1,
				.num_pkts = 0,
			};
			mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		}
		sq->stats->nops += contig_wqebbs;

		pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	}

	return pi;
}

static void mlx5e_xdp_mpwqe_session_start(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_tx_mpwqe *session = &sq->mpwqe;
	struct mlx5e_xdpsq_stats *stats = sq->stats;
	struct mlx5e_tx_wqe *wqe;
	u16 pi;

	pi = mlx5e_xdpsq_get_next_pi(sq, sq->max_sq_mpw_wqebbs);
	wqe = MLX5E_TX_FETCH_WQE(sq, pi);
	net_prefetchw(wqe->data);

	*session = (struct mlx5e_tx_mpwqe) {
		.wqe = wqe,
		.bytes_count = 0,
		.ds_count = MLX5E_TX_WQE_EMPTY_DS_COUNT,
		.pkt_count = 0,
		.inline_on = mlx5e_xdp_get_inline_state(sq, session->inline_on),
	};

	if (test_bit(MLX5E_SQ_STATE_TX_XDP_CSUM, &sq->state)) {
		struct mlx5_wqe_eth_seg *eseg = &wqe->eth;

		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;
	}
	stats->mpwqe++;
}

void mlx5e_xdp_mpwqe_complete(struct mlx5e_xdpsq *sq)
{
	struct mlx5_wq_cyc       *wq    = &sq->wq;
	struct mlx5e_tx_mpwqe *session = &sq->mpwqe;
	struct mlx5_wqe_ctrl_seg *cseg = &session->wqe->ctrl;
	u16 ds_count = session->ds_count;
	u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	struct mlx5e_xdp_wqe_info *wi = &sq->db.wqe_info[pi];

	cseg->opmod_idx_opcode =
		cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_ENHANCED_MPSW);
	cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_count);

	wi->num_wqebbs = DIV_ROUND_UP(ds_count, MLX5_SEND_WQEBB_NUM_DS);
	wi->num_pkts   = session->pkt_count;

	sq->pc += wi->num_wqebbs;

	sq->doorbell_cseg = cseg;

	session->wqe = NULL; /* Close session */
}

enum {
	MLX5E_XDP_CHECK_OK = 1,
	MLX5E_XDP_CHECK_START_MPWQE = 2,
};

INDIRECT_CALLABLE_SCOPE int mlx5e_xmit_xdp_frame_check_mpwqe(struct mlx5e_xdpsq *sq)
{
	if (unlikely(!sq->mpwqe.wqe)) {
		if (unlikely(!mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc,
						     sq->stop_room))) {
			/* SQ is full, ring doorbell */
			mlx5e_xmit_xdp_doorbell(sq);
			sq->stats->full++;
			return -EBUSY;
		}

		return MLX5E_XDP_CHECK_START_MPWQE;
	}

	return MLX5E_XDP_CHECK_OK;
}

INDIRECT_CALLABLE_SCOPE bool
mlx5e_xmit_xdp_frame(struct mlx5e_xdpsq *sq, struct mlx5e_xmit_data *xdptxd,
		     struct skb_shared_info *sinfo, int check_result);

INDIRECT_CALLABLE_SCOPE bool
mlx5e_xmit_xdp_frame_mpwqe(struct mlx5e_xdpsq *sq, struct mlx5e_xmit_data *xdptxd,
			   struct skb_shared_info *sinfo, int check_result)
{
	struct mlx5e_tx_mpwqe *session = &sq->mpwqe;
	struct mlx5e_xdpsq_stats *stats = sq->stats;

	if (unlikely(sinfo)) {
		/* MPWQE is enabled, but a multi-buffer packet is queued for
		 * transmission. MPWQE can't send fragmented packets, so close
		 * the current session and fall back to a regular WQE.
		 */
		if (unlikely(sq->mpwqe.wqe))
			mlx5e_xdp_mpwqe_complete(sq);
		return mlx5e_xmit_xdp_frame(sq, xdptxd, sinfo, 0);
	}

	if (unlikely(xdptxd->len > sq->hw_mtu)) {
		stats->err++;
		return false;
	}

	if (!check_result)
		check_result = mlx5e_xmit_xdp_frame_check_mpwqe(sq);
	if (unlikely(check_result < 0))
		return false;

	if (check_result == MLX5E_XDP_CHECK_START_MPWQE) {
		/* Start the session when nothing can fail, so it's guaranteed
		 * that if there is an active session, it has at least one dseg,
		 * and it's safe to complete it at any time.
		 */
		mlx5e_xdp_mpwqe_session_start(sq);
	}

	mlx5e_xdp_mpwqe_add_dseg(sq, xdptxd, stats);

	if (unlikely(mlx5e_xdp_mpwqe_is_full(session, sq->max_sq_mpw_wqebbs)))
		mlx5e_xdp_mpwqe_complete(sq);

	stats->xmit++;
	return true;
}

static int mlx5e_xmit_xdp_frame_check_stop_room(struct mlx5e_xdpsq *sq, int stop_room)
{
	if (unlikely(!mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc, stop_room))) {
		/* SQ is full, ring doorbell */
		mlx5e_xmit_xdp_doorbell(sq);
		sq->stats->full++;
		return -EBUSY;
	}

	return MLX5E_XDP_CHECK_OK;
}

INDIRECT_CALLABLE_SCOPE int mlx5e_xmit_xdp_frame_check(struct mlx5e_xdpsq *sq)
{
	return mlx5e_xmit_xdp_frame_check_stop_room(sq, 1);
}

INDIRECT_CALLABLE_SCOPE bool
mlx5e_xmit_xdp_frame(struct mlx5e_xdpsq *sq, struct mlx5e_xmit_data *xdptxd,
		     struct skb_shared_info *sinfo, int check_result)
{
	struct mlx5_wq_cyc       *wq   = &sq->wq;
#ifdef HAVE_XDP_HAS_FRAGS
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5_wqe_eth_seg *eseg;
	struct mlx5e_tx_wqe *wqe;
#else
	u16                       pi   = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);

	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
	struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
	struct mlx5_wqe_data_seg *dseg = wqe->data;
#endif

	dma_addr_t dma_addr = xdptxd->dma_addr;
	u32 dma_len = xdptxd->len;
	u16 ds_cnt, inline_hdr_sz;
#ifdef HAVE_XDP_HAS_FRAGS
	u8 num_wqebbs = 1;
	int num_frags = 0;
	u16 pi;
#endif

	struct mlx5e_xdpsq_stats *stats = sq->stats;
#ifndef HAVE_XDP_HAS_FRAGS
	net_prefetchw(wqe);
#endif

	if (unlikely(dma_len < MLX5E_XDP_MIN_INLINE || sq->hw_mtu < dma_len)) {
		stats->err++;
		return false;
	}

#ifdef HAVE_XDP_HAS_FRAGS
	ds_cnt = MLX5E_TX_WQE_EMPTY_DS_COUNT + 1;
	if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE)
		ds_cnt++;

	/* check_result must be 0 if sinfo is passed. */
	if (!check_result) {
		int stop_room = 1;

		if (unlikely(sinfo)) {
			ds_cnt += sinfo->nr_frags;
			num_frags = sinfo->nr_frags;
			num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
			/* Assuming MLX5_CAP_GEN(mdev, max_wqe_sz_sq) is big
			 * enough to hold all fragments.
			 */
			stop_room = MLX5E_STOP_ROOM(num_wqebbs);
		}

		check_result = mlx5e_xmit_xdp_frame_check_stop_room(sq, stop_room);
	}
#else
	if (!check_result)
		check_result = mlx5e_xmit_xdp_frame_check(sq);
#endif
	if (unlikely(check_result < 0))
		return false;


#ifdef HAVE_XDP_HAS_FRAGS
	pi = mlx5e_xdpsq_get_next_pi(sq, num_wqebbs);
	wqe = mlx5_wq_cyc_get_wqe(wq, pi);
	net_prefetchw(wqe);

	cseg = &wqe->ctrl;
	eseg = &wqe->eth;
	dseg = wqe->data;
#else
	ds_cnt = MLX5E_TX_WQE_EMPTY_DS_COUNT + 1;
#endif
	inline_hdr_sz = 0;

	/* copy the inline part if required */
	if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE) {
		memcpy(eseg->inline_hdr.start, xdptxd->data, sizeof(eseg->inline_hdr.start));
		memcpy(dseg, xdptxd->data + sizeof(eseg->inline_hdr.start),
		       MLX5E_XDP_MIN_INLINE - sizeof(eseg->inline_hdr.start));
		dma_len  -= MLX5E_XDP_MIN_INLINE;
		dma_addr += MLX5E_XDP_MIN_INLINE;
		inline_hdr_sz = MLX5E_XDP_MIN_INLINE;
		dseg++;
#ifndef HAVE_XDP_HAS_FRAGS
		ds_cnt++;
#endif
	}

	if (test_bit(MLX5E_SQ_STATE_TX_XDP_CSUM, &sq->state))
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;

	/* write the dma part */
	dseg->addr       = cpu_to_be64(dma_addr);
	dseg->byte_count = cpu_to_be32(dma_len);

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_SEND);

	if (unlikely(test_bit(MLX5E_SQ_STATE_XDP_MULTIBUF, &sq->state))) {
#ifdef HAVE_XDP_HAS_FRAGS
		u8 num_pkts = 1 + num_frags;
		int i;
#else
		u8 num_pkts = 1;
		u8 num_wqebbs;
#endif
#ifdef HAVE_STRUCT_GROUP
		memset(&cseg->trailer, 0, sizeof(cseg->trailer));
#else
		memset(&cseg->signature, 0, sizeof(*cseg) -
				sizeof(cseg->opmod_idx_opcode) - sizeof(cseg->qpn_ds));
#endif

		memset(eseg, 0, sizeof(*eseg) - sizeof(eseg->trailer));

		eseg->inline_hdr.sz = cpu_to_be16(inline_hdr_sz);
		dseg->lkey = sq->mkey_be;
#ifdef HAVE_XDP_HAS_FRAGS
		for (i = 0; i < num_frags; i++) {
			skb_frag_t *frag = &sinfo->frags[i];
			dma_addr_t addr;

			addr = page_pool_get_dma_addr(skb_frag_page(frag)) +
				skb_frag_off(frag);

			dseg++;
			dseg->addr = cpu_to_be64(addr);
			dseg->byte_count = cpu_to_be32(skb_frag_size(frag));
			dseg->lkey = sq->mkey_be;
		}
#endif

		cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
#ifndef HAVE_XDP_HAS_FRAGS
		num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
#endif
		sq->db.wqe_info[pi] = (struct mlx5e_xdp_wqe_info) {
			.num_wqebbs = num_wqebbs,
			.num_pkts = num_pkts,
		};

		sq->pc += num_wqebbs;
	} else {
		cseg->fm_ce_se = 0;

		sq->pc++;
	}

	sq->doorbell_cseg = cseg;

	stats->xmit++;
	return true;
}

static void mlx5e_free_xdpsq_desc(struct mlx5e_xdpsq *sq,
				  struct mlx5e_xdp_wqe_info *wi,
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				  u32 *xsk_frames,
#endif
				  bool recycle
#ifdef HAVE_XDP_FRAME_BULK
				  , struct xdp_frame_bulk *bq)
#else
				  )
#endif
{
	struct mlx5e_xdp_info_fifo *xdpi_fifo = &sq->db.xdpi_fifo;
	u16 i;

	for (i = 0; i < wi->num_pkts; i++) {
		struct mlx5e_xdp_info xdpi = mlx5e_xdpi_fifo_pop(xdpi_fifo);

		switch (xdpi.mode) {
		case MLX5E_XDP_XMIT_MODE_FRAME:
			/* XDP_TX from the XSK RQ and XDP_REDIRECT */
			dma_unmap_single(sq->pdev, xdpi.frame.dma_addr,
					 xdpi.frame.xdpf->len, DMA_TO_DEVICE);
#ifdef HAVE_XDP_FRAME_BULK
			xdp_return_frame_bulk(xdpi.frame.xdpf, bq);
#elif defined(HAVE_XDP_FRAME)
			xdp_return_frame(xdpi.frame.xdpf);
#else
			/* Assumes order0 page*/
			put_page(virt_to_page(xdpi.frame.xdpf->data));
#endif

			break;
		case MLX5E_XDP_XMIT_MODE_PAGE:
			/* XDP_TX from the regular RQ */
			mlx5e_page_release_dynamic(xdpi.page.rq, &xdpi.page.au, recycle);
			break;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
		case MLX5E_XDP_XMIT_MODE_XSK:
			/* AF_XDP send */
			(*xsk_frames)++;
			break;
#endif
		default:
			WARN_ON_ONCE(true);
		}
	}
}

bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq)
{
#ifdef HAVE_XDP_FRAME_BULK
	struct xdp_frame_bulk bq;
#endif
	struct mlx5e_xdpsq *sq;
	struct mlx5_cqe64 *cqe;
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	u32 xsk_frames = 0;
#endif
	u16 sqcc;
	int i;

#ifdef HAVE_XDP_FRAME_BULK
	xdp_frame_bulk_init(&bq);
#endif

	sq = container_of(cq, struct mlx5e_xdpsq, cq);

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return false;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return false;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	i = 0;
	do {
		struct mlx5e_xdp_wqe_info *wi;
		u16 wqe_counter, ci;
		bool last_wqe;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter = be16_to_cpu(cqe->wqe_counter);

		do {
			last_wqe = (sqcc == wqe_counter);
			ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc);
			wi = &sq->db.wqe_info[ci];

			sqcc += wi->num_wqebbs;

			mlx5e_free_xdpsq_desc(sq, wi
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
					     , &xsk_frames
#endif
					     , true
#ifdef HAVE_XDP_FRAME_BULK
					     , &bq
#endif
					     );
		} while (!last_wqe);

		if (unlikely(get_cqe_opcode(cqe) != MLX5_CQE_REQ)) {
			netdev_WARN_ONCE(sq->channel->netdev,
					 "Bad OP in XDPSQ CQE: 0x%x\n",
					 get_cqe_opcode(cqe));
			mlx5e_dump_error_cqe(&sq->cq, sq->sqn,
					     (struct mlx5_err_cqe *)cqe);
			mlx5_wq_cyc_wqe_dump(&sq->wq, ci, wi->num_wqebbs);
		}
	} while ((++i < MLX5E_TX_CQ_POLL_BUDGET) && (cqe = mlx5_cqwq_get_cqe(&cq->wq)));

#ifdef HAVE_XDP_FRAME_BULK
	xdp_flush_frame_bulk(&bq);
#endif

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (xsk_frames)
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		xsk_tx_completed(sq->xsk_pool, xsk_frames);
#else
		xsk_umem_complete_tx(sq->umem, xsk_frames);
#endif
#endif

	sq->stats->cqes += i;

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->cc = sqcc;
	return (i == MLX5E_TX_CQ_POLL_BUDGET);
}

void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq)
{
#ifdef HAVE_XDP_FRAME_BULK
	struct xdp_frame_bulk bq;
#endif
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	u32 xsk_frames = 0;
#endif

#ifdef HAVE_XDP_FRAME_BULK
	xdp_frame_bulk_init(&bq);

	rcu_read_lock(); /* need for xdp_return_frame_bulk */
#endif

	while (sq->cc != sq->pc) {
		struct mlx5e_xdp_wqe_info *wi;
		u16 ci;

		ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->cc);
		wi = &sq->db.wqe_info[ci];

		sq->cc += wi->num_wqebbs;

		mlx5e_free_xdpsq_desc(sq, wi
#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
				     , &xsk_frames
#endif
				     , false
#ifdef HAVE_XDP_FRAME_BULK
				     , &bq
#endif
				     );
	}

#ifdef HAVE_XDP_FRAME_BULK
	xdp_flush_frame_bulk(&bq);

	rcu_read_unlock();
#endif

#ifdef HAVE_XSK_ZERO_COPY_SUPPORT
	if (xsk_frames)
#ifdef HAVE_NETDEV_BPF_XSK_BUFF_POOL
		xsk_tx_completed(sq->xsk_pool, xsk_frames);
#else
		xsk_umem_complete_tx(sq->umem, xsk_frames);
#endif
#endif
}

void mlx5e_xdp_rx_poll_complete(struct mlx5e_rq *rq)
{
	struct mlx5e_xdpsq *xdpsq = rq->xdpsq;

	if (xdpsq->mpwqe.wqe)
		mlx5e_xdp_mpwqe_complete(xdpsq);

	mlx5e_xmit_xdp_doorbell(xdpsq);
	if (test_bit(MLX5E_RQ_FLAG_XDP_REDIRECT, rq->flags)) {
		xdp_do_flush_map();
		__clear_bit(MLX5E_RQ_FLAG_XDP_REDIRECT, rq->flags);
	}
}

void mlx5e_set_xmit_fp(struct mlx5e_xdpsq *sq, bool is_mpw)
{
	sq->xmit_xdp_frame_check = is_mpw ?
		mlx5e_xmit_xdp_frame_check_mpwqe : mlx5e_xmit_xdp_frame_check;
	sq->xmit_xdp_frame = is_mpw ?
		mlx5e_xmit_xdp_frame_mpwqe : mlx5e_xmit_xdp_frame;
}

#ifdef HAVE_NDO_XDP_XMIT
#ifndef HAVE_NDO_XDP_FLUSH
int mlx5e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		   u32 flags)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_xdpsq *sq;
	int nxmit = 0;
	int sq_num;
	int i;

	/* this flag is sufficient, no need to test internal sq state */
	if (unlikely(!mlx5e_xdp_tx_is_enabled(priv)))
		return -ENETDOWN;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	sq_num = smp_processor_id();

	if (unlikely(sq_num >= priv->channels.num))
		return -ENXIO;

	sq = &priv->channels.c[sq_num]->xdpsq;

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];
		struct mlx5e_xmit_data xdptxd;
		struct mlx5e_xdp_info xdpi;
		bool ret;

		xdptxd.data = xdpf->data;
		xdptxd.len = xdpf->len;
		xdptxd.dma_addr = dma_map_single(sq->pdev, xdptxd.data,
						 xdptxd.len, DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(sq->pdev, xdptxd.dma_addr)))
			break;

		xdpi.mode           = MLX5E_XDP_XMIT_MODE_FRAME;
		xdpi.frame.xdpf     = xdpf;
		xdpi.frame.dma_addr = xdptxd.dma_addr;

		ret = INDIRECT_CALL_2(sq->xmit_xdp_frame, mlx5e_xmit_xdp_frame_mpwqe,
				      mlx5e_xmit_xdp_frame, sq, &xdptxd, NULL, 0);
		if (unlikely(!ret)) {
			dma_unmap_single(sq->pdev, xdptxd.dma_addr,
					 xdptxd.len, DMA_TO_DEVICE);
			break;
		}
		mlx5e_xdpi_fifo_push(&sq->db.xdpi_fifo, &xdpi);
		nxmit++;
	}

	if (flags & XDP_XMIT_FLUSH) {
		if (sq->mpwqe.wqe)
			mlx5e_xdp_mpwqe_complete(sq);
		mlx5e_xmit_xdp_doorbell(sq);
	}

	return nxmit;
}

#else
int mlx5e_xdp_xmit(struct net_device *dev, struct xdp_buff *xdp)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_xmit_data xdptxd;
	struct mlx5e_xdp_info xdpi;
	struct xdp_frame *xdpf;
	struct mlx5e_xdpsq *sq;
	int sq_num;
	int err = 0;

	/* this flag is sufficient, no need to test internal sq state */
	if (unlikely(!mlx5e_xdp_tx_is_enabled(priv)))
		return -ENETDOWN;

	sq_num = smp_processor_id();

	if (unlikely(sq_num >= priv->channels.num))
		return -ENXIO;

	sq = &priv->channels.c[sq_num]->xdpsq;

	xdpf = convert_to_xdp_frame(xdp);

	if (unlikely(!xdpf))
		return -EINVAL;

	xdptxd.data = xdpf->data;
	xdptxd.len  = xdpf->len;

	xdptxd.dma_addr = dma_map_single(sq->pdev, xdptxd.data,
					 xdptxd.len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(sq->pdev, xdptxd.dma_addr))) {
		err = -ENOMEM;
		goto err_release_page;
	}

	xdpi.mode = MLX5E_XDP_XMIT_MODE_FRAME;
	xdpi.frame.xdpf = xdpf;
	xdpi.frame.dma_addr = xdptxd.dma_addr;

	if (unlikely(!sq->xmit_xdp_frame(sq, &xdptxd, NULL, 0))) {
		dma_unmap_single(sq->pdev, xdptxd.dma_addr,
				 xdptxd.len, DMA_TO_DEVICE);
		err = -ENOSPC;
		goto err_release_page;
	}

	return 0;

err_release_page:
#ifdef HAVE_XDP_FRAME
	xdp_return_frame_rx_napi(xdpf);
#else
	/* Assumes order0 page */
	put_page(virt_to_page(xdpf->data));
#endif

	return err;
}

void mlx5e_xdp_flush(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_xdpsq *sq;
	int sq_num;

	/* this flag is sufficient, no need to test internal sq state */
	if (unlikely(!mlx5e_xdp_tx_is_enabled(priv)))
		return;

	sq_num = smp_processor_id();

	if (unlikely(sq_num >= priv->channels.num))
		return;

	sq = &priv->channels.c[sq_num]->xdpsq;

	if (sq->mpwqe.wqe)
		mlx5e_xdp_mpwqe_complete(sq);
	mlx5e_xmit_xdp_doorbell(sq);
}
#endif
#endif
#endif

