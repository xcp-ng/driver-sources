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

#include <rdma/ib_verbs.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include "qedr_compat.h"

#if DEFINE_IB_DEV_OPS
/* This function is copied from kernel */
void ib_set_device_ops(struct ib_device *dev, const struct ib_device_ops *ops)
{
	struct ib_device *dev_ops = dev; /* compat line */

#define SET_DEVICE_OP(ptr, name)                                               \
	do {                                                                   \
		if (ops->name)                                                 \
			if (!((ptr)->name))				       \
				(ptr)->name = ops->name;                       \
	} while (0)
#ifndef REMOVE_DEVICE_ADD_DEL_GID /* !QEDR_UPSTREAM */
#if DEFINE_ROCE_GID_TABLE
	SET_DEVICE_OP(dev_ops, add_gid);
#endif
#endif
	SET_DEVICE_OP(dev_ops, alloc_fmr);
#ifdef DEFINE_ALLOC_MR 
	SET_DEVICE_OP(dev_ops, alloc_mr);
#endif
#ifdef _HAS_MW_SUPPORT
	SET_DEVICE_OP(dev_ops, alloc_mw);
#endif
	SET_DEVICE_OP(dev_ops, alloc_pd);
	SET_DEVICE_OP(dev_ops, alloc_ucontext);
#ifdef _HAS_XRC_SUPPORT
	SET_DEVICE_OP(dev_ops, alloc_xrcd);
#endif

	SET_DEVICE_OP(dev_ops, create_ah);
	SET_DEVICE_OP(dev_ops, create_cq);
	SET_DEVICE_OP(dev_ops, create_qp);
	SET_DEVICE_OP(dev_ops, create_srq);
	SET_DEVICE_OP(dev_ops, dealloc_fmr);
#ifdef _HAS_MW_SUPPORT
	SET_DEVICE_OP(dev_ops, dealloc_mw);
#endif
	SET_DEVICE_OP(dev_ops, dealloc_pd);
	SET_DEVICE_OP(dev_ops, dealloc_ucontext);
#ifdef _HAS_XRC_SUPPORT
	SET_DEVICE_OP(dev_ops, dealloc_xrcd);
#endif
#ifndef REMOVE_DEVICE_ADD_DEL_GID /* !QEDR_UPSTREAM */
#if DEFINE_ROCE_GID_TABLE
	SET_DEVICE_OP(dev_ops, del_gid);
#endif
#endif
	SET_DEVICE_OP(dev_ops, dereg_mr);
	SET_DEVICE_OP(dev_ops, destroy_ah);
	SET_DEVICE_OP(dev_ops, destroy_cq);
	SET_DEVICE_OP(dev_ops, destroy_qp);
	SET_DEVICE_OP(dev_ops, destroy_srq);
	
#if DEFINE_GET_DEV_FW_STR /* QEDR_UPSTREAM */
	SET_DEVICE_OP(dev_ops, get_dev_fw_str);
#endif
	SET_DEVICE_OP(dev_ops, get_dma_mr);
	SET_DEVICE_OP(dev_ops, get_link_layer);
#ifdef DEFINE_GET_NETDEV  
	SET_DEVICE_OP(dev_ops, get_netdev);
#endif
#if DEFINE_PORT_IMMUTABLE /* QEDR_UPSTREAM */
	SET_DEVICE_OP(dev_ops, get_port_immutable);
#endif
#ifdef DEFINE_ALLOC_MR 
	SET_DEVICE_OP(dev_ops, map_mr_sg);
#endif
	SET_DEVICE_OP(dev_ops, map_phys_fmr);
	SET_DEVICE_OP(dev_ops, mmap);
	SET_DEVICE_OP(dev_ops, modify_ah);
	SET_DEVICE_OP(dev_ops, modify_port);
	SET_DEVICE_OP(dev_ops, modify_qp);
	SET_DEVICE_OP(dev_ops, modify_srq);
	SET_DEVICE_OP(dev_ops, peek_cq);
	SET_DEVICE_OP(dev_ops, poll_cq);
	SET_DEVICE_OP(dev_ops, post_recv);
	SET_DEVICE_OP(dev_ops, post_send);
	SET_DEVICE_OP(dev_ops, post_srq_recv);
	SET_DEVICE_OP(dev_ops, process_mad);
	SET_DEVICE_OP(dev_ops, query_ah);
	SET_DEVICE_OP(dev_ops, query_device);
	SET_DEVICE_OP(dev_ops, query_gid);
	SET_DEVICE_OP(dev_ops, query_pkey);
	SET_DEVICE_OP(dev_ops, query_port);
	SET_DEVICE_OP(dev_ops, query_qp);
	SET_DEVICE_OP(dev_ops, query_srq);
	SET_DEVICE_OP(dev_ops, reg_user_mr);
	SET_DEVICE_OP(dev_ops, req_ncomp_notif);
	SET_DEVICE_OP(dev_ops, req_notify_cq);
	SET_DEVICE_OP(dev_ops, resize_cq);
	SET_DEVICE_OP(dev_ops, unmap_fmr);
}
#endif
