/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
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
This program is free software; you may redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#ifndef _FNIC_STATS_H_
#define _FNIC_STATS_H_

struct fnic_nvmef_info {
	char *info_buffer;
	void *i_private;
	int buf_size;
	int buffer_len;
};

struct stats_debug_info {
        char *debug_buffer;
        void *i_private;
        int buf_size;
        int buffer_len;
};
struct stats_timestamps{
#if FNIC_HAVE_TIMESPEC64_SUB
        struct timespec64 last_reset_time;
        struct timespec64 last_read_time;
#else
        struct timespec last_reset_time;
        struct timespec last_read_time;
#endif
};

struct io_path_stats {
	atomic64_t active_ios;
	atomic64_t max_active_ios;
	atomic64_t io_completions;
	atomic64_t io_failures;
	atomic64_t ioreq_null;
	atomic64_t alloc_failures;
	atomic64_t sc_null;
	atomic64_t tag_alloc_failures;
	atomic64_t io_not_found;
	atomic64_t num_ios;
	atomic64_t io_blocked;
	atomic64_t sc_alloc_failures;
	atomic64_t sc_done_data_alloc_failure;
	atomic64_t io_btw_0_to_1_msec;
	atomic64_t io_btw_1_to_2_msec;
	atomic64_t io_btw_2_to_5_msec;
	atomic64_t io_btw_5_to_10_msec;
	atomic64_t io_btw_10_to_100_msec;
	atomic64_t io_btw_100_to_500_msec;
	atomic64_t io_btw_500_to_5000_msec;
	atomic64_t io_btw_5000_to_10000_msec;
	atomic64_t io_btw_10000_to_30000_msec;
	atomic64_t io_greater_than_30000_msec;
	atomic64_t current_max_io_time;
	atomic64_t num_ios_in_waitq;
	atomic64_t io_in_waitq_3000_msec;
	atomic64_t io_in_waitq_max_time;

	atomic64_t io_reqs_rcvd;
	atomic64_t ios_queued_for_rsp;
	atomic64_t io_rsps_unqueued;
	atomic64_t io_rsps_sending;
	atomic64_t io_rsps_sent;

	atomic64_t readio;
	atomic64_t writeio;
	atomic64_t readio_failures;
	atomic64_t writeio_failures;

	atomic64_t io_512;
	atomic64_t io_1k;
	atomic64_t io_2k;
	atomic64_t io_4k;
	atomic64_t io_gt4k;

};

struct abort_stats {
	atomic64_t aborts;
	atomic64_t abort_failures;
	atomic64_t abort_drv_timeouts;
	atomic64_t abort_fw_timeouts;
	atomic64_t abort_io_not_found;
	atomic64_t abort_issued_btw_0_to_6_sec;
	atomic64_t abort_issued_btw_6_to_20_sec;
	atomic64_t abort_issued_btw_20_to_30_sec;
	atomic64_t abort_issued_btw_30_to_40_sec;
	atomic64_t abort_issued_btw_40_to_50_sec;
	atomic64_t abort_issued_btw_50_to_60_sec;
	atomic64_t abort_issued_greater_than_60_sec;
	atomic64_t ioreq_not_found;
};

struct terminate_stats {
	atomic64_t terminates;
	atomic64_t terminates_after_lun_reset;
	atomic64_t max_terminates;
	atomic64_t terminate_drv_timeouts;
	atomic64_t terminate_fw_timeouts;
	atomic64_t terminate_io_not_found;
	atomic64_t terminate_failures;
};

struct reset_stats {
	atomic64_t virtual_reset_called;
	atomic64_t device_reset_called;
	atomic64_t device_reset_issued;
	atomic64_t device_reset_failures;
	atomic64_t device_reset_aborts;
	atomic64_t device_reset_timeouts;
	atomic64_t device_reset_terminates;
	atomic64_t fw_resets;
	atomic64_t fw_reset_completions;
	atomic64_t fw_reset_failures;
	atomic64_t fw_reset_timeouts;
	atomic64_t fnic_resets;
	atomic64_t fnic_reset_completions;
	atomic64_t fnic_reset_failures;
};

struct fw_stats {
	atomic64_t active_fw_reqs;
	atomic64_t max_fw_reqs;
	atomic64_t fw_out_of_resources;
	atomic64_t io_fw_errs;
};

struct vlan_stats {
	atomic64_t vlan_disc_reqs;
	atomic64_t resp_withno_vlanID;
	atomic64_t sol_expiry_count;
	atomic64_t flogi_rejects;
};

struct misc_stats {
	u64 last_isr_time;
	u64 last_ack_time;
      atomic64_t max_isr_jiffies;
      atomic64_t max_isr_time_ms;
      atomic64_t corr_work_done;
	atomic64_t isr_count;
	atomic64_t max_cq_entries;
	atomic64_t ack_index_out_of_range;
	atomic64_t data_count_mismatch;
	atomic64_t fcpio_timeout;
	atomic64_t fcpio_aborted;
	atomic64_t sgl_invalid;
	atomic64_t mss_invalid;
	atomic64_t abts_cpwq_alloc_failures;
	atomic64_t devrst_cpwq_alloc_failures;
	atomic64_t io_cpwq_alloc_failures;
	atomic64_t no_icmnd_itmf_cmpls;
	atomic64_t queue_fulls;
	atomic64_t check_condition;
	
	atomic64_t tport_not_ready;
	atomic64_t iport_not_ready;
	atomic64_t frame_errors;
      atomic64_t intx_dummy;
	atomic64_t port_speed_in_mbps;
};

struct fnic_exch_stats {
        /* fc exches statistics */
        atomic64_t no_free_exch;            /* no free exch memory */
        atomic64_t no_free_exch_xid;        /* no free exch id */
        atomic64_t xid_not_found;           /* exch not found for a response */
        atomic64_t xid_busy;                /* exch exist for new a request */
        atomic64_t seq_not_found;           /* seq is not found for exchange */
        atomic64_t non_bls_resp;            /* a non BLS response frame with
                                           a sequence responder in new exch */
        atomic64_t fnic_ex_closed;
};

struct fnic_iport_stats {
        atomic64_t      num_linkdn;
        atomic64_t      num_linkup;
        atomic64_t      link_failure_count;
        atomic64_t      num_rscns;
        atomic64_t      rscn_redisc;
        atomic64_t      rscn_not_redisc;
        atomic64_t      frame_err;
        atomic64_t      num_rnid;
        atomic64_t      fabric_flogi_sent;
        atomic64_t      fabric_flogi_ls_accepts;
        atomic64_t      fabric_flogi_ls_rejects;
        atomic64_t      fabric_flogi_misc_rejects;
        atomic64_t      fabric_plogi_sent;
        atomic64_t      fabric_plogi_ls_accepts;
        atomic64_t      fabric_plogi_ls_rejects;
        atomic64_t      fabric_plogi_misc_rejects;
        atomic64_t      fabric_scr_sent;
        atomic64_t      fabric_scr_ls_accepts;
        atomic64_t      fabric_scr_ls_rejects;
        atomic64_t      fabric_scr_misc_rejects;
        atomic64_t      fabric_logo_sent;
        atomic64_t      tport_alive;
        atomic64_t      tport_plogi_sent;
        atomic64_t      tport_plogi_ls_accepts;
        atomic64_t      tport_plogi_ls_rejects;
        atomic64_t      tport_plogi_misc_rejects;
        atomic64_t      tport_prli_sent;
        atomic64_t      tport_prli_ls_accepts;
        atomic64_t      tport_prli_ls_rejects;
        atomic64_t      tport_prli_misc_rejects;
        atomic64_t      tport_adisc_sent;
        atomic64_t      tport_adisc_ls_accepts;
        atomic64_t      tport_adisc_ls_rejects;
        atomic64_t      tport_logo_sent;
	atomic64_t      unsupported_frames_ls_rejects;
	atomic64_t      unsupported_frames_dropped;
};
struct nvme_host_statistics {
        atomic64_t nvme_input_requests;
        atomic64_t nvme_output_requests;
        atomic64_t nvme_control_requests;
        atomic64_t nvme_ersps;
	atomic64_t nvme_ls_requests;
	atomic64_t nvme_ls_responses;
	atomic64_t nvme_ls_aborts;
	atomic64_t nvme_ls_abort_responses;
};

/*
 * fnic_stats is part of fnic structure so memory allocation and
 * setting it to zero is done
 * at the of memory allocation for fnic,
 */

struct fnic_stats {
	struct stats_timestamps stats_timestamps;
	struct io_path_stats io_stats;
	struct abort_stats abts_stats;
	struct terminate_stats term_stats;
	struct reset_stats reset_stats;
	struct fw_stats fw_stats;
	struct vlan_stats vlan_stats;
	struct misc_stats misc_stats;
	struct fnic_exch_stats exch_stats;
	struct fc_host_statistics host_stats;
	struct nvme_host_statistics nvme_stats;
};

/*
 * FC Local Port (Host) Statistics
 */

#if 0
struct fc_els_lesb {
        __be32          lesb_link_fail; /* link failure count */
        __be32          lesb_sync_loss; /* loss of synchronization count */
        __be32          lesb_sig_loss;  /* loss of signal count */
        __be32          lesb_prim_err;  /* primitive sequence error count */
        __be32          lesb_inv_word;  /* invalid transmission word count */
        __be32          lesb_inv_crc;   /* invalid CRC count */
};
static struct fcoe_sysfs_function_template fcoe_sysfs_templ = {
        .set_fcoe_ctlr_mode = fcoe_ctlr_set_fip_mode,
        .set_fcoe_ctlr_enabled = fcoe_ctlr_enabled,
        .get_fcoe_ctlr_link_fail = fcoe_ctlr_get_lesb,
        .get_fcoe_ctlr_vlink_fail = fcoe_ctlr_get_lesb,
        .get_fcoe_ctlr_miss_fka = fcoe_ctlr_get_lesb,
        .get_fcoe_ctlr_symb_err = fcoe_ctlr_get_lesb,
        .get_fcoe_ctlr_err_block = fcoe_ctlr_get_lesb,
        .get_fcoe_ctlr_fcs_error = fcoe_ctlr_get_lesb,

        .get_fcoe_fcf_selected = fcoe_fcf_get_selected,
        .get_fcoe_fcf_vlan_id = fcoe_fcf_get_vlan_id,
};
struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *shost)

/**
 * struct fc_stats - fc stats structure
 * @SecondsSinceLastReset: Seconds since the last reset
 * @TxFrames:              Number of transmitted frames
 * @TxWords:               Number of transmitted words
 * @RxFrames:              Number of received frames
 * @RxWords:               Number of received words
 * @ErrorFrames:           Number of received error frames
 * @DumpedFrames:          Number of dumped frames
 * @FcpPktAllocFails:      Number of fcp packet allocation failures
 * @FcpPktAborts:          Number of fcp packet aborts
 * @FcpFrameAllocFails:    Number of fcp frame allocation failures
 * @LinkFailureCount:      Number of link failures
 * @LossOfSignalCount:     Number for signal losses
 * @InvalidTxWordCount:    Number of invalid transmitted words
 * @InvalidCRCCount:       Number of invalid CRCs
 * @InputRequests:         Number of input requests
 * @OutputRequests:        Number of output requests
 * @ControlRequests:       Number of control requests
 * @InputBytes:            Number of received bytes
 * @OutputBytes:           Number of transmitted bytes
 * @VLinkFailureCount:     Number of virtual link failures
 * @MissDiscAdvCount:      Number of missing FIP discovery advertisement
 */
struct fc_stats {
        u64             SecondsSinceLastReset;
        u64             TxFrames;
        u64             TxWords;
        u64             RxFrames;
        u64             RxWords;
        u64             ErrorFrames;
        u64             DumpedFrames;
        u64             FcpPktAllocFails;
        u64             FcpPktAborts;
        u64             FcpFrameAllocFails;
        u64             LinkFailureCount;
        u64             LossOfSignalCount;
        u64             InvalidTxWordCount;
        u64             InvalidCRCCount;
        u64             InputRequests;
        u64             OutputRequests;
        u64             ControlRequests;
        u64             InputBytes;
        u64             OutputBytes;
        u64             VLinkFailureCount;
        u64             MissDiscAdvCount;
};
#endif
#endif /* _FNIC_STATS_H_ */
