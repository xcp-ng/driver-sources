/*
 * Copyright 2008-2013 Cisco Systems, Inc.  All rights reserved.
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
#ifndef __ENIC_API_H__
#define __ENIC_API_H__

#include <linux/netdevice.h>

#include "vnic_dev.h"
#include "vnic_devcmd.h"
#include "vnic_enet.h"
#include "vnic_cq.h"
#include "vnic_wq.h"
#include "wq_enet_desc.h"

#define ENIC_RDMA_V3_ENABLED	MK_RDMA_FW_VER(RDMA_FW_VER_3)
#define ENIC_RDMA_V4_ENABLED	MK_RDMA_FW_VER(RDMA_FW_VER_4)

extern struct bus_type enic_rdma_bus;

enum enic_wq_type {
	ENIC_WQ,
	ENIC_RDMA_DATA_WQ,
	ENIC_RDMA_REG_WQ
};

enum enic_cq_type {
	ENIC_CQ,
	ENIC_RDMA_DATA_CQ
};

int enic_api_devcmd_proxy_by_index(struct net_device *netdev, int vf,
					int cmd, u64 *args, int nargs, int wait);
int enic_api_devcmd(struct net_device *netdev, int cmd,
		    u64 *args, int nargs, int wait);
int enic_api_reserve_intr(struct net_device *netdev);
int enic_api_release_intr(struct net_device *netdev, int intr);
int enic_api_init_intr(struct net_device *netdev, int intr,
		       u32 coalescing_timer, unsigned int coalescing_type,
		       unsigned int mask_on_assertion);
int enic_api_mask_intr(struct net_device *netdev, int intr);
int enic_api_unmask_intr(struct net_device *netdev, int intr);
int enic_api_get_irqvec(struct net_device *netdev, int intr);
int enic_api_get_devspec(struct net_device *netdev,
			 const struct vnic_enet_config **devspec);
int enic_api_alloc_ring(struct net_device *netdev, struct vnic_dev_ring *ring,
			unsigned int num_desc, unsigned int desc_size);
void enic_api_free_ring(struct net_device *netdev, struct vnic_dev_ring *ring);
int enic_api_alloc_wq(struct net_device *netdev, struct vnic_wq *wq,
		      unsigned int index, unsigned int num_desc,
		      unsigned int desc_size, enum enic_wq_type type);
void enic_api_free_wq(struct vnic_wq *wq);
unsigned int enic_api_wq_error_status(struct vnic_wq *wq);
void enic_api_enable_wq(struct vnic_wq *wq);
void enic_api_disable_wq(struct vnic_wq *wq);
void enic_api_init_wq(struct vnic_wq *wq, unsigned int cq_index,
		      unsigned int error_intr_enable,
		      unsigned int error_intr_offset);
int enic_api_alloc_cq(struct net_device *netdev, struct vnic_cq *cq,
		      unsigned int index, unsigned int num_desc,
		      unsigned int desc_size, enum enic_cq_type type);
void enic_api_free_cq(struct vnic_cq *cq);
void enic_api_init_cq(struct vnic_cq *cq, unsigned int flow_control_enable,
		      unsigned int color_enable, unsigned int cq_head,
		      unsigned int cq_tail, unsigned int cq_tail_color,
		      unsigned int interrupt_enable,
		      unsigned int cq_entry_enable,
		      unsigned int cq_message_enable,
		      unsigned int interrupt_offset,
		      u64 cq_message_addr);
void enic_api_clean_cq(struct vnic_cq *cq);
int enic_api_get_rdma_cap(struct net_device *netdev, u64 *cap, u64 *features);
dma_addr_t enic_api_get_res_bus_addr(struct net_device *netdev,
			enum vnic_res_type res_type);
unsigned long enic_api_get_res_len(struct net_device *netdev,
			enum vnic_res_type res_type);
#endif
