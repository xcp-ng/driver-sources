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

#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include "vnic_dev.h"
#include "vnic_devcmd.h"

#include "kcompat.h"
#include "enic.h"
#include "enic_api.h"
#include "enic_dev.h"

int enic_api_devcmd_proxy_by_index(struct net_device *netdev, int vf,
    int cmd, u64 *args, int nargs, int wait)
{
    int err;
    struct enic *enic = netdev_priv(netdev);
    struct vnic_dev *vdev = enic->vdev;

    spin_lock(&enic->enic_api_lock);
    spin_lock_bh(&enic->devcmd_lock);

    vnic_dev_cmd_proxy_by_index_start(vdev, vf);
    err = vnic_dev_cmd_args(vdev, (enum vnic_devcmd_cmd)cmd, args, nargs, wait);
    vnic_dev_cmd_proxy_end(vdev);

    spin_unlock_bh(&enic->devcmd_lock);
    spin_unlock(&enic->enic_api_lock);

    return err;
}
EXPORT_SYMBOL(enic_api_devcmd_proxy_by_index);

int enic_api_devcmd(struct net_device *netdev, int cmd,
		u64 *args, int nargs, int wait)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;
	int err;

	spin_lock(&enic->enic_api_lock);
	spin_lock_bh(&enic->devcmd_lock);

	err = vnic_dev_cmd_args(vdev, (enum vnic_devcmd_cmd)cmd,
				args, nargs, wait);

	spin_unlock_bh(&enic->devcmd_lock);
	spin_unlock(&enic->enic_api_lock);

	return enic_dev_status_to_errno(err);
}
EXPORT_SYMBOL(enic_api_devcmd);

static inline struct vnic_intr *to_vnic_intr(struct enic *enic, int intr)
{
	return &enic->intr[intr];
}

int enic_api_get_irqvec(struct net_device *netdev, int intr)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;

	vintr = to_vnic_intr(enic, intr);

	return enic->msix_entry[vintr->index].vector;
}
EXPORT_SYMBOL(enic_api_get_irqvec);

int enic_api_reserve_intr(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;
	int intr;
	int ret;

	mutex_lock(&enic->intr_map_lock);

	if (bitmap_full(enic->intr_map, enic->total_intr_count)) {
		mutex_unlock(&enic->intr_map_lock);
		return -ENOSPC;
	}

	intr = bitmap_find_next_zero_area(enic->intr_map,
					  enic->total_intr_count,
					  0, 1, 0);
	bitmap_set(enic->intr_map, intr, 1);
	vintr = to_vnic_intr(enic, intr);

	ret = vnic_intr_alloc(enic->vdev, vintr, intr);
	if (ret < 0) {
		bitmap_clear(enic->intr_map, intr, 1);
		mutex_unlock(&enic->intr_map_lock);
		return ret;
	}

	mutex_unlock(&enic->intr_map_lock);

	return intr;
}
EXPORT_SYMBOL(enic_api_reserve_intr);

int enic_api_release_intr(struct net_device *netdev, int intr)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;

	if (intr < 0 || intr >= enic->total_intr_count)
		return -EINVAL;

	vintr = to_vnic_intr(enic, intr);
	vnic_intr_free(vintr);

	mutex_lock(&enic->intr_map_lock);
	bitmap_clear(enic->intr_map, vintr->index, 1);
	mutex_unlock(&enic->intr_map_lock);

	return 0;
}
EXPORT_SYMBOL(enic_api_release_intr);

int enic_api_init_intr(struct net_device *netdev, int intr,
		       u32 coalescing_timer, unsigned int coalescing_type,
		       unsigned int mask_on_assertion)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;

	if (intr < 0 || intr >= enic->total_intr_count)
		return -EINVAL;

	vintr = to_vnic_intr(enic, intr);

	vnic_intr_init(vintr, coalescing_timer, coalescing_type,
		       mask_on_assertion);

	return 0;
}
EXPORT_SYMBOL(enic_api_init_intr);

int enic_api_mask_intr(struct net_device *netdev, int intr)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;

	if (intr < 0 || intr >= enic->total_intr_count)
		return -EINVAL;

	vintr = to_vnic_intr(enic, intr);
	vnic_intr_mask(vintr);

	return 0;
}
EXPORT_SYMBOL(enic_api_mask_intr);

int enic_api_unmask_intr(struct net_device *netdev, int intr)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_intr *vintr;

	if (intr < 0 || intr >= enic->total_intr_count)
		return -EINVAL;

	vintr = to_vnic_intr(enic, intr);
	vnic_intr_unmask(vintr);

	return 0;
}
EXPORT_SYMBOL(enic_api_unmask_intr);

int enic_api_get_devspec(struct net_device *netdev,
			 const struct vnic_enet_config **devspec)
{
	struct enic *enic = netdev_priv(netdev);

	if (!devspec)
		return -EINVAL;

	*devspec = &enic->config;

	return 0;
}
EXPORT_SYMBOL(enic_api_get_devspec);

int enic_api_alloc_ring(struct net_device *netdev,
			struct vnic_dev_ring *ring,
			unsigned int num_desc,
			unsigned int desc_size)
{
	struct enic *enic = netdev_priv(netdev);

	return vnic_dev_alloc_desc_ring(enic->vdev, ring,
					num_desc, desc_size);
}
EXPORT_SYMBOL(enic_api_alloc_ring);

void enic_api_free_ring(struct net_device *netdev,
			struct vnic_dev_ring *ring)
{
	struct enic *enic = netdev_priv(netdev);

	vnic_dev_free_desc_ring(enic->vdev, ring);
}
EXPORT_SYMBOL(enic_api_free_ring);

int enic_api_alloc_wq(struct net_device *netdev, struct vnic_wq *wq,
		      unsigned int index, unsigned int desc_cnt,
		      unsigned int desc_size, enum enic_wq_type type)
{
	struct enic *enic = netdev_priv(netdev);

	switch (type) {
	case ENIC_WQ:
		return vnic_wq_alloc(enic->vdev, wq, index,
				     desc_cnt, desc_size);
		break;
	case ENIC_RDMA_DATA_WQ:
		return vnic_rdma2_data_wq_alloc(enic->vdev, wq,
					        index, desc_cnt,
					        desc_size);
		break;
	case ENIC_RDMA_REG_WQ:
		return vnic_rdma2_reg_wq_alloc(enic->vdev, wq,
					       index, desc_cnt,
					       desc_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(enic_api_alloc_wq);

void enic_api_free_wq(struct vnic_wq *wq)
{
	vnic_wq_free(wq);
}
EXPORT_SYMBOL(enic_api_free_wq);

unsigned int enic_api_wq_error_status(struct vnic_wq *wq)
{
	return vnic_wq_error_status(wq);
}
EXPORT_SYMBOL(enic_api_wq_error_status);

void enic_api_enable_wq(struct vnic_wq *wq)
{
	vnic_wq_enable(wq);
}
EXPORT_SYMBOL(enic_api_enable_wq);

void enic_api_disable_wq(struct vnic_wq *wq)
{
	vnic_wq_disable(wq);
}
EXPORT_SYMBOL(enic_api_disable_wq);

void enic_api_init_wq(struct vnic_wq *wq, unsigned int cq_index,
		      unsigned int error_intr_enable,
		      unsigned int error_intr_offset)
{
	vnic_wq_init(wq, cq_index, error_intr_enable, error_intr_offset);
}
EXPORT_SYMBOL(enic_api_init_wq);

int enic_api_alloc_cq(struct net_device *netdev, struct vnic_cq *cq,
		      unsigned int index, unsigned int desc_cnt,
		      unsigned int desc_size, enum enic_cq_type type)
{
	struct enic *enic = netdev_priv(netdev);

	switch (type) {
	case ENIC_CQ:
		return vnic_cq_alloc(enic->vdev, cq, index,
				     desc_cnt, desc_size);
		break;
	case ENIC_RDMA_DATA_CQ:
		return vnic_rdma2_cq_alloc(enic->vdev, cq, index,
					   desc_cnt, desc_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(enic_api_alloc_cq);

void enic_api_clean_cq(struct vnic_cq *cq)
{
	vnic_cq_clean(cq);
}
EXPORT_SYMBOL(enic_api_clean_cq);

void enic_api_free_cq(struct vnic_cq *cq)
{
	vnic_cq_free(cq);
}
EXPORT_SYMBOL(enic_api_free_cq);

void enic_api_init_cq(struct vnic_cq *cq, unsigned int flow_control_enable,
		      unsigned int color_enable, unsigned int cq_head,
		      unsigned int cq_tail, unsigned int cq_tail_color,
		      unsigned int interrupt_enable,
		      unsigned int cq_entry_enable,
		      unsigned int cq_message_enable,
		      unsigned int interrupt_offset,
		      u64 cq_message_addr)
{
	vnic_cq_init(cq, flow_control_enable, color_enable, cq_head,
		     cq_tail, cq_tail_color, interrupt_enable, cq_entry_enable,
		     cq_message_enable, interrupt_offset, cq_message_addr);
}
EXPORT_SYMBOL(enic_api_init_cq);

int enic_api_get_rdma_cap(struct net_device *netdev, u64 *cap, u64 *features)
{
	struct enic *enic = netdev_priv(netdev);

	*cap = enic->rdma_cap;
	*features = enic->rdma_features;

	return 0;
}
EXPORT_SYMBOL(enic_api_get_rdma_cap);

dma_addr_t enic_api_get_res_bus_addr(struct net_device *netdev,
					enum vnic_res_type res_type)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;

	return vnic_dev_get_res_bus_addr(vdev, res_type, 0);
}
EXPORT_SYMBOL(enic_api_get_res_bus_addr);

unsigned long enic_api_get_res_len(struct net_device *netdev,
					enum vnic_res_type res_type)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;

	return vnic_dev_get_res_type_len(vdev, res_type);
}
EXPORT_SYMBOL(enic_api_get_res_len);
