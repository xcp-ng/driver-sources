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
#include "fnic_config.h"

#include <linux/mempool.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/version.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#if FNIC_HAVE_SCSI_DEVICE_H
#include <scsi/scsi_device.h>
#endif
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcoe.h>
#include <linux/mutex.h>
//#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include "fnic.h"
#include "fnic_io.h"

enum terminate_io_return {
	TERM_SUCCESS = 0,
	TERM_NO_SC = 1,
	TERM_IO_REQ_NOT_FOUND,
	TERM_ANOTHER_PORT,
	TERM_GSTATE,
	TERM_IO_BLOCKED,
	TERM_OUT_OF_WQ_DESC,
	TERM_TIMED_OUT,
	TERM_MISC,
};

extern void fnic_free_txq(struct list_head *list);
extern void fnic_do_tgts_logo(struct fnic *fnic);
extern void fnic_fdls_link_status_change(struct fnic *fnic, int linkup);
extern int nvfnic_add_lport(struct fnic *fnic);
extern void fnic_fcpio_nvme_fast_cmpl_handler(struct fnic *fnic, struct fcpio_fw_req *desc);
extern void fnic_fcpio_nvme_itmf_cmpl_handler(struct fnic *fnic, struct fcpio_fw_req *desc);
extern void fnic_fcpio_ersp_cmpl_handler(struct fnic *fnic, struct fcpio_fw_req *desc, int sw_flag);
extern void fnic_cleanup_all_nvme_io(struct fnic *fnic);
extern void fnic_flush_tport_event_list(struct fnic *fnic);
extern void fnic_delete_fcp_tports(struct fnic *fnic);

extern struct list_head reset_fnic_list;
extern struct workqueue_struct *reset_fnic_work_queue;
extern unsigned int pc_rscn_handling_feature_flag;
extern spinlock_t reset_fnic_list_lock;
extern struct work_struct reset_fnic_work;

extern int fnic_wqerr_debug;

const char *fnic_state_str[] = {
	[FNIC_IN_FC_MODE] =           "FNIC_IN_FC_MODE",
	[FNIC_IN_FC_TRANS_ETH_MODE] = "FNIC_IN_FC_TRANS_ETH_MODE",
	[FNIC_IN_ETH_MODE] =          "FNIC_IN_ETH_MODE",
	[FNIC_IN_ETH_TRANS_FC_MODE] = "FNIC_IN_ETH_TRANS_FC_MODE",
};

static const char *fnic_ioreq_state_str[] = {
	[FNIC_IOREQ_NOT_INITED] = "FNIC_IOREQ_NOT_INITED",
	[FNIC_IOREQ_CMD_PENDING] = "FNIC_IOREQ_CMD_PENDING",
	[FNIC_IOREQ_ABTS_PENDING] = "FNIC_IOREQ_ABTS_PENDING",
	[FNIC_IOREQ_ABTS_COMPLETE] = "FNIC_IOREQ_ABTS_COMPLETE",
	[FNIC_IOREQ_CMD_COMPLETE] = "FNIC_IOREQ_CMD_COMPLETE",
};

static const char *fcpio_status_str[] =  {
	[FCPIO_SUCCESS] = "FCPIO_SUCCESS", /*0x0*/
	[FCPIO_INVALID_HEADER] = "FCPIO_INVALID_HEADER",
	[FCPIO_OUT_OF_RESOURCE] = "FCPIO_OUT_OF_RESOURCE",
	[FCPIO_INVALID_PARAM] = "FCPIO_INVALID_PARAM]",
	[FCPIO_REQ_NOT_SUPPORTED] = "FCPIO_REQ_NOT_SUPPORTED",
	[FCPIO_IO_NOT_FOUND] = "FCPIO_IO_NOT_FOUND",
	[FCPIO_ABORTED] = "FCPIO_ABORTED", /*0x41*/
	[FCPIO_TIMEOUT] = "FCPIO_TIMEOUT",
	[FCPIO_SGL_INVALID] = "FCPIO_SGL_INVALID",
	[FCPIO_MSS_INVALID] = "FCPIO_MSS_INVALID",
	[FCPIO_DATA_CNT_MISMATCH] = "FCPIO_DATA_CNT_MISMATCH",
	[FCPIO_FW_ERR] = "FCPIO_FW_ERR",
	[FCPIO_ITMF_REJECTED] = "FCPIO_ITMF_REJECTED",
	[FCPIO_ITMF_FAILED] = "FCPIO_ITMF_FAILED",
	[FCPIO_ITMF_INCORRECT_LUN] = "FCPIO_ITMF_INCORRECT_LUN",
	[FCPIO_CMND_REJECTED] = "FCPIO_CMND_REJECTED",
	[FCPIO_NO_PATH_AVAIL] = "FCPIO_NO_PATH_AVAIL",
	[FCPIO_PATH_FAILED] = "FCPIO_PATH_FAILED",
	[FCPIO_LUNMAP_CHNG_PEND] = "FCPIO_LUNHMAP_CHNG_PEND",
};

const char *fnic_state_to_str(unsigned int state)
{
	if (state >= ARRAY_SIZE(fnic_state_str) || !fnic_state_str[state])
		return "unknown";

	return fnic_state_str[state];
}

const char *fnic_ioreq_state_to_str(unsigned int state)
{
	if (state >= ARRAY_SIZE(fnic_ioreq_state_str) ||
	    !fnic_ioreq_state_str[state])
		return "unknown";

	return fnic_ioreq_state_str[state];
}

const char *fnic_fcpio_status_to_str(unsigned int status)
{
	if (status >= ARRAY_SIZE(fcpio_status_str) || !fcpio_status_str[status])
		return "unknown";

	return fcpio_status_str[status];
}

static void
fnic_debug_print_details(struct fnic *fnic, struct fnic_io_req *io_req,
    struct scsi_cmnd *sc);


static void fnic_cleanup_io(struct fnic *fnic, int exclude_id);

/*
 * Unmap the data buffer and sense buffer for an io_req,
 * also unmap and free the device-private scatter/gather list.
 */
static void fnic_release_ioreq_buf(struct fnic *fnic,
		                   struct fnic_io_req *io_req,
			           struct scsi_cmnd *sc)
{

	if (io_req->sgl_list_pa)
		pci_unmap_single(fnic->pdev, io_req->sgl_list_pa,
				 sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt,
				 PCI_DMA_TODEVICE);
	scsi_dma_unmap(sc);

	if (io_req->sgl_cnt)
		mempool_free(io_req->sgl_list_alloc,
			     fnic->io_sgl_pool[io_req->sgl_type]);
	if (io_req->sense_buf_pa)
		pci_unmap_single(fnic->pdev, io_req->sense_buf_pa,
				 SCSI_SENSE_BUFFERSIZE, PCI_DMA_FROMDEVICE);
}

inline void fnic_print_ioreq(struct fnic_io_req *io_req, u32 hwq)
{
		printk("\t\ttagq:0x%x: portid:0x%x hwq:%d\n", io_req->tag, io_req->port_id, hwq);

}

int fnic_count_ioreqs_wq(struct fnic *fnic, u32 hwq, u32 portid)
{
	unsigned long flags = 0;
	int i=0, count=0;

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	for(i=0; i!= fnic->fnic_cpy_wq[hwq].ioreq_table_size; ++i) {
		if(fnic->fnic_cpy_wq[hwq].io_req_table[i] != NULL &&
			(!portid || fnic->fnic_cpy_wq[hwq].io_req_table[i]->port_id == portid))
	       		count++;       
			//fnic_print_ioreq(io_req, hwq);
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	return count;
}

unsigned int  fnic_count_ioreqs(struct fnic *fnic,u32 portid)
{
	int i;
	unsigned int count=0;

	for (i = 0; i < fnic->wq_copy_count; i++)
		count += fnic_count_ioreqs_wq(fnic, i, portid);

	return count;
}

unsigned int  fnic_count_all_ioreqs(struct fnic *fnic)
{
	return fnic_count_ioreqs(fnic, 0);
}

unsigned int fnic_count_lun_ioreqs_wq(struct fnic*fnic, u32 hwq, struct scsi_device *device) {
	struct fnic_io_req *io_req;
	int i;
	unsigned int count=0;
	unsigned long flags = 0;

	//printk("\tIn%s:hwq=%d\n", __func__,hwq);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	for(i=0; i!= fnic->fnic_cpy_wq[hwq].ioreq_table_size; ++i) {
		io_req = fnic->fnic_cpy_wq[hwq].io_req_table[i];

		if(io_req != NULL) {
			struct scsi_cmnd *sc =
			       	scsi_host_find_tag(fnic->host, io_req->tag);

			if(!sc)
				continue;

			if(sc->device == device)
				count++;
		}
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	return count;
}

unsigned int fnic_count_lun_ioreqs(struct fnic*fnic, struct scsi_device *device) {
	int hwq;
	unsigned int count=0;

	/*count if any pending IOs on this lun*/
	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++)
		count += fnic_count_lun_ioreqs_wq(fnic, hwq, device);

	return count;
}

/* Free up Copy Wq descriptors. Called with copy_wq lock held */
int free_wq_copy_descs(struct fnic *fnic, struct vnic_wq_copy *wq, unsigned int hwq)
{
	/* if no Ack received from firmware, then nothing to clean */
	if (!fnic->fw_ack_recd[hwq])
		return 1;

	/*
	 * Update desc_available count based on number of freed descriptors
	 * Account for wraparound
	 */
	if (wq->to_clean_index <= fnic->fw_ack_index[hwq])
		wq->ring.desc_avail += (fnic->fw_ack_index[hwq]
					- wq->to_clean_index + 1);
	else
		wq->ring.desc_avail += (wq->ring.desc_count
					- wq->to_clean_index
					+ fnic->fw_ack_index[hwq] + 1);

	/*
	 * just bump clean index to ack_index+1 accounting for wraparound
	 * this will essentially free up all descriptors between
	 * to_clean_index and fw_ack_index, both inclusive
	 */
	wq->to_clean_index =
		(fnic->fw_ack_index[hwq] + 1) % wq->ring.desc_count;

	/* we have processed the acks received so far */
	fnic->fw_ack_recd[hwq] = 0;
	return 0;
}

/* Free up Copy Wq descriptors. Called with copy_wq lock held */
static int free_wq_copy_descs_mq(struct fnic *fnic, struct vnic_wq_copy *wq, unsigned int hwq)
{
	/* if no Ack received from firmware, then nothing to clean */
	if (!fnic->fw_ack_recd[hwq])
		return 1;

	/*
	 * Update desc_available count based on number of freed descriptors
	 * Account for wraparound
	 */
	if (wq->to_clean_index <= fnic->fw_ack_index[hwq])
		wq->ring.desc_avail += (fnic->fw_ack_index[hwq]
					- wq->to_clean_index + 1);
	else
		wq->ring.desc_avail += (wq->ring.desc_count
					- wq->to_clean_index
					+ fnic->fw_ack_index[hwq] + 1);

	/*
	 * just bump clean index to ack_index+1 accounting for wraparound
	 * this will essentially free up all descriptors between
	 * to_clean_index and fw_ack_index, both inclusive
	 */
	wq->to_clean_index =
		(fnic->fw_ack_index[hwq] + 1) % wq->ring.desc_count;

	/* we have processed the acks received so far */
	fnic->fw_ack_recd[hwq] = 0;
	return 0;
}

/**
 * __fnic_set_state_flags
 * Sets/Clears bits in fnic's state_flags
 **/
void
__fnic_set_state_flags(struct fnic *fnic, unsigned long st_flags,
		       unsigned long clearbits)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (clearbits)
		fnic->state_flags &= ~st_flags;
	else
		fnic->state_flags |= st_flags;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
}


/*
 * fnic_fw_reset_handler
 * Routine to send reset msg to fw
 */
int fnic_fw_reset_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	int ret = 0;
	unsigned long flags;
	unsigned int ioreq_count;

	/* indicate fwreset to io path */
	fnic_set_state_flags(fnic, FNIC_FLAGS_FWRESET);
	ioreq_count = fnic_count_all_ioreqs(fnic);

	// TODO need to purge linked list
//	skb_queue_purge(&fnic->frame_queue);
//	skb_queue_purge(&fnic->tx_queue);

	/* wait for io cmpl */
	while (atomic_read(&fnic->in_flight))
		schedule_timeout(msecs_to_jiffies(1));

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq, 0);

	if (!vnic_wq_copy_desc_avail(wq))
		ret = -EAGAIN;
	else {
		fnic_printk(KERN_DEBUG, fnic,
			      "fnic:Sending fnic fw reset on hwq 0 : active ios:%lld ioreq_count:%u\n" ,
						 atomic64_read(&fnic->fnic_stats.io_stats.active_ios), 
						 ioreq_count);
		fnic_queue_wq_copy_desc_fw_reset(wq, SCSI_NO_TAG);
		atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
		if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
			atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
			atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
			atomic64_read(
				&fnic->fnic_stats.fw_stats.active_fw_reqs));
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	if (!ret) {
		atomic64_inc(&fnic->fnic_stats.reset_stats.fw_resets);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "Issued fw reset\n");
	} else {
		fnic_clear_state_flags(fnic, FNIC_FLAGS_FWRESET);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "Failed to issue fw reset\n");
	}

	return ret;
}


/*
 * fnic_flogi_reg_handler
 * Routine to send flogi register msg to fw
 */
int fnic_flogi_reg_handler(struct fnic *fnic, u32 fc_id)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	enum fcpio_flogi_reg_format_type format;
	u8 gw_mac[ETH_ALEN];
	int ret = 0;
	unsigned long flags;
	fnic_iport_t *iport = &fnic->iport;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq, 0);

	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto flogi_reg_ioreq_end;
	}

	memcpy(gw_mac, fnic->iport.fcfmac, ETH_ALEN);
	format = FCPIO_FLOGI_REG_GW_DEST;

	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		fnic_queue_wq_copy_desc_fip_reg(wq, SCSI_NO_TAG,
						fc_id, gw_mac,
						fnic->iport.fpma,
						iport->r_a_tov, iport->e_d_tov);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "FLOGI FIP reg issued fcid %x src %pM dest %pM\n",
			      fc_id, fnic->iport.fpma, gw_mac);
	} else {
		fnic_queue_wq_copy_desc_flogi_reg(wq, SCSI_NO_TAG,
						  format, fc_id, gw_mac);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "FLOGI reg issued fcid %x dest %pM\n",
			      fc_id, gw_mac);
	}

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

flogi_reg_ioreq_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
	return ret;
}

/*
 * fnic_queue_wq_copy_desc
 * Routine to enqueue a wq copy desc
 */
static inline int fnic_queue_wq_copy_desc(struct fnic *fnic,
					  struct vnic_wq_copy *wq,
					  struct fnic_io_req *io_req,
					  struct scsi_cmnd *sc,
					  int sg_count,
					  uint32_t mq_tag,
					  uint16_t hwq)
{
	struct scatterlist *sg;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc->device));
	struct host_sg_desc *desc;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	u8 pri_tag = 0;
	unsigned int i;
	int flags;
	u8 exch_flags;
	struct scsi_lun fc_lun;
	fnic_tport_t *tport;
	rport_dd_data_t * rdd_data;
	int r;
	unsigned int xfer_len;

	rdd_data = rport->dd_data;
  	tport = rdd_data->tport;
	if (sg_count) {
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
		for_each_sg(scsi_sglist(sc), sg, sg_count, i) {
                      desc->addr = cpu_to_le64(sg_dma_address(sg));
			desc->len = cpu_to_le32(sg_dma_len(sg));
			desc->_resvd = 0;
			desc++;
		}

		io_req->sgl_list_pa = pci_map_single
			(fnic->pdev,
			 io_req->sgl_list,
			 sizeof(io_req->sgl_list[0]) * sg_count,
			 PCI_DMA_TODEVICE);

		r = pci_dma_mapping_error(fnic->pdev, io_req->sgl_list_pa);
		if (r) {
			printk(KERN_ERR "PCI mapping failed with error %d\n", r);
			return SCSI_MLQUEUE_HOST_BUSY;
		}
	}

	io_req->sense_buf_pa = pci_map_single(fnic->pdev,
					      sc->sense_buffer,
					      SCSI_SENSE_BUFFERSIZE,
					      PCI_DMA_FROMDEVICE);

	r = pci_dma_mapping_error(fnic->pdev, io_req->sense_buf_pa);
	if (r) {
		pci_unmap_single(fnic->pdev, io_req->sgl_list_pa,
				sizeof(io_req->sgl_list[0]) * sg_count,
				PCI_DMA_TODEVICE);
		printk(KERN_ERR "PCI mapping failed with error %d\n", r);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	int_to_scsilun(sc->device->lun, &fc_lun);

	/* Enqueue the descriptor in the Copy WQ */

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs(fnic, wq, hwq);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
//		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], intr_flags);
		FNIC_SCSI_DBG(KERN_INFO, fnic,
			  "fnic_queue_wq_copy_desc failure - no descriptors\n");
		atomic64_inc(&misc_stats->io_cpwq_alloc_failures);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	flags = 0;
	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		flags = FCPIO_ICMND_RDDATA;
		atomic64_inc(&fnic->fnic_stats.io_stats.readio);

	} else if (sc->sc_data_direction == DMA_TO_DEVICE) {
		flags = FCPIO_ICMND_WRDATA;
		atomic64_inc(&fnic->fnic_stats.io_stats.writeio);
	}

	xfer_len = scsi_transfer_length(sc);
	if (xfer_len <= 512)
		atomic64_inc(&fnic->fnic_stats.io_stats.io_512);
	else if (xfer_len <= 1024)
		atomic64_inc(&fnic->fnic_stats.io_stats.io_1k);
	else if (xfer_len <= 2048)
		atomic64_inc(&fnic->fnic_stats.io_stats.io_2k);
	else if (xfer_len <= 4096)
		atomic64_inc(&fnic->fnic_stats.io_stats.io_4k);
	else
		atomic64_inc(&fnic->fnic_stats.io_stats.io_gt4k);

	exch_flags = 0;
	if ((fnic->config.flags & VFCF_FCP_SEQ_LVL_ERR) &&
	    (tport->tgt_flags & FC_RP_FLAGS_RETRY))
		exch_flags |= FCPIO_ICMND_SRFLAG_RETRY;

//	shost_printk(KERN_ERR, fnic->host, "queueing cmd hwq = %d\n", hwq);

	fnic_queue_wq_copy_desc_icmnd_16(wq, mq_tag,
					 0, exch_flags, io_req->sgl_cnt,
					 SCSI_SENSE_BUFFERSIZE,
					 io_req->sgl_list_pa,
					 io_req->sense_buf_pa,
					 0, /* scsi cmd ref, always 0 */
					 pri_tag, /* scsi pri and tag */
					 flags,	/* command flags */
					 sc->cmnd, sc->cmd_len,
					 scsi_bufflen(sc),
					 fc_lun.scsi_lun, io_req->port_id,
					 //rport->maxframe_size, tport->r_a_tov,
					 tport->max_payload_size, tport->r_a_tov,
					 tport->e_d_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	return 0;
}

//#define DEBUG_SCSI_CMD

/*
 * fnic_queuecommand
 * Routine to send a scsi cdb
 * Called with host_lock held and interrupts disabled.
 */
static int fnic_queuecommand_lck(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *),
						 uint32_t mqtag, uint16_t hwq)
{
	struct fnic *fnic = *((struct fnic **)shost_priv(sc->device->host));
	fnic_iport_t *iport = &fnic->iport;
	struct fc_rport *rport;
	struct fnic_io_req *io_req = NULL;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct vnic_wq_copy *wq;
	int ret;
	u64 cmd_trace;
	uint32_t portid;
	int sg_count = 0;
	unsigned long flags = 0;
	unsigned long ptr;
	int io_lock_acquired = 0;
	uint16_t lun0_delay = 0;
	fnic_tport_t * tport = NULL;
	rport_dd_data_t * rdd_data;
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      struct request *scsi_req = NULL;
#endif

	rport = starget_to_rport(scsi_target(sc->device));
	if (unlikely(!rport)) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"returning DID_NO_CONNECT for IO as rport is NULL\n");
		sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
	}

	ret = fc_remote_port_chkready(rport);
	if (unlikely(ret)) {
//		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
//				"rport is not ready\n");
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		sc->result = ret;
		done(sc);
		return 0;
	}


	spin_lock_irqsave(&fnic->fnic_lock, flags);

	iport = &fnic->iport;

	if (iport->state != FNIC_IPORT_STATE_READY){
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
	                    "returning DID_NO_CONNECT for IO as iport state : %d\n",iport->state);
	            sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
        }

	rdd_data= rport->dd_data;

	tport = rdd_data->tport;

	// fc_remote_port_add() may have added the tport to fc_transport but dd_data not yet set
	if(!tport || (rdd_data->iport != iport)) {
		FNIC_SCSI_DBG(KERN_INFO, fnic,
			"dd_data not yet set in SCSI for %x\n",
			rport->port_id);
		tport = fnic_find_tport_by_fcid(iport, rport->port_id);
		if (!tport) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			FNIC_SCSI_DBG(KERN_INFO, fnic,
				"returning DID_BUS_BUSY for IO as tport not found for:%x\n",
				rport->port_id);
			sc->result = DID_BUS_BUSY << 16;
			done(sc);
			return 0;
		}

		/* Re-assign same pararms as in fnic_fdls_add_tport */
		rport->maxframe_size = FNIC_FC_MAX_PAYLOAD_LEN;
		rport->supported_classes =  FC_COS_CLASS3 | FC_RPORT_ROLE_FCP_TARGET;
		rdd_data = rport->dd_data; // the dd_data is allocated by fctransport of size dd_fcrport_size
		rdd_data->tport = tport;
		rdd_data->iport = iport;
		tport->rport = rport;
		tport->flags |= FNIC_FDLS_SCSI_REGISTERED;

	}

	if ((tport->state != fdls_tgt_state_ready) &&
	        (tport->state != fdls_tgt_state_adisc)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	        FNIC_SCSI_DBG(KERN_DEBUG, fnic,
	                    "returning DID_NO_CONNECT for IO as tport state : %d\n",tport->state);
	//        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	                    sc->result = DID_NO_CONNECT << 16;
	                    done(sc);
	                    return 0;
	  }

	atomic_inc(&fnic->in_flight);
	atomic_inc(&tport->in_flight);

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED))){
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	if(!tport->lun0_delay) {
		lun0_delay = 1;
		tport->lun0_delay++;
		portid = rport->port_id;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * Release host lock, use driver resource specific locks from here.
	 * Don't re-enable interrupts in case they were disabled prior to the
	 * caller disabling them.
	 */
	CMD_STATE(sc) = FNIC_IOREQ_NOT_INITED;
	CMD_FLAGS(sc) = FNIC_NO_FLAGS;

	/* Get a new io_req for this SCSI IO */
	io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
	if (unlikely(!io_req)) {
		atomic64_inc(&fnic_stats->io_stats.alloc_failures);
		ret = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	memset(io_req, 0, sizeof(*io_req));

	/* Map the data buffer */
	sg_count = scsi_dma_map(sc);
	if (unlikely(sg_count < 0)) {
#if FNIC_HAVE_SCSI_CMD_TO_RQ
              scsi_req = scsi_cmd_to_rq(sc);
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  scsi_req->tag, sc, 0, sc->cmnd[0],
			  sg_count, CMD_STATE(sc));
#else
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  sc->request->tag, sc, 0, sc->cmnd[0],
			  sg_count, CMD_STATE(sc));
#endif
		mempool_free(io_req, fnic->io_req_pool);
		goto out;
	}
	io_req->tport = tport;

#ifdef DEBUG_SCSI_CMD /*currently not defined*/
	if(sc->cmnd[0] != 0x8  && 
		sc->cmnd[0] != 0x28 &&
		sc->cmnd[0] != 0x88 &&
		sc->cmnd[0] != 0xA8 && 
		sc->cmnd[0] != 0xAA &&
		sc->cmnd[0] != 0xA  &&
		sc->cmnd[0] != 0x2A && 
		sc->cmnd[0] != 0x8A) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"Sending to rport 0x%x scsi command 0x%x with tag=0x%x\n",
			rport->port_id,  sc->cmnd[0], mqtag);
	}
#endif /*DEBUG_SCSI_CMD*/

	/* Determine the type of scatter/gather list we need */
	io_req->sgl_cnt = sg_count;
	io_req->sgl_type = FNIC_SGL_CACHE_DFLT;
	if (sg_count > FNIC_DFLT_SG_DESC_CNT)
		io_req->sgl_type = FNIC_SGL_CACHE_MAX;

	if (likely(sg_count)) {
		io_req->sgl_list =
			mempool_alloc(fnic->io_sgl_pool[io_req->sgl_type],
				      GFP_ATOMIC);
		if (!io_req->sgl_list) {
			atomic64_inc(&fnic_stats->io_stats.alloc_failures);
			ret = SCSI_MLQUEUE_HOST_BUSY;
			scsi_dma_unmap(sc);
			mempool_free(io_req, fnic->io_req_pool);
			goto out;
		}

		/* Cache sgl list allocated address before alignment */
		io_req->sgl_list_alloc = io_req->sgl_list;
		ptr = (unsigned long) io_req->sgl_list;
		if (ptr % FNIC_SG_DESC_ALIGN) {
			io_req->sgl_list = (struct host_sg_desc *)
				(((unsigned long) ptr
				  + FNIC_SG_DESC_ALIGN - 1)
				 & ~(FNIC_SG_DESC_ALIGN - 1));
		}
	}

	/*
	* Will acquire lock defore setting to IO initialized.
	*/

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	/* initialize rest of io_req */
	io_lock_acquired = 1;
	io_req->port_id = rport->port_id;
	io_req->start_time = jiffies;
	CMD_STATE(sc) = FNIC_IOREQ_CMD_PENDING;
	CMD_SP(sc) = (char *)io_req;
	io_req->sc = sc;

	if(fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] != NULL) {
		FNIC_SCSI_DBG(KERN_ERR, fnic, "tag %d already exists\n", mqtag);
		BUG();
	}

	fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] 
								= io_req;
	io_req->tag=mqtag;
	CMD_FLAGS(sc) |= FNIC_IO_INITIALIZED;
	sc->scsi_done = done;

	/* create copy wq desc and enqueue it */
	wq = &fnic->wq_copy[hwq];
	ret = fnic_queue_wq_copy_desc(fnic, wq, io_req, sc, sg_count, mqtag, hwq);
	if(unlikely(ret)) {
		/*
		 * In case another thread cancelled the request,
		 * refetch the pointer under the lock.
		 */
#if FNIC_HAVE_SCSI_CMD_TO_RQ
              scsi_req = scsi_cmd_to_rq(sc);
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  scsi_req->tag, sc, 0, 0, 0,
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#else
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  sc->request->tag, sc, 0, 0, 0,
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#endif
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		CMD_SP(sc) = NULL;
		io_req->sc = NULL;

		if (io_req)
			fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = NULL;

		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if (io_req) {
			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
		}
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		/* acquire host lock before returning to SCSI */
		return ret;
	} else {
		atomic64_inc(&fnic_stats->io_stats.active_ios);
		atomic64_inc(&fnic_stats->io_stats.num_ios);
		if (atomic64_read(&fnic_stats->io_stats.active_ios) >
			atomic64_read(&fnic_stats->io_stats.max_active_ios))
			atomic64_set(&fnic_stats->io_stats.max_active_ios ,
			     atomic64_read(&fnic_stats->io_stats.active_ios));

		/* REVISIT: Use per IO lock in the final code */
		CMD_FLAGS(sc) |= FNIC_IO_ISSUED;

	}
out:
	cmd_trace = ((u64)sc->cmnd[0] << 56 | (u64)sc->cmnd[7] << 40 |
			(u64)sc->cmnd[8] << 32 | (u64)sc->cmnd[2] << 24 |
			(u64)sc->cmnd[3] << 16 | (u64)sc->cmnd[4] << 8 |
			sc->cmnd[5]);

#if FNIC_HAVE_SCSI_CMD_TO_RQ
      scsi_req = scsi_cmd_to_rq(sc);
	FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
		  scsi_req->tag, sc, io_req,
		  sg_count, cmd_trace,
		  (((u64)CMD_FLAGS(sc) >> 32) | CMD_STATE(sc)));
#else
	FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
		  sc->request->tag, sc, io_req,
		  sg_count, cmd_trace,
		  (((u64)CMD_FLAGS(sc) >> 32) | CMD_STATE(sc)));
#endif

	/* if only we issued IO, will we have the io lock */
	if (io_lock_acquired)
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
#if 0
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"Done Sending to rport 0x%x scsi command:0x%x tag:0x%x hwq:0x%x ioreq_count:%d\n",
			rport->port_id,  sc->cmnd[0], mqtag, hwq, fnic_count_all_ioreqs(fnic));
#endif 
	atomic_dec(&fnic->in_flight);
	atomic_dec(&tport->in_flight);
#if 1
	if(lun0_delay)
	{
		FNIC_SCSI_DBG(KERN_DEBUG, fnic, "delaying before returing %s\n",__func__);
		mdelay(9);
	}

#endif
	return ret;
}

int fnic_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	int rc;	
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      int tag = 0;
	struct request *scsi_req = NULL;
#else
	int tag = cmd->request->tag;
#endif
	uint16_t hwq=0;

	struct fnic *fnic = *((struct fnic **)shost_priv(cmd->device->host));
#if FNIC_HAVE_SCSI_CMD_TO_RQ
	scsi_req = scsi_cmd_to_rq(cmd);
	tag = scsi_req->tag;
#endif

	if (tag >= fnic->fnic_max_tag_id) {
		FNIC_SCSI_DBG(KERN_ERR, fnic,
				"%s called with out of range tag %x\n",
				__func__, tag);
		cmd->result = DID_ERROR << 16;
		cmd->scsi_done(cmd);
		return 0;
	}
#if FNIC_HAVE_SCSI_GET_SERIAL
	scsi_cmd_get_serial(shost, cmd);
#endif
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	if (shost_use_blk_mq(shost)) {
#endif

#if FNIC_HAVE_SCSI_CMD_TO_RQ
		tag = blk_mq_unique_tag(scsi_cmd_to_rq(cmd));
#else
		tag = blk_mq_unique_tag(cmd->request);
#endif

		hwq = blk_mq_unique_tag_to_hwq(tag);
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	}
#endif

	rc = fnic_queuecommand_lck (cmd, cmd->scsi_done, tag, hwq);
	return rc;
}

/*
 * fnic_fcpio_fw_reset_cmpl_handler
 * Routine to handle fw reset completion
 */
static int fnic_fcpio_fw_reset_cmpl_handler(struct fnic *fnic,
					    struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	int ret = 0;
	unsigned long flags;
	struct reset_stats *reset_stats = &fnic->fnic_stats.reset_stats;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	atomic64_inc(&reset_stats->fw_reset_completions);

	/* Clean up all outstanding io requests */
	if (IS_FNIC_FCP_INITIATOR(fnic)) {
		fnic_cleanup_io(fnic, SCSI_NO_TAG);
	} else if (IS_FNIC_NVME_INITIATOR(fnic)) {
		fnic_cleanup_all_nvme_io(fnic);
	}
	
	atomic64_set(&fnic->fnic_stats.fw_stats.active_fw_reqs, 0);
	atomic64_set(&fnic->fnic_stats.io_stats.active_ios, 0);
	atomic64_set(&fnic->io_cmpl_skip, 0);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	/* fnic should be in FC_TRANS_ETH_MODE */
	if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE) {
		/* Check status of reset completion */
		if (!hdr_status) {
		    FNIC_SCSI_DBG(KERN_ERR, fnic, "reset cmpl success\n: %d",hdr_status);
			/* Ready to send flogi out */
			fnic->state = FNIC_IN_ETH_MODE;
		} else {
		    FNIC_SCSI_DBG(KERN_ERR, fnic, "fnic fw_reset : failed %s\n",
		            fnic_fcpio_status_to_str(hdr_status) );

			/*
			 * Unable to change to eth mode, cannot send out flogi
			 * Change state to fc mode, so that subsequent Flogi
			 * requests from libFC will cause more attempts to
			 * reset the firmware. Free the cached flogi
			 */
			fnic->state = FNIC_IN_FC_MODE;
			atomic64_inc(&reset_stats->fw_reset_failures);
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_ERR,
			      fnic,
			      "Unexpected state %s while processing"
			      " reset cmpl\n", fnic_state_to_str(fnic->state));
		atomic64_inc(&reset_stats->fw_reset_failures);
		ret = -1;
	}


	if (fnic->fw_reset_done)
		complete(fnic->fw_reset_done);

	/*
	 * If fnic is being removed, or fw reset failed
	 * free the flogi frame. Else, send it out
	 */
	if (ret) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fnic_free_txq(&fnic->tx_queue); // saheli :  do we need this ??
		goto reset_cmpl_handler_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	// TODO need to add flush_tx of linked list elements
	fnic_flush_tx(fnic); // saheli :  do we need this ??

 reset_cmpl_handler_end:
	fnic_clear_state_flags(fnic, FNIC_FLAGS_FWRESET);

	return ret;
}

/*
 * fnic_fcpio_flogi_reg_cmpl_handler
 * Routine to handle flogi register completion
 */
static int fnic_fcpio_flogi_reg_cmpl_handler(struct fnic *fnic,
					     struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	int ret = 0;
	unsigned long flags;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	/* Update fnic state based on status of flogi reg completion */
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE) {

		/* Check flogi registration completion status */
		if (!hdr_status) {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				      "flog reg succeeded\n");
			fnic->state = FNIC_IN_FC_MODE;
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG,
				      fnic,
				      "fnic flogi reg :failed %s\n",
				      fnic_fcpio_status_to_str(hdr_status));
			fnic->state = FNIC_IN_ETH_MODE;
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "Unexpected fnic state %s while"
			      " processing flogi reg completion\n",
			      fnic_state_to_str(fnic->state));
		ret = -1;
	}

	if (!ret) {
		if (fnic->stop_rx_link_events) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			goto reg_cmpl_handler_end;
		}
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		fnic_flush_tx(fnic);
		queue_work(fnic_event_queue, &fnic->frame_work);
		/* update for nvfnic */
		if (fnic->role == FNIC_ROLE_NVME_INITIATOR)
			//nvfnic_add_lport(fnic);
			queue_work(fnic_event_queue, &fnic->iport_work);

	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}

reg_cmpl_handler_end:
	return ret;
}

static inline int is_ack_index_in_range(struct vnic_wq_copy *wq,
					u16 request_out)
{
	if (wq->to_clean_index <= wq->to_use_index) {
		/* out of range, stale request_out index */
		if (request_out < wq->to_clean_index ||
		    request_out >= wq->to_use_index)
			return 0;
	} else {
		/* out of range, stale request_out index */
		if (request_out < wq->to_clean_index &&
		    request_out >= wq->to_use_index)
			return 0;
	}
	/* request_out index is in range */
	return 1;
}


/*
 * Mark that ack received and store the Ack index. If there are multiple
 * acks received before Tx thread cleans it up, the latest value will be
 * used which is correct behavior. This state should be in the copy Wq
 * instead of in the fnic
 */
static inline void fnic_fcpio_ack_handler(struct fnic *fnic,
					  unsigned int cq_index,
					  struct fcpio_fw_req *desc)
{
	struct vnic_wq_copy *wq;
	u16 request_out = desc->u.ack.request_out;
	unsigned long flags;
	u64 *ox_id_tag = (u64 *)(void *)desc;
	unsigned int wq_index=cq_index;
	/* mark the ack state */
	wq = &fnic->wq_copy[cq_index];
	spin_lock_irqsave(&fnic->wq_copy_lock[wq_index], flags);

	fnic->fnic_stats.misc_stats.last_ack_time = jiffies;
	if (is_ack_index_in_range(wq, request_out)) {
		fnic->fw_ack_index[wq_index] = request_out;
		fnic->fw_ack_recd[wq_index] = 1;
	} else
		atomic64_inc(
			&fnic->fnic_stats.misc_stats.ack_index_out_of_range);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[wq_index], flags);
	FNIC_TRACE(fnic_fcpio_ack_handler,
		  fnic->fnic_num, 0, 0, ox_id_tag[2], ox_id_tag[3],
		  ox_id_tag[4], ox_id_tag[5]);
}

void
fnic_debug_print_details(struct fnic *fnic, struct fnic_io_req *io_req,
    struct scsi_cmnd *sc)
{
     struct host_sg_desc *desc;
     struct scatterlist *sg;
     int sg_count, i;
     unsigned long addr;
     unsigned int len;
     unsigned int xfer_len;

     sg_count = io_req->sgl_cnt;
     xfer_len = scsi_transfer_length(sc);

     fnic_log_info(fnic->fnic_num, "IO tag:[0x%x], Read:[%d]\n",
         io_req->tag, (sc->sc_data_direction == DMA_FROM_DEVICE) ? 1 : 0);
     fnic_log_info(fnic->fnic_num, "DBG from scsi_sglist xferlen:%u, sg_count:%d",
         xfer_len, sg_count);

     if (sg_count <= 0)
	     return;

     for_each_sg(scsi_sglist(sc), sg, sg_count, i) {
	 addr = cpu_to_le64(sg_dma_address(sg));
	 len = cpu_to_le32(sg_dma_len(sg));
          fnic_log_info(fnic->fnic_num,
              "0x%px:%d", (void *)addr, len);
     }

     fnic_log_info(fnic->fnic_num, "from local sglist, sgl_list_pa: 0x%px\n",
         (void *)io_req->sgl_list_pa);
     desc = io_req->sgl_list;
     for (i = 0; i < sg_count; i++) {
          fnic_log_info(fnic->fnic_num,
              "0x%px:%d\n", (void *)desc->addr, desc->len);
          desc++;
     }
}


/*
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle icmnd completions
 */
static void fnic_fcpio_icmnd_cmpl_handler(struct fnic *fnic,unsigned int cq_index,
					 struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	u64 xfer_len = 0;
	struct fcpio_icmnd_cmpl *icmnd_cmpl;
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags;
	u64 cmd_trace;
	unsigned long start_time;
	unsigned long io_duration_time;
	unsigned int hwq=cq_index; 
	unsigned int mqtag;
	fnic_tport_t *tport;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);
	icmnd_cmpl = &desc->u.icmnd_cmpl;
#if 0
	shost_printk(KERN_ERR, fnic->host,
				"ICMD completion on hwq = %d cq index = %d \
				 tag = 0x% mqtag = 0x%x  hdr status = %s\n",
				  hwq, cq_index, id, mqtag, fnic_fcpio_status_to_str(hdr_status));
#endif
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	if (shost_use_blk_mq(fnic->host)) {
#endif
		mqtag = blk_mq_unique_tag_to_tag(id);
		hwq   = blk_mq_unique_tag_to_hwq(id);

		if (hwq != cq_index) {
			shost_printk(KERN_ERR, fnic->host,
				"ICMD completion on wrong queue hwq = %d cq index = %d\
				 tag = 0x%u mqtag = 0x%x  hdr status = %s\n",
				  hwq, cq_index, id, mqtag, fnic_fcpio_status_to_str(hdr_status));
		}

#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	} else {
		mqtag = id;
	}
#endif

	if (mqtag >= fnic->fnic_max_tag_id) {
		shost_printk(KERN_ERR, fnic->host,
			"Tag out of range tag %x hdr status = %s\n",
			     mqtag, fnic_fcpio_status_to_str(hdr_status));
		return;
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	sc = scsi_host_find_tag(fnic->host, id);
	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "icmnd_cmpl sc is null - "
			  "hdr status = %s tag = 0x%x desc = 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, desc);
		FNIC_TRACE(fnic_fcpio_icmnd_cmpl_handler,
			  fnic->host->host_no, id,
			  ((u64)icmnd_cmpl->_resvd0[1] << 16 |
			  (u64)icmnd_cmpl->_resvd0[0]),
			  ((u64)hdr_status << 16 |
			  (u64)icmnd_cmpl->scsi_status << 8 |
			  (u64)icmnd_cmpl->flags), desc,
			  (u64)icmnd_cmpl->residual, 0);
		return;
	}

	io_req = (struct fnic_io_req *)CMD_SP(sc);

	if(fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] != io_req) {
		shost_printk(KERN_ERR, fnic->host, "io_req mismatch tag 0x%x \n", id);
		BUG_ON(1);
	}

	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		CMD_FLAGS(sc) |= FNIC_IO_REQ_NULL;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "icmnd_cmpl io_req is null - "
			  "hdr status = %s tag = 0x%x sc 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, sc);
		return;
	}
	start_time = io_req->start_time;

	tport = io_req->tport;

	/* firmware completed the io */
	io_req->io_completed = 1;

#if 0
	FNIC_SCSI_DBG(KERN_INFO, fnic,
			"icmnd_cmpl  "
			  "hdr status = %s tag = 0x%x sc = 0x%p"
			  "scsi_status = %x \n",
			  fnic_fcpio_status_to_str(hdr_status),
			  id, sc,
			  icmnd_cmpl->scsi_status);
#endif /*0*/


     if (fnic_wqerr_debug) {
         if (hdr_status == FCPIO_ABORTED) {
             shost_printk(KERN_INFO, fnic->host,
                 "IOError DBG -  printing the SGL details");
             fnic_debug_print_details(fnic, io_req, sc);
         }
     }

	/*
	 *  if SCSI-ML has already issued abort on this command,
	 *  set completion of the IO. The abts path will clean it up
	 */
	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {

		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */
		CMD_FLAGS(sc) |= FNIC_IO_DONE;
		CMD_FLAGS(sc) |= FNIC_IO_ABTS_PENDING;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if(FCPIO_ABORTED == hdr_status)
			CMD_FLAGS(sc) |= FNIC_IO_ABORTED;

		FNIC_SCSI_DBG(KERN_INFO, fnic,
			"icmnd_cmpl abts pending "
			  "hdr status = %s tag = 0x%x sc = 0x%p"
			  "scsi_status = %x residual = %d\n",
			  fnic_fcpio_status_to_str(hdr_status),
			  id, sc,
			  icmnd_cmpl->scsi_status,
			  icmnd_cmpl->residual);
		return;
	}

	/* Mark the IO as complete */
	CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;

	icmnd_cmpl = &desc->u.icmnd_cmpl;
	scsi_set_resid(sc, 0);

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		sc->result = (DID_OK << 16) | icmnd_cmpl->scsi_status;
		xfer_len = scsi_bufflen(sc);

		if (icmnd_cmpl->flags & FCPIO_ICMND_CMPL_RESID_UNDER) {
			xfer_len -= icmnd_cmpl->residual;
			scsi_set_resid(sc, icmnd_cmpl->residual);
                }

		if (icmnd_cmpl->scsi_status == SAM_STAT_CHECK_CONDITION)
			atomic64_inc(&fnic_stats->misc_stats.check_condition);

		if (icmnd_cmpl->scsi_status == SAM_STAT_TASK_SET_FULL)
			atomic64_inc(&fnic_stats->misc_stats.queue_fulls);
		break;

	case FCPIO_TIMEOUT:          /* request was timed out */
		atomic64_inc(&fnic_stats->misc_stats.fcpio_timeout);
		sc->result = (DID_TIME_OUT << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_ABORTED:          /* request was aborted */
		atomic64_inc(&fnic_stats->misc_stats.fcpio_aborted);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_DATA_CNT_MISMATCH: /* recv/sent more/less data than exp. */
		atomic64_inc(&fnic_stats->misc_stats.data_count_mismatch);
		scsi_set_resid(sc, icmnd_cmpl->residual);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_OUT_OF_RESOURCE:  /* out of resources to complete request */
		atomic64_inc(&fnic_stats->fw_stats.fw_out_of_resources);
		sc->result = (DID_REQUEUE << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_IO_NOT_FOUND:     /* requested I/O was not found */
		atomic64_inc(&fnic_stats->io_stats.io_not_found);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_SGL_INVALID:      /* request was aborted due to sgl error */
		atomic64_inc(&fnic_stats->misc_stats.sgl_invalid);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_FW_ERR:           /* request was terminated due fw error */
		atomic64_inc(&fnic_stats->fw_stats.io_fw_errs);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_MSS_INVALID:      /* request was aborted due to mss error */
		atomic64_inc(&fnic_stats->misc_stats.mss_invalid);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_INVALID_HEADER:   /* header contains invalid data */
	case FCPIO_INVALID_PARAM:    /* some parameter in request invalid */
	case FCPIO_REQ_NOT_SUPPORTED:/* request type is not supported */
	default:
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;
	}

	/* Break link with the SCSI command */
	CMD_SP(sc) = NULL;
	io_req->sc = NULL;
	CMD_FLAGS(sc) |= FNIC_IO_DONE;
	fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = NULL;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
        if (sc->sc_data_direction == DMA_FROM_DEVICE)
		atomic64_inc(&fnic_stats->io_stats.readio_failures);
        else
		atomic64_inc(&fnic_stats->io_stats.writeio_failures);

	shost_printk(KERN_ERR, fnic->host, "hdr status = %s\n",
		     fnic_fcpio_status_to_str(hdr_status));
	}

	fnic_release_ioreq_buf(fnic, io_req, sc);

	mempool_free(io_req, fnic->io_req_pool);

	cmd_trace = ((u64)hdr_status << 56) |
		  (u64)icmnd_cmpl->scsi_status << 48 |
		  (u64)icmnd_cmpl->flags << 40 | (u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5];

	FNIC_TRACE(fnic_fcpio_icmnd_cmpl_handler,
		  sc->device->host->host_no, id, sc,
		  ((u64)icmnd_cmpl->_resvd0[1] << 56 |
		  (u64)icmnd_cmpl->_resvd0[0] << 48 |
		  jiffies_to_msecs(jiffies - start_time)),
		  desc, cmd_trace,
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		fnic_stats->host_stats.fcp_input_requests++;
		fnic->fcp_input_bytes += xfer_len;
	} else if (sc->sc_data_direction == DMA_TO_DEVICE) {
		fnic_stats->host_stats.fcp_output_requests++;
		fnic->fcp_output_bytes += xfer_len;
	} else
		fnic_stats->host_stats.fcp_control_requests++;

	/* Call SCSI completion function to complete the IO */
	if (sc->scsi_done)
		sc->scsi_done(sc);

	atomic64_dec(&fnic_stats->io_stats.active_ios);
	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);


	io_duration_time = jiffies_to_msecs(jiffies) - jiffies_to_msecs(io_req->start_time);

	if(io_duration_time <= 1)
		atomic64_inc(&fnic_stats->io_stats.io_btw_0_to_1_msec);
	else if(io_duration_time <= 2)
		atomic64_inc(&fnic_stats->io_stats.io_btw_1_to_2_msec);
	else if(io_duration_time <= 5)
		atomic64_inc(&fnic_stats->io_stats.io_btw_2_to_5_msec);
	else if(io_duration_time <= 10)
		atomic64_inc(&fnic_stats->io_stats.io_btw_5_to_10_msec);
	else if(io_duration_time <= 100)
		atomic64_inc(&fnic_stats->io_stats.io_btw_10_to_100_msec);
	else if(io_duration_time <= 500)
		atomic64_inc(&fnic_stats->io_stats.io_btw_100_to_500_msec);
	else if(io_duration_time <= 5000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_500_to_5000_msec);
	else if(io_duration_time <= 10000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_5000_to_10000_msec);
	else if(io_duration_time <= 30000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_10000_to_30000_msec);
	else {
		atomic64_inc(&fnic_stats->io_stats.io_greater_than_30000_msec);

		if(io_duration_time > atomic64_read(&fnic_stats->io_stats.current_max_io_time))
			atomic64_set(&fnic_stats->io_stats.current_max_io_time, io_duration_time);
	}
}

void reset_fnic_work_handler(struct work_struct *work)
{
	struct fnic *cur_fnic, *next_fnic;
	unsigned long reset_fnic_list_lock_flags;
	int host_reset_ret_code;

	// This is a single thread. It is per fnic module, NOT per fnic
	// All the fnics that need to be reset have been serialized via the reset fnic list.
	spin_lock_irqsave(&reset_fnic_list_lock, reset_fnic_list_lock_flags);
	list_for_each_entry_safe(cur_fnic, next_fnic, &reset_fnic_list, links) {
        list_del(&cur_fnic->links);
        spin_unlock_irqrestore(&reset_fnic_list_lock, reset_fnic_list_lock_flags);
        
        printk(KERN_ERR "fnic: <%d>: pcrscn: issuing a host reset\n", cur_fnic->fnic_num);
        host_reset_ret_code = fnic_host_reset(cur_fnic->host);
        printk(KERN_ERR "fnic: <%d>: pcrscn: returned from host reset with status: %d\n", cur_fnic->fnic_num, host_reset_ret_code);
        
        MY_SPIN_LOCK_IRQ_SAVE(&cur_fnic->fnic_lock, cur_fnic->lock_flags);
        cur_fnic->pc_rscn_handling_status = PC_RSCN_HANDLING_NOT_IN_PROGRESS;
        MY_SPIN_UNLOCK_IRQRESTORE(&cur_fnic->fnic_lock, cur_fnic->lock_flags);

        spin_lock_irqsave(&reset_fnic_list_lock, reset_fnic_list_lock_flags);
	} //end loop
	spin_unlock_irqrestore(&reset_fnic_list_lock, reset_fnic_list_lock_flags);
} //reset_fnic_work_handler

/* fnic_fcpio_itmf_cmpl_handler
 * Routine to handle itmf completions
 */
static void fnic_fcpio_itmf_cmpl_handler(struct fnic *fnic, unsigned int cq_index,
					struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	struct scsi_cmnd *sc = NULL;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;
	unsigned long start_time;
	unsigned int hwq = cq_index;
	unsigned int mqtag;
	fnic_tport_t *tport;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	if (shost_use_blk_mq(fnic->host)) {
#endif
		mqtag = blk_mq_unique_tag_to_tag(id & FNIC_TAG_MASK);
		hwq   = blk_mq_unique_tag_to_hwq(id & FNIC_TAG_MASK);

		if (hwq != cq_index) {
			shost_printk(KERN_ERR, fnic->host,
				"ICMD completion on wrong queue hwq = %d cq index = %d \
				 tag = 0x%u mqtag = 0x%x  hdr status = %s\n",
				  hwq, cq_index, id, mqtag, fnic_fcpio_status_to_str(hdr_status));
		}

#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	} else {
		mqtag = id & FNIC_TAG_MASK;
	}
#endif

	if (mqtag > fnic->fnic_max_tag_id) {
		shost_printk(KERN_ERR, fnic->host,
		"Tag out of range tag %x hdr status = %s\n",
		mqtag, fnic_fcpio_status_to_str(hdr_status));
		return;
	} else if ((mqtag == fnic->fnic_max_tag_id) && !(id & FNIC_TAG_DEV_RST)) {
		shost_printk(KERN_ERR, fnic->host,
		"Tag out of range tag %x hdr status = %s\n",
		mqtag, fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	/* If it is sg3utils allocated SC then tag_id is max_tag_id and SC is retrievd from io_req */
	if ((mqtag == fnic->fnic_max_tag_id) && (id & FNIC_TAG_DEV_RST)) {
		io_req = fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)];
		if (io_req)
			sc = io_req->sc;
	} else {
		sc = scsi_host_find_tag(fnic->host, id & FNIC_TAG_MASK);
	}
	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "itmf_cmpl sc is null - hdr status = %s tag = 0x%x\n",
			  fnic_fcpio_status_to_str(hdr_status), id);
		return;
	}

	io_req = (struct fnic_io_req *)CMD_SP(sc);
	WARN_ON_ONCE(!io_req);

	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		shost_printk(KERN_ERR, fnic->host,
			  "itmf_cmpl io_req is null - "
			  "hdr status = %s tag = 0x%x sc 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, sc);
		return;
	}

	tport = io_req->tport;
	start_time = io_req->start_time;

	if ((id & FNIC_TAG_ABORT) && (id & FNIC_TAG_DEV_RST)) {
		/* Abort and terminate completion of device reset req */
		/* REVISIT : Add asserts about various flags */
		FNIC_SCSI_DBG(KERN_INFO, fnic,
			      "dev reset abts cmpl recd. id %x status %s\n",
			      id, fnic_fcpio_status_to_str(hdr_status));
		CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;
		CMD_ABTS_STATUS(sc) = hdr_status;
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		if (io_req->abts_done)
			complete(io_req->abts_done);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	} else if (id & FNIC_TAG_ABORT) {

		/* Completion of abort cmd */
		switch (hdr_status) {
		case FCPIO_SUCCESS:
			break;
		case FCPIO_TIMEOUT:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_fw_timeouts);
			else
				atomic64_inc(
					&term_stats->terminate_fw_timeouts);
			break;
		case FCPIO_ITMF_REJECTED:
			FNIC_SCSI_DBG(KERN_WARNING, fnic,
				"abort reject recd. id %d\n",
				(int)(id & FNIC_TAG_MASK));
			break;

		case FCPIO_IO_NOT_FOUND:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_io_not_found);
			else
				atomic64_inc(
					&term_stats->terminate_io_not_found);
			break;
		default:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_failures);
			else
				atomic64_inc(
					&term_stats->terminate_failures);
			break;
		}
		if (CMD_STATE(sc) != FNIC_IOREQ_ABTS_PENDING) {
			/* This is a late completion. Ignore it */
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			return;
		}

		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_DONE;
		CMD_ABTS_STATUS(sc) = hdr_status;

		/* If the status is IO not found consider it as success */
		if (hdr_status == FCPIO_IO_NOT_FOUND)
			CMD_ABTS_STATUS(sc) = FCPIO_SUCCESS;

		if(!(CMD_FLAGS(sc) & (FNIC_IO_ABORTED | FNIC_IO_DONE)))
			atomic64_inc(&misc_stats->no_icmnd_itmf_cmpls);

		FNIC_SCSI_DBG(KERN_INFO, fnic,
			      "abts cmpl recd. id %x status %s\n",
			      (int)(id & FNIC_TAG_MASK),
			      fnic_fcpio_status_to_str(hdr_status));

		/*
		 * If scsi_eh thread is blocked waiting for abts to complete,
		 * signal completion to it. IO will be cleaned in the thread
		 * else clean it in this context
		 */
		if (io_req->abts_done) {
			complete(io_req->abts_done);
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				      "abts cmpl, completing IO\n");
			CMD_SP(sc) = NULL;
			io_req->sc = NULL;
			sc->result = (DID_ERROR << 16);
			fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = NULL;

			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
			if (sc->scsi_done) {
				FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
					sc->device->host->host_no, id,
					sc,
					jiffies_to_msecs(jiffies - start_time),
					desc,
					(((u64)hdr_status << 40) |
					(u64)sc->cmnd[0] << 32 |
					(u64)sc->cmnd[2] << 24 |
					(u64)sc->cmnd[3] << 16 |
					(u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
					(((u64)CMD_FLAGS(sc) << 32) |
					CMD_STATE(sc)));
				sc->scsi_done(sc);
				atomic64_dec(&fnic_stats->io_stats.active_ios);
				if (atomic64_read(&fnic->io_cmpl_skip))
					atomic64_dec(&fnic->io_cmpl_skip);
				else
					atomic64_inc(&fnic_stats->io_stats.io_completions);
			}
		}

	} else if (id & FNIC_TAG_DEV_RST) {
		/* Completion of device reset */
		CMD_LR_STATUS(sc) = hdr_status;
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			CMD_FLAGS(sc) |= FNIC_DEV_RST_ABTS_PENDING;
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0,
				  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
			FNIC_SCSI_DBG(KERN_WARNING, fnic,
				"Terminate pending "
				"dev reset cmpl recd. id %d status %s\n",
				(int)(id & FNIC_TAG_MASK),
				fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		if (CMD_FLAGS(sc) & FNIC_DEV_RST_TIMED_OUT) {
			/* Need to wait for terminate completion */
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0,
				  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
			FNIC_SCSI_DBG(KERN_ERR, fnic,
				"dev reset cmpl recd after time out. "
				"id %d status %s\n",
				(int)(id & FNIC_TAG_MASK),
				fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		FNIC_SCSI_DBG(KERN_INFO, fnic,
			      "dev reset cmpl recd. id %d status %s\n",
			      (int)(id & FNIC_TAG_MASK),
			      fnic_fcpio_status_to_str(hdr_status));
		if (io_req->dr_done)
			complete(io_req->dr_done);

		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	} else {
		shost_printk(KERN_ERR, fnic->host,
			     "Unexpected itmf io state %s tag %x\n",
			     fnic_ioreq_state_to_str(CMD_STATE(sc)), id);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	}

}

/*
 * fnic_fcpio_cmpl_handler
 * Routine to service the cq for wq_copy
 */
static int fnic_fcpio_cmpl_handler(struct vnic_dev *vdev,
				   unsigned int cq_index,
				   struct fcpio_fw_req *desc)
{
	struct fnic *fnic = vnic_dev_priv(vdev);

	switch (desc->hdr.type) {
	case FCPIO_ICMND_CMPL: /* fw completed a command */
	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
	case FCPIO_FLOGI_FIP_REG_CMPL: /* fw completed flogi_fip_reg */
	case FCPIO_RESET_CMPL: /* fw completed reset */
		atomic64_dec(&fnic->fnic_stats.fw_stats.active_fw_reqs);
		break;
	default:
		break;
	}

	cq_index -= fnic->cpy_wq_base;

	switch (desc->hdr.type) {
	case FCPIO_ACK: /* fw copied copy wq desc to its queue */
		fnic_fcpio_ack_handler(fnic, cq_index, desc);
		break;

	case FCPIO_ICMND_CMPL: /* fw completed a command */
		if (IS_FNIC_FCP_INITIATOR(fnic)) {
			fnic_fcpio_icmnd_cmpl_handler(fnic, cq_index, desc);
		} else if (IS_FNIC_NVME_INITIATOR(fnic)) {
			fnic_fcpio_nvme_fast_cmpl_handler(fnic, desc);
		}
		break;
	case FCPIO_NVME_ERSP_HW_CMPL:
		fnic_fcpio_ersp_cmpl_handler(fnic, desc, 1);
		break;

	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
		if (IS_FNIC_FCP_INITIATOR(fnic)) {
			fnic_fcpio_itmf_cmpl_handler(fnic, cq_index, desc);
		} else if (IS_FNIC_NVME_INITIATOR(fnic)) {
			fnic_fcpio_nvme_itmf_cmpl_handler(fnic, desc);
		}
		break;

	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
	case FCPIO_FLOGI_FIP_REG_CMPL: /* fw completed flogi_fip_reg */
		fnic_fcpio_flogi_reg_cmpl_handler(fnic, desc);
		break;

	case FCPIO_RESET_CMPL: /* fw completed reset */
		fnic_fcpio_fw_reset_cmpl_handler(fnic, desc);
		break;

	default:
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			      "firmware completion type %d\n",
			      desc->hdr.type);
		break;
	}

	return 0;
}

/*
 * fnic_wq_copy_cmpl_handler
 * Routine to process wq copy
 * cq_index is starting from 1st CQ of the device (rq-cq)
 */
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int copy_work_to_do, unsigned int cq_index)
{
	unsigned int cur_work_done;
      struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
      u64 start_jiffies = 0;
      u64 end_jiffies = 0;
      u64 delta_jiffies = 0;
      u64 delta_ms = 0;

	//unsigned int  ioreq_count_before = fnic_count_all_ioreqs(fnic);
      start_jiffies = jiffies;
	cur_work_done = vnic_cq_copy_service(&fnic->cq[cq_index],
					     fnic_fcpio_cmpl_handler,
					     copy_work_to_do);
      end_jiffies = jiffies;
      delta_jiffies = end_jiffies - start_jiffies;
      if(delta_jiffies > (u64) atomic64_read(&misc_stats->max_isr_jiffies)) {
              atomic64_set(&misc_stats->max_isr_jiffies, delta_jiffies);
              delta_ms = jiffies_to_msecs(delta_jiffies);
              atomic64_set(&misc_stats->max_isr_time_ms, delta_ms);
              atomic64_set(&misc_stats->corr_work_done, cur_work_done);
      }

//	printk("fnic num :%d work done in cpy cmpl handler :%d ioreq count before:%d ioreq count after:%d \n",fnic->fnic_num, 
//			cur_work_done, ioreq_count_before, fnic_count_all_ioreqs(fnic));

	return cur_work_done;
}

static void fnic_cleanup_one_io(struct fnic_io_req *io_req, struct fnic *fnic, uint16_t hwq)
{
	int tag;
	struct scsi_cmnd *sc;
	unsigned long start_time = 0;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	if (!io_req) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s: tag:0x%x hwq:%d ioreq is NULL \n",
		      __func__,tag , hwq);
		//goto cleanup_scsi_cmd;
		return;
	}

	tag = io_req->tag;

	sc = scsi_host_find_tag(fnic->host, tag);
	if (!sc) {
		return;
	}

	CMD_SP(sc) = NULL;
	io_req->sc = NULL;
	fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(tag)] = NULL;

	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
		!(CMD_FLAGS(sc) & FNIC_DEV_RST_DONE)) {
		/*
		 * We will be here only when FW completes reset
		 * without sending completions for outstanding ios.
		*/
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		if (io_req && io_req->dr_done)
			complete(io_req->dr_done);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		return;
	} else if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		return;
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

	/*
	 * If there is a scsi_cmnd associated with this io_req, then
	 * free the corresponding state
	 */
	start_time = io_req->start_time;
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

//cleanup_scsi_cmd:
	sc->result = DID_TRANSPORT_DISRUPTED << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		      "%s: tag:0x%x hwq:%d sc duration = %lu DID_TRANSPORT_DISRUPTED\n",
		      __func__,tag , hwq, (jiffies - start_time));

	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);

	/* Complete the command to SCSI */
	if (sc->scsi_done) {
		FNIC_TRACE(fnic_cleanup_io,
			  sc->device->host->host_no, tag, sc,
			  jiffies_to_msecs(jiffies - start_time),
			  0, ((u64)sc->cmnd[0] << 32 |
			  (u64)sc->cmnd[2] << 24 |
			  (u64)sc->cmnd[3] << 16 |
			  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

		sc->scsi_done(sc);
	}
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

}

static void fnic_cleanup_io(struct fnic *fnic, int exclude_id)
{
	int hwq, i;
	unsigned int io_count=0;
	struct fnic_io_req *io_req = NULL;
	io_count = fnic_count_all_ioreqs(fnic);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		"%s: Outstanding ioreq count:%d, active io count=%lld waiting.\n",__func__, io_count,
		atomic64_read(&fnic->fnic_stats.io_stats.active_ios));

	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		for(i=0; i!= fnic->fnic_cpy_wq[hwq].ioreq_table_size; ++i) {
			io_req = fnic->fnic_cpy_wq[hwq].io_req_table[i];
			if(io_req != NULL)
				fnic_cleanup_one_io(io_req, fnic, hwq);
		}
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	}
	/* with sg3utils device reset, SC needs to be retrieved from ioreq */
	spin_lock_irqsave(&fnic->wq_copy_lock[0], fnic->fnic_cpy_wq[0].hw_lock_flags);
	io_req = fnic->fnic_cpy_wq[0].io_req_table[fnic->fnic_max_tag_id];
	if (io_req) {
		struct scsi_cmnd *sc = io_req->sc;
		if (sc) {
			if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
			!(CMD_FLAGS(sc) & FNIC_DEV_RST_DONE)) {
				CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
				if (io_req && io_req->dr_done)
					complete(io_req->dr_done);
			}

		}

	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], fnic->fnic_cpy_wq[0].hw_lock_flags);

	while((io_count = fnic_count_all_ioreqs(fnic))) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s: Outstanding ioreq count:%d, active io count=%lld waiting.\n",
				__func__, io_count,
				atomic64_read(&fnic->fnic_stats.io_stats.active_ios));

		schedule_timeout(msecs_to_jiffies(100));
	}
}

void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq,
				  struct fcpio_host_req *desc)
{
	u32 id;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	unsigned long flags;
	uint16_t hwq;
	unsigned long start_time = 0;

	/* Clean up all outstanding io requests. For FC initiator or NVME
	initiator we issue firmware reset before this and all I/Os are already freed */
	if (IS_FNIC_FCP_INITIATOR(fnic) || IS_FNIC_NVME_INITIATOR(fnic)) {
		return;
	}

	/* get the tag reference */
	fcpio_tag_id_dec(&desc->hdr.tag, &id);
	id &= FNIC_TAG_MASK;

	if (id >= fnic->fnic_max_tag_id)
		return;

	sc = scsi_host_find_tag(fnic->host, id);
	if (!sc)
		return;

	hwq = blk_mq_unique_tag_to_hwq(id);
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	/* Get the IO context which this desc refers to */
	io_req = (struct fnic_io_req *)CMD_SP(sc);

	/* fnic interrupts are turned off by now */

	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto wq_copy_cleanup_scsi_cmd;
	}

	CMD_SP(sc) = NULL;
	io_req->sc = NULL;
	fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(id)] = NULL;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	start_time = io_req->start_time;
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

wq_copy_cleanup_scsi_cmd:
	sc->result = DID_NO_CONNECT << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic, "wq_copy_cleanup_handler:"
		      " DID_NO_CONNECT\n");

	if (sc->scsi_done) {
		FNIC_TRACE(fnic_wq_copy_cleanup_handler,
			  sc->device->host->host_no, id, sc,
			  jiffies_to_msecs(jiffies - start_time),
			  0, ((u64)sc->cmnd[0] << 32 |
			  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
			  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

		sc->scsi_done(sc);
	}
}

static inline int fnic_queue_abort_io_req(struct fnic *fnic, int tag,
					  u32 task_req, u8 *fc_lun,
					  struct fnic_io_req *io_req,
					  unsigned int hwq)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[hwq];
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;
	fnic_tport_t *tport = io_req->tport;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	atomic_inc(&fnic->in_flight);
	atomic_inc(&tport->in_flight);
	//note:-Blocking the below code,FNIC_FLAGS_IO_BLOCKED is not Set anywhere.
	//In RSCN -> delete tport-> fnic_tport_exch_reset() path host_lock is going into dead lock.
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return TERM_IO_BLOCKED;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs_mq(fnic, wq, hwq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"fnic_queue_abort_io_req: failure: no descriptors\n");
		atomic64_inc(&misc_stats->abts_cpwq_alloc_failures);
		return TERM_IO_BLOCKED;
	}

	fnic_queue_wq_copy_desc_itmf(wq, tag | FNIC_TAG_ABORT,
				     0, task_req, tag, fc_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	atomic_dec(&fnic->in_flight);
	atomic_dec(&tport->in_flight);

	return 0;
}


static int fnic_rport_exch_terminate_io(struct fnic_io_req *io_req, struct fnic *fnic,
	       					u32 port_id, int hwq, int new_sc)
{
	int tag;
	int abt_tag;
	int ret=0;
	unsigned long flags;
	struct scsi_cmnd *sc;
	struct reset_stats *reset_stats = &fnic->fnic_stats.reset_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct scsi_lun fc_lun;

	tag = io_req->tag;

	abt_tag = tag;
	
	sc = io_req->sc;

	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"%s tag=0x%p hwq=%d matching for rport:0x%x\n",__func__,
		       	sc, hwq, port_id);
	

	if (!sc) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"%s called sc NULL  tag=0x%x hwq=%d\n",__func__,  tag, hwq);
		return TERM_NO_SC;
	}

	io_req = (struct fnic_io_req *)CMD_SP(sc);

	if (!io_req) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"%s ioreq not found sc 0x%p tag=0x%x hwq=%d\n",__func__, sc, tag, hwq);
		return TERM_IO_REQ_NOT_FOUND;
	}

	if(io_req->port_id != port_id) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"%s port id not matching  looking for :0x%x this io remotport:0x%x  tag=0x%x hwq=%d\n",
				__func__, port_id, io_req->port_id,  tag, hwq);
		return TERM_ANOTHER_PORT;
	}


	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
			(!(CMD_FLAGS(sc) & FNIC_DEV_RST_ISSUED))) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s dev rst not pending sc 0x%p\n",__func__,
				sc);
	}

	if (io_req->abts_done) {
		shost_printk(KERN_ERR, fnic->host,
				"%s: io_req->abts_done is set "
				"state is %s\n",__func__,
				fnic_ioreq_state_to_str(CMD_STATE(sc)));
	}

	if (!(CMD_FLAGS(sc) & FNIC_IO_ISSUED)) {
		shost_printk(KERN_ERR, fnic->host,
				"rport_exch_reset "
				"IO not yet issued %p tag 0x%x flags "
				"%x state %d\n",
				sc, tag, CMD_FLAGS(sc), CMD_STATE(sc));
	}

	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

	if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
		atomic64_inc(&reset_stats->device_reset_terminates);
		abt_tag = (tag | FNIC_TAG_DEV_RST);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s dev rst sc 0x%p\n",__func__,
				sc);
	}

	BUG_ON(io_req->abts_done);
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

#if 1 /*temp disable to suppress noise*/
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"%s: Issuing abts, rport:0x%x tag:0x%x, hwq:%d\n",
			__func__, io_req->port_id, tag, hwq);
#endif /*0*/

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

SEND_TERMINATE:
	if ((ret=fnic_queue_abort_io_req(fnic, abt_tag,
				FCPIO_ITMF_ABT_TASK_TERM,
				fc_lun.scsi_lun, io_req, hwq))) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		return ret;
	} else {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET)
			CMD_FLAGS(sc) |= FNIC_DEV_RST_TERM_ISSUED;
		else
			CMD_FLAGS(sc) |= FNIC_IO_INTERNAL_TERM_ISSUED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		atomic64_inc(&term_stats->terminates);
	}

	/*for lun reset send terminate to the original io if it is not sg3reset*/
	if(abt_tag != tag && !new_sc) {
		abt_tag = tag;
		goto SEND_TERMINATE;
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	return TERM_SUCCESS;
}

void fnic_rport_exch_reset(struct fnic *fnic, u32 port_id)
{

	struct terminate_stats *term_stats;
	unsigned int io_count=0;
	int hwq, i;
	unsigned long flags;
	unsigned int term_count=0, nosc_count=0, noioreq_count=0, another_port_count=0;
	int ret;
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	unsigned long old_state;

	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		      "fnic_rport_exch_reset called portid 0x%06x\n",
		      port_id);

	if (fnic->in_remove)
		return;


	io_count = fnic_count_ioreqs(fnic, port_id);
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		" %s: Starting terminates:: rport:0x%x  portid-io-count:%d active-io-count:%lld\n",
		__func__, port_id, io_count, 
		atomic64_read(&fnic->fnic_stats.io_stats.active_ios));
	
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	atomic_inc(&fnic->in_flight);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		for(i = 0; i!= fnic->fnic_cpy_wq[hwq].ioreq_table_size; ++i) {

			io_req = fnic->fnic_cpy_wq[hwq].io_req_table[i];

			if(!io_req)
				continue;

			if(io_req->port_id != port_id)
				continue;

			sc = scsi_host_find_tag(fnic->host, io_req->tag);

			if(!sc)
				continue;

			if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
				continue;

			/*TODO:Do we need to save the old state in case we are not able to send terminate*/
			old_state = CMD_STATE(sc);
			CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
			
			ret = fnic_rport_exch_terminate_io(io_req, fnic, port_id, hwq, 0);

			switch(ret)
			{
				case TERM_ANOTHER_PORT: another_port_count++; break;
				case TERM_IO_BLOCKED:
					atomic_dec(&fnic->in_flight);
					CMD_STATE(sc) = old_state;
					spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
					return; /*TODO:review*/ 
			}

		}
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[0], fnic->fnic_cpy_wq[0].hw_lock_flags);
	/* if it sg3reset for this port we need to send terminate for this */
	if ((io_req = fnic->fnic_cpy_wq[0].io_req_table[fnic->fnic_max_tag_id]) &&
		(io_req->port_id == port_id)) {

		sc = io_req->sc;

		if (sc && (CMD_STATE(sc) != FNIC_IOREQ_ABTS_PENDING))
		{
			old_state = CMD_STATE(sc);
			CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
			FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s sg3 dev rst sc 0x%p sg\n",__func__,
				sc);
			if (fnic_rport_exch_terminate_io(io_req, fnic, port_id, 0, 1) == TERM_IO_BLOCKED) {
				atomic_dec(&fnic->in_flight);
				CMD_STATE(sc) = old_state;
				spin_unlock_irqrestore(&fnic->wq_copy_lock[0], fnic->fnic_cpy_wq[0].hw_lock_flags);
				return; /*TODO:review*/ 
			}

		}
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], fnic->fnic_cpy_wq[0].hw_lock_flags);

	atomic_dec(&fnic->in_flight);

	/*TODO:Do we need to check for (tport->state == fdls_tgt_state_offline)) blew??
	 * No need to of this */
	while((io_count = fnic_count_ioreqs(fnic, port_id)))
	{	
		schedule_timeout(msecs_to_jiffies(1000));
	}
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				" %s: Done terminating: rport:0x%x  terminated:%d remaining portid-io-count:%d\n"
				"\t\t[nosc_count:%d noioreq_count:%d another_port_count:%d]\n",
				__func__, port_id, term_count, io_count, 
				nosc_count, noioreq_count, another_port_count);

	term_stats = &fnic->fnic_stats.term_stats;
	/*TODO:max_terminate calculation*/
	if (term_count > atomic64_read(&term_stats->max_terminates))
		atomic64_set(&term_stats->max_terminates, term_count);
}



void fnic_terminate_rport_io(struct fc_rport *rport)
{
	fnic_tport_t * tport;
	rport_dd_data_t * rdd_data;

	if (!rport) {
		printk(KERN_ERR "fnic_terminate_rport_io: rport is NULL\n");
		return;
	}

	rdd_data = rport->dd_data;
	if(rdd_data){
		tport = rdd_data->tport;
		if(!tport)  {
			printk(KERN_DEBUG "fnic_terminate_rport_io called after tport is deleted. Returning 0x%8x\n", rport->port_id);
			return;
		} else {
			printk(KERN_ERR "fnic_terminate_rport_io called after tport is set 0x%8x. tport maybe rediscovered\n", rport->port_id);
		}
	}else{
		return;
	}
	return;
}


/*
 * FCP-SCSI specific handling for module unload
 *
 */
void
fnic_scsi_unload(struct fnic *fnic)
{
	int hwq = 0;
	unsigned long flags;

	/*
	 * Mark state so that the workqueue thread stops forwarding
	 * received frames and link events to the local port. ISR and
	 * other threads that can queue work items will also stop
	 * creating work items on the fnic workqueue
	 */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->iport.state = FNIC_IPORT_STATE_LINK_WAIT;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (fdls_get_state(&fnic->iport.fabric) != FDLS_STATE_INIT)
		fnic_scsi_fcpio_reset(fnic);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->in_remove = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	fnic_flush_tport_event_list(fnic);
	fnic_delete_fcp_tports(fnic);
	fc_remove_host(fnic->host);
	scsi_remove_host(fnic->host);
	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++)
		kfree( fnic->fnic_cpy_wq[hwq].io_req_table);

}

/*
 * This function is exported to SCSI for sending abort cmnds.
 * A SCSI IO is represented by a io_req in the driver.
 * The ioreq is linked to the SCSI Cmd, thus a link with the ULP's IO.
 * Called without fnic lock. Returns without fnic lock.
 * Acquires and relinquishes fnic lock many times.
 * Acquires and relinquishes copy wq lock many times.
 */
int fnic_abort_cmd(struct scsi_cmnd *sc)
{
        fnic_iport_t *iport;
        fnic_tport_t *tport;
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	rport_dd_data_t * rdd_data;
	unsigned long flags;
	unsigned long start_time = 0;
	int ret = SUCCESS;
	u32 task_req = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct abort_stats *abts_stats;
	struct terminate_stats *term_stats;
	enum fnic_ioreq_state old_ioreq_state;
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      struct request *scsi_req = scsi_cmd_to_rq(sc);
      int tag = scsi_req->tag;
#else
	int tag = sc->request->tag;
#endif
	unsigned long abt_issued_time;
	uint16_t hwq=0;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	/* Wait for rport to unblock */
#if defined(__RHEL56__)
	fnic_block_error_handler(sc);
#else	
	fc_block_scsi_eh(sc);
#endif


	/* Get local-port, check ready and link up */
	fnic = *((struct fnic **)shost_priv(sc->device->host));
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	iport = &fnic->iport;

	fnic_stats = &fnic->fnic_stats;
	abts_stats = &fnic->fnic_stats.abts_stats;
	term_stats = &fnic->fnic_stats.term_stats;

	rport = starget_to_rport(scsi_target(sc->device));
	
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	if (shost_use_blk_mq(sc->device->host)) {
#endif

#if FNIC_HAVE_SCSI_CMD_TO_RQ
              tag = blk_mq_unique_tag(scsi_cmd_to_rq(sc));
#else
		tag = blk_mq_unique_tag(sc->request);
#endif

		hwq = blk_mq_unique_tag_to_hwq(tag);
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	}
#endif
	rdd_data = rport->dd_data;
	tport = rdd_data->tport;

	if (!tport) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			"Abort Cmd called after tport delete!! FCID 0x%x, LUN 0x%llu TAG 0x%x hwq:0x%x Op=0x%x flags %x\n",
			rport->port_id, sc->device->lun, tag, hwq, sc->cmnd[0], CMD_FLAGS(sc));
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_abort_cmd_end;
	}
	FNIC_SCSI_DBG(KERN_INFO,
		fnic,
		"Abort Cmd called FCID 0x%x, LUN 0x%llu TAG 0x%x hwq:0x%x Op=0x%x flags %x\n",
		rport->port_id, sc->device->lun, tag, hwq, sc->cmnd[0], CMD_FLAGS(sc));

	if (iport->state != FNIC_IPORT_STATE_READY) {
		atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic, "iport NOT in READY state");
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_abort_cmd_end;
        }
     if ((tport->state != fdls_tgt_state_ready) &&
	        (tport->state != fdls_tgt_state_adisc)) {
	        FNIC_SCSI_DBG(KERN_DEBUG, fnic,
	                    "tport state : %d\n",tport->state);	 
         spin_unlock_irqrestore(&fnic->fnic_lock, flags);    
	     goto fnic_abort_cmd_end;           
	  }
 	 spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	/*
	 * Avoid a race between SCSI issuing the abort and the device
	 * completing the command.
	 *
	 * If the command is already completed by the fw cmpl code,
	 * we just return SUCCESS from here. This means that the abort
	 * succeeded. In the SCSI ML, since the timeout for command has
	 * happened, the completion wont actually complete the command
	 * and it will be considered as an aborted command
	 *
	 * The CMD_SP will not be cleared except while holding io_req_lock.
	 */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto fnic_abort_cmd_end;
	}

      io_req->abts_done = &tm_done;

	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto wait_pending;
	}

	abt_issued_time = jiffies_to_msecs(jiffies) - jiffies_to_msecs(io_req->start_time);
	if (abt_issued_time <= 6000)
		atomic64_inc(&abts_stats->abort_issued_btw_0_to_6_sec);
	else if (abt_issued_time > 6000 && abt_issued_time <= 20000)
		atomic64_inc(&abts_stats->abort_issued_btw_6_to_20_sec);
	else if (abt_issued_time > 20000 && abt_issued_time <= 30000)
		atomic64_inc(&abts_stats->abort_issued_btw_20_to_30_sec);
	else if (abt_issued_time > 30000 && abt_issued_time <= 40000)
		atomic64_inc(&abts_stats->abort_issued_btw_30_to_40_sec);
	else if (abt_issued_time > 40000 && abt_issued_time <= 50000)
		atomic64_inc(&abts_stats->abort_issued_btw_40_to_50_sec);
	else if (abt_issued_time > 50000 && abt_issued_time <= 60000)
		atomic64_inc(&abts_stats->abort_issued_btw_50_to_60_sec);
	else
		atomic64_inc(&abts_stats->abort_issued_greater_than_60_sec);

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		"CDB Opcode: %02x Abort issued time: %lu msec\n", sc->cmnd[0], abt_issued_time);
	/*
	 * Command is still pending, need to abort it
	 * If the firmware completes the command after this point,
	 * the completion wont be done till mid-layer, since abort
	 * has already started.
	 */
	old_ioreq_state = CMD_STATE(sc);
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/*
	 * Check readiness of the remote port. If the path to remote
	 * port is up, then send abts to the remote port to terminate
	 * the IO. Else, just locally terminate the IO in the firmware
	 */
	if (fc_remote_port_chkready(rport) == 0)
		task_req = FCPIO_ITMF_ABT_TASK;
	else {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	}

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, tag, task_req,
				    fc_lun.scsi_lun, io_req, hwq)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
			CMD_STATE(sc) = old_ioreq_state;
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->abts_done = NULL;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	if (task_req == FCPIO_ITMF_ABT_TASK) {
		CMD_FLAGS(sc) |= FNIC_IO_ABTS_ISSUED;
		atomic64_inc(&fnic_stats->abts_stats.aborts);
	} else {
		CMD_FLAGS(sc) |= FNIC_IO_TERM_ISSUED;
		atomic64_inc(&fnic_stats->term_stats.terminates);
	}

	/*
	 * We queued an abort IO, wait for its completion.
	 * Once the firmware completes the abort command, it will
	 * wake up this thread.
	 */
 wait_pending:
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies
				    (2 * fnic->config.ra_tov +
				     fnic->config.ed_tov));

	/* Check the abort status */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	io_req->abts_done = NULL;

	/* fw did not complete abort, timed out */
	if (CMD_ABTS_STATUS(sc) == FCPIO_INVALID_CODE) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if (task_req == FCPIO_ITMF_ABT_TASK) {
			atomic64_inc(&abts_stats->abort_drv_timeouts);
		} else {
			atomic64_inc(&term_stats->terminate_drv_timeouts);
		}
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_TIMED_OUT;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	/* IO out of order */

	if (!(CMD_FLAGS(sc) & (FNIC_IO_ABORTED | FNIC_IO_DONE))) {
		FNIC_SCSI_DBG(KERN_ERR, fnic,
			"possible out of order IO\n");
	
/*TODO:Revisit the driver cause of out of order*/
#if 0	
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_INFO, fnic,
			"Issuing Host reset due to out of order IO\n");
		if (fnic_host_reset(sc) == FAILED) {
			FNIC_SCSI_DBG(KERN_INFO, fnic,
				      "fnic_host_reset failed.\n");
		}
		ret = FAILED;
		goto fnic_abort_cmd_end;
#endif
	}

	CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;

	start_time = io_req->start_time;
	/*
	 * firmware completed the abort, check the status,
	 * free the io_req if successful. If abort fails,
	 * Device reset will clean the I/O.
	 */
	if ((CMD_ABTS_STATUS(sc) == FCPIO_SUCCESS) || (CMD_ABTS_STATUS(sc) == FCPIO_ABORTED)) {
		CMD_SP(sc) = NULL;
		io_req->sc = NULL;
	} else {
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto fnic_abort_cmd_end;
	}

	fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(tag)] = NULL;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

	if (sc->scsi_done) {
	/* Call SCSI completion function to complete the IO */
		sc->result = (DID_ABORT << 16);
		sc->scsi_done(sc);
		atomic64_dec(&fnic_stats->io_stats.active_ios);
		if (atomic64_read(&fnic->io_cmpl_skip))
			atomic64_dec(&fnic->io_cmpl_skip);
		else
			atomic64_inc(&fnic_stats->io_stats.io_completions);
	}

fnic_abort_cmd_end:
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      scsi_req = scsi_cmd_to_rq(sc);
	FNIC_TRACE(fnic_abort_cmd, sc->device->host->host_no,
		  scsi_req->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#else
	FNIC_TRACE(fnic_abort_cmd, sc->device->host->host_no,
		  sc->request->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#endif

	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		      "Returning from abort cmd type %x %s\n", task_req,
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");
	return ret;
}

static inline int fnic_queue_dr_io_req(struct fnic *fnic,
				       struct scsi_cmnd *sc,
				       struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct Scsi_Host *host = fnic->host;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	struct scsi_lun fc_lun;
	int ret = 0;
	unsigned long intr_flags;
	fnic_tport_t *tport = io_req->tport;
	uint16_t hwq=0;
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      struct request *scsi_req = scsi_cmd_to_rq(sc);
      uint32_t tag=scsi_req->tag;
#else
	uint32_t tag=sc->request->tag;
#endif

#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	if (shost_use_blk_mq(sc->device->host)) {
#endif
		tag = io_req->tag;
		hwq = blk_mq_unique_tag_to_hwq(tag);
		wq = &fnic->wq_copy[hwq];
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
	} else {
		tag = io_req->tag;
		hwq = 0;
	}
#endif

	spin_lock_irqsave(&fnic->fnic_lock, intr_flags);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		spin_unlock_irqrestore(host->host_lock, intr_flags);
		return FAILED;
	} else {
		atomic_inc(&fnic->in_flight);
		atomic_inc(&tport->in_flight);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, intr_flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs_mq(fnic, wq, hwq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			  "queue_dr_io_req failure - no descriptors\n");
		atomic64_inc(&misc_stats->devrst_cpwq_alloc_failures);
		ret = -EAGAIN;
		goto lr_io_req_end;
	}

	/* fill in the lun info */
	int_to_scsilun(sc->device->lun, &fc_lun);

	fnic_queue_wq_copy_desc_itmf(wq, tag | FNIC_TAG_DEV_RST,
				     0, FCPIO_ITMF_LUN_RESET, SCSI_NO_TAG,
				     fc_lun.scsi_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

lr_io_req_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], intr_flags);
	atomic_dec(&fnic->in_flight);
	atomic_dec(&tport->in_flight);

	return ret;
}

struct lun_io_data {
	struct fnic *fnic;
	struct scsi_device *lun;
	struct scsi_cmnd * lr_sc;
	int return_code;
};


static int fnic_clean_a_pending_abort(struct fnic_io_req *io_req, struct fnic *fnic, 
					struct scsi_cmnd *lr_sc,	 uint16_t hwq)
{
	int tag, abt_tag;
	struct scsi_cmnd *sc;
	struct scsi_lun fc_lun;
	int ret;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	if(!io_req) {
		return  TERM_ANOTHER_PORT;
	}

	tag = io_req->tag;

	sc = io_req->sc;

	if (!sc) {
		return TERM_NO_SC;
	}

	if(sc->device != lr_sc->device) {
		return  TERM_ANOTHER_PORT;
	}

	io_req = (struct fnic_io_req *)CMD_SP(sc);

	if (!io_req) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
				"%s ioreq not found sc 0x%p tag=0x%x hwq=%d\n",__func__, sc, tag, hwq);
		return TERM_IO_REQ_NOT_FOUND;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to the LUN that we are resetting
	 */
	FNIC_SCSI_DBG(KERN_DEBUG, fnic,
			  "Found IO in %s on lun\n",
			   fnic_ioreq_state_to_str(CMD_STATE(sc)));


	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
			(!(CMD_FLAGS(sc) & FNIC_DEV_RST_ISSUED))) {
			FNIC_SCSI_DBG(KERN_INFO, fnic,
			"%s dev rst not pending sc 0x%p\n", __func__,
			sc);
		return TERM_MISC;
	}

	if (io_req->abts_done)
			shost_printk(KERN_ERR, fnic->host,
			  "%s: io_req->abts_done is set state is %s\n",
			  __func__, fnic_ioreq_state_to_str(CMD_STATE(sc)));

		/*
		 * Any pending IO issued prior to reset is expected to be
		 * in abts pending state, if not we need to set
		 * FNIC_IOREQ_ABTS_PENDING to indicate the IO is abort pending.
		 * When IO is completed, the IO will be handed over and
		 * handled in this function.
		 */
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;

	BUG_ON(io_req->abts_done);

	abt_tag = tag;

	if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
			FNIC_SCSI_DBG(KERN_INFO, fnic,
				  "%s: dev rst sc 0x%p\n", __func__, sc);
	}

	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;
	io_req->abts_done = &tm_done;

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

	if ((ret=fnic_queue_abort_io_req(fnic, abt_tag,
				FCPIO_ITMF_ABT_TASK_TERM,
				fc_lun.scsi_lun, io_req, blk_mq_unique_tag_to_hwq(tag)))) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->abts_done = NULL;
		return ret;
	} else {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET)
			CMD_FLAGS(sc) |= FNIC_DEV_RST_TERM_ISSUED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	}
	CMD_FLAGS(sc) |= FNIC_IO_INTERNAL_TERM_ISSUED;

	wait_for_completion_timeout(&tm_done,
			msecs_to_jiffies(fnic->config.ed_tov));

	/* Recheck cmd state to check if it is now aborted */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		return TERM_IO_REQ_NOT_FOUND;
	}

	io_req->abts_done = NULL;

	/* if abort is still pending with fw, fail */
	if (CMD_ABTS_STATUS(sc) == FCPIO_INVALID_CODE) {
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_DONE;
		return TERM_TIMED_OUT;
	}
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;

	/* original sc used for lr is handled by dev reset code */
	if (sc != lr_sc) {
		CMD_SP(sc) = NULL;
		fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(tag)] = NULL;
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

	/* original sc used for lr is handled by dev reset code */
	if (sc != lr_sc) {
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);

		/*
		 * Any IO is returned during reset, it needs to call scsi_done
		 * to return the scsi_cmnd to upper layer.
		 */
		if (sc->scsi_done) {
			/* Set result to let upper SCSI layer retry */
			sc->result = DID_RESET << 16;
			sc->scsi_done(sc);
		}
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);

//clean_pending_aborts_end:
	return TERM_SUCCESS;
}


/*
 * Clean up any pending aborts on the lun
 * For each outstanding IO on this lun, whose abort is not completed by fw,
 * issue a local abort. Wait for abort to complete. Return 0 if all commands
 * successfully aborted, 1 otherwise
 */
static int fnic_clean_pending_aborts_mq(struct fnic *fnic,
				     struct scsi_cmnd *lr_sc,
					 bool new_sc)

{
	unsigned long flags;
	int ret = 0;
	int hwq, i;
	unsigned int term_count=0, nosc_count=0, noioreq_count=0, another_port_count=0, io_count=0;
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	unsigned long old_state;

	/*TODO:Review: since new sc is not in the ioreq list any more nothing to do for it*/

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	atomic_inc(&fnic->in_flight);

	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return TERM_IO_BLOCKED;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
		for(i=0; i!= fnic->fnic_cpy_wq[hwq].ioreq_table_size; ++i) {
			io_req = fnic->fnic_cpy_wq[hwq].io_req_table[i];

			if(!io_req)
				continue;
			sc = io_req->sc;

			if(!sc)
				continue;

			if(sc->device != lr_sc->device)
				continue;

			if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
				continue;

			/* fnic_clean_pending_aborts_mq could change the state so save old state*/
			old_state = CMD_STATE(sc);

			ret = fnic_clean_a_pending_abort(io_req, fnic, lr_sc, hwq);
			switch(ret)
				{

				case TERM_SUCCESS: term_count++; break;
				case TERM_NO_SC: nosc_count++; break;
				case TERM_IO_REQ_NOT_FOUND: noioreq_count++; break;
				case TERM_ANOTHER_PORT: another_port_count++; break;
				case TERM_IO_BLOCKED:
				case TERM_OUT_OF_WQ_DESC:
				case TERM_TIMED_OUT:
				case TERM_MISC:
					atomic_dec(&fnic->in_flight);
					CMD_STATE(sc) = old_state;
					spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
					FNIC_SCSI_DBG(KERN_DEBUG, fnic,
							" %s: count not terminate ioreq:0x%p  terminated:%d remaining portid-io-count:%d\n"
							"\t\t[nosc_count:%d noioreq_count:%d another_lun_count:%d]\n",
							__func__, io_req, term_count, io_count,
							nosc_count, noioreq_count, another_port_count);
					return ret; /*TODO:review, returning error makes sense to me*/ 
				}
		}
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], fnic->fnic_cpy_wq[hwq].hw_lock_flags);
	}

	atomic_dec(&fnic->in_flight);


	return 0;/*SUCCESS*/
}

extern bool blk_mq_get_driver_tag(struct request *rq, struct blk_mq_hw_ctx **hctx,
					   bool wait);
/*
 * SCSI Eh thread issues a Lun Reset when one or more commands on a LUN
 * fail to get aborted. It calls driver's eh_device_reset with a SCSI command
 * on the LUN.
 */
int fnic_device_reset(struct scsi_cmnd *sc)
{
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	int status, count = 0;
	int ret = FAILED;
	unsigned long flags;
	unsigned long start_time = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct reset_stats *reset_stats;
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      struct request *scsi_req = scsi_cmd_to_rq(sc);
      int tag = scsi_req->tag;
#else
	int tag = sc->request->tag;
#endif
	DECLARE_COMPLETION_ONSTACK(tm_done);
	bool new_sc = 0;
	uint16_t hwq=0;
	fnic_iport_t *iport = NULL;
	rport_dd_data_t * rdd_data;
  	fnic_tport_t * tport;
	u32 old_soft_reset_count;
	u32 old_link_down_cnt;
	bool exit_dr = 0;
	/* Wait for rport to unblock */
#if defined(__RHEL56__)
	fnic_block_error_handler(sc);
#else
	fc_block_scsi_eh(sc);
#endif

	/* Get local-port, check ready and link up */
	fnic = *((struct fnic **)shost_priv(sc->device->host));
	iport = &fnic->iport;

	fnic_stats = &fnic->fnic_stats;
	reset_stats = &fnic->fnic_stats.reset_stats;

	atomic64_inc(&reset_stats->device_reset_called);

	rport = starget_to_rport(scsi_target(sc->device));
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	FNIC_SCSI_DBG(KERN_INFO, fnic,
		      "Device reset called FCID 0x%x, LUN 0x%llu sc 0x%p\n",
		      rport->port_id, sc->device->lun, sc);

	rdd_data= rport->dd_data;
	tport = rdd_data->tport;
	if (!tport) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic,
		"Device reset called after tport delete!! FCID 0x%x, LUN 0x%llu sc 0x%p \n",
		rport->port_id, sc->device->lun, sc);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
	}
	if (iport->state != FNIC_IPORT_STATE_READY) {
                atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
        }
	if ((tport->state != fdls_tgt_state_ready) &&
             (tport->state != fdls_tgt_state_adisc)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Check if remote port up */
	if (fc_remote_port_chkready(rport)) {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		goto fnic_device_reset_end;
	}

	CMD_FLAGS(sc) = FNIC_DEVICE_RESET;
	/* Allocate tag if not present */
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      scsi_req = scsi_cmd_to_rq(sc);
      tag = scsi_req->tag;
#else
	tag = sc->request->tag;
#endif
	if (unlikely(tag < 0)) {
		/*
		 * For device reset issued through sg3utils, we let
		 * only one LUN_RESET to go through and use a special
		 * tag equal to max_tag_id so that we dont have to allocate
		 * or free it. It wont interact with tags allocated by midlayer
		 */
		mutex_lock(&fnic->sg3utils_devreset_mutex);
		tag = fnic->fnic_max_tag_id;
		new_sc = 1;
	} else {
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
		if (shost_use_blk_mq(sc->device->host)) {
#endif

#if FNIC_HAVE_SCSI_CMD_TO_RQ
                      tag = blk_mq_unique_tag(scsi_cmd_to_rq(sc));
#else
			tag = blk_mq_unique_tag(sc->request);
#endif

			hwq = blk_mq_unique_tag_to_hwq(tag);
#if FNIC_HAVE_SHOST_USE_BLK_MQ	
		}
#endif
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);

	/*
	 * If there is a io_req attached to this command, then use it,
	 * else allocate a new one.
	 */
	if (!io_req) {
		io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
		if (!io_req) {
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			goto fnic_device_reset_end;
		}
		memset(io_req, 0, sizeof(*io_req));
		io_req->port_id = rport->port_id;
		io_req->tport = tport;
		CMD_SP(sc) = (char *)io_req;
		io_req->sc = sc;

		if(fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(tag)] != NULL)
			FNIC_SCSI_DBG(KERN_ERR, fnic, "%s:tag %d already exists\n",__func__, tag);

		fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(tag)] 
			= io_req;

		io_req->tag=tag;
	}
	io_req->dr_done = &tm_done;
	CMD_STATE(sc) = FNIC_IOREQ_CMD_PENDING;
	CMD_LR_STATUS(sc) = FCPIO_INVALID_CODE;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	FNIC_SCSI_DBG(KERN_INFO, fnic, "TAG %x\n", tag);

	/*
	 * issue the device reset, if enqueue failed, clean up the ioreq
	 * and break assoc with scsi cmd
	 */
	if (fnic_queue_dr_io_req(fnic, sc, io_req)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->dr_done = NULL;
		goto fnic_device_reset_clean;
	}
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	CMD_FLAGS(sc) |= FNIC_DEV_RST_ISSUED;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	old_link_down_cnt = iport->fnic->link_down_cnt;
	old_soft_reset_count = fnic->soft_reset_count;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	/*
	 * Wait on the local completion for LUN reset.  The io_req may be
	 * freed while we wait since we hold no lock.
	 */
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies(FNIC_LUN_RESET_TIMEOUT));


	/* The wakeup can be :-
	 * 1) The device reset completed from target.
	 * 2) Device reset timed out.
	 * 3) A link-down/host_reset may have happened inbetween.
	 * 4) The device reset was Aborted and io_req->dr_done called
	*/
	exit_dr=0;
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if ( (old_link_down_cnt != fnic->link_down_cnt) || // Physical link went down
		(fnic->reset_in_progress) ||                         // Soft host_reset is in progress
		(fnic->soft_reset_count != old_soft_reset_count ) ||   //  Soft host_reset have completed
		(iport->state != FNIC_IPORT_STATE_READY))              //Extra iport state check
	{
		exit_dr = 1;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic,
				"io_req is null tag 0x%x sc 0x%p\n", tag, sc);
		goto fnic_device_reset_end;
	}
	if(exit_dr){
		FNIC_SCSI_DBG(KERN_ERR, fnic,
		"Exiting DeviceReset! looks like Host reset happened for fcid :0x%x \n", fnic->fnic_num );
		io_req->dr_done = NULL;
		goto fnic_device_reset_clean;
	}

	io_req->dr_done = NULL;

	status = CMD_LR_STATUS(sc);

	/*
	 * If lun reset not completed, bail out with failed. io_req
	 * gets cleaned up during higher levels of EH
	 */
	if (status == FCPIO_INVALID_CODE) {
		atomic64_inc(&reset_stats->device_reset_timeouts);
		FNIC_SCSI_DBG(KERN_ERR, fnic,
			      "Device reset timed out\n");
		CMD_FLAGS(sc) |= FNIC_DEV_RST_TIMED_OUT;
		int_to_scsilun(sc->device->lun, &fc_lun);
		goto fnic_device_reset_clean;
	} else {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	}

	/* Completed, but not successful, clean up the io_req, return fail */
	if (status != FCPIO_SUCCESS) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_WARNING, fnic,
			      "Device reset completed - failed\n");
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		goto fnic_device_reset_clean;
	}

	/*
	 * Clean up any aborts on this lun that have still not
	 * completed. If any of these fail, then LUN reset fails.
	 * clean_pending_aborts cleans all cmds on this lun except
	 * the lun reset cmd. If all cmds get cleaned, the lun reset
	 * succeeds
	 */
	if (fnic_clean_pending_aborts_mq(fnic, sc, new_sc)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		FNIC_SCSI_DBG(KERN_ERR, fnic,
			      "Device reset failed"
			      " since could not abort all IOs\n");
		goto fnic_device_reset_clean;
	}

	/* Clean lun reset command */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (io_req)
		/* Completed, and successful */
		ret = SUCCESS;

fnic_device_reset_clean:
	if (io_req) {
		CMD_SP(sc) = NULL;
		io_req->sc = NULL;
		fnic->fnic_cpy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(io_req->tag)] = NULL;
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	if (io_req) {
		start_time = io_req->start_time;
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}
	/* If link-event is seen while LUN reset is issued we need to complete the LUN reset here */
	if (!new_sc && sc->scsi_done) {
		sc->result = DID_RESET << 16;
		sc->scsi_done(sc);
	}

fnic_device_reset_end:
#if FNIC_HAVE_SCSI_CMD_TO_RQ
      scsi_req = scsi_cmd_to_rq(sc);
	FNIC_TRACE(fnic_device_reset, sc->device->host->host_no,
		  scsi_req->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#else
	FNIC_TRACE(fnic_device_reset, sc->device->host->host_no,
		  sc->request->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
#endif

	/* release mutex so that another sg3utils can proceed */
	if (new_sc) {
		mutex_unlock(&fnic->sg3utils_devreset_mutex);
	}

	while((ret == SUCCESS) && fnic_count_lun_ioreqs(fnic, sc->device))
	{
		if (count >= 2) {
			ret = FAILED;
			break;
		}
		FNIC_SCSI_DBG(KERN_ERR, fnic,
				" %s:unable to clean-up all the IOs for the LUN %p\n", __FUNCTION__, sc);
		schedule_timeout(msecs_to_jiffies(1000));
		count++;
	}

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		      "Returning from device reset %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");

	if (ret == FAILED)
		atomic64_inc(&reset_stats->device_reset_failures);

	return ret;
}

static  void
fnic_post_flogo_linkflap(struct fnic *fnic)
{

	unsigned long flags;

	fnic_fdls_link_status_change(fnic, 0);
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->link_status) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fnic_fdls_link_status_change(fnic, 1);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

/* Logout from all the targets and simulate link flap */
int fnic_reset(struct Scsi_Host *shost)
{
	struct fnic *fnic;
	int ret = 0;
	struct reset_stats *reset_stats;

	fnic = *((struct fnic **)shost_priv(shost));
	reset_stats = &fnic->fnic_stats.reset_stats;

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		      "Issuing fnic reset\n");

	atomic64_inc(&reset_stats->fnic_resets);

        fnic_post_flogo_linkflap(fnic);

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		      "Returning from fnic reset %s\n",
		      (ret == 0) ?
		      "SUCCESS" : "FAILED");

	if (ret == 0)
		atomic64_inc(&reset_stats->fnic_reset_completions);
	else
		atomic64_inc(&reset_stats->fnic_reset_failures);

	return ret;
}

int
fnic_issue_fc_host_lip(struct Scsi_Host *shost)
{
	int ret = 0;
	struct fnic *fnic = *((struct fnic **)shost_priv(shost));

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		"FC host lip issued");

	ret = fnic_host_reset(shost);
	return ret;
}


int fnic_host_reset(struct Scsi_Host *shost)
{
	int ret;
	unsigned long wait_host_tmo;
	struct fnic *fnic = *((struct fnic **)shost_priv(shost));
	unsigned long flags;
	fnic_iport_t *iport = &fnic->iport;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->reset_in_progress == NOT_IN_PROGRESS) {
		fnic->reset_in_progress = IN_PROGRESS;
	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		wait_for_completion_timeout(&fnic->reset_completion_wait,
			msecs_to_jiffies(10000));

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->reset_in_progress == IN_PROGRESS) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			FNIC_SCSI_DBG(KERN_WARNING, fnic,
				"Firmware reset in progress. Skipping another host reset\n");
			return SUCCESS;
		}
                fnic->reset_in_progress = IN_PROGRESS;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * If fnic_reset is successful, wait for fabric login to complete
	 * scsi-ml tries to send a TUR to every device if host reset is
	 * successful, so before returning to scsi, fabric should be up
	*/
	ret = (fnic_reset(shost) == 0) ? SUCCESS : FAILED;
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->reset_in_progress = NOT_IN_PROGRESS;
	complete(&fnic->reset_completion_wait);
	fnic->soft_reset_count++;
	//wait till the link is up
	if (fnic->link_status) {
		wait_host_tmo = jiffies + FNIC_HOST_RESET_SETTLE_TIME * HZ;
		ret = FAILED;
		while (time_before(jiffies, wait_host_tmo)) {
			if(iport->state != FNIC_IPORT_STATE_READY && fnic->link_status) {
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				ssleep(1);
				spin_lock_irqsave(&fnic->fnic_lock, flags);
			}else{
				ret = SUCCESS;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return ret;
}

/*
 * SCSI Error handling calls driver's eh_host_reset if all prior
 * error handling levels return FAILED. If host reset completes
 * successfully, and if link is up, then Fabric login begins.
 *
 * Host Reset is the highest level of error recovery. If this fails, then
 * host is offlined by SCSI.
 *
 */
int
fnic_eh_host_reset_handler(struct scsi_cmnd *sc)
{
	int ret = 0;
	struct Scsi_Host *shost = sc->device->host;
	struct fnic *fnic = *((struct fnic **)shost_priv(shost));

	FNIC_SCSI_DBG(KERN_INFO, fnic,
		"FC fnic_eh_host_reset_handler");

	ret = fnic_host_reset(shost);
	return ret;
}

/*
 *
 */
void fnic_scsi_fcpio_reset(struct fnic *fnic)
{
	unsigned long flags;
	enum fnic_state old_state;
	fnic_iport_t *iport = &fnic->iport;
	DECLARE_COMPLETION_ONSTACK(fw_reset_done);
	int time_remain;

	/* issue fw reset */
        spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)) {
		/* fw reset is in progress, poll for its completion */
	    spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	    fnic_log_info(fnic->fnic_num,
		"fNIC is in unexpected state(%d) for fw_reset\n",
		fnic->state);
	    return;
	}
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	fnic_update_mac_locked(fnic, iport->hwmac);
	fnic->fw_reset_done = &fw_reset_done;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	if (fnic_fw_reset_handler(fnic)) {
	    spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	} else {
		time_remain = wait_for_completion_timeout(&fw_reset_done,
                             msecs_to_jiffies
                             (FNIC_FW_RESET_TIMEOUT));
		if (time_remain == 0) {
			fnic_log_info(fnic->fnic_num,
				"FW_RESET completion Timed out (%d msecs)\n",
				 FNIC_FW_RESET_TIMEOUT);
	        }
		atomic64_inc(&fnic->fnic_stats.reset_stats.fw_reset_timeouts);
	}
	fnic->fw_reset_done = NULL;
}
