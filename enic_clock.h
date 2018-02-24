/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright 2008-2019 Cisco Systems, Inc.  All rights reserved.
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

#ifndef _ENIC_CLOCK_H_
#define _ENIC_CLOCK_H_

#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <linux/clocksource.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/seqlock.h>

#define ENIC_PTP_CLOCK_UNIT		4000000
#define ENIC_PTP_CLOCK_UNIT_RANGE	8000000
#define ENIC_PTP_CLOCK_BASE		0xff
#define ENIC_PTP_CLOCK_SEC		0x1fffffff
#define ENIC_TSTAMP_WRAP_SEC	(4)
#define ENIC_CLOCK_FREQ_HZ	(500 * 1000 * 1000)
#define ENIC_PTP_CLOCK_MASK	GENMASK_ULL(35, 0)
#define ENIC_PTP_CC_SHIFT	29

struct enic;
struct enic_qp;

struct vnic_ptp_ctrl {
	u32 ptp_clock_unit;
	u32 ptp_clock_unit_range;
	u32 ptp_clock_base;
	u32 ptp_clock_sec;
	u32 ptp_clock_stop;
	u32 ptp_adj_ns;
	u32 ptp_adj_sec;
	u32 pad;
	u32 ptp_clock_lo;
	u32 ptp_clock_hi;
} __attribute__ ((__packed__));

struct enic_hwtstamp {
	struct ptp_clock *ptp;
	seqlock_t lock;
	struct timecounter clock;
	struct hwtstamp_config tsconfig;
	struct ptp_clock_info ptp_info;
	u64 nominal_c_mult;
	struct cyclecounter cc;
	struct delayed_work work;
	u64 freq; /* In Hz */
};

enum enic_trace_queue {
	TRACE_WQ,
	TRACE_RQ,
};

int enic_set_hwtstamp(struct net_device *netdev, struct ifreq *ifr);
void enic_clock_init(struct enic *enic);
void enic_clock_cleanup(struct enic *enic);
void enic_fill_hwtstamp(struct enic_qp *qp, struct enic_hwtstamp *tstamp,
			u64 cycles, struct skb_shared_hwtstamps *hwts,
			enum enic_trace_queue queue);
int enic_get_hwtstamp(struct net_device *netdev, struct ifreq *ifr);

#endif /* _ENIC_CLOCK_H_ */
