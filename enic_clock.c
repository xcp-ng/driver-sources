// SPDX-License-Identifier: GPL-2.0

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

#include <linux/skbuff.h>

#include "kcompat.h"
#include "enic.h"
#include "enic_clock.h"
#include "enic_trace.h"

int enic_set_hwtstamp(struct net_device *netdev, struct ifreq *ifr)
{
	struct enic *enic = netdev_priv(netdev);
	struct hwtstamp_config config;

	if (!enic->tstamp.ptp)
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags)
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -EINVAL;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		config.rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	}
	memcpy(&enic->tstamp.tsconfig, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
			-EFAULT : 0;
}

int enic_get_hwtstamp(struct net_device *netdev, struct ifreq *ifr)
{
	struct enic *enic = netdev_priv(netdev);
	struct hwtstamp_config *cfg = &enic->tstamp.tsconfig;

	if (!enic->tstamp.ptp)
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ?  -EFAULT : 0;
}

void enic_fill_hwtstamp(struct enic_qp *qp, struct enic_hwtstamp *tstamp,
			u64 cycles, struct skb_shared_hwtstamps *hwts,
			enum enic_trace_queue queue)
{
	unsigned int seq;
	u64 nsec;

	if (!tstamp->ptp ||
	    tstamp->tsconfig.rx_filter == HWTSTAMP_FILTER_NONE)
		return;

	do {
		seq = read_seqbegin(&tstamp->lock);
		nsec = timecounter_cyc2time(&tstamp->clock, cycles);
	} while (read_seqretry(&tstamp->lock, seq));

	hwts->hwtstamp = ns_to_ktime(nsec);
	trace_enic_fill_hwtstamp(qp, cycles, nsec, queue);
}

static u64 enic_read_cycles(const struct cyclecounter *cc)
{
	struct enic_hwtstamp *tstamp;
	struct enic *enic;
	u64 clock;

	tstamp = container_of(cc, struct enic_hwtstamp, cc);
	enic = container_of(tstamp, struct enic, tstamp);

	clock = ioread32(&enic->ptp_ctrl->ptp_clock_hi);
	clock <<= 32;
	clock |= ioread32(&enic->ptp_ctrl->ptp_clock_lo);
	clock >>= 3;

	trace_enic_read_cycles(enic, clock, clock & cc->mask);
	return clock & cc->mask;
}

static void enic_cycle_overflow(struct work_struct *work)
{
	struct enic_hwtstamp *tstamp;
	struct delayed_work *dwork;
	struct enic *enic;
	u64 nsec;

	dwork = to_delayed_work(work);
	tstamp = container_of(dwork, struct enic_hwtstamp, work);
	enic = container_of(tstamp, struct enic, tstamp);
	write_seqlock_bh(&tstamp->lock);
	nsec = timecounter_read(&tstamp->clock);
	write_sequnlock_bh(&tstamp->lock);
	schedule_delayed_work(&tstamp->work, ENIC_TSTAMP_WRAP_SEC * HZ);
}

static int enic_ptp_settime(struct ptp_clock_info *ptp_info,
			    const struct timespec64 *ts)
{
	struct enic_hwtstamp *tstamp;
	struct enic *enic;
	u64 ns;

	ns = timespec64_to_ns(ts);
	tstamp = container_of(ptp_info, struct enic_hwtstamp, ptp_info);
	enic = container_of(tstamp, struct enic, tstamp);

	write_seqlock_bh(&tstamp->lock);
	timecounter_init(&tstamp->clock, &tstamp->cc, ns);
	write_sequnlock_bh(&tstamp->lock);

	return 0;
}

static int enic_ptp_gettime(struct ptp_clock_info *ptp_info,
			    struct timespec64 *ts)
{
	struct enic_hwtstamp *tstamp;
	struct enic *enic;
	u64 ns;

	tstamp = container_of(ptp_info, struct enic_hwtstamp, ptp_info);
	enic = container_of(tstamp, struct enic, tstamp);

	write_seqlock_bh(&tstamp->lock);
	ns = timecounter_read(&tstamp->clock);
	write_sequnlock_bh(&tstamp->lock);
	*ts = ns_to_timespec64(ns);
	trace_enic_ptp_gettime(enic, ns);

	return 0;
}

static int enic_ptp_adjtime(struct ptp_clock_info *ptp_info, s64 delta)
{
	struct enic_hwtstamp *tstamp;
	struct enic *enic;

	tstamp = container_of(ptp_info, struct enic_hwtstamp, ptp_info);
	enic = container_of(tstamp, struct enic, tstamp);

	write_seqlock_bh(&tstamp->lock);
	timecounter_adjtime(&tstamp->clock, delta);
	write_sequnlock_bh(&tstamp->lock);
	trace_enic_ptp_adjtime(enic, delta);

	return 0;
}

static int enic_ptp_adjfreq(struct ptp_clock_info *ptp_info, s32 delta)
{
	struct enic_hwtstamp *tstamp;
	struct enic *enic;
	u32 abs_delta;
	u64 adj;
	u64 mult;
	u32 diff;

	tstamp = container_of(ptp_info, struct enic_hwtstamp, ptp_info);
	enic = container_of(tstamp, struct enic, tstamp);
	abs_delta = abs(delta);
	mult = tstamp->nominal_c_mult;
	adj = mult;
	adj *= abs_delta;
	diff = div_u64(adj, 1000000000ULL);
	if (delta < 0)
		mult -= diff;
	else
		mult += diff;
	write_seqlock_bh(&tstamp->lock);
	timecounter_read(&tstamp->clock);
	tstamp->cc.mult = mult;
	write_sequnlock_bh(&tstamp->lock);
	trace_enic_ptp_adjfreq(enic, mult);

	return 0;
}

void enic_clock_init(struct enic *enic)
{
	struct enic_hwtstamp *tstamp = &enic->tstamp;
	u64 vic_feature_ptp_ver;
	int err;
	u64 a1 = 0;

	enic->ptp_ctrl = vnic_dev_get_res(enic->vdev, RES_TYPE_PTP, 0);
	seqlock_init(&tstamp->lock);

	if (!enic->ptp_ctrl)
		return;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_get_supported_feature_ver(enic->vdev, VIC_FEATURE_PTP,
						 &vic_feature_ptp_ver, &a1);
	spin_unlock_bh(&enic->devcmd_lock);
	if (err) {
		netdev_info(enic->netdev, "Could not get PTP VIC version.");
		return;
	}

	if (vic_feature_ptp_ver != 1) {
		netdev_info(enic->netdev, "Driver does not support VIC PTP ver %llu.",
			    vic_feature_ptp_ver);
		return;
	}

	iowrite32(ENIC_PTP_CLOCK_UNIT, &enic->ptp_ctrl->ptp_clock_unit);
	iowrite32(ENIC_PTP_CLOCK_UNIT_RANGE,
		  &enic->ptp_ctrl->ptp_clock_unit_range);
	iowrite32(ENIC_PTP_CLOCK_BASE, &enic->ptp_ctrl->ptp_clock_base);
	iowrite32(ENIC_PTP_CLOCK_SEC, &enic->ptp_ctrl->ptp_clock_sec);

	tstamp->freq = ENIC_CLOCK_FREQ_HZ;
	tstamp->tsconfig.tx_type = HWTSTAMP_TX_ON;
	tstamp->tsconfig.rx_filter = HWTSTAMP_FILTER_ALL;
	tstamp->cc.mask = ENIC_PTP_CLOCK_MASK;
	tstamp->cc.read = enic_read_cycles;
	tstamp->cc.shift = ENIC_PTP_CC_SHIFT;
	tstamp->cc.mult = clocksource_hz2mult(tstamp->freq, tstamp->cc.shift);
	tstamp->nominal_c_mult = tstamp->cc.mult;
	timecounter_init(&tstamp->clock, &tstamp->cc,
			 ktime_to_ns(ktime_get_real()));

	tstamp->ptp_info.owner = THIS_MODULE;
	tstamp->ptp_info.gettime64 = enic_ptp_gettime;
	tstamp->ptp_info.settime64 = enic_ptp_settime;
	tstamp->ptp_info.adjtime = enic_ptp_adjtime;
	tstamp->ptp_info.adjfreq = enic_ptp_adjfreq;
	tstamp->ptp_info.max_adj = 1000000000;
	snprintf(tstamp->ptp_info.name, 16, "%s-ptp", enic->netdev->name);
	tstamp->ptp = ptp_clock_register(&tstamp->ptp_info, enic_get_dev(enic));
	if (IS_ERR_OR_NULL(tstamp->ptp)) {
		netdev_warn(enic->netdev, "ptp_clock_reg failed %ld",
			    PTR_ERR(tstamp->ptp));
		tstamp->ptp = NULL;
	} else {
		netdev_info(enic->netdev, "%s: ptp %s registered.", __func__,
			    tstamp->ptp_info.name);
	}

	INIT_DELAYED_WORK(&tstamp->work, enic_cycle_overflow);
	schedule_delayed_work(&tstamp->work, 0);
}

void enic_clock_cleanup(struct enic *enic)
{
	struct enic_hwtstamp *tstamp = &enic->tstamp;

	if (!enic->ptp_ctrl)
		return;

	cancel_delayed_work_sync(&tstamp->work);
	ptp_clock_unregister(tstamp->ptp);
	tstamp->ptp = NULL;
}

