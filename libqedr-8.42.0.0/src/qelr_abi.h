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

#ifndef __QELR_ABI_H__
#define __QELR_ABI_H__

#include <infiniband/kern-abi.h>

#define QELR_ABI_VERSION			(8)

struct qelr_get_context {
	struct ibv_get_context cmd;		/* must be first */
};

enum qelr_rdma_dpm_type {
	QELR_RDMA_TYPE_NONE		= 0,
	QELR_RDMA_DPM_TYPE_ENHANCED	= 1 << 0,
	QELR_RDMA_DPM_TYPE_LEGACY	= 1 << 1,
};

struct qelr_alloc_ucontext_resp {
	struct ibv_get_context_resp ibv_resp;	/* must be first */
	__u64 db_pa;
	__u32 db_size;

	__u32 max_send_wr;
	__u32 max_recv_wr;
	__u32 max_srq_wr;
	__u32 sges_per_send_wr;
	__u32 sges_per_recv_wr;
	__u32 sges_per_srq_wr;
	__u32 max_cqes;
	__u8 dpm_enabled;
	__u8 wids_enabled;
	__u16 wid_count;
};

struct qelr_alloc_pd_req {
	struct ibv_alloc_pd cmd;		/* must be first */
};

struct qelr_alloc_pd_resp {
	struct ibv_alloc_pd_resp ibv_resp;	/* must be first */
	__u32 pd_id;
};

struct qelr_create_cq_req {
	struct ibv_create_cq ibv_cmd;		/* must be first */

	__u64 addr;	   /* user space virtual address of CQ buffer */
	__u64 len;	   /* size of CQ buffer */
	__u64 db_rec_addr; /* address of CQ doorbell recovery user entry */
};

struct qelr_create_cq_resp {
	struct ibv_create_cq_resp ibv_resp;	/* must be first */
	__u32 db_offset;
	__u16 icid;
};

struct qelr_reg_mr {
	struct ibv_reg_mr ibv_cmd;		/* must be first */
};

struct qelr_reg_mr_resp {
	struct ibv_reg_mr_resp ibv_resp;	/* must be first */
};

struct qelr_create_srq_req {
	struct ibv_create_srq ibv_cmd;
	/* user space virtual address of producer pair */
	__u64 prod_pair_addr;

	/* SRQ */
	__u64 srq_addr;		/* user space virtual address of SRQ buffer */
	__u64 srq_len;		/* length of SRQ buffer */
};

struct qelr_create_srq_resp {
	struct ibv_create_srq_resp ibv_resp;
	__u16 srq_id;
};

struct qelr_create_qp_req {
	struct ibv_create_qp ibv_qp;	/* must be first */

	__u32 qp_handle_hi;
	__u32 qp_handle_lo;

	/* SQ */
	__u64 sq_addr;		/* user space virtual address of SQ buffer */
	__u64 sq_len;		/* length of SQ buffer */

	/* RQ */
	__u64 rq_addr;		/* user space virtual address of RQ buffer */
	__u64 rq_len;		/* length of RQ buffer */

	/* doorbell recovery */
	__u64 sq_db_rec_addr;   /* address of SQ doorbell recovery user entry */
	__u64 rq_db_rec_addr;   /* address of RQ doorbell recovery user entry */
};

struct qelr_create_qp_resp {
	struct ibv_create_qp_resp ibv_resp;	/* must be first */

	__u32 qp_id;
	__u32 atomic_supported;

	/* SQ */
	__u32 sq_db_offset;
	__u16 sq_icid;

	/* RQ */
	__u32 rq_db_offset;
	__u16 rq_icid;

	__u32 rq_db2_offset;
};

struct qelr_user_db_rec {
	__u64 db_data;
};

#endif /* __QELR_ABI_H__ */
