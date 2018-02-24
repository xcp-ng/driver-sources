/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __COVERITY__

#undef TRACE_SYSTEM
#define TRACE_SYSTEM enic

#if !defined(_ENIC_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _ENIC_TRACE_H_

#include <linux/tracepoint.h>

#include "enic_qp.h"
#include "cq_desc.h"
#include "cq_enet_desc.h"
#include "enic_clock.h"

TRACE_EVENT(enic_napi_ret,
	TP_PROTO(struct enic_qp *qp, int work, int credit, int mask),
	TP_ARGS(qp, work, credit, mask),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, work)
		__field(int, credit)
		__field(int, qp_index)
		__field(int, mask)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->work = work;
		__entry->credit = credit;
		__entry->mask = mask;
		__entry->qp_index = qp->index;
	),
	TP_printk(
		"%s: qp[%d]: work done: %d, credit ret: %d, unmask intr: %d",
		__get_str(devname), __entry->qp_index, __entry->work,
		__entry->credit, __entry->mask
	)
);

TRACE_EVENT(enic_isr_msi,
	TP_PROTO(struct enic_qp *qp, int irq),
	TP_ARGS(qp, irq),
	TP_STRUCT__entry(
		 __string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(int, irq)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->irq = irq;
	),
	TP_printk("%s: qp[%d]: irq = %d",
		__get_str(devname), __entry->qp_index, __entry->irq
	)
);

TRACE_EVENT(enic_wq_service,
	TP_PROTO(struct enic_qp *qp, int work),
	TP_ARGS(qp, work),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(int, work)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->work = work;
	),
	TP_printk(
		"%s: qp[%d] wq work done = %d",
		__get_str(devname), __entry->qp_index, __entry->work
	)
);

TRACE_EVENT(enic_rq_posted_index,
	TP_PROTO(struct enic_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(int, posted_index)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->posted_index = ioread32(&qp->rq.ctrl->posted_index);
	),
	TP_printk("%s: qp[%d]: posted_index = %d",
		__get_str(devname), __entry->qp_index, __entry->posted_index
	)
);

TRACE_EVENT(enic_wq_posted_index,
	TP_PROTO(struct enic_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(int, posted_index)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->posted_index = ioread32(&qp->wq.ctrl->posted_index);
	),
	TP_printk("%s: qp[%d]: posted_index = %d",
		__get_str(devname), __entry->qp_index, __entry->posted_index
	)
);

TRACE_EVENT(enic_tx_wake_queue,
	TP_PROTO(struct enic_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
	),
	TP_printk("%s: qp[%d]: queue wake",
		__get_str(devname), __entry->qp_index
	)
);

TRACE_EVENT(enic_tx_stop_queue,
	TP_PROTO(struct enic_qp *qp, unsigned int wq_desc_avail),
	TP_ARGS(qp, wq_desc_avail),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(unsigned int, wq_desc_avail)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->wq_desc_avail = wq_desc_avail;
	),
	TP_printk("%s: qp[%d]: queue stop. wq_desc_avail = %d",
		__get_str(devname), __entry->qp_index, __entry->wq_desc_avail
	)
);

TRACE_EVENT(enic_rq_desc,
	TP_PROTO(struct enic_qp *qp, struct enic_rq_buf *buf),
	TP_ARGS(qp, buf),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(u16, buf_index)
		__dynamic_array(u8, rq_desc, sizeof(struct rq_enet_desc))
		__field(size_t, rq_desc_len)
		__field(u64, address)
		__field(u16, length)
		__field(u8, type)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->buf_index = buf->index;
		memcpy(__get_dynamic_array(rq_desc),
		       &qp->rq.rq_base[buf->index],
		       sizeof(struct rq_enet_desc));
		__entry->rq_desc_len = sizeof(struct rq_enet_desc);
		rq_enet_desc_dec(&qp->rq.rq_base[buf->index],
				 &__entry->address, &__entry->type,
				 &__entry->length);
	),
	TP_printk("%s: qp[%d]: buf->index: %d address: 0x%llx length: %d type: %d <%s>",
		__get_str(devname), __entry->qp_index, __entry->buf_index,
		__entry->address, __entry->length, __entry->type,
		__print_hex(__get_dynamic_array(rq_desc), __entry->rq_desc_len)
	)
);

TRACE_EVENT(enic_wq_desc,
	TP_PROTO(
		struct enic_qp *qp, struct enic_wq_buf *buf,
		dma_addr_t dma_addr_offset, unsigned int length,
		unsigned int mss, unsigned int header_length,
		unsigned int offload_mode, bool eop, bool cq_entry,
		u16 vlan_tag
	),
	TP_ARGS(
		qp, buf, dma_addr_offset, length, mss, header_length,
		offload_mode, eop, cq_entry, vlan_tag
	),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__dynamic_array(u8, wq_desc, sizeof(struct wq_enet_desc))
		__field(size_t, wq_desc_len)
		__field(bool, sop)
		__field(dma_addr_t, dma_addr)
		__field(u16, len)
		__field(struct sk_buff *, skb)
		__field(unsigned int, length)
		__field(dma_addr_t, dma_addr_offset)
		__field(unsigned int, mss)
		__field(unsigned int, header_length)
		__field(u8, offload_mode)
		__field(bool, eop)
		__field(bool, cq_entry)
		__field(u16, vlan_tag)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->wq_desc_len = sizeof(struct wq_enet_desc);
		memcpy(__get_dynamic_array(wq_desc),
		       &qp->wq.wq_base[buf->index], __entry->wq_desc_len);
		__entry->qp_index = qp->index;
		__entry->sop = buf->sop;
		__entry->dma_addr = buf->dma_addr;
		__entry->len = buf->len;
		__entry->skb = buf->skb;
		__entry->length = length;
		__entry->dma_addr_offset = dma_addr_offset;
		__entry->mss = mss;
		__entry->header_length = header_length;
		__entry->offload_mode = offload_mode;
		__entry->eop = eop;
		__entry->cq_entry = cq_entry;
		__entry->vlan_tag = vlan_tag;
	),
	TP_printk("%s: qp[%d]: %d/0x%llx/%d/0x%p/%d/%llu/%d/%d/%d/%d/%d/%d <%s>",
		__get_str(devname),
		__entry->qp_index,
		__entry->sop,
		__entry->dma_addr,
		__entry->len,
		__entry->skb,
		__entry->length,
		__entry->dma_addr_offset,
		__entry->mss,
		__entry->header_length,
		__entry->offload_mode,
		__entry->eop,
		__entry->cq_entry,
		__entry->vlan_tag,
		__print_hex(__get_dynamic_array(wq_desc), __entry->wq_desc_len)
	)
);

TRACE_EVENT(enic_wq_desc_pull,
	TP_PROTO(
		struct enic_qp *qp, struct sk_buff *skb,
		unsigned int length, unsigned int header_length,
		unsigned int pull_len, unsigned int mss,
		unsigned int offload_mode, u16 vlan_tag
	),
	TP_ARGS(
		qp, skb, length, header_length, pull_len,
		mss, offload_mode, vlan_tag
	),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(struct sk_buff *, skb)
		__field(unsigned int, skb_len)
		__field(unsigned int, head_len)
		__field(unsigned int, data_len)
		__field(unsigned int, length)
		__field(unsigned int, header_length)
		__field(unsigned int, pull_len)
		__field(unsigned int, mss)
		__field(unsigned int, headroom)
		__field(int, tailroom)
		__field(u8, offload_mode)
		__field(u16, vlan_tag)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->skb = skb;
		__entry->skb_len = skb->len;
		__entry->head_len = skb_headlen(skb);
		__entry->data_len = skb->data_len;
		__entry->length = length;
		__entry->header_length = header_length;
		__entry->pull_len = pull_len;
		__entry->mss = mss;
		__entry->headroom = skb_headroom(skb);
		__entry->tailroom = skb_tailroom(skb);
		__entry->offload_mode = offload_mode;
		__entry->vlan_tag = vlan_tag;
	),
	TP_printk("%s: qp[%d]: 0x%p/%u/%u/%u/%u/%u/%u/%u/%u/%d/%d/%d",
		__get_str(devname),
		__entry->qp_index,
		__entry->skb,
		__entry->skb_len,
		__entry->head_len,
		__entry->data_len,
		__entry->length,
		__entry->header_length,
		__entry->pull_len,
		__entry->mss,
		__entry->headroom,
		__entry->tailroom,
		__entry->offload_mode,
		__entry->vlan_tag
	)
);

TRACE_EVENT(enic_rq_cq_get,
	TP_PROTO(struct enic_qp *qp, int work),
	TP_ARGS(qp, work),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(int, qp_index)
		__field(int, work)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->work = work;
	),
	TP_printk(
		"%s: qp[%d] rq work done = %d",
		__get_str(devname), __entry->qp_index, __entry->work
	)
);

TRACE_EVENT(enic_rq_fill_bufs,
	TP_PROTO(struct enic_qp *qp, int ret),
	TP_ARGS(qp, ret),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(u16, qp_index)
		__field(int, ret)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->ret = ret;
	),
	TP_printk(
		"%s: qp[%d] ret = %d",
		__get_str(devname), __entry->qp_index, __entry->ret
	)
);

TRACE_EVENT(enic_wq_cq_desc,
	TP_PROTO(struct enic_qp *qp, struct cq_enet_wq_desc *cq_desc),
	TP_ARGS(qp, cq_desc),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(u16, qp_index)
		__field(u16, q_number)
		__field(u16, completed_index)
		__field(u8, type)
		__field(u8, color)
		__field(u64, ts)
		__field(bool, ts_val)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->ts = CQ_WQ_DESC_TS(cq_desc);
		cq_enet_wq_desc_dec(cq_desc, &__entry->type, &__entry->color,
				    &__entry->q_number,
				    &__entry->completed_index);
		__entry->completed_index = CQ_WQ_DESC_INDEX(cq_desc);
		__entry->ts_val = CQ_WQ_DESC_TS_VAL(cq_desc);
	),
	TP_printk(
		"%s: qp[%d]: q_number: %d, completed_index: %d, type: %d, ts: 0x%llx, ts_val: %d, color: %d",
		__get_str(devname), __entry->qp_index, __entry->q_number,
		__entry->completed_index, __entry->type, __entry->ts,
		__entry->ts_val, __entry->color
	)
);

TRACE_EVENT(enic_read_cycles,
	TP_PROTO(struct enic *enic, u64 clock, u64 clock_w_mask),
	TP_ARGS(enic, clock, clock_w_mask),
	TP_STRUCT__entry(
		__string(devname, enic->netdev->name)
		__field(u64, clock)
		__field(u64, clock_w_mask)
	),
	TP_fast_assign(
		__assign_str(devname, enic->netdev->name);
		__entry->clock = clock;
		__entry->clock_w_mask = clock_w_mask;
	),
	TP_printk("%s: clock: 0x%llx clock_w_mask: 0x%llx",
		__get_str(devname), __entry->clock, __entry->clock_w_mask
	)
);

TRACE_EVENT(enic_fill_hwtstamp,
	TP_PROTO(struct enic_qp *qp, u64 cycles, u64 nsec, bool queue),
	TP_ARGS(qp, cycles, nsec, queue),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(u64, cycles)
		__field(u64, nsec)
		__field(bool, queue)
		__field(u16, qp_index)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->cycles = cycles;
		__entry->nsec = nsec;
		__entry->queue = queue;
		__entry->qp_index = qp->index;
	),
	TP_printk("%s: qp[%d]: %s: cycles: 0x%llx nsec = %llu",
		__get_str(devname), __entry->qp_index,
		__entry->queue == TRACE_RQ ? "rq" : "wq", __entry->cycles,
		__entry->nsec
	)
);

TRACE_EVENT(enic_ptp_gettime,
	TP_PROTO(struct enic *enic, u64 ns),
	TP_ARGS(enic, ns),
	TP_STRUCT__entry(
		__string(devname, enic->netdev->name)
		__field(u64, ns)
	),
	TP_fast_assign(
		__assign_str(devname, enic->netdev->name);
		__entry->ns = ns;
	),
	TP_printk("%s: ns: %llu",
		__get_str(devname), __entry->ns
	)
);

TRACE_EVENT(enic_ptp_adjfreq,
	TP_PROTO(struct enic *enic, u32 freq),
	TP_ARGS(enic, freq),
	TP_STRUCT__entry(
		__string(devname, enic->netdev->name)
		__field(u32, freq)
	),
	TP_fast_assign(
		__assign_str(devname, enic->netdev->name);
		__entry->freq = freq;
	),
	TP_printk("%s: freq: %u",
		__get_str(devname), __entry->freq
	)
);

TRACE_EVENT(enic_ptp_adjtime,
	TP_PROTO(struct enic *enic, s64 delta),
	TP_ARGS(enic, delta),
	TP_STRUCT__entry(
		__string(devname, enic->netdev->name)
		__field(s64, delta)
	),
	TP_fast_assign(
		__assign_str(devname, enic->netdev->name);
		__entry->delta = delta;
	),
	TP_printk("%s: delta: %lld",
		__get_str(devname), __entry->delta
	)
);

TRACE_EVENT(enic_rq_buf,
	TP_PROTO(struct enic_qp *qp, struct enic_rq_buf *buf),
	TP_ARGS(qp, buf),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(u16, qp_index)
		__field(u16, index)
		__field(dma_addr_t, dma_addr)
		__field(u32, len)
		__field(struct page*, page)
		__field(u16, page_offset)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->index = buf->index;
		__entry->dma_addr = buf->dma_addr;
		__entry->len = buf->len;
		__entry->page = buf->page;
		__entry->page_offset = buf->page_offset;
	),
	TP_printk("%s: qp[%d]: index: %d dma_addr: 0x%llx len: %d, page: 0x%llx, offset: %u",
		__get_str(devname), __entry->qp_index, __entry->index,
		__entry->dma_addr, __entry->len, (u64)__entry->page,
		__entry->page_offset
	)
);

TRACE_EVENT(enic_rq_cq_desc_16,
	TP_PROTO(struct enic_qp *qp, struct cq_enet_rq_desc *cq_desc),
	TP_ARGS(qp, cq_desc),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__dynamic_array(u8, cq, sizeof(struct cq_enet_rq_desc))
		__field(int, cq_len)
		__field(u16, qp_index)
		__field(u16, index)
		__field(bool, pkt_err)
		__field(bool, fcs_ok)
		__field(u16, pkt_len)
		__field(u32, rss_hash)
		__field(u16, rss_type)
		__field(bool, fcoe)
		__field(bool, fc_crc_ok)
		__field(bool, csum_not_calc)
		__field(bool, tcp_udp_csum_ok)
		__field(bool, ipv6)
		__field(bool, ipv4_csum_ok)
		__field(bool, vlan_stripped)
		__field(u16, vlan_tci)
		__field(u8, desc_type)
		__field(bool, color)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		memcpy(__get_dynamic_array(cq), cq_desc, sizeof(*cq_desc));
		__entry->cq_len = sizeof(*cq_desc);
		__entry->qp_index = qp->index;
		__entry->index = CQ_DESC_INDEX_16(cq_desc);
		__entry->pkt_err = CQ_DESC_PKT_ERR(cq_desc);
		__entry->fcs_ok = CQ_DESC_FCS_OK(cq_desc);
		__entry->pkt_len = CQ_DESC_PKT_LEN(cq_desc);
		__entry->rss_hash = CQ_DESC_RSS_HASH(cq_desc);
		__entry->rss_type = CQ_DESC_RSS_TYPE(cq_desc);
		__entry->fcoe = CQ_DESC_FCOE(cq_desc);
		__entry->fc_crc_ok = CQ_DESC_FC_CRC_OK(cq_desc);
		__entry->csum_not_calc = CQ_DESC_CSUM_NOT_CALC(cq_desc);
		__entry->tcp_udp_csum_ok = CQ_DESC_TCP_UDP_CSUM_OK(cq_desc);
		__entry->ipv6 = CQ_DESC_IPV6(cq_desc);
		__entry->ipv4_csum_ok = CQ_DESC_IPV4_CSUM_OK(cq_desc);
		__entry->vlan_stripped = CQ_DESC_VLAN_STRIPPED(cq_desc);
		__entry->vlan_tci = CQ_DESC_VLAN_TCI(cq_desc);
		__entry->desc_type = CQ_DESC_TYPE_16(cq_desc);
		__entry->color = CQ_DESC_COLOR_16(cq_desc);

	),
	TP_printk("%s: qp[%d]: %d/%d/%d/%d/0x%x/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d <%s>",
		__get_str(devname), __entry->qp_index,
		__entry->index,
		__entry->pkt_err,
		__entry->fcs_ok,
		__entry->pkt_len,
		__entry->rss_hash,
		__entry->rss_type,
		__entry->fcoe,
		__entry->fc_crc_ok,
		__entry->csum_not_calc,
		__entry->tcp_udp_csum_ok,
		__entry->ipv6,
		__entry->ipv4_csum_ok,
		__entry->vlan_stripped,
		__entry->vlan_tci,
		__entry->desc_type,
		__entry->color,
		__print_hex(__get_dynamic_array(cq), __entry->cq_len)
	)
);

TRACE_EVENT(enic_rq_cq_desc_32,
	TP_PROTO(struct enic_qp *qp, struct cq_enet_rq_desc_32 *cq_desc),
	TP_ARGS(qp, cq_desc),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__dynamic_array(u8, cq, sizeof(struct cq_enet_rq_desc_32))
		__field(int, cq_len)
		__field(u16, qp_index)
		__field(u16, index)
		__field(bool, pkt_err)
		__field(bool, fcs_ok)
		__field(u16, pkt_len)
		__field(u32, rss_hash)
		__field(u16, rss_type)
		__field(bool, fcoe)
		__field(bool, fc_crc_ok)
		__field(bool, csum_not_calc)
		__field(bool, tcp_udp_csum_ok)
		__field(bool, ipv6)
		__field(bool, ipv4_csum_ok)
		__field(bool, vlan_stripped)
		__field(u16, vlan_tci)
		__field(u8, desc_type)
		__field(bool, ts_val)
		__field(u64, ts)
		__field(bool, color)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		memcpy(__get_dynamic_array(cq), cq_desc, sizeof(*cq_desc));
		__entry->cq_len = sizeof(*cq_desc);
		__entry->qp_index = qp->index;
		__entry->index = CQ_DESC_INDEX_32(cq_desc);
		__entry->pkt_err = CQ_DESC_PKT_ERR(cq_desc);
		__entry->fcs_ok = CQ_DESC_FCS_OK(cq_desc);
		__entry->pkt_len = CQ_DESC_PKT_LEN(cq_desc);
		__entry->rss_hash = CQ_DESC_RSS_HASH(cq_desc);
		__entry->rss_type = CQ_DESC_RSS_TYPE(cq_desc);
		__entry->fcoe = CQ_DESC_FCOE(cq_desc);
		__entry->fc_crc_ok = CQ_DESC_FC_CRC_OK(cq_desc);
		__entry->csum_not_calc = CQ_DESC_CSUM_NOT_CALC(cq_desc);
		__entry->tcp_udp_csum_ok = CQ_DESC_TCP_UDP_CSUM_OK(cq_desc);
		__entry->ipv6 = CQ_DESC_IPV6(cq_desc);
		__entry->ipv4_csum_ok = CQ_DESC_IPV4_CSUM_OK(cq_desc);
		__entry->vlan_stripped = CQ_DESC_VLAN_STRIPPED(cq_desc);
		__entry->vlan_tci = CQ_DESC_VLAN_TCI(cq_desc);
		__entry->desc_type = CQ_DESC_TYPE_32(cq_desc);
		__entry->ts_val = CQ_DESC_TS_VAL(cq_desc);
		__entry->ts = CQ_DESC_TS(cq_desc);
		__entry->color = CQ_DESC_COLOR_32(cq_desc);

	),
	TP_printk("%s: qp[%d]: %d/%d/%d/%d/0x%x/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/0x%llx/%d <%s>",
		__get_str(devname), __entry->qp_index,
		__entry->index,
		__entry->pkt_err,
		__entry->fcs_ok,
		__entry->pkt_len,
		__entry->rss_hash,
		__entry->rss_type,
		__entry->fcoe,
		__entry->fc_crc_ok,
		__entry->csum_not_calc,
		__entry->tcp_udp_csum_ok,
		__entry->ipv6,
		__entry->ipv4_csum_ok,
		__entry->vlan_stripped,
		__entry->vlan_tci,
		__entry->desc_type,
		__entry->ts_val,
		__entry->ts,
		__entry->color,
		__print_hex(__get_dynamic_array(cq), __entry->cq_len)
	)
);

TRACE_EVENT(enic_wq_buf,
	TP_PROTO(struct enic_qp *qp, struct enic_wq_buf *buf),
	TP_ARGS(qp, buf),
	TP_STRUCT__entry(
		__string(devname, qp->netdev->name)
		__field(u16, qp_index)
		__field(dma_addr_t, dma_addr)
		__field(struct sk_buff *, skb)
		__field(u32, len)
		__field(u16, index)
		__field(bool, sop)
	),
	TP_fast_assign(
		__assign_str(devname, qp->netdev->name);
		__entry->qp_index = qp->index;
		__entry->dma_addr = buf->dma_addr;
		__entry->skb = buf->skb;
		__entry->len = buf->len;
		__entry->index = buf->index;
		__entry->sop = buf->sop;
	),
	TP_printk(
		"%s: qp[%d]: dma_addr: 0x%llx skb: 0x%p len: %d index: %d sop: %d",
		__get_str(devname), __entry->qp_index, __entry->dma_addr,
		__entry->skb, __entry->len, __entry->index, __entry->sop
	)
);

#endif /* _ENIC_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE enic_trace
#include <trace/define_trace.h>

#endif /* __COVERITY__ */
