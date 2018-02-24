/*
* Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
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
#ident "$Id$"

#ifndef _VNIC_RESOURCE_H_
#define _VNIC_RESOURCE_H_

#define VNIC_RES_MAGIC		0x766E6963L	/* 'vnic' */
#define VNIC_RES_VERSION	0x00000000L
#define MGMTVNIC_MAGIC		0x544d474dL	/* 'MGMT' */
#define MGMTVNIC_VERSION	0x00000000L

/* The MAC address assigned to the CFG vNIC is fixed. */
#define MGMTVNIC_MAC		{ 0x02, 0x00, 0x54, 0x4d, 0x47, 0x4d }

/* vNIC resource types */
enum vnic_res_type {
	RES_TYPE_EOL,			/* End-of-list */
	RES_TYPE_WQ,			/* Work queues */
	RES_TYPE_RQ,			/* Receive queues */
	RES_TYPE_CQ,			/* Completion queues */
	RES_TYPE_MEM,			/* Window to dev memory */
	RES_TYPE_NIC_CFG,		/* Enet NIC config registers */
	RES_TYPE_RSS_KEY,		/* Enet RSS secret key */
	RES_TYPE_RSS_CPU,		/* Enet RSS indirection table */
	RES_TYPE_TX_STATS,		/* Netblock Tx statistic regs */
	RES_TYPE_RX_STATS,		/* Netblock Rx statistic regs */
	RES_TYPE_INTR_CTRL,		/* Interrupt ctrl table */
	RES_TYPE_INTR_TABLE,		/* MSI/MSI-X Interrupt table */
	RES_TYPE_INTR_PBA,		/* MSI/MSI-X PBA table */
	RES_TYPE_INTR_PBA_LEGACY,	/* Legacy intr status */
	RES_TYPE_DEBUG,			/* Debug-only info */
	RES_TYPE_DEV,			/* Device-specific region */
	RES_TYPE_DEVCMD,		/* Device command region */
	RES_TYPE_PASS_THRU_PAGE,	/* Pass-thru page */
	RES_TYPE_SUBVNIC,		/* subvnic resource type */
	RES_TYPE_MQ_WQ,			/* MQ Work queues */
	RES_TYPE_MQ_RQ,			/* MQ Receive queues */
	RES_TYPE_MQ_CQ,			/* MQ Completion queues */
	RES_TYPE_DEPRECATED1,		/* Old version of devcmd 2 */
	RES_TYPE_DEPRECATED2,		/* Old version of devcmd 2 */
	RES_TYPE_DEVCMD2,		/* Device control region */
	RES_TYPE_RDMA_WQ,		/* RDMA WQ */
	RES_TYPE_RDMA_RQ,		/* RDMA RQ */
	RES_TYPE_RDMA_CQ,		/* RDMA CQ */
	RES_TYPE_RDMA_RKEY_TABLE,	/* RDMA RKEY table */
	RES_TYPE_RDMA_RQ_HEADER_TABLE,	/* RDMA RQ Header Table */
	RES_TYPE_RDMA_RQ_TABLE,		/* RDMA RQ Table */
	RES_TYPE_RDMA_RD_RESP_HEADER_TABLE,	/* RDMA Read Response Header Table */
	RES_TYPE_RDMA_RD_RESP_TABLE,	/* RDMA Read Response Table */
	RES_TYPE_RDMA_QP_STATS_TABLE,	/* RDMA per QP stats table */
	RES_TYPE_WQ_MREGS,		/* XXX snic proto only */
	RES_TYPE_GRPMBR_INTR,		/* Group member interrupt control */
	RES_TYPE_DPKT,			/* Direct Packet memory region */
	RES_TYPE_RDMA2_DATA_WQ,		/* RDMA datapath command WQ */
	RES_TYPE_RDMA2_REG_WQ,		/* RDMA registration command WQ */
	RES_TYPE_RDMA2_CQ,		/* RDMA datapath CQ */
	RES_TYPE_MQ_RDMA2_DATA_WQ,	/* RDMA datapath command WQ */
	RES_TYPE_MQ_RDMA2_REG_WQ,	/* RDMA registration command WQ */
	RES_TYPE_MQ_RDMA2_CQ,		/* RDMA datapath CQ */
	RES_TYPE_PTP,                   /* PTP registers */
	RES_TYPE_INTR_CTRL2,            /* Extended INTR CTRL registers */

	RES_TYPE_MAX,			/* Count of resource types */
};

struct vnic_resource_header {
	u32 magic;
	u32 version;
};

struct mgmt_barmap_hdr {
	u32 magic;			/* magic number */
	u32 version;			/* header format version */
	u16 lif;			/* loopback lif for mgmt frames */
	u16 pci_slot;			/* installed pci slot */
	char serial[16];		/* card serial number */
};

struct vnic_resource {
	u8 type;
	u8 bar;
	u8 pad[2];
	u32 bar_offset;
	u32 count;
};

#endif /* _VNIC_RESOURCE_H_ */
