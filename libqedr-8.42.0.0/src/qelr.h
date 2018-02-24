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

#ifndef __QELR_H__
#define __QELR_H__

#include <inttypes.h>
#include <stddef.h>
#include <endian.h>
#include <stdbool.h>

#include <infiniband/driver.h>
#include "util/udma_barrier.h"

#define writel(b, p) (*(uint32_t *)(p) = (b))
#define writeq(b, p) (*(uint64_t *)(p) = (b))

#include "qelr_compat.h"
#include "qelr_hsi_roce.h"
#include "qelr_hsi_iwarp.h"

#define QELR_VERB_REGISTER_DRIVER	0

/* Strippable QELR featrues */
#define QELR_LDPM		(1)
#define QELR_SRQ		(1)
#define QELR_DB_RECOVERY	(1)

#define qelr_err(format, arg...) printf(format, ##arg)

#define IS_IWARP(_dev) (_dev->node_type == IBV_NODE_RNIC)
#define IS_ROCE(_dev) (_dev->node_type == IBV_NODE_CA)

#define __ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))
#define ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a)-1)
#define ALIGN_DIV(x, a)		(__ALIGN_MASK(x, (typeof(x))(a)-1) / (a))

extern uint32_t qelr_dp_level;
extern uint32_t qelr_dp_module;

enum DP_MODULE {
	QELR_MSG_CQ		= 0x10000,
	QELR_MSG_RQ		= 0x20000,
	QELR_MSG_SQ		= 0x40000,
	QELR_MSG_QP		= (QELR_MSG_SQ | QELR_MSG_RQ),
	QELR_MSG_MR		= 0x80000,
	QELR_MSG_INIT		= 0x100000,
#if QELR_SRQ
	QELR_MSG_SRQ		= 0x200000,
#endif
	/* to be added...up to 0x8000000 */
};

enum DP_LEVEL {
	QELR_LEVEL_VERBOSE	= 0x0,
	QELR_LEVEL_INFO		= 0x1,
	QELR_LEVEL_NOTICE	= 0x2,
	QELR_LEVEL_ERR		= 0x3,
};

#define DP_ERR(fd, fmt, ...)					\
do {								\
	fprintf(fd, "[%s:%d]" fmt,				\
		__func__, __LINE__,				\
		##__VA_ARGS__);					\
	fflush(fd);						\
} while (0)

#define DP_NOTICE(fd, fmt, ...)					\
do {								\
	if (qelr_dp_level <= QELR_LEVEL_NOTICE) {		\
		fprintf(fd, "[%s:%d]" fmt,			\
			__func__, __LINE__,			\
			##__VA_ARGS__);				\
			fflush(fd); }				\
} while (0)

#define DP_INFO(fd, fmt, ...)					\
do {								\
	if (qelr_dp_level <= QELR_LEVEL_INFO)	{		\
		fprintf(fd, "[%s:%d]" fmt,			\
			__func__, __LINE__,			\
		      ##__VA_ARGS__); fflush(fd);		\
	}							\
} while (0)

#define DP_VERBOSE(fd, module, fmt, ...)			\
do {								\
	if ((qelr_dp_level <= QELR_LEVEL_VERBOSE) &&		\
		     (qelr_dp_module & (module))) {		\
		fprintf(fd, "[%s:%d]" fmt,			\
			__func__, __LINE__,			\
			##__VA_ARGS__);				\
			fflush(fd); }				\
} while (0)

struct qelr_buf {
	void		*addr;
	size_t		len;		/* a 64 uint is used as s preparation
					 * for double layer pbl.
					 */
};

struct qelr_device {
#if QELR_VERB_REGISTER_DRIVER /* QELR_UPSTREAM */
	struct verbs_device ibv_dev;
#else
	struct ibv_device ibv_dev;
#endif
};

struct qelr_devctx {
	struct ibv_context	ibv_ctx;
	FILE			*dbg_fp;
	void			*db_addr;
	uint64_t		db_pa;
	uint32_t		db_size;
	uint8_t			dpm_enabled;
	uint8_t			wids_enabled;
	uint16_t		wid_count;
	uint32_t		kernel_page_size;

	uint32_t		max_send_wr;
	uint32_t		max_recv_wr;
#if QELR_SRQ
	uint32_t		max_srq_wr;
#endif
	uint32_t		sges_per_send_wr;
	uint32_t		sges_per_recv_wr;
#if QELR_SRQ
	uint32_t		sges_per_srq_wr;
#endif
	int			max_cqes;
};

struct qelr_pd {
	struct ibv_pd		ibv_pd;
	uint32_t		pd_id;
};

struct qelr_mr {
	struct ibv_mr		ibv_mr;
};

union db_prod64 {
	struct rdma_pwm_val32_data data;
	uint64_t raw;
};

struct qelr_chain {
	void		*first_addr;	/* Address of first element in chain */
	void		*last_addr;	/* Address of last element in chain */

	/* Point to next element to produce/consume */
	void		*p_prod_elem;
	void		*p_cons_elem;

	uint32_t	prod_idx;
	uint32_t	cons_idx;

	uint32_t	n_elems;
	uint32_t	size;
	uint16_t	elem_size;
};

struct qelr_cq {
	struct ibv_cq		ibv_cq;	/* must be first */

	struct qelr_chain	chain;

	void			*db_addr;
	union db_prod64		db;
#if QELR_DB_RECOVERY
	struct qelr_user_db_rec *db_rec_addr;
#endif

	uint8_t			chain_toggle;
	union rdma_cqe		*latest_cqe;
	union rdma_cqe		*toggle_cqe;

	uint8_t			arm_flags;
};

enum qelr_qp_state {
	QELR_QPS_RST,
	QELR_QPS_INIT,
	QELR_QPS_RTR,
	QELR_QPS_RTS,
	QELR_QPS_SQD,
	QELR_QPS_ERR,
	QELR_QPS_SQE
};

union db_prod32 {
	struct rdma_pwm_val16_data	data;
	uint32_t			raw;
};

struct qelr_qp_hwq_info {
	/* WQE */
	struct qelr_chain			chain;
	uint8_t					max_sges;

	/* WQ */
	uint16_t				prod;     /* WQE prod index for SW ring */
	uint16_t				wqe_cons;
	uint16_t				cons;     /* WQE cons index for SW ring */
	uint16_t				max_wr;   /* cons and prod wrap when they reach it */

	/* DB */
	union db_prod32				db_data;  /* Doorbell data */
	void					*db;		/* Doorbell address */
	void					*edpm_db;	/* Doorbell address for EDPM */
#if QELR_DB_RECOVERY
	struct qelr_user_db_rec			*db_rec_addr;	/* Doorbell recovery entry address */
#endif

	void					*iwarp_db2;      /* iWARP RQ Doorbell address */
	union db_prod32				iwarp_db2_data;  /* iWARP RQ Doorbell data */

	uint16_t				icid;
};

struct qelr_rdma_ext {
	__be64	remote_va;
	__be32	remote_key;
	__be32	dma_length;
};

/* The maximum inline data either RoCE or iWARP can handle */
#define QELR_MAX_INLINE_DATA	(ROCE_REQ_MAX_INLINE_DATA_SIZE)

#if 1 /* !QELR_UPSTREAM */
#if (ROCE_REQ_MAX_INLINE_DATA_SIZE < IWARP_REQ_MAX_INLINE_DATA_SIZE)
#error "Update QELR_MAX_INLINE_DATA to hold the maximum inline of RoCE/iWARP"
#endif
#endif

/* rdma extension, invalidate / immediate data + padding, inline data... */
#define QELR_MAX_DPM_PAYLOAD (sizeof(struct qelr_rdma_ext) + sizeof(uint64_t) +\
			      QELR_MAX_INLINE_DATA)
struct qelr_dpm {
	uint32_t		payload_offset;
	uint32_t		payload_size;
	struct qelr_rdma_ext	*rdma_ext; /* Enhanced DPM only */

	int			cpu;

	uint8_t			is_edpm;
#if QELR_LDPM
	uint8_t			is_ldpm;
#endif
	union {
		struct db_rdma_dpm_data	data;
		uint64_t raw;
	} msg;

	uint8_t			payload[QELR_MAX_DPM_PAYLOAD];
};

#if QELR_SRQ
struct qelr_srq_hwq_info {
	u32 max_sges;
	u32 max_wr;
	struct qelr_chain chain;
	u32 wqe_prod;     /* WQE prod index in HW ring */
	u32 sge_prod;     /* SGE prod index in HW ring */
	u32 wr_prod_cnt; /* wr producer count */
	u32 wr_cons_cnt; /* wr consumer count */
	u32 num_elems;

	u32 *virt_prod_pair_addr; /* producer pair virtual address */
	u64 phy_prod_pair_addr; /* producer pair physical address */
};

struct qelr_srq {
	struct ibv_srq ibv_srq;
	struct qelr_srq_hwq_info hw_srq;
	u16 srq_id;
	pthread_spinlock_t lock;
};
#endif

enum qelr_qp_err_bitmap {
	QELR_QP_ERR_SQ_FULL	= 1 << 0,
	QELR_QP_ERR_RQ_FULL	= 1 << 1,
	QELR_QP_ERR_BAD_SR	= 1 << 2,
	QELR_QP_ERR_BAD_RR	= 1 << 3,
	QELR_QP_ERR_SQ_PBL_FULL	= 1 << 4,
	QELR_QP_ERR_RQ_PBL_FULL	= 1 << 5,
};

enum qelr_qp_flags {
	QELR_QP_FLAG_IWARP	= 1 << 0,
	QELR_QP_FLAG_LDPM_EN	= 1 << 1,
	QELR_QP_FLAG_EDPM_EN	= 1 << 2,
	QELR_QP_FLAG_WIDS_EN	= 1 << 3,
	QELR_QP_FLAG_ATOMIC_EN	= 1 << 4,
	QELR_QP_FLAG_SQ		= 1 << 5,
	QELR_QP_FLAG_RQ		= 1 << 6,
};

struct qelr_qp {
	struct ibv_qp				ibv_qp;

	pthread_spinlock_t			q_lock;
	enum qelr_qp_state			state;	/*  QP state */
	uint32_t				qp_id;
	uint32_t				err_bitmap;
	uint16_t				wid_count;
	uint8_t					flags;
	uint8_t					prev_wqe_size;

	/* SQ */
	struct qelr_qp_hwq_info			sq;
	struct {
		uint64_t wr_id;
		enum ibv_wc_opcode opcode;
		uint32_t bytes_len;
		uint8_t wqe_size;
		uint8_t signaled;
	} *wqe_wr_id;
	uint32_t				max_inline_data;
	int					sq_sig_all;

	/* RQ */
	struct qelr_qp_hwq_info			rq;
	struct {
		uint64_t wr_id;
		uint8_t wqe_size;
	} *rqe_wr_id;

	/* SRQ */
#if QELR_SRQ
	struct qelr_srq				*srq;
#endif
};

static inline struct qelr_devctx *get_qelr_ctx(struct ibv_context *ibctx)
{
	return container_of(ibctx, struct qelr_devctx, ibv_ctx);
}

static inline struct qelr_device *get_qelr_dev(struct ibv_device *ibdev)
{
	return container_of(ibdev, struct qelr_device, ibv_dev);
}

static inline struct qelr_qp *get_qelr_qp(struct ibv_qp *ibqp)
{
	return container_of(ibqp, struct qelr_qp, ibv_qp);
}

static inline struct qelr_pd *get_qelr_pd(struct ibv_pd *ibpd)
{
	return container_of(ibpd, struct qelr_pd, ibv_pd);
}

static inline struct qelr_cq *get_qelr_cq(struct ibv_cq *ibcq)
{
	return container_of(ibcq, struct qelr_cq, ibv_cq);
}

#if QELR_SRQ
static inline struct qelr_srq *get_qelr_srq(struct ibv_srq *ibsrq)
{
	return get_qelr_xxx(srq, srq);
}
#endif

#define SET_FIELD(value, name, flag)				\
	do {							\
		(value) &= ~(name ## _MASK << name ## _SHIFT);	\
		(value) |= ((flag) << (name ## _SHIFT));	\
	} while (0)

#define GET_FIELD(value, name) \
	(((value) >> (name ## _SHIFT)) & name ## _MASK)

#define ROCE_WQE_ELEM_SIZE	sizeof(struct rdma_sq_sge)
#define RDMA_WQE_BYTES		(16)

#define QELR_RESP_IMM (RDMA_CQE_RESPONDER_IMM_FLG_MASK <<	\
		       RDMA_CQE_RESPONDER_IMM_FLG_SHIFT)
#define QELR_RESP_RDMA (RDMA_CQE_RESPONDER_RDMA_FLG_MASK <<	\
			RDMA_CQE_RESPONDER_RDMA_FLG_SHIFT)
#define QELR_RESP_RDMA_IMM (QELR_RESP_IMM | QELR_RESP_RDMA)

#define TYPEPTR_ADDR_SET(type_ptr, field, vaddr)		\
	do {							\
		(type_ptr)->field.hi = htole32(U64_HI(vaddr));	\
		(type_ptr)->field.lo = htole32(U64_LO(vaddr));	\
	} while (0)

#define RQ_SGE_SET(sge, vaddr, vlength, vflags)			\
	do {							\
		TYPEPTR_ADDR_SET(sge, addr, vaddr);		\
		(sge)->length = htole32(vlength);		\
		(sge)->flags = htole32(vflags);			\
	} while (0)

#if QELR_SRQ
#define SRQ_HDR_SET(hdr, vwr_id, num_sge)			\
	do {							\
		TYPEPTR_ADDR_SET(hdr, wr_id, vwr_id);		\
		(hdr)->num_sges = num_sge;			\
	} while (0)

#define SRQ_SGE_SET(sge, vaddr, vlength, vlkey)			\
	do {							\
		TYPEPTR_ADDR_SET(sge, addr, vaddr);		\
		(sge)->length = cpu_to_le32(vlength);		\
		(sge)->l_key = cpu_to_le32(vlkey);		\
	} while (0)
#endif

#define U64_HI(val) ((uint32_t)(((uint64_t)(uintptr_t)(val)) >> 32))
#define U64_LO(val) ((uint32_t)(((uint64_t)(uintptr_t)(val)) & 0xffffffff))
#define HILO_U64(hi, lo) ((uintptr_t)((((uint64_t)(hi)) << 32) + (lo)))

#define QELR_MAX_RQ_WQE_SIZE (RDMA_MAX_SGE_PER_RQ_WQE)
#define QELR_MAX_SQ_WQE_SIZE (ROCE_REQ_MAX_SINGLE_SQ_WQE_SIZE /	\
			      ROCE_WQE_ELEM_SIZE)

#define QELR_ANON_FD            (-1)    /* MAP_ANONYMOUS => file desc.= -1  */
#define QELR_ANON_OFFSET        (0)     /* MAP_ANONYMOUS => offset    = d/c */

#endif /* __QELR_H__ */
