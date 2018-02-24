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

#ifndef _QED_PTP_IF_H
#define _QED_PTP_IF_H
#include <linux/types.h>
/**
 * @file
 *
 * @brief QED_PTP
 *
 */
#ifdef CONFIG_QED_PTP
#include "qed_ptp_api.h"
#else
struct qed_hwfn;
static inline int qed_ptp_enable_pkt2host(struct qed_hwfn *hwfn,
					  struct qed_ptt *p_ptt)
{return -EPERM;}
static inline int qed_ptp_hwtstamp_tx_on(struct qed_hwfn *hwfn,
					 struct qed_ptt *p_ptt)
{return -EPERM;}
static inline int qed_ptp_cfg_rx_filters(struct qed_hwfn *hwfn,
					 struct qed_ptt *p_ptt,
					 enum qed_ptp_filter_type type)
{return -EPERM;}
static inline int qed_ptp_read_rx_ts(struct qed_hwfn *hwfn,
				     struct qed_ptt *p_ptt, u64 *timestamp)
{return -EPERM;}
static inline int qed_ptp_read_tx_ts(struct qed_hwfn *hwfn,
				     struct qed_ptt *p_ptt, u64 *timestamp)
{return -EPERM;}
static inline int qed_ptp_read_cc(struct qed_hwfn *hwfn,
				  struct qed_ptt *p_ptt, u64 *cycles)
{return -EPERM;}
static inline int qed_ptp_disable(struct qed_hwfn *hwfn, struct qed_ptt *p_ptt)
{return -EPERM;}
static inline int qed_ptp_adjfreq(struct qed_hwfn *hwfn, struct qed_ptt *p_ptt,
				  s32 ppb)
{return -EPERM;}
static inline int qed_ptp_enable(struct qed_hwfn *hwfn, struct qed_ptt *p_ptt)
{return -EPERM;}
#endif /* End CONFIG_QED_PTP */

#endif /* End  _QED_PTP_IF_H */
