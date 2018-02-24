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

#ifndef _ENIC_RES_H_
#define _ENIC_RES_H_

#include <linux/skbuff.h>

#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "vnic_wq.h"
#include "vnic_rq.h"

#define ENIC_MIN_WQ_DESCS		64
#define ENIC_MAX_WQ_DESCS_DEFAULT	4096
#define ENIC_MAX_WQ_DESCS		16384
#define ENIC_MIN_RQ_DESCS		64
#define ENIC_MAX_RQ_DESCS		16384
#define ENIC_MAX_RQ_DESCS_DEFAULT	4096
#define ENIC_MAX_CQ_DESCS_DEFAULT	(64 * 1024)

#ifndef ETH_MIN_MTU
#define ENIC_MIN_MTU			68
#else
#define ENIC_MIN_MTU			ETH_MIN_MTU
#endif
#define ENIC_PAGE_ORDER			0
#define ENIC_MAX_MTU			9000
#define ENIC_MAX_PAGES			((ENIC_MAX_MTU + VLAN_ETH_HLEN + \
					+ VLAN_HLEN + PAGE_SIZE - 1) / PAGE_SIZE)

/* Divisor of the RQ ring used to determine the number of descriptors to hold
 * before updating posted index to the VIC (1500 series and beyond only).
 * Assuming 8K RQ rings, the VIC could have up to 8K/8 = 1024 of buffers
 * unavailable. If a line rate burst of IMIX traffic on 1 empty RQ arrives,
 * host has (8K - 1K) * 32.3ns/packet = 231us of processing time before
 * flow control or packet drops vs 264us if the posted index is updated every
 * 16 buffers. Posting consumes PCI and VIC resources and posting less often
 * is worth the reduction in host processing time.
 */
#define ENIC_RQ_FREE_THRESH_DIV		8
#define ENIC_MIN_RQ_FREE_THRESH		16

#define ENIC_MULTICAST_PERFECT_FILTERS	32
#define ENIC_UNICAST_PERFECT_FILTERS	32

#define ENIC_NON_TSO_MAX_DESC		16

#define ENIC_SETTING(enic, f) ((enic->config.flags & VENETF_##f) ? 1 : 0)

struct enic;

int enic_get_vnic_config(struct enic *);
int enic_add_vlan(struct enic *enic, u16 vlanid);
int enic_del_vlan(struct enic *enic, u16 vlanid);
int enic_set_nic_cfg(struct enic *enic, u8 rss_default_cpu, u8 rss_hash_type,
		     u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable,
		     u8 tso_ipid_split_en, u8 ig_vlan_strip_en);
int enic_set_rss_key(struct enic *enic, dma_addr_t key_pa, u64 len);
int enic_set_rss_cpu(struct enic *enic, dma_addr_t cpu_pa, u64 len);
void enic_get_res_counts(struct enic *enic);
void enic_print_resources(struct enic *enic);

#endif /* _ENIC_RES_H_ */
