// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2008-2018 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
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

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#endif
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/ip6_checksum.h>

#include "enic_config.h"
#include "kcompat.h"
#include "enic_qp.h"
#include "enic.h"
#include "cq_enet_desc.h"
#include "rq_enet_desc.h"
#include "enic_res.h"
#include "enic_trace.h"
#include "enic_clock.h"
#include "enic_dev.h"

static int enic_rq_service(struct enic_qp *qp, u16 budget);
static int enic_rq_service_page(struct enic_qp *qp, u16 budget);

#define ENIC_MAX_COALESCE_TIMERS        10
/*  Interrupt moderation table, which will be used to decide the
 *  coalescing timer values
 *  {rx_rate in Mbps, mapping percentage of the range}
 */
static struct enic_intr_mod_table mod_table[ENIC_MAX_COALESCE_TIMERS + 1] = {
	{4000,  0},
	{4400, 10},
	{5060, 20},
	{5230, 30},
	{5540, 40},
	{5820, 50},
	{6120, 60},
	{6435, 70},
	{6745, 80},
	{7000, 90},
	{0xFFFFFFFF, 100}
};

static void enic_wq_enable(struct enic_qp *qp)
{
	struct enic_wq *wq = &qp->wq;

	if (!wq->ctrl)
		return;
	iowrite32(1, &wq->ctrl->enable);
}

static int enic_wq_disable(struct enic_qp *qp)
{
	struct enic_wq *wq = &qp->wq;
	int i;

	if (!wq->ctrl)
		return 0;
	iowrite32(0, &wq->ctrl->enable);

	/* Wait for HW to ACK disable request */
	for (i = 0; i < 1000; i++) {
		if (!ioread32(&wq->ctrl->running))
			return 0;
		usleep_range(10, 20);
	}

	netdev_err(qp->netdev, "Failed to disable WQ[%d]", qp->index);

	return -ETIMEDOUT;
}

static void enic_rq_enable(struct enic_qp *qp)
{
	struct enic_rq *rq = &qp->rq;

	if (!rq->ctrl)
		return;
	iowrite32(1, &rq->ctrl->enable);
}

static int enic_rq_disable(struct enic_qp *qp)
{
	unsigned int wait;
	struct enic_rq *rq = &qp->rq;
	int i;

	if (!rq->ctrl)
		return 0;
	/* Due to a race condition with clearing RQ "mini-cache" in hw, we need
	 * to disable the RQ twice to guarantee that stale descriptors are not
	 * used when this RQ is re-enabled.
	 */
	for (i = 0; i < 2; i++) {
		iowrite32(0, &rq->ctrl->enable);

		/* Wait for HW to ACK disable request */
		for (wait = 20000; wait > 0; wait--) {
			if (!ioread32(&rq->ctrl->running))
				break;
			if (!wait) {
				netdev_err(qp->netdev, "Failed to disable RQ[%d]",
					   i);
				return -ETIMEDOUT;
			}
		}
	}

	return 0;
}

static void enic_wq_free_bufs(struct enic_qp *qp)
{
	struct enic_wq_buf *buf, *end, *next;

	buf = end = qp->wq.to_use;

	if (!buf)
		return;

	do {
		next = buf->next;
		if (buf->sop)
			dma_unmap_single(qp->dev, buf->dma_addr, buf->len,
					 DMA_TO_DEVICE);
		else if (buf->dma_addr)
			dma_unmap_page(qp->dev, buf->dma_addr, buf->len,
				       DMA_TO_DEVICE);
		dev_kfree_skb(buf->skb);
		kfree(buf);
		buf = next;
	} while (buf && buf != end);

	qp->wq.to_clean = qp->wq.to_use = NULL;
}

static int enic_wq_alloc_bufs(struct enic_qp *qp)
{
	struct enic_wq_buf *buf;
	struct enic *enic = qp->enic;
	struct enic_qp_ring *qp_ring = &enic->qp_ring[qp->index];
	int i;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	qp->wq.to_clean = qp->wq.to_use = buf->next = buf;

	for (i = 1; i < qp_ring->wq_ring.desc_count; i++) {
		buf->next = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf->next) {
			enic_wq_free_bufs(qp);
			return -ENOMEM;
		}
		buf = buf->next;
		buf->index = i;
	}
	buf->next = qp->wq.to_clean;

	return 0;
}

static void enic_rq_free_bufs(struct enic_qp *qp)
{
	struct enic_rq_buf *buf, *end, *next;

	buf = end = qp->rq.to_clean;

	if (!buf)
		return;

	do {
		next = buf->next;
		if (buf->skb)
			dev_kfree_skb_any(buf->skb);
		if (buf->page) {
			/* For split pages, we unmap page at 2nd half of the page */
			if (!qp->rq.split_pages || buf->page_offset != 0) {
				dma_unmap_page(qp->dev, buf->dma_addr, PAGE_SIZE,
					       DMA_FROM_DEVICE);
			}
			/* Decrement refcnt and free page. It works correctly
			 * for split pages too, as we increment refcnt when filling rq
			 */
			__free_pages(buf->page, ENIC_PAGE_ORDER);
		} else if (buf->dma_addr) {
			dma_unmap_single(qp->dev, buf->dma_addr, buf->len,
					 DMA_FROM_DEVICE);
		}
		kfree(buf);
		buf = next;
	} while (buf && buf != end);

	/* There may be an allocated page with only 1st half posted to rq */
	if (qp->rq.page != NULL) {
		dma_unmap_page(qp->dev, qp->rq.dma_addr, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		__free_pages(qp->rq.page, ENIC_PAGE_ORDER);
		qp->rq.page = NULL;
	}

	qp->rq.to_clean = qp->rq.to_use = qp->rq.next_frag = NULL;
}

static int enic_rq_alloc_bufs(struct enic_qp *qp)
{
	struct enic_rq_buf *buf;
	struct enic *enic = qp->enic;
	struct enic_qp_ring *qp_ring = &enic->qp_ring[qp->index];
	int i;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	qp->rq.to_clean = qp->rq.to_use = qp->rq.next_frag = buf;
	qp->rq.num_hold = 0;
	qp->rq.posted_index = 0;
	qp->rq.frag_num = 0;

	for (i = 1; i < qp_ring->rq_ring.desc_count; i++) {
		buf->next = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf->next)
			goto out;
		buf = buf->next;
		buf->index = i;
		/* If split pages are being used, contiguous buffer pairs
		 * share a PAGE_SIZE (4K) page. The offset within the page
		 * is hard coded to either 0 or 2K.
		 */
		if (qp->rq.split_pages && (buf->index & 1))
			buf->page_offset = qp->rq.buf_size;
	}
	buf->next = qp->rq.to_clean;

	return 0;
out:
	enic_rq_free_bufs(qp);
	return -ENOMEM;
}

void enic_free_qp(struct enic *enic)
{
	kfree(enic->qp);
	enic->qp = NULL;
	kfree(enic->qp_ring);
	enic->qp_ring = NULL;
}

void enic_deinit_qp(struct enic *enic)
{
	struct enic_qp_ring *qp_ring;
	struct enic_qp *qp;
	int i;

	for (i = 0; i < enic->wq_count; i++) {
		qp = &enic->qp[i];
		qp_ring = &enic->qp_ring[i];

		enic_wq_disable(qp);
		enic_wq_free_bufs(qp);
		vnic_dev_free_desc_ring(enic->vdev, &qp_ring->wq_ring);
		vnic_dev_free_desc_ring(enic->vdev, &qp_ring->wcq_ring);
	}
	for (i = 0; i < enic->rq_count; i++) {
		qp = &enic->qp[i];
		qp_ring = &enic->qp_ring[i];

		enic_rq_disable(qp);
		enic_rq_free_bufs(qp);
		vnic_dev_free_desc_ring(enic->vdev, &qp_ring->rq_ring);
		vnic_dev_free_desc_ring(enic->vdev, &qp_ring->rcq_ring);
	}

	for (i = 0; i < enic->qp_count; i++)
		iowrite32(0, &enic->qp[i].ctrl->int_credits);

	enic->notify_ctrl = NULL;
	enic->err_ctrl = NULL;
}

static void enic_wq_init_ctrl(struct vnic_wq_ctrl __iomem *ctrl, u64 paddr,
			      u32 count, u32 cq_index,
			      u32 error_interrupt_enable,
			      u32 error_interrupt_offset, u32 error_status,
			      u32 fetch_index, u32 posted_index)
{
	writeq(paddr, &ctrl->ring_base);
	iowrite32(count, &ctrl->ring_size);
	iowrite32(cq_index, &ctrl->cq_index);
	iowrite32(error_interrupt_enable, &ctrl->error_interrupt_enable);
	iowrite32(error_interrupt_offset, &ctrl->error_interrupt_offset);
	iowrite32(error_status, &ctrl->error_status);
	iowrite32(fetch_index, &ctrl->fetch_index);
	iowrite32(posted_index, &ctrl->posted_index);
}

static void enic_wq_init(struct enic_qp *qp)
{
	u64 paddr;
	struct enic_qp_ring *qp_ring = &qp->enic->qp_ring[qp->index];
	unsigned int count = qp_ring->wq_ring.desc_count;
	struct vnic_wq_ctrl __iomem *ctrl = qp->wq.ctrl;
	unsigned int error_interrupt_offset;
	unsigned int error_interrupt_enable;

	if (!ctrl)
		return;

	switch (vnic_dev_get_intr_mode(qp->enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		error_interrupt_offset = 0;
		error_interrupt_enable = 1;
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		error_interrupt_offset = qp->enic->intr_count - 2;
		error_interrupt_enable = 1;
		break;
	default:
		error_interrupt_offset = 0;
		error_interrupt_enable = 0;
		break;
	}
	paddr = (u64)qp_ring->wq_ring.base_addr | VNIC_PADDR_TARGET;
	enic_wq_init_ctrl(ctrl, paddr, count, qp->index,
			  error_interrupt_enable, error_interrupt_offset,
			  0,	/* error_status */
			  0,	/* fetch index */
			  0	/* posted_index */);
	/* fetch_index and posted_index are initialized to 0 here.
	 * enic_wq_alloc_bufs should make sure wq->to_clean and wq->to_use
	 * should point to buf with index 0;
	 */
	skb_queue_head_init(&qp->wq.skb_list);
}

static void enic_rq_init_ctrl(struct vnic_rq_ctrl __iomem *ctrl, u64 paddr,
			      u32 count, u32 cq_index,
			      u32 error_interrupt_enable,
			      u32 error_interrupt_offset,
			      u32 error_status, u32 fetch_index,
			      u32 posted_index)
{
	writeq(paddr, &ctrl->ring_base);
	iowrite32(count, &ctrl->ring_size);
	iowrite32(cq_index, &ctrl->cq_index);
	iowrite32(error_interrupt_enable, &ctrl->error_interrupt_enable);
	iowrite32(error_interrupt_offset, &ctrl->error_interrupt_offset);
	iowrite32(error_status, &ctrl->error_status);
	iowrite32(fetch_index, &ctrl->fetch_index);
	iowrite32(posted_index, &ctrl->posted_index);
}

static void enic_rq_init(struct enic_qp *qp)
{
	u64 paddr;
	struct enic_qp_ring *qp_ring = &qp->enic->qp_ring[qp->index];
	unsigned int count = qp_ring->rq_ring.desc_count;
	struct vnic_rq_ctrl __iomem *ctrl = qp->rq.ctrl;
	unsigned int error_interrupt_offset;
	unsigned int error_interrupt_enable;

	if (!ctrl)
		return;

	switch (vnic_dev_get_intr_mode(qp->enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		error_interrupt_offset = 0;
		error_interrupt_enable = 1;
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		error_interrupt_offset = qp->enic->intr_count - 2;
		error_interrupt_enable = 1;
		break;
	default:
		error_interrupt_offset = 0;
		error_interrupt_enable = 0;
		break;
	}
	paddr = (u64)qp_ring->rq_ring.base_addr | VNIC_PADDR_TARGET;
	enic_rq_init_ctrl(ctrl, paddr, count, (qp->index + qp->enic->wq_count),
			  error_interrupt_enable, error_interrupt_offset,
			  0,	/* error_status */
			  0,	/* fetch_index */
			  0	/* posted_index */);
	/* fetch_index and posted_index are initialized before rq bufs are
	 * allocated. While rq bufs are allocated, we should make sure
	 * rq->to_use and rq->to_clean points to buf with index 0, later
	 * enic_rq_alloc_bufs will set posted_index to correct value.
	 */
}

static void enic_cq_init(struct vnic_cq_ctrl __iomem *ctrl, u64 paddr,
			 u32 desc_count, u32 flow_control_enable,
			 u32 color_enable, u32 cq_head, u32 cq_tail,
			 u32 cq_tail_color, u32 interrupt_enable,
			 u32 cq_entry_enable, u32 cq_message_enable,
			 u32 interrupt_offset, u64 cq_message_addr)
{
	writeq(paddr, &ctrl->ring_base);
	iowrite32(desc_count, &ctrl->ring_size);
	iowrite32(flow_control_enable, &ctrl->flow_control_enable);
	iowrite32(color_enable, &ctrl->color_enable);
	iowrite32(cq_head, &ctrl->cq_head);
	iowrite32(cq_tail, &ctrl->cq_tail);
	iowrite32(cq_tail_color, &ctrl->cq_tail_color);
	iowrite32(interrupt_enable, &ctrl->interrupt_enable);
	iowrite32(cq_entry_enable, &ctrl->cq_entry_enable);
	iowrite32(cq_message_enable, &ctrl->cq_message_enable);
	iowrite32(interrupt_offset, &ctrl->interrupt_offset);
	writeq(cq_message_addr, &ctrl->cq_message_addr);
}

static void enic_rcq_init(struct enic_qp *qp)
{
	struct enic_qp_ring *qp_ring = &qp->enic->qp_ring[qp->index];

	enic_cq_init(qp_ring->rcq_ctrl,
		     qp_ring->rcq_ring.base_addr | VNIC_PADDR_TARGET,
		     qp_ring->rcq_ring.desc_count,
		     0, /* flow_control_enable */
		     1, /* color_enable */
		     0, /* cq_head */
		     0, /* cq_tail */
		     1, /* cq_tail_color */
		     1, /* interrupt_enable */
		     1, /* cq_entry_enable */
		     0, /* cq_message_enable */
		     qp->index, /* interrupt offset */
		     0 /*cq_message_addr */);
}

static void enic_wcq_init(struct enic_qp *qp)
{
	struct enic_qp_ring *qp_ring = &qp->enic->qp_ring[qp->index];

	enic_cq_init(qp_ring->wcq_ctrl,
		     qp_ring->wcq_ring.base_addr | VNIC_PADDR_TARGET,
		     qp_ring->wcq_ring.desc_count,
		     0, /* flow_control_enable */
		     1, /* color_enable */
		     0, /* cq_head */
		     0, /* cq_tail */
		     1, /* cq_tail_color */
		     1, /* interrupt_enable */
		     1, /* cq_entry_enable */
		     0, /* cq_message_enable */
		     qp->index, /* interrupt offset */
		     0 /*cq_message_addr */);
}
void enic_intr_ctrl_init(struct vnic_intr_ctrl __iomem *ctrl,
				       u32 coalescing_timer,
				       u32 coalescing_type,
				       u32 mask_on_assertion, u32 int_credits)
{
	iowrite32(coalescing_timer, &ctrl->coalescing_timer);
	iowrite32(coalescing_type, &ctrl->coalescing_type);
	iowrite32(mask_on_assertion, &ctrl->mask_on_assertion);
	iowrite32(int_credits, &ctrl->int_credits);

}

static void enic_qp_ctrl_init(struct enic_qp *qp)
{
	struct enic *enic = qp->enic;
	struct enic_rx_coal *rxcoal = &enic->rx_coal;
	struct vnic_intr_ctrl __iomem *ctrl = qp->ctrl;
	u32 coal_timer_hw;
	u32 coal_timer_usec = rxcoal->coal_usecs;
	u32 mask_on_assertion = 0;

	coal_timer_hw = vnic_dev_intr_coal_timer_usec_to_hw(enic->vdev,
							    coal_timer_usec);
	qp->rq.adaptive_coal = rxcoal->use_adaptive_rx_coalesce;
	qp->rq.acoal_low = rxcoal->acoal_low;
	qp->rq.acoal_high = rxcoal->acoal_high;
	qp->rq.timer_mul = vnic_dev_get_coal_timer_mul(enic->vdev);
	qp->rq.timer_div = vnic_dev_get_coal_timer_div(enic->vdev);

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
	case VNIC_DEV_INTR_MODE_MSIX:
		mask_on_assertion = 1;
		break;
	case VNIC_DEV_INTR_MODE_INTX:
		mask_on_assertion = 0;
		break;
	default:
		netdev_err(qp->netdev, "interrupt type unknown");
		break;
	}

	enic_intr_ctrl_init(ctrl, coal_timer_hw, enic->config.intr_timer_type,
			    mask_on_assertion, 0);
}

static int enic_rq_fill_bufs(struct enic_qp *qp)
{
	struct net_device *netdev = qp->netdev;
	unsigned int len = netdev->mtu + VLAN_ETH_HLEN;
	dma_addr_t dma_addr;
	struct enic_rq_buf *buf;
	int ret = 0;

	buf = qp->rq.to_use;

	if (unlikely(!qp->rq.ctrl))
		return 0;
	while (buf->next != qp->rq.to_clean) {
		buf->skb = netdev_alloc_skb_ip_align(netdev, len);
		if (unlikely(!buf->skb)) {
			qp->rq.stats.no_skb++;
			ret = -ENOMEM;
			break;
		}
		dma_addr = dma_map_single(qp->dev, buf->skb->data, len,
					  DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(qp->dev, dma_addr))) {
			net_warn_ratelimited("%s: DMA mapping failed for rq[%d], desc[%d]\n",
					     qp->netdev->name, qp->index,
					     buf->index);
			dev_kfree_skb(buf->skb);
			buf->skb = NULL;
			ret = -ENOMEM;
			qp->rq.stats.dma_map_error++;
			break;
		}
		rq_enet_desc_enc(&qp->rq.rq_base[buf->index],
				 (u64)dma_addr | VNIC_PADDR_TARGET,
				 RQ_ENET_TYPE_ONLY_SOP, len);
		trace_enic_rq_desc(qp, buf);
		buf->dma_addr = dma_addr;
		buf->len = len;
		buf = buf->next;
		/* Hw caches the rq_desc in multiple of 16 entries (256 bytes)
		 * Update posted_index (PI) in multiple of 16 so that hw does
		 * not have to read rq_desc multiple time if posted_index falls
		 * in same cache line.
		 * For example, if driver write PI = 4, hw fetches 0-15 desc.
		 * descriptors 4-15 are not valid. If driver later writes
		 * PI = 10 Hw has to fetch 0-15 again.
		 *
		 * Also HW disables rq_desc cache if PI and fetch_index (FI)
		 * falls on same cacheline. Making sure PI is multiple of 16
		 * prevents this.
		 */
		if (!(buf->index & 0xf)) {
			/* desc should be written before posting posted_index to
			 * hw
			 */
			wmb();
			iowrite32(buf->index, &qp->rq.ctrl->posted_index);
			trace_enic_rq_posted_index(qp);
		}
	}
	qp->rq.to_use = buf;

	trace_enic_rq_fill_bufs(qp,  ret);
	return ret;
}

static u32 enic_ring_add(u32 desc_count, u32 posted_index, u32 num_hold)
{
	u32 desc_idx;

	desc_idx = posted_index + num_hold;
	desc_idx -= (desc_idx >= desc_count) ? desc_count : 0;
	return desc_idx;
}

struct page *enic_rq_alloc_buf_page(struct enic_qp *qp, dma_addr_t *dma_addr)
{
	struct page *page;

	page = alloc_pages(GFP_ATOMIC | __GFP_COMP, ENIC_PAGE_ORDER);
	if (unlikely(!page)) {
		qp->rq.stats.alloc_error++;
		return NULL;
	}
	*dma_addr = dma_map_page(qp->dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(qp->dev, *dma_addr))) {
		net_warn_ratelimited("%s: DMA mapping failed for rq[%d]\n",
				     qp->netdev->name, qp->index);
		__free_pages(page, ENIC_PAGE_ORDER);
		qp->rq.stats.dma_map_error++;
		return NULL;
	}
	return page;
}

static int enic_rq_fill_buf_pages(struct enic_qp *qp)
{
	struct enic_rq_buf *buf = qp->rq.to_use;
	u16 num_received = 0;
	int ret = 0;
	u8 rem;

	if (unlikely(!qp->rq.ctrl))
		return 0;
	while (buf->next != qp->rq.to_clean) {
		/* Only allocate a new page if offset is 0. Otherwise page is
		 * being shared with the previously allocated page.
		 */
		if (buf->page_offset == 0) {
			qp->rq.page = enic_rq_alloc_buf_page(qp, &qp->rq.dma_addr);
			if (qp->rq.page == NULL) {
				ret = -ENOMEM;
				break;
			}
			/* Hold 2 refs: 1 for 1st half and 1 for 2nd half */
			if (qp->rq.split_pages)
				get_page(qp->rq.page);
		}
		buf->page = qp->rq.page;
		buf->dma_addr = qp->rq.dma_addr;
		rq_enet_desc_enc(&qp->rq.rq_base[buf->index],
				 (u64)(buf->dma_addr + buf->page_offset) |
				 VNIC_PADDR_TARGET, RQ_ENET_TYPE_SOP_OR_EOP,
				 qp->rq.buf_size);
		trace_enic_rq_desc(qp, buf);
		/* Done with the allocated page. Allocate a new one next time */
		if (!qp->rq.split_pages || buf->page_offset != 0)
			qp->rq.page = NULL;
		buf = buf->next;
		num_received++;
	}
	/* Post RQ buffers if free threshold reached. Post in multiples of 16
	 * descriptors (256B) so VIC can read full cachelines.
	 */
	qp->rq.num_hold += num_received;
	if (qp->rq.num_hold > qp->rq.free_thresh) {
		rem = qp->rq.num_hold & 0xf;
		qp->rq.posted_index = enic_ring_add(qp->enic->config.rq_desc_count,
						    qp->rq.posted_index,
						    qp->rq.num_hold - rem);

		/* desc should be written before posting posted_index to hw
		 */
		wmb();
		iowrite32(qp->rq.posted_index, &qp->rq.ctrl->posted_index);
		qp->rq.num_hold = rem;
		trace_enic_rq_posted_index(qp);
	}
	qp->rq.to_use = buf;

	trace_enic_rq_fill_bufs(qp,  ret);
	return ret;
}

static int enic_alloc_wq(struct enic_qp *qp)
{
	struct enic *enic = qp->enic;
	int index = qp->index;
	struct enic_qp_ring *qp_ring = &enic->qp_ring[index];
	int ret;

	qp->wq.ctrl = vnic_dev_get_res(enic->vdev, RES_TYPE_WQ, index);
	if (!qp->wq.ctrl) {
		netdev_err(qp->netdev, "WQ[%d] ctrl not found", index);
		return -ENODEV;
	}

	qp_ring->wcq_ctrl = vnic_dev_get_res(enic->vdev, RES_TYPE_CQ,
					     index);
	if (!qp_ring->wcq_ctrl) {
		netdev_err(qp->netdev, "WCQ[%d] ctrl not found", index);
		return -ENODEV;
	}
	ret = enic_wq_disable(qp);
	if (ret)
		return ret;
	ret = vnic_dev_alloc_desc_ring(enic->vdev, &qp_ring->wq_ring,
				       enic->config.wq_desc_count,
				       sizeof(struct wq_enet_desc));
	if (ret)
		return ret;
	enic_wq_init(qp);
	ret = vnic_dev_alloc_desc_ring(enic->vdev,
				       &qp_ring->wcq_ring,
				       enic->config.wq_desc_count,
				       sizeof(struct cq_enet_wq_desc));
	if (ret)
		return ret;
	qp->wq.cq_base = qp_ring->wcq_ring.descs;
	qp->wq.wq_base = qp_ring->wq_ring.descs;
	atomic_set(&qp->wq.desc_avail, qp_ring->wq_ring.desc_count - 1);
	qp->wq.cq_desc_count = qp_ring->wcq_ring.desc_count;
	enic_wcq_init(qp);
	ret = enic_wq_alloc_bufs(qp);
	if (ret)
		return ret;
	enic_wq_enable(qp);

	return 0;
}

static int enic_alloc_rq(struct enic_qp *qp)
{
	struct enic *enic = qp->enic;
	int index = qp->index;
	int ret;
	int cq_desc_size;
	struct enic_qp_ring *qp_ring = &enic->qp_ring[index];

	switch (enic->ext_cq) {
	case ENIC_RQ_CQ_ENTRY_SIZE_16:
		cq_desc_size = sizeof(struct cq_enet_rq_desc);
		enic->config.rq_desc_count = min_t(u32,
						   enic->config.rq_desc_count,
						   ENIC_MAX_RQ_DESCS_DEFAULT);
		enic->config.wq_desc_count = min_t(u32,
						   enic->config.wq_desc_count,
						   ENIC_MAX_WQ_DESCS_DEFAULT);
		break;
	case ENIC_RQ_CQ_ENTRY_SIZE_32:
		cq_desc_size = sizeof(struct cq_enet_rq_desc_32);
		break;
	case ENIC_RQ_CQ_ENTRY_SIZE_64:
		cq_desc_size = sizeof(struct cq_enet_rq_desc_64);
		break;
	default:
		return -EINVAL;
	}

	qp->rq.ctrl = vnic_dev_get_res(enic->vdev, RES_TYPE_RQ, index);
	if (!qp->rq.ctrl) {
		netdev_err(qp->netdev, "RQ[%d] ctrl not found", index);
		return -ENODEV;
	}

	qp_ring->rcq_ctrl = vnic_dev_get_res(enic->vdev, RES_TYPE_CQ,
					     enic->wq_count + index);
	if (!qp_ring->rcq_ctrl) {
		netdev_err(qp->netdev, "RCQ[%d] ctrl not found", index);
		return -ENODEV;
	}
	ret = enic_rq_disable(qp);
	if (ret)
		return ret;
	ret = vnic_dev_alloc_desc_ring(enic->vdev, &qp_ring->rq_ring,
				       enic->config.rq_desc_count,
				       sizeof(struct rq_enet_desc));
	if (ret)
		return ret;
	enic_rq_init(qp);
	ret = vnic_dev_alloc_desc_ring(enic->vdev, &qp_ring->rcq_ring,
				       enic->config.rq_desc_count,
				       cq_desc_size);
	if (ret)
		return ret;
	qp->rq.cq_base = qp_ring->rcq_ring.descs;
	qp->rq.rq_base = qp_ring->rq_ring.descs;
	qp->rq.cq_desc_count = qp_ring->rcq_ring.desc_count;
	qp->rq.cq_desc_size = cq_desc_size;

	/* Set the number of RQ descriptors to hold before posting to VIC
	 */
	qp->rq.free_thresh = max_t(u16, enic->config.rq_desc_count /
			     ENIC_RQ_FREE_THRESH_DIV, ENIC_MIN_RQ_FREE_THRESH);

	enic_rcq_init(qp);
	/* RQ should be enabled before posting to RQ desc
	 */
	enic_rq_enable(qp);
	ret = enic_rq_alloc_bufs(qp);
	if (ret)
		return ret;
	ret = qp->rq.fill_bufs(qp);
	if (ret)
		return ret;

	return 0;
}

int enic_alloc_qp(struct enic *enic)
{
	int ret = 0;
	int i;

	WARN_ON_ONCE(enic->qp);
	WARN_ON_ONCE(enic->qp_ring);

	enic->qp = kcalloc(enic->qp_count, sizeof(struct enic_qp), GFP_KERNEL);
	if (!enic->qp) {
		ret = -ENOMEM;
		goto out;
	}

	enic->qp_ring = kcalloc(enic->qp_count, sizeof(struct enic_qp_ring),
				GFP_KERNEL);
	if (!enic->qp_ring) {
		ret = -ENOMEM;
		goto free_qp;
	}

	for (i = 0; i < enic->qp_count; i++) {
		struct enic_qp *qp = &enic->qp[i];

		qp->dev = &enic->pdev->dev;
		qp->netdev = enic->netdev;
		qp->enic = enic;
		qp->index = i;
		qp->rq.adaptive_coal = true;
	}

out:
	return ret;
free_qp:
	kfree(enic->qp);
	enic->qp = NULL;
	return ret;
}

/*
 * Set the rq_service and buffer fill functions depending on MTU and
 * if VIC has support for multiple pages per packet.
 */
static void enic_rq_set_fns(struct enic_qp *qp, bool mixed_rq_descs)
{
	/* use per packet alloc if VIC has support and MTU > 1 page
	 */
	if (mixed_rq_descs) {
		qp->rq.service = enic_rq_service_page;
		qp->rq.fill_bufs = enic_rq_fill_buf_pages;
		qp->rq.split_pages = (qp->netdev->mtu + ETH_HLEN + VLAN_HLEN
				    <= (PAGE_SIZE / 2));
		qp->rq.buf_size = (qp->rq.split_pages) ? PAGE_SIZE / 2 :
				   PAGE_SIZE;
		if (qp->index == 0)
			netdev_info(qp->netdev, "Using page per RQ buffers%s\n",
				    (qp->rq.split_pages) ? " and split pages" : "");
	} else {
		if (qp->index == 0)
			netdev_info(qp->netdev, "Using packet per RQ buffers\n");
		qp->rq.service = enic_rq_service;
		qp->rq.fill_bufs = enic_rq_fill_bufs;
	}
}

int enic_init_qp(struct enic *enic)
{
	struct vnic_intr *notify_intr = &enic->intr[enic->qp_count + 1];
	struct vnic_intr *err_intr = &enic->intr[enic->qp_count];
	int ret = 0;
	int i;
	bool mixed_rq_descs;

	/* VIC support for SOP and non-SOP descs on same RQ
	 */
	mixed_rq_descs = ENIC_SETTING(enic, MIXED_RQ_DESC_TYPE);

	enic_ext_cq(enic);
	for (i = 0; i < enic->qp_count; i++) {
		struct vnic_intr *intr = &enic->intr[i];
		struct enic_qp *qp = &enic->qp[i];

		qp->ctrl = intr->ctrl;
		if (!qp->ctrl) {
			ret = -ENODEV;
			netdev_err(qp->netdev, "Intr[%d] ctrl not found", i);
			goto out;
		}
		qp->rq.last_color = 0;
		qp->rq.cq_to_clean = 0;
		qp->wq.last_color = 0;
		qp->wq.cq_to_clean = 0;
		enic_qp_ctrl_init(qp);
		qp->rq.vxlan_patch_level = enic->vxlan.patch_level;
		if (enic->vxlan.vxlan_udp_port_number)
			qp->rq.vxlan_offload = true;
		qp->rq.ext_cq = enic->ext_cq;
		enic_rq_set_fns(qp, mixed_rq_descs);
	}
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
	case VNIC_DEV_INTR_MODE_INTX:
		enic->err_ctrl = err_intr->ctrl;
		enic->notify_ctrl = notify_intr->ctrl;
		if (!enic->err_ctrl || !enic->notify_ctrl) {
			ret = -ENODEV;
			netdev_err(enic->netdev, "Couldn't find error/notify interrupt");
			goto out;
		}
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		break;
	default:
		netdev_err(enic->netdev, "Unknown interrupt type");
		break;
	}

	enic->legacy_pba = vnic_dev_get_res(enic->vdev,
					    RES_TYPE_INTR_PBA_LEGACY, 0);
	if (!enic->legacy_pba && (vnic_dev_get_intr_mode(enic->vdev) ==
				  VNIC_DEV_INTR_MODE_INTX)) {
		ret = -ENODEV;
		goto out;
	}
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		enic_intr_ctrl_init(enic->err_ctrl, 0, 0, 1, 0);
		enic_intr_ctrl_init(enic->notify_ctrl, 0, 0, 1, 0);
		break;
	case VNIC_DEV_INTR_MODE_INTX:
		enic_intr_ctrl_init(enic->err_ctrl, 0, 0, 0, 0);
		enic_intr_ctrl_init(enic->notify_ctrl, 0, 0, 0, 0);
		break;
	default:
		netdev_err(enic->netdev, "interrupt mode unknown");
		break;
	}

	for (i = 0; i < enic->wq_count; i++) {
		ret = enic_alloc_wq(&enic->qp[i]);
		if (ret)
			goto out;
	}

	for (i = 0; i < enic->rq_count; i++) {
		ret = enic_alloc_rq(&enic->qp[i]);
		if (ret)
			goto out;
	}

	return 0;

out:
	enic_deinit_qp(enic);
	return ret;
}

static inline unsigned int enic_wq_service(struct enic_qp *qp, int budget)
{
	struct netdev_queue *txq;
	struct cq_enet_wq_desc *cq_desc;
	struct enic_wq_buf *buf;
	u16 work = 0;
	u16 toclean;

	if (unlikely(!qp->wq.ctrl))
		return 0;
again:
	cq_desc = &qp->wq.cq_base[qp->wq.cq_to_clean];
	trace_enic_wq_cq_desc(qp, cq_desc);
	if (CQ_DESC_COLOR_16(cq_desc) != qp->wq.last_color) {
		bool ts_val;
		u64 ts;

		toclean = CQ_WQ_DESC_INDEX(cq_desc);
		ts_val = CQ_WQ_DESC_TS_VAL(cq_desc);
		ts = CQ_WQ_DESC_TS(cq_desc);
		qp->wq.cq_to_clean++;
		if (qp->wq.cq_to_clean == qp->wq.cq_desc_count) {
			qp->wq.cq_to_clean = 0;
			qp->wq.last_color = qp->wq.last_color ? 0 : 1;
		}
		if (unlikely(ts_val)) {
			struct sk_buff *skb;

			skb = skb_dequeue(&qp->wq.skb_list);
			enic_fill_hwtstamp(qp, &qp->enic->tstamp, ts,
					   skb_hwtstamps(skb), TRACE_WQ);
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			skb_tstamp_tx(skb, skb_hwtstamps(skb));
			napi_consume_skb(skb, budget);
			goto again;
		}
		work++;
		goto again;
	} else if (work) {
		buf = qp->wq.to_clean;

		toclean++;
		toclean = (toclean == qp->wq.cq_desc_count) ? 0 : toclean;
		/* clean until (toclean - 1) index
		 */
		while (buf->index != toclean) {
			trace_enic_wq_buf(qp, buf);
			if (buf->sop) {
				dma_unmap_single(qp->dev, buf->dma_addr,
						 buf->len,
						 DMA_TO_DEVICE);
				qp->wq.stats.cq_bytes += buf->len;
			} else if (buf->dma_addr) {
				dma_unmap_page(qp->dev, buf->dma_addr, buf->len,
					       DMA_TO_DEVICE);
				qp->wq.stats.cq_bytes += buf->len;
			}
			napi_consume_skb(buf->skb, budget);
			buf->skb = NULL;
			buf->dma_addr = 0;
			buf->sop = false;
			atomic_inc(&qp->wq.desc_avail);
			buf = buf->next;
		}
		qp->wq.to_clean = buf;
		txq = netdev_get_tx_queue(qp->enic->netdev, qp->index);
		if (netif_tx_queue_stopped(txq) &&
		    (enic_wq_desc_avail(qp) >=
		     (MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS))) {
			netif_tx_wake_queue(txq);
			trace_enic_tx_wake_queue(qp);
			qp->wq.stats.wake++;
		}
	}
	qp->wq.stats.cq_work += work;
	trace_enic_wq_service(qp, work);
	return work;
}

static void *enic_rq_cq_get(struct enic_qp *qp, u16 budget, u16 work,
				 u16 *index_ptr, u8 *type_ptr)
{
	void *cq_desc;
	bool color;
	u16 index;
	u8 type;

	cq_desc = (char *)qp->rq.cq_base + qp->rq.cq_desc_size * qp->rq.cq_to_clean;
	color = *(uint8_t *)((uintptr_t)cq_desc + qp->rq.cq_desc_size - 1)
		>> CQ_DESC_COLOR_SHIFT;
	if ((color == qp->rq.last_color) ||
	    (work >= budget)) {
		qp->rq.stats.packets += work;
		trace_enic_rq_cq_get(qp, work);
		return NULL;
	}
	/* color bit must be valid before reading other fields
	 */
	rmb();

	switch (qp->rq.ext_cq) {
	case ENIC_RQ_CQ_ENTRY_SIZE_16:
		trace_enic_rq_cq_desc_16(qp, cq_desc);
		index = CQ_DESC_INDEX_16(cq_desc);
		type = CQ_DESC_TYPE_16(cq_desc);
		break;
	case ENIC_RQ_CQ_ENTRY_SIZE_32:
		trace_enic_rq_cq_desc_32(qp, cq_desc);
		index = CQ_DESC_INDEX_32(cq_desc);
		type = CQ_DESC_TYPE_32(cq_desc);
		break;
	case ENIC_RQ_CQ_ENTRY_SIZE_64:
		trace_enic_rq_cq_desc_32(qp, cq_desc);
		index = CQ_DESC_INDEX_64(cq_desc);
		type = CQ_DESC_TYPE_64(cq_desc);
		break;
	default:
		/* exit, do not clean any */
		WARN_ONCE(1, "enic: cq_desc type unknown");
		return NULL;
	}

	*index_ptr = index;
	*type_ptr = type;
	qp->rq.cq_to_clean++;
	if (qp->rq.cq_to_clean == qp->rq.cq_desc_count) {
		qp->rq.cq_to_clean = 0;
		qp->rq.last_color = qp->rq.last_color ? 0 : 1;
	}
	return cq_desc;
}

static int enic_rq_pkt_error(struct enic_qp *qp, void *cq_desc)
{
	int ret = 0;

	if (unlikely(CQ_DESC_PKT_ERR(cq_desc))) {
		if (!CQ_DESC_FCS_OK(cq_desc)) {
			if (CQ_DESC_PKT_LEN(cq_desc) > 0) {
				qp->rq.stats.bad_fcs++;
				ret = 1;
			} else if (CQ_DESC_PKT_LEN(cq_desc) == 0) {
				qp->rq.stats.pkt_truncated++;
				ret = 2;
			}
		}
	}
	return ret;
}

static void enic_rq_set_skb_flags(struct enic_qp *qp, void *cq_desc,
					 u8 type, struct sk_buff *skb)
{
	struct net_device *netdev = qp->netdev;
	bool outer_csum_ok = true;
	bool encap = false;
	u32 rss_hash;
	u8 rss_type;

	rss_hash = CQ_DESC_RSS_HASH(cq_desc);
	rss_type = CQ_DESC_RSS_TYPE(cq_desc);
	if ((netdev->features & NETIF_F_RXHASH) && rss_hash && (type == 3)) {
		switch (rss_type) {
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv4:
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6:
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6_EX:
			skb_set_hash(skb, rss_hash, PKT_HASH_TYPE_L4);
			qp->rq.stats.l4_rss_hash++;
			break;
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv4:
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv6:
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv6_EX:
			skb_set_hash(skb, rss_hash, PKT_HASH_TYPE_L3);
			qp->rq.stats.l3_rss_hash++;
			break;
		}
	}
	if (qp->rq.vxlan_offload) {
		switch (qp->rq.vxlan_patch_level) {
		case 0:
			if (CQ_DESC_FCOE(cq_desc)) {
				encap = true;
				outer_csum_ok = CQ_DESC_FC_CRC_OK(cq_desc);
			}
			break;
		case 2:
			if ((type == 7) && (rss_hash & BIT(0))) {
				encap = true;
				outer_csum_ok = (rss_hash & BIT(0)) &&
						(rss_hash & BIT(2));
			}
			break;
		}
	}
	if ((netdev->features & NETIF_F_RXCSUM) &&
	    !CQ_DESC_CSUM_NOT_CALC(cq_desc)	&&
	    CQ_DESC_TCP_UDP_CSUM_OK(cq_desc)	&&
	    (CQ_DESC_IPV6(cq_desc) || CQ_DESC_IPV4_CSUM_OK(cq_desc)) &&
	    outer_csum_ok) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		qp->rq.stats.csum_unnecessary++;
#if IS_ENABLED(CONFIG_VXLAN)
#if !((SLES_RELEASE_CODE && (SLES_RELEASE_CODE <= SLES_RELEASE_VERSION(12, 1))) || \
      (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7, 1)))   || \
      (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 00)))
		if (encap) {
			skb->csum_level = encap;
			qp->rq.stats.csum_unnecessary_encap++;
		}
#endif
#endif /* CONFIG_VXLAN */
	}
	if (CQ_DESC_TS_VAL(cq_desc)) {
		enic_fill_hwtstamp(qp, &qp->enic->tstamp, CQ_DESC_TS(cq_desc),
				   skb_hwtstamps(skb), TRACE_RQ);
	}
#if IS_ENABLED(CONFIG_VXLAN)
#if ((SLES_RELEASE_CODE && (SLES_RELEASE_CODE <= SLES_RELEASE_VERSION(12, 1))) || \
     (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7, 1))))
	/* Need to test with eng build sles12sp1 kernel with the fixes
	 */
	if (encap) {
		skb->encapsulation = encap;
		qp->rq.stats.csum_unnecessary_encap++;
	}
#endif
#endif /* CONFIG_VXLAN */
	if (CQ_DESC_VLAN_STRIPPED(cq_desc)) {
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       CQ_DESC_VLAN_TCI(cq_desc));
		qp->rq.stats.vlan_stripped++;
	}
}

static int enic_rq_service(struct enic_qp *qp, u16 budget)
{
	struct net_device *netdev = qp->netdev;
	void *cq_desc;
	struct enic_rq_buf *buf;
	struct sk_buff *skb;
	u32 pkt_len;
	u8 type;
	u16 work = 0;
	u16 index;

	if (unlikely(!qp->rq.ctrl))
		return 0;

next_cq_desc:
	cq_desc = enic_rq_cq_get(qp, budget, work, &index, &type);
	if (!cq_desc)
		return work;

next_buf:
	buf = qp->rq.to_clean;
	trace_enic_rq_buf(qp, buf);
	skb = buf->skb;
	buf->skb = NULL;
	qp->rq.to_clean = buf->next;
	dma_unmap_single(qp->dev, buf->dma_addr, buf->len,
			 DMA_FROM_DEVICE);
	buf->dma_addr = 0;
	work++;

	/* check for improbable case of missed or corrupt CQ entry
	 */
	if (unlikely(buf->index != index)) {
		if (qp->rq.to_clean == qp->rq.to_use)
			return 0;
		netdev_warn_once(netdev,
				 "rq[%d] index missmatch: CQ desc: %u, RQ buf: %u\n",
				 qp->index, index, buf->index);
		napi_consume_skb(skb, budget);
		qp->rq.stats.desc_skip++;
		goto next_buf;
	}

	if (enic_rq_pkt_error(qp, cq_desc)) {
		napi_consume_skb(skb, budget);
		goto next_cq_desc;
	}

	pkt_len = CQ_DESC_PKT_LEN(cq_desc);
	qp->rq.stats.bytes += pkt_len;
	prefetch(skb->data - NET_IP_ALIGN);
	skb_put(skb, pkt_len);
	skb->protocol = eth_type_trans(skb, netdev);
	skb_record_rx_queue(skb, qp->index);

	enic_rq_set_skb_flags(qp, cq_desc, type, skb);

	skb_mark_napi_id(skb, &qp->napi);
	if (!(netdev->features & NETIF_F_GRO))
		netif_receive_skb(skb);
	else
		napi_gro_receive(&qp->napi, skb);
	qp->rq.bytes += pkt_len;
	goto next_cq_desc;
}

/* One 4K page is allocated for each RQ buffer and large packets can consume
 * multiple buffers. The SOP and EOP flags in the CQ entries indicate which
 * part of the packet the buffer contains. Check for invalid flags.
 */
static int enic_rq_sop_error(struct enic_qp *qp, u8 sop, u16 buf_index)
{
	int ret = 0;

	if (unlikely((qp->rq.frag_num == 0) && !sop)) {
		net_err_ratelimited("%s: rq[%d], desc[%d] invalid desc sequence: non-SOP following an EOP\n",
				     qp->netdev->name, qp->index, buf_index);
		qp->rq.stats.desc_order_error++;
		ret++;
	}
	if (unlikely((qp->rq.frag_num != 0) && sop)) {
		net_err_ratelimited("%s: rq[%d], desc[%d] invalid desc sequence: SOP following a non-EOP\n",
				     qp->netdev->name, qp->index, buf_index);
		qp->rq.stats.desc_order_error++;
		ret++;
	}
	if (unlikely(qp->rq.frag_num >= ENIC_MAX_PAGES)) {
		net_err_ratelimited("%s: too many packet fragments, rq[%d], desc[%d], num frags: %u\n",
				     qp->netdev->name, qp->index, buf_index,
				     qp->rq.frag_num + 1);
		qp->rq.stats.num_frags_error++;
		ret++;
	}
	return ret;
}

/* RQ service function used with allocating 1 page per buffer
 */
static int enic_rq_service_page(struct enic_qp *qp, u16 budget)
{
	struct enic_rq_buf *frag;
	struct enic_rq_buf *buf;
	struct sk_buff *skb;
	u16 completed_index;
	void *cq_desc;
	u16 work = 0;
	u8 type;
	u8 sop;
	u8 eop;

	if (unlikely(!qp->rq.ctrl))
		return 0;

	while ((cq_desc = enic_rq_cq_get(qp, budget, work, &completed_index,
		&type))) {

		buf = qp->rq.next_frag;
		trace_enic_rq_buf(qp, buf);
		qp->rq.next_frag = buf->next;

		/* check for improbable case of missed or corrupt CQ entry
		 */
		if (unlikely(buf->index != completed_index)) {
			netdev_warn_once(qp->netdev,
					 "rq[%d] index missmatch: CQ desc: %u, RQ buf: %u\n",
					 qp->index, completed_index, buf->index);
			qp->rq.stats.desc_skip++;
			goto error_exit;
		}

		sop = CQ_DESC_SOP(cq_desc);
		eop = CQ_DESC_EOP(cq_desc);

		/* check for packet errors and CQ desc ordering errors
		 */
		if (enic_rq_pkt_error(qp, cq_desc) ||
		    enic_rq_sop_error(qp, sop, completed_index))
			goto error_exit;

		/* The fragment length is needed for when the skb is built.
		 */
		buf->len = CQ_DESC_PKT_LEN(cq_desc);
		qp->rq.frag_num++;

		if (!eop)
			continue;

		/* Last page of packet- get an skb and fill it in with the
		 * packet fragments.
		 */
		skb = napi_get_frags(&qp->napi);
		if (unlikely(!skb)) {
			net_warn_ratelimited("%s: skb alloc error rq[%d], desc[%d]\n",
					     qp->netdev->name, qp->index,
					     completed_index);
			qp->rq.stats.no_skb++;
			goto error_exit;
		}

		frag = qp->rq.to_clean;

		while (frag != qp->rq.next_frag) {
			dma_sync_single_for_cpu(qp->dev, frag->dma_addr +
						frag->page_offset, frag->len,
						DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, frag->page,
					frag->page_offset, frag->len,
					qp->rq.buf_size);
			/* For split pages, unmap when we indicate the 2nd half
			 */
			if (!qp->rq.split_pages || frag->page_offset != 0) {
				dma_unmap_page(qp->dev, frag->dma_addr, PAGE_SIZE,
					       DMA_FROM_DEVICE);
			}
			frag->page = NULL;
			frag->dma_addr = 0;
			frag = frag->next;
		}

		/* Indicate that the buffers for the fragments making up the packet
		 * are now free to be reallocated
		 */
		qp->rq.to_clean = frag;
		qp->rq.bytes += skb->len;
		qp->rq.frag_num = 0;
		work++;
		/* CQ descriptor flags are valid only for CQ descriptors with
		 * EOP set
		 */
		enic_rq_set_skb_flags(qp, cq_desc, type, skb);
		skb_record_rx_queue(skb, qp->index);
		napi_gro_frags(&qp->napi);
	}
	return work;

error_exit:
	qp->rq.frag_num = 0;
	frag = qp->rq.to_clean;
	while (frag != qp->rq.next_frag) {
		if (!qp->rq.split_pages || frag->page_offset != 0) {
			dma_unmap_page(qp->dev, frag->dma_addr, frag->len,
				       DMA_TO_DEVICE);
		}
		__free_pages(frag->page, ENIC_PAGE_ORDER);
		frag->page = NULL;
		frag->dma_addr = 0;
		frag = frag->next;
	}
	/* Next descriptor will be SOP */
	qp->rq.to_clean = frag;
	return work;
}

void enic_adaptive_coal(struct enic_qp *qp)
{
	ktime_t now = ktime_get();
	u32 timer;
	u64 delta;
	u64 traffic;
	int i;

	if (!qp->rq.adaptive_coal)
		return;
	delta = ktime_us_delta(now, qp->rq.prev_ts);
	if (unlikely(delta < ENIC_AIC_TS_BREAK))
		return;
	qp->rq.prev_ts = now;
	traffic = qp->rq.bytes - qp->rq.bytes_delta;
	qp->rq.bytes_delta = qp->rq.bytes;

	/* The table takes Mbps
	 * traffic *= 8		=> bits
	 * traffic *= (10^6 / delta)	=> bps
	 * traffic /= 10^6	=>Mbps
	 *
	 * Combining, traffic *=(8/delta)
	 */
	traffic <<= 3;
	do_div(traffic, delta);

	for (i = 0; i < ENIC_MAX_COALESCE_TIMERS; i++)
		if (traffic < mod_table[i].rx_rate)
			break;
	timer = qp->rq.acoal_low + ((qp->rq.acoal_high - qp->rq.acoal_low) *
				    mod_table[i].range_percent) / 100;
	timer = (timer + qp->rq.coal_timer) >> 1;
	if (timer != qp->rq.coal_timer) {
		u32 timer_hw;

		timer_hw = timer * qp->rq.timer_mul;
		do_div(timer_hw, qp->rq.timer_div);
		qp->rq.coal_timer = timer;
		iowrite32(timer_hw, &qp->ctrl->coalescing_timer);
	}
}

int enic_napi_poll(struct napi_struct *napi, int budget)
{
	struct enic_qp *qp = container_of(napi, struct enic_qp, napi);
	unsigned int credit_ret;
	int rq_work = 0;
	int ret = 0;

	credit_ret = enic_wq_service(qp, budget);
	if (budget > 0) {
		rq_work = qp->rq.service(qp, budget);
		credit_ret += rq_work;
		ret = qp->rq.fill_bufs(qp);
		if (ret)
			rq_work = budget;
	}

#if (ENIC_HAVE_NAPI_COMPLETE_DONE && ENIC_HAVE_NAPI_COMPLETE_DONE_RET)
	if ((rq_work < budget) &&
	    napi_complete_done(napi, rq_work)) {
#else
	if (rq_work < budget) {
		napi_complete_done(napi, rq_work);
#endif
		qp->rq.stats.napi_complete++;
		enic_adaptive_coal(qp);
		enic_intr_return_credits(qp->ctrl,
					credit_ret,
					1,	/* Unmask interrupt */
					0);	/* do not reset timer */

		trace_enic_napi_ret(qp, rq_work, credit_ret, 1);
		return rq_work;
	} else {
		enic_intr_return_credits(qp->ctrl,
					credit_ret,
					0,	/* Do not Unmask interrupt */
					0);	/* do not reset timer */
	}

	qp->rq.stats.napi_repoll++;
	trace_enic_napi_ret(qp, rq_work, credit_ret, 0);
	return rq_work;
}

static inline void enic_enc_wq_desc(struct wq_enet_desc *desc,
				    dma_addr_t dma_addr,
				    u16 length,
				    u16 mss,
				    u16 header_length,
				    u8 offload_mode,
				    bool eop,
				    bool cq_entry,
				    u16 vlan_tag)
{
	u16 hlf = 0;

	desc->address = cpu_to_le64(dma_addr);
	desc->length = cpu_to_le16(length & WQ_ENET_LEN_MASK);
	/* loopback is always 0 */
	desc->mss_loopback = cpu_to_le16((mss & WQ_ENET_MSS_MASK) <<
					 WQ_ENET_MSS_SHIFT);
	desc->vlan_tag = cpu_to_le16(vlan_tag);

	hlf = (header_length & WQ_ENET_HDRLEN_MASK) |
	      ((offload_mode & WQ_ENET_FLAGS_OM_MASK) << WQ_ENET_HDRLEN_BITS) |
	      (eop ? (1 << WQ_ENET_FLAGS_EOP_SHIFT) : 0) |
	      (cq_entry ? (1 << WQ_ENET_FLAGS_CQ_ENTRY_SHIFT) : 0) |
	      /* skipping fcoe_encap, it's 0 for ethernet frames */
	      (vlan_tag ? (1 << WQ_ENET_FLAGS_VLAN_TAG_INSERT_SHIFT) : 0);
	desc->header_length_flags = cpu_to_le16(hlf);
}
#if IS_ENABLED(CONFIG_VXLAN)
static inline void enic_preload_tcp_csum_encap(struct sk_buff *skb)
{
	const struct ethhdr *eth = (struct ethhdr *)skb_inner_mac_header(skb);

	switch (eth->h_proto) {
	case ntohs(ETH_P_IP):
		inner_ip_hdr(skb)->check = 0;
		inner_tcp_hdr(skb)->check =
			~csum_tcpudp_magic(inner_ip_hdr(skb)->saddr,
					   inner_ip_hdr(skb)->daddr, 0,
					   IPPROTO_TCP, 0);
		break;
	case ntohs(ETH_P_IPV6):
		inner_tcp_hdr(skb)->check =
			~csum_ipv6_magic(&inner_ipv6_hdr(skb)->saddr,
					 &inner_ipv6_hdr(skb)->daddr, 0,
					 IPPROTO_TCP, 0);
		break;
	default:
		WARN_ONCE(1, "Non ipv4/ipv6 inner pkt for encap offload");
		break;
	}
}
#endif

static inline void enic_preload_tcp_csum(struct sk_buff *skb)
{
	/* Preload TCP csum field with IP pseudo hdr calculated
	 * with IP length set to zero.  HW will later add in length
	 * to each TCP segment resulting from the TSO.
	 */

	if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
		ip_hdr(skb)->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(ip_hdr(skb)->saddr,
			ip_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
	} else if (skb->protocol == cpu_to_be16(ETH_P_IPV6)) {
		tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
			&ipv6_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
	}
}


static inline int enic_post_xmit_skb(struct enic_qp *qp, struct sk_buff *skb)
{
	dma_addr_t dma_addr;
	skb_frag_t *frag;
	struct enic_wq_buf *buf, *backup;
	unsigned int length;
	unsigned int mss;
	unsigned int header_length = 0;
	unsigned int skb_head_len;
	unsigned int skb_data_len;
	unsigned int offset = 0;
	u8 offload_mode = WQ_ENET_OFFLOAD_MODE_CSUM;
	bool eop;
	bool cq_entry;
	bool tx_tstamp;
	u16 vlan_tag = 0;
	unsigned int pull_len;
	int err;

	backup = buf = qp->wq.to_use;
	mss = skb_shinfo(skb)->gso_size;
	if (skb_vlan_tag_present(skb)) {
		vlan_tag = skb_vlan_tag_get(skb);
		qp->wq.stats.add_vlan++;
	}
	tx_tstamp = skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;

	if (mss) {
		offload_mode = WQ_ENET_OFFLOAD_MODE_TSO;
		tx_tstamp = false;
		/* For offload mode TSO, mss is max segment size. Adapter will
		 * do the csum of each packet.
		 */
#if IS_ENABLED(CONFIG_VXLAN)
		if (skb->encapsulation) {
			header_length = skb_inner_transport_header(skb) -
					skb->data;
			header_length += inner_tcp_hdrlen(skb);
			enic_preload_tcp_csum_encap(skb);
			qp->wq.stats.encap_tso++;
		} else {
#else
		{
#endif
			header_length = skb_transport_offset(skb) +
					tcp_hdrlen(skb);
			enic_preload_tcp_csum(skb);
			qp->wq.stats.tso++;
		}
#if IS_ENABLED(CONFIG_VXLAN)
	} else if (skb->encapsulation) {
		/* Offload mode WQ_ENET_OFFLOAD_MODE_CSUM:
		 * For csum offload mode, mss is 0 means csum on
		 * outer/non-encap pkt.
		 *
		 * For encap pkt, BIT(0), BIT(1) and BIT(2) should be set.
		 */
		mss = 7;
		qp->wq.stats.encap_csum++;
#endif
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/* For offload mode WQ_ENET_OFFLOAD_MODE_CSUM_L4:
		 * mms is checksum field offset from beginning of packet.
		 */
		header_length = skb_checksum_start_offset(skb);
		mss = header_length + skb->csum_offset;
		offload_mode = WQ_ENET_OFFLOAD_MODE_CSUM_L4;
		qp->wq.stats.csum_partial++;
	} else {
		qp->wq.stats.csum++;
	}

	if (tx_tstamp) {
		mss |= WQ_ENET_MSS_TX_TSTAMP;
		skb_queue_tail(&qp->wq.skb_list, skb);
	}

	skb_head_len = skb_headlen(skb);
	skb_data_len = skb->data_len;

	/* First descriptor length
	 */
	length = min_t(unsigned int, skb_head_len, WQ_ENET_MAX_DESC_LEN);

	/* For TSO we need all headers (L2-L4) to be in the 1st descriptor
	 * (ie, linear area)
	 */
	if (unlikely(length < header_length)) {
		err = -EINVAL;
		pull_len = header_length - length;
		trace_enic_wq_desc_pull(qp, skb, length, header_length,
					pull_len, mss, offload_mode, vlan_tag);
		if (skb_data_len >= pull_len) {
			if (!__pskb_pull_tail(skb, pull_len)) {
				netdev_warn_once(qp->netdev,
						 "head_len=%u/data_len=%u/length=%u/header_length=%u - pulling %u bytes failed\n",
						 skb_head_len, skb_data_len,
						 length, header_length,
						 pull_len);
				qp->wq.stats.pull_err_api++;
				goto drop;
			}

			netdev_warn_once(qp->netdev,
					 "head_len=%u/data_len=%u/length=%u/header_length=%u - pulled %u bytes\n",
					 skb_head_len, skb_data_len,
					 length, header_length, pull_len);
			qp->wq.stats.pull_ok++;
			skb_head_len = skb_headlen(skb);
			skb_data_len = skb->data_len;
		} else {
			netdev_warn_once(qp->netdev,
					 "head_len=%u/data_len=%u/length=%u/header_length=%u - unable to pull %u bytes\n",
					 skb_head_len, skb_data_len,
					 length, header_length, pull_len);
			qp->wq.stats.pull_err_trunc++;
			goto drop;
		}
	}

	/* sop is 1: buf->dma_addr = dma_map_single
	 * sop is 0: buf->dma_addr = dma_map_page
	 * Used in wq cleanup
	 */
	buf->sop = true;
	dma_addr = dma_map_single(qp->dev, skb->data, skb_head_len,
				  DMA_TO_DEVICE);
	frag = &skb_shinfo(skb)->frags[0];
again:
	if (unlikely(dma_mapping_error(qp->dev, dma_addr))) {
		qp->wq.stats.dma_map_error++;
		goto dma_error;
	}
	offset = 0;
	/* Hw only supports max of WQ_ENET_MAX_DESC_LEN len data per desc.
	 * Post the remaining in next desc.
	 */
	while (skb_head_len) {
		if (!offset) {
			buf->dma_addr = dma_addr;
			buf->len = skb_head_len;
		}
		length = min_t(unsigned int, skb_head_len, WQ_ENET_MAX_DESC_LEN);
		skb_head_len -= length;
		eop = !skb_head_len && !skb_data_len;
		buf->skb = (eop && !tx_tstamp) ? skb : NULL;
		cq_entry = eop;
		enic_enc_wq_desc(&qp->wq.wq_base[buf->index],
				 dma_addr + offset, length, mss, header_length,
				 offload_mode, eop, cq_entry, vlan_tag);
		trace_enic_wq_desc(qp, buf, dma_addr + offset, length, mss,
				   header_length, offload_mode, eop, cq_entry,
				   vlan_tag);
		atomic_dec(&qp->wq.desc_avail);
		offset += length;
		buf = buf->next;
	}

	if (!skb_data_len) {
		qp->wq.to_use = buf;
		return 0;
	}
	skb_head_len = skb_frag_size(frag);
	skb_data_len -= skb_head_len;
	dma_addr = skb_frag_dma_map(qp->dev, frag, 0, skb_head_len,
				    DMA_TO_DEVICE);
	frag++;
	goto again;

dma_error:
	err = -ENOMEM;
	qp->wq.to_use = backup;
	while (backup != buf) {
		if (backup->sop)
			dma_unmap_single(qp->dev, backup->dma_addr,
					 backup->len, DMA_TO_DEVICE);
		else if (backup->dma_addr)
			dma_unmap_page(qp->dev, backup->dma_addr,
				       backup->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(backup->skb);
		backup->skb = NULL;
		backup->dma_addr = 0;
		atomic_inc(&qp->wq.desc_avail);
		backup = backup->next;
	}
drop:
	dev_kfree_skb_any(skb);

	return err;
}

/* netif_tx_lock held, process context with BHs disabled, or BH */
netdev_tx_t enic_hard_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int wq_desc_avail;
	struct enic_qp *qp;
	unsigned int txq_map;
	struct netdev_queue *txq;

	txq_map = skb_get_queue_mapping(skb) % enic->wq_count;
	qp = &enic->qp[txq_map];
	txq = netdev_get_tx_queue(netdev, txq_map);

	if (skb->len <= 0) {
		dev_kfree_skb(skb);
		qp->wq.stats.null_pkt++;
		return NETDEV_TX_OK;
	}

	/* Non-TSO sends must fit within ENIC_NON_TSO_MAX_DESC descs,
	 * which is very likely.  In the off chance it's going to take
	 * more than * ENIC_NON_TSO_MAX_DESC, linearize the skb.
	 */

	if (unlikely(skb_shinfo(skb)->gso_size == 0 &&
		     skb_shinfo(skb)->nr_frags + 1 > ENIC_NON_TSO_MAX_DESC &&
		     skb_linearize(skb))) {
		dev_kfree_skb(skb);
		qp->wq.stats.skb_linear_fail++;
		return NETDEV_TX_OK;
	}

	wq_desc_avail = enic_wq_desc_avail(qp);
	if (unlikely(wq_desc_avail <
		     skb_shinfo(skb)->nr_frags + ENIC_DESC_MAX_SPLITS)) {
		netif_tx_stop_queue(txq);
		trace_enic_tx_stop_queue(qp, wq_desc_avail);
		/* This is a hard error, log it */
		netdev_err(netdev, "BUG! Tx ring full when queue awake!\n");
		qp->wq.stats.desc_full_awake++;
		return NETDEV_TX_BUSY;
	}

	if (enic_post_xmit_skb(qp, skb))
		return NETDEV_TX_OK;

	wq_desc_avail = enic_wq_desc_avail(qp);
	if (wq_desc_avail < MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS) {
		netif_tx_stop_queue(txq);
		trace_enic_tx_stop_queue(qp, wq_desc_avail);
		qp->wq.stats.stopped++;
	}
#if (ENIC_HAVE_SKB_TX_TIMESTAMP)
	skb_tx_timestamp(skb);
#endif
	enic_qp_doorbell(qp);
	trace_enic_wq_posted_index(qp);

	qp->wq.stats.packets++;
	qp->wq.stats.bytes += skb->len;

	return NETDEV_TX_OK;
}

