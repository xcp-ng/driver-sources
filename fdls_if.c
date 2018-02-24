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

#include "fnic.h"
#include "fnic_res.h"
#include "cq_enet_desc.h"
#include "cq_exch_desc.h"
#include "fnic_trace.h"
#include "fdls_fc.h"
#include "fnic_fdls.h"
#include <linux/etherdevice.h>

#define MAX_RESET_WAIT_COUNT    64
extern spinlock_t fnic_list_lock;
extern spinlock_t reset_fnic_list_lock;

/* Frame initialization */
/*
 * Variables:
 * dst_mac, src_mac
 */
fnic_eth_hdr_t fnic_eth_hdr_fcoe = {
        .ether_type = 0x0689
};

static int FNIC_IS_NVFNIC_FRAME(fc_hdr_t *fchdr)
{
        return (fchdr->type == 0x28);
}

/*
 * Variables:
 * None
 */
fnic_fcoe_hdr_t fnic_fcoe_hdr = {
        .sof = 0x2E
};

/* TBD_REVISIT do it properly with add/release etc */

uint8_t fcoe_all_fcf_mac[6] = {0x0e, 0xfc, 0x00, 0xff, 0xff, 0xfe};


/* external */
extern int fnic_fw_reset_handler(struct fnic *fnic);
extern void fdls_delete_tport(fnic_iport_t *iport, fnic_tport_t *tport);

static inline int fnic_import_rq_eth_pkt(struct fnic *fnic, void *fp);
extern struct workqueue_struct *fnic_event_queue;
extern struct workqueue_struct *fnic_fip_queue;
void nvfnic_lsrsp_recv(fnic_iport_t *iport, fc_hdr_t *fchdr, int len);
/*
 * Internal Functions
 * This function will initialize the src_mac address to be
 * used in outgoing frames
 */
static void inline
fnic_fdls_set_fcoe_srcmac(struct fnic *fnic, uint8_t *src_mac)
{
	fnic_log_info(fnic->fnic_num,
            "Setting src mac: %02x:%02x:%02x:%02x:%02x:%02x",
            src_mac[0], src_mac[1], src_mac[2],
            src_mac[3], src_mac[4], src_mac[5]);

        memcpy(fnic->iport.fpma, src_mac, 6);
}

void fnic_update_mac_locked(struct fnic *fnic, u8 *new)
{
	fnic_iport_t *iport = &fnic->iport;
	u8 *ctl = iport->hwmac;
	u8 *data = fnic->data_src_addr;

	if (is_zero_ether_addr(new))
		new = ctl;
        if (ether_addr_equal(data, new))
                return;

	fnic_log_info(fnic->fnic_num, "update_mac %p", new);
	if (!is_zero_ether_addr(data) && !ether_addr_equal(data, ctl))
		vnic_dev_del_addr(fnic->vdev, data);

	memcpy(data, new, ETH_ALEN);
	if (!ether_addr_equal(new, ctl))
		vnic_dev_add_addr(fnic->vdev, new);
}


/*
 * This function will initialize the dst_mac address to be
 * used in outgoing frames
 */
static void inline
fnic_fdls_set_fcoe_dstmac(struct fnic *fnic, uint8_t *dst_mac)
{
	fnic_log_info(fnic->fnic_num,
            "Setting dst_mac: %02x:%02x:%02x:%02x:%02x:%02x",
            dst_mac[0], dst_mac[1], dst_mac[2],
            dst_mac[3], dst_mac[4], dst_mac[5]);

        memcpy(fnic->iport.fcfmac, dst_mac, 6);
}

void fnic_get_host_port_state(struct Scsi_Host *shost)
{
	struct fnic *fnic = *((struct fnic **)shost_priv(shost));
        struct fnic_iport_s *iport = &fnic->iport;
	unsigned long flags;
	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);

	if (!fnic->link_status)
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
	else if (iport->state == FNIC_IPORT_STATE_READY)
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
}
void
fnic_fdls_link_status_change(struct fnic *fnic, int linkup)
{
        struct fnic_iport_s *iport = &fnic->iport;

	fnic_log_info(fnic->fnic_num,
            "fnic%d: FDLS link status change link up:%d, usefip:%d",
            fnic->fnic_num, linkup, iport->usefip);
        // We should make sure during driver unload this function should not be called
	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);

        if (linkup) {
            if (iport->usefip) {
                iport->state = FNIC_IPORT_STATE_FIP;
                fnic_fcoe_send_vlan_req(fnic);
             } else {
                iport->state = FNIC_IPORT_STATE_FABRIC_DISC;
                fnic_fdls_disc_start(iport);
            }
        } else {
            iport->state = FNIC_IPORT_STATE_LINK_WAIT;
           if (!is_zero_ether_addr(iport->fpma))
                vnic_dev_del_addr(fnic->vdev, iport->fpma);
            fnic_common_fip_cleanup(fnic);
            fnic_fdls_link_down(iport);
        }
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
}

void
fnic_fdls_remove_tport(fnic_iport_t *iport, fnic_tport_t *tport,unsigned long flags)
{
	struct fnic *fnic = iport->fnic;
	rport_dd_data_t * rdd_data;

	struct fc_rport *rport;

        if(!tport) return;

    fdls_set_tport_state(tport, fdls_tgt_state_offline);
	rport = tport->rport;

	if (rport)
	{
		// INTERFACE to scsi_fc_transport
	    // tport resource release will be done after fnic_terminate_rport_io()
	    tport->flags |= FNIC_FDLS_TPORT_DELETED;
	    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
	    fc_remote_port_delete(rport);
	    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
	    fnic_log_info(fnic->fnic_num,
                   "deregistered tport from scsi_transport tid:%x and freeing",
                   tport->fcid);
	    rdd_data = rport->dd_data; // the dd_data is allocated by fctransport of size dd_fcrport_size
	    rdd_data->tport = NULL;
	    rdd_data->iport = NULL;
	    list_del(&tport->links);
	    kfree(tport);
	} else {

		fnic_del_tport_timer_sync();
		list_del(&tport->links);
		kfree(tport);
	}
}

/*
 * TBD_REVISIT including target_id, use bitmap lib.
 * currently its running counter
 */
void
fnic_fdls_add_tport(fnic_iport_t *iport, fnic_tport_t *tport,unsigned long flags)
{
    struct fnic *fnic =  iport->fnic;
    struct fc_rport *rport;
    struct fc_rport_identifiers ids;
    rport_dd_data_t * rdd_data;

    fnic_log_info(fnic->fnic_num, "Adding rport 0x%x", tport->fcid);

    ids.node_name = tport->wwnn;
    ids.port_name = tport->wwpn;
    ids.port_id = tport->fcid;
    ids.roles = FC_RPORT_ROLE_FCP_TARGET;


    // INTERFACE to scsi_fc_transport
    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
    rport = fc_remote_port_add(fnic->host, 0, &ids);
    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
    if (!rport)
    {
	fnic_log_info(fnic->fnic_num,
               "Failed to add rport for tport %x", tport->fcid);
	return ;
    }
    fnic_log_info(fnic->fnic_num, "Added rport 0x%x", tport->fcid);

    /* Mimic these assigments in queuecommand to avoid timing issues */
    rport->maxframe_size = FNIC_FC_MAX_PAYLOAD_LEN;
    rport->supported_classes =  FC_COS_CLASS3 | FC_RPORT_ROLE_FCP_TARGET;
    rdd_data = rport->dd_data; // the dd_data is allocated by fctransport of size dd_fcrport_size
    rdd_data->tport = tport;
    rdd_data->iport = iport;
    tport->rport = rport;
    tport->flags |= FNIC_FDLS_SCSI_REGISTERED;
    return ;
}

fnic_tport_t *
fnic_find_tport_by_wwpn(fnic_iport_t *iport,uint64_t  wwpn)
{
	fnic_tport_t *tport, *next;
	struct fnic *fnic = iport->fnic;
	fnic_log_info(fnic->fnic_num, "Entered fnic_find_tport_by_wwpn.0%llx\n", wwpn);
	list_for_each_entry_safe(tport,next, &(iport->tport_list), links) {

	if ((tport->wwpn == wwpn) && !(tport->flags & FNIC_FDLS_TPORT_TERMINATING))
		return tport;
	}
	return NULL;
}

fnic_tport_t *
fnic_find_tport_by_fcid(fnic_iport_t *iport, uint32_t fcid)
{
	fnic_tport_t *tport, *next;
	struct fnic *fnic = iport->fnic;
	fnic_log_info(fnic->fnic_num, "Entered fnic_find_tport_by_fcid.0%x\n", fcid);
	list_for_each_entry_safe(tport,next, &(iport->tport_list), links) {
	   // fnic_log_info(fnic->fnic_num, "tport->fcid 0x%x\n", tport->fcid);
                if ((tport->fcid == fcid) && !(tport->flags & FNIC_FDLS_TPORT_TERMINATING))
                        return tport;
        }
        return NULL;
}

/*
 * FPMA can be either taken from ethhdr(dst_mac) or flogi resp
 * or derive from FC_MAP and FCID combination. While it should be
 * same, revisit this if there is any possibility of not-correct.
 */
void
fnic_fdls_learn_fcoe_macs(fnic_iport_t *iport, void *rx_frame, uint8_t *fcid)
{
        struct fnic *fnic = iport->fnic;
        fnic_eth_hdr_t *ethhdr = (fnic_eth_hdr_t *)rx_frame;
        uint8_t fcmac[6] = {0x0E, 0xFC, 0x00, 0x00, 0x00, 0x00};

        memcpy(&fcmac[3], fcid, 3);

	fnic_log_info(fnic->fnic_num,
            "learn fcoe: dst_mac: %02x:%02x:%02x:%02x:%02x:%02x",
            ethhdr->dst_mac[0],  ethhdr->dst_mac[1],  ethhdr->dst_mac[2],
            ethhdr->dst_mac[3],  ethhdr->dst_mac[4],  ethhdr->dst_mac[5]);

	fnic_log_info(fnic->fnic_num,
            "learn fcoe: fc_mac: %02x:%02x:%02x:%02x:%02x:%02x",
            fcmac[0], fcmac[1], fcmac[2], fcmac[3], fcmac[4], fcmac[5]);

        fnic_fdls_set_fcoe_srcmac(fnic, fcmac);
        //fnic_fdls_set_fcoe_srcmac(fnic, ethhdr->dst_mac);
        fnic_fdls_set_fcoe_dstmac(fnic, ethhdr->src_mac);
}


/* Public Functions */
void
fnic_fdls_init(struct fnic *fnic, int usefip)
{
        fnic_iport_t *iport = &fnic->iport;
        //uint8_t dstmac[6] = {0x0e, 0xfc, 0x00, 0xff, 0xff, 0xfe};

        /* Initialize iPort structure */
        iport->state = FNIC_IPORT_STATE_INIT;
        iport->fnic = fnic;
        iport->usefip = usefip;

      
	fnic_log_info(fnic->fnic_num,
            "iportsrcmac: %02x:%02x:%02x:%02x:%02x:%02x",
            iport->hwmac[0], iport->hwmac[1], iport->hwmac[2],
            iport->hwmac[3], iport->hwmac[4], iport->hwmac[5]);

        /* Initialize the src, and dst mac addresses for frame sender */
        //fnic_fdls_set_fcoe_srcmac(fnic, iport->hwmac);
        //fnic_fdls_set_fcoe_dstmac(fnic, dstmac);

        INIT_LIST_HEAD(&iport->tport_list);
        INIT_LIST_HEAD(&iport->tport_list_pending_del);

        fnic_fdls_disc_init(iport);
}


/***********************************************************************
 * fnic_do_tgts_logo
 *
 * \brief Send PLOGO to all the targets ASAP on this fnic 
 *
 *
 * \param[in]  fnic        fnic device to send PLOGOS on
 *
 * \retval void 
 * \locks no fnic locks held when it called (callback)
 * \locks takes/drops fnic_lock
 ***********************************************************************/
void
fnic_do_tgts_logo(struct fnic *fnic)
{
   fnic_tport_t *tport, *next;
   fnic_iport_t *iport = &fnic->iport;
	unsigned long flags;

	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);

	list_for_each_entry_safe(tport,next, &(iport->tport_list), links) {

		fnic_log_info(fnic->fnic_num, "logging out tport->fcid: 0x%x",
                              tport->fcid);
                fdls_tgt_logout(&fnic->iport, tport);
        }
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
}

void
fnic_handle_link(struct work_struct *work)
{
        struct fnic *fnic = container_of(work, struct fnic, link_work);
        // struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        int old_link_status;
        u32 old_link_down_cnt;
	int max_count = 0;

        // struct fnic_lport_stats *host_stats;

       if (vnic_dev_get_intr_mode(fnic->vdev) != VNIC_DEV_INTR_MODE_MSI)
            printk(KERN_INFO PFX "started fnic_handle_link\n");

        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
        if (fnic->stop_rx_link_events) {
             MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
             fnic_log_info(fnic->fnic_num,"fnic 0x%p Stop RX link event", fnic);
             return ;
         }

        /* Do not process if the fnic is already in transitional state */
        if ((fnic->state != FNIC_IN_ETH_MODE) && (fnic->state != FNIC_IN_FC_MODE)) {
                MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
                fnic_log_info(fnic->fnic_num,
                        "fNIC in transitional state %d." \
                        "FDLS link status change(linkup:%d) ignored. " \
                        "cur link status:%d, iport state:%d \n",
                        fnic->state, vnic_dev_link_status(fnic->vdev),
                        fnic->link_status,
                        fnic->iport.state);
                return;
        }

         old_link_down_cnt = fnic->link_down_cnt;
         old_link_status = fnic->link_status;
         fnic->link_status = vnic_dev_link_status(fnic->vdev);
         fnic->link_down_cnt = vnic_dev_link_down_cnt(fnic->vdev);

	while (fnic->reset_in_progress == IN_PROGRESS) {
		MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
		wait_for_completion_timeout(&fnic->reset_completion_wait,
			msecs_to_jiffies(5000));
		MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
		max_count++;
		if (max_count >= MAX_RESET_WAIT_COUNT) {
			fnic_log_info(fnic->fnic_num,
			"Reset thread waited for too long. Skipping handle link event %p\n", fnic);
			MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}
		fnic_log_info(fnic->fnic_num,
			"fnic reset in progress.link event needs to wait %p", fnic);
	}
	fnic->reset_in_progress  = IN_PROGRESS;

       if ((vnic_dev_get_intr_mode(fnic->vdev) != VNIC_DEV_INTR_MODE_MSI) ||
           (fnic->link_status != old_link_status)) {
            fnic_log_info(fnic->fnic_num, "link status %d down cnt %d",
                    (int)fnic->link_status, (int)fnic->link_down_cnt);
            fnic_log_info(fnic->fnic_num,
                    "old status %d old down cnt %d",
                    (int)old_link_status, old_link_down_cnt);
       }

         // host_stats = &(fnic_stats->host_stats);
         if (old_link_status == fnic->link_status) {
                        if (!fnic->link_status) {
                                /* DOWN -> DOWN */
                                // atomic64_inc(&host_stats->num_linkdn);
				MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
		} else {
              		if (old_link_down_cnt != fnic->link_down_cnt) {
                               /* UP -> DOWN -> UP */
                                        // atomic64_inc(
                                        //    &host_stats->link_failure_count);
                                        /* tfnic_fc_trace_set_data(
                                            tfnic->TBDAdapter->hostNum,
                                            TFNIC_FC_LE,
                                            "Link Status:UP_DOWN_UP",
                                            TBD_Strnlen(
                                                "Link_Status:UP_DOWN_UP", 64));
                                         */
                                        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
                                        fnic_log_info(fnic->fnic_num,
                                            "fnic%d: link down",
                                            fnic->fnic_num);
                                        fnic_fdls_link_status_change(fnic, 0);

                                        fnic_log_info(fnic->fnic_num,
                                            "fnic%d: link up",
                                            fnic->fnic_num);
                                        fnic_fdls_link_status_change(fnic, 1);
                                } else {
                                        /* UP -> UP */
                                        /* tfnic_fc_trace_set_data(
                                            tfnic->TBDAdapter->hostNum,
                                            TFNIC_FC_LE, "Link Status: UP_UP",
                                            TBD_Strnlen("Link Status: UP_UP",
                                            64));
                                         */
                                        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
                                    }
                        }
         } else if (fnic->link_status) {
                        /* DOWN -> UP */
                        // atomic64_inc(&tfnic_stats->host_stats.num_linkup);

                        /* tfnic_fc_trace_set_data(tfnic->TBDAdapter->hostNum,
                            TFNIC_FC_LE, "Link Status: DOWN_UP",
                            TBD_Strnlen("Link Status: DOWN_UP", 64));
                         */
                        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);

                        fnic_log_info(fnic->fnic_num, "fnic%d: link up",
                            fnic->fnic_num);
                        //review: use #defines(also for usefip)
                        fnic_fdls_link_status_change(fnic, 1);

         } else {
                        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);

                        fnic_log_info(fnic->fnic_num,
                            "fnic%d: recvd up to down event", fnic->fnic_num);
                        /* UP -> DOWN */
                        // atomic64_inc(&host_stats->num_linkdn);
                        // atomic64_inc(&host_stats->link_failure_count);
                        /* tfnic_fc_trace_set_data(tfnic->TBDAdapter->hostNum,
                            TFNIC_FC_LE, "Link Status: UP_DOWN",
                            TBD_Strnlen("Link Status: UP_DOWN", 64));
                         */
                        fnic_fdls_link_status_change(fnic, 0);
        }
	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
	fnic->reset_in_progress  = NOT_IN_PROGRESS;
	complete(&fnic->reset_completion_wait);
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
}

void
fnic_handle_frame(struct work_struct *work)
{
    struct fnic_frame_list *cur_frame, *next;
    struct fnic *fnic = container_of(work, struct fnic, frame_work);
    int fchdr_offset = 0;
    fc_hdr_t *fchdr;

    //fnic_log_info(fnic->fnic_num, "recvd an fc frame\n");

    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
    list_for_each_entry_safe(cur_frame, next, &fnic->frame_queue, links) {
        if (fnic->stop_rx_link_events) {
            list_del(&cur_frame->links);
            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
            kfree(cur_frame->fp);
            kfree(cur_frame);
            return;
        }

        /*
         * If we're in a transitional state, just re-queue and return.
         * The queue will be serviced when we get to a stable state.
         */
        if (fnic->state != FNIC_IN_FC_MODE &&
            fnic->state != FNIC_IN_ETH_MODE) {
            fnic_log_info(fnic->fnic_num, "fnic <%d>: fnic not-processing frame in transistional state\n", fnic->fnic_num);
            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
            return;
        }

        list_del(&cur_frame->links);

        /* Frames from FCP_RQ will have ethhdrs stripped off */
        fchdr_offset = (cur_frame->rx_ethhdr_stripped) ?
                            0 : FNIC_FCOE_FCHDR_OFFSET;
	fchdr = (fc_hdr_t *) ((uint8_t *)cur_frame->fp + fchdr_offset);
        // spin_lock(&fnic->lport.lport_lock);

	if (fnic->role == FNIC_ROLE_NVME_INITIATOR && FNIC_IS_NVFNIC_FRAME(fchdr)) {
    		MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
            	fnic_log_info(fnic->fnic_num, "fnic LS REQ received %p\n", fchdr);
		nvfnic_lsrsp_recv(&fnic->iport, fchdr, cur_frame->frame_len - fchdr_offset);
    		MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
	} else {
        	fnic_fdls_recv_frame(&fnic->iport, cur_frame->fp,
                             cur_frame->frame_len, fchdr_offset);
	}
        // spin_unlock(&tfnic->lport.lport_lock);
        kfree(cur_frame->fp);
        kfree(cur_frame);
    }
    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
}

/* Process FIP frames */
void
fnic_handle_fip_frame(struct work_struct *work)
{
    struct fnic_stats *fnic_stats;
    struct fnic_frame_list *cur_frame, *next;
    struct fnic *fnic = container_of(work, struct fnic, fip_frame_work);

    fnic_stats = &fnic->fnic_stats;
    // fnic_log_info(fnic->fnic_num,"fnic 0x%p starting fnic_handle_fip_frame\n", fnic);

    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
    list_for_each_entry_safe(cur_frame, next, &fnic->fip_frame_queue, links) {
       //  fnic_log_info(fnic->fnic_num,"fnic 0x%p got fip frame\n", fnic);

        if (fnic->stop_rx_link_events) {
            list_del(&cur_frame->links);
            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
            kfree(cur_frame->fp);
            kfree(cur_frame);
            return;
        }

        /*
         * If we're in a transitional state, just re-queue and return.
         * The queue will be serviced when we get to a stable state.
         */
        if (fnic->state != FNIC_IN_FC_MODE &&
            fnic->state != FNIC_IN_ETH_MODE) {
            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
            return;
        }

        list_del(&cur_frame->links);
        

        if (fdls_fip_recv_frame(fnic, cur_frame->fp)) {
            // fnic_log_info(fnic->fnic_num,"fnic 0x%p fip frame processed\n", fnic);
            kfree(cur_frame->fp);
            kfree(cur_frame);
        }
        
    }
    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
}

/**
 * fnic_import_rq_eth_pkt() - handle received FCoE or FIP frame.
 * @fnic:        fnic instance.
 * @skb:        Ethernet Frame.
 */
static int
fnic_import_rq_eth_pkt(struct fnic *fnic, void *fp)
{
	fnic_eth_hdr_t *eh;
	struct fnic_frame_list *fip_fr_elem;
	unsigned long flags;

        // fnic_log_info(fnic->fnic_num,"fnic 0x%p fnic_import_rq_eth_pkt\n", fnic);
        eh = (fnic_eth_hdr_t *) fp;
	if ((eh->ether_type == htons(ETH_TYPE_FIP)) && (fnic->iport.usefip)) {
	    //fnic_log_info(fnic->fnic_num,"fnic 0x%p fip frame enqueued\n", fnic);
		fip_fr_elem = (struct fnic_frame_list *)
			kmalloc(sizeof(struct fnic_frame_list), GFP_ATOMIC);
		if (!fip_fr_elem) {
			fnic_log_alert(fnic->fnic_num, "Mem alloc failure size:%ld\n",
				sizeof(struct fnic_frame_list));
			return 0;
		}
	    memset(fip_fr_elem, 0, sizeof(struct fnic_frame_list));
            fip_fr_elem->fp = fp;
	    spin_lock_irqsave(&fnic->fnic_lock, flags);
            list_add_tail(&fip_fr_elem->links, &fnic->fip_frame_queue);
	    spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	    queue_work(fnic_fip_queue, &fnic->fip_frame_work);

            return 1;               /* let caller know packet was used */
        } else {
            return 0;
        }
}

static void
fnic_rq_cmpl_frame_recv(struct vnic_rq *rq, struct cq_desc
    *cq_desc, struct vnic_rq_buf *buf, int skipped, void *opaque)
{
        struct fnic *fnic = vnic_dev_priv(rq->vdev);
        uint8_t *fp;
        struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        unsigned int ethhdr_stripped;
        u8 type, color, eop, sop, ingress_port, vlan_stripped;
        u8 fcoe_fnic_crc_ok = 1, fcoe_enc_error = 0;
        u8 fcs_ok = 1, packet_error = 0;
        u16 q_number, completed_index, vlan;
        u32 rss_hash;
        u16 checksum;
        u8 csum_not_calc, rss_type, ipv4, ipv6, ipv4_fragment;
        u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
        u8 fcoe = 0, fcoe_sof, fcoe_eof;
        u16 exchange_id, tmpl;
        u8 sof = 0;
        u8 eof = 0;
        u32 fcp_bytes_written = 0;
        u16 enet_bytes_written = 0;
        u32 bytes_written = 0;
        unsigned long flags;
        struct fnic_frame_list * frame_elem = NULL;
        fnic_eth_hdr_t *eh;

        pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len,
                     PCI_DMA_FROMDEVICE);
        fp = (uint8_t *)buf->os_buf;
        buf->os_buf = NULL;

#if 0
        {
            int i;
            unsigned char *c;

            c = (unsigned char *) (fp);

            for (i = 0; i < 60; i = i+4) {
                fnic_log_info(fnic->fnic_num, "%02x %02x %02x %02x\n",
                        c[i], c[i+1], c[i+2], c[i+3]);
            }
        }
#endif

        cq_desc_dec(cq_desc, &type, &color, &q_number, &completed_index);
        if (type == CQ_DESC_TYPE_RQ_FCP) {
                cq_fcp_rq_desc_dec((struct cq_fcp_rq_desc *)cq_desc,
                                   &type, &color, &q_number, &completed_index,
                                   &eop, &sop, &fcoe_fnic_crc_ok, &exchange_id,
                                   &tmpl, &fcp_bytes_written, &sof, &eof,
                                   &ingress_port, &packet_error,
                                   &fcoe_enc_error, &fcs_ok, &vlan_stripped,
                                   &vlan);
                ethhdr_stripped = 1;
                bytes_written = fcp_bytes_written;

                fnic_log_info(fnic->fnic_num, "recvd FCP frame. sz:%d\n", fcp_bytes_written);
        } else if (type == CQ_DESC_TYPE_RQ_ENET) {
                cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc,
                                    &type, &color, &q_number, &completed_index,
                                    &ingress_port, &fcoe, &eop, &sop,
                                    &rss_type, &csum_not_calc, &rss_hash,
                                    &enet_bytes_written, &packet_error,
                                    &vlan_stripped, &vlan, &checksum,
                                    &fcoe_sof, &fcoe_fnic_crc_ok,
                                    &fcoe_enc_error, &fcoe_eof,
                                    &tcp_udp_csum_ok, &udp, &tcp,
                                    &ipv4_csum_ok, &ipv6, &ipv4,
                                    &ipv4_fragment, &fcs_ok);

                ethhdr_stripped = 0;
                bytes_written = enet_bytes_written;
                // fnic_log_info(fnic->fnic_num, "recvd ENET frame. sz:%d\n",
                //    enet_bytes_written);

                if (!fcs_ok) {
                        atomic64_inc(&fnic_stats->misc_stats.frame_errors);
                        fnic_log_info(fnic->fnic_num,"fnic 0x%p fcs error.  dropping packet.\n", fnic);
                        goto drop;
                }
		eh = (fnic_eth_hdr_t *) fp;
		if (eh->ether_type != htons(ETH_TYPE_FCOE)){

			if (fnic_import_rq_eth_pkt(fnic, fp)) {
				return;
			}
			else{
				fnic_log_debug(fnic->fnic_num, "Dropping ether_type 0x%x",
				ntohs(eh->ether_type));
				goto drop;
			}
		}
        } else {
                /* wrong CQ type*/
                fnic_log_info(fnic->fnic_num,
                    "fnic rq_cmpl wrong cq type x%x\n", type);
                goto drop;
        }

        if (!fcs_ok || packet_error || !fcoe_fnic_crc_ok || fcoe_enc_error) {
                atomic64_inc(&fnic_stats->misc_stats.frame_errors);
                fnic_log_info(fnic->fnic_num,
                    "fnic rq_cmpl fcoe x%x fcsok x%x"
                    " pkterr x%x fcoe_fnic_crc_ok x%x, fcoe_enc_err"
                    " x%x\n", fcoe, fcs_ok, packet_error,
                    fcoe_fnic_crc_ok, fcoe_enc_error);
                goto drop;
        }
   
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
        if (fnic->stop_rx_link_events) {
            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
            printk(KERN_DEBUG
                   "fnic->stop_rx_link_events %x \n",
                   fnic->stop_rx_link_events);
            goto drop;
        }

        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);

        frame_elem = (struct fnic_frame_list *)
                    kmalloc(sizeof(struct fnic_frame_list), GFP_ATOMIC);
	if(!frame_elem){
		fnic_log_alert(fnic->fnic_num,
			"Error in fnicHeapAlloc for frame_elem size:%ld\n",
			sizeof(struct fnic_frame_list));
		goto drop;
	}
        memset(frame_elem, 0, sizeof(struct fnic_frame_list));
        frame_elem->fp = fp;
        frame_elem->rx_ethhdr_stripped = ethhdr_stripped;
        frame_elem->frame_len = bytes_written;

        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
        list_add_tail(&frame_elem->links, &fnic->frame_queue);
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);

        queue_work(fnic_event_queue, &fnic->frame_work);
        return;
drop:
        kfree(fp);
}

static int fnic_rq_cmpl_handler_cont(struct vnic_dev *vdev,
    struct cq_desc *cq_desc, u8 type, u16 q_number, u16 completed_index,
    void *opaque)
{
        struct fnic *fnic = vnic_dev_priv(vdev);

        //fnic_log_verbose(fnic->fnic_num, fnic_mod.log_id, "in fnic_rq_cmpl_handler_cont");
        vnic_rq_service(&fnic->rq[q_number], cq_desc, completed_index,
            VNIC_RQ_RETURN_DESC, fnic_rq_cmpl_frame_recv, NULL);
        return 0;
}

int
fnic_rq_cmpl_handler(struct fnic *fnic, int rq_work_to_do)
{
        unsigned int tot_rq_work_done = 0, cur_work_done;
        unsigned int i;
        int err;

        // fnic_log_info(fnic->fnic_num,
        //    "RQ completion: work_to_do:%d rq_count: %d",
        //    rq_work_to_do, fnic->rq_count);

        for (i = 0; i < fnic->rq_count; i++) {
                cur_work_done = vnic_cq_service(&fnic->cq[i], rq_work_to_do,
                    fnic_rq_cmpl_handler_cont, NULL);
                if (cur_work_done && fnic->stop_rx_link_events != 1 ) {
                        err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
                        if (err)
				fnic_log_info(fnic->fnic_num,
                                    "RQ completion: can't alloc frame! %p", fnic);
                }
                tot_rq_work_done += cur_work_done;
        }
        return tot_rq_work_done;
}



/*
 * This function is called once at init time to allocate and fill RQ
 * buffers. Subsequently, it is called in the interrupt context after RQ
 * buffer processing to replenish the buffers in the RQ
 */
int fnic_alloc_rq_frame(struct vnic_rq *rq)
{   
    struct fnic *fnic = vnic_dev_priv(rq->vdev);
    void *buf;
    u16 len;
    dma_addr_t pa;
    
    len = 2148; // TBD FC_FRAME_HEADROOM + FC_MAX_FRAME + FC_FRAME_TAILROOM;
    buf = kmalloc(len, GFP_ATOMIC);
    if (!buf) {
	fnic_log_alert(fnic->fnic_num,
                        "Unable to allocate RQ sk_buff size:%d\n", len);
        return -ENOMEM;
    }
    
    pa = pci_map_single(fnic->pdev, buf, len, PCI_DMA_FROMDEVICE);
    fnic_queue_rq_desc(rq, buf, pa, len);
    return 0;
}


void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
    void *rq_buf = buf->os_buf;
    struct fnic *fnic = vnic_dev_priv(rq->vdev);

    pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len,
                    PCI_DMA_FROMDEVICE);
  

    kfree(rq_buf);
    buf->os_buf = NULL;

}


void fnic_free_txq(struct list_head *head)
{
	struct fnic_frame_list *cur_frame, *next;

	list_for_each_entry_safe(cur_frame, next, head, links) {
		list_del(&cur_frame->links);
        	kfree(cur_frame->fp);
		kfree(cur_frame);
        }
}

static void
fnic_wq_complete_frame_send(struct vnic_wq *wq, struct cq_desc *cq_desc,
    struct vnic_wq_buf *buf, void *opaque)
{
    struct fnic *fnic = vnic_dev_priv(wq->vdev);

    pci_unmap_single(fnic->pdev, buf->dma_addr,
                    buf->len, PCI_DMA_TODEVICE);
    kfree(buf->os_buf);
    buf->os_buf = NULL;

}


static int
fnic_wq_cmpl_handler_cont(struct vnic_dev *vdev, struct cq_desc *cq_desc,
    u8 type, u16 q_number, u16 completed_index, void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(vdev);
	unsigned long flags;

	MY_SPIN_LOCK_IRQ_SAVE(&fnic->wq_lock[q_number], flags);
	vnic_wq_service(&fnic->wq[q_number], cq_desc, completed_index,
                        fnic_wq_complete_frame_send, NULL);
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->wq_lock[q_number], flags);

        return 0;
}

int fnic_wq_cmpl_handler(struct fnic *fnic, int work_to_do)
{
        unsigned int wq_work_done = 0;
        unsigned int i;

        // fnic_log_verbose(fnic->fnic_num, fnic_mod.log_id, "reached wq_cmpl_handler");
        for (i = 0; i < fnic->raw_wq_count; i++) {
                wq_work_done += vnic_cq_service(&fnic->cq[fnic->rq_count+i],
                    work_to_do, fnic_wq_cmpl_handler_cont, NULL);
        }

        return wq_work_done;
}


void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf)
{
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr,
                     buf->len, PCI_DMA_TODEVICE);

	kfree(buf->os_buf);
	buf->os_buf = NULL;

}

/*
 * Send FC frame.
 */
static int
fnic_send_frame(struct fnic *fnic, void *frame, int frame_len)
{
    struct vnic_wq *wq = &fnic->wq[0];
    dma_addr_t pa;
    int ret = 0;
    unsigned long flags;

    pa = pci_map_single(fnic->pdev, frame, frame_len, PCI_DMA_TODEVICE);

    if ((fnic_fc_trace_set_data(fnic->fnic_num,
                        FNIC_FC_SEND|0x80, (char *)frame, frame_len)) != 0) {
	printk(KERN_ERR "fnic ctlr frame trace error!!!");
    }

    MY_SPIN_LOCK_IRQ_SAVE(&fnic->wq_lock[0], flags);

    if (!vnic_wq_desc_avail(wq)) {
        pci_unmap_single(fnic->pdev, pa,
                         frame_len, PCI_DMA_TODEVICE);
        printk(KERN_ERR "vnic work queue descriptor is not available");
        ret = -1;
        goto fnic_send_frame_end;
    }
   
    fnic_queue_wq_desc(wq, frame, pa, frame_len, FNIC_FCOE_EOF,
                       0 /* hw inserts cos value */,
                       fnic->vlan_id, 1, 1, 1);

fnic_send_frame_end:
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->wq_lock[0], flags);

    if (ret) {
        // TBD free buffer
    }
   
    return ret;

}

static int
fdls_send_fcoe_frame(struct fnic *fnic, void *payload, int payload_sz,
    uint8_t *srcmac, uint8_t *dstmac)
{
    uint8_t *frame;
    fnic_eth_hdr_t *ethhdr;
    struct fnic_frame_list *frame_elem;
    int max_framesz = FNIC_FCOE_FRAME_MAXSZ;
    int len = 0;
    int ret;
//#if DBG_FRM
#if 0
        int i;
        uint8_t *u8arr;
/*        u8arr = payload;
        for (i = 0; i < payload_sz; i = i+8) {
            fnic_log_info(fnic->fnic_num,
                "%02x %02x %02x %02x %02x %02x %02x %02x",
                u8arr[i + 0], u8arr[i + 1], u8arr[i + 2], u8arr[i+ 3],
                u8arr[i + 4], u8arr[i+5], u8arr[i+6], u8arr[i+7]);
        } */
#endif
        /*
    fnic_log_info(fnic->fnic_num,
                 "fnic%d: fdls_send_fcoe_frame paylodsz:%d", fnic->fnic_num,
                 payload_sz);
        */

    frame = (uint8_t *) kmalloc(max_framesz, GFP_ATOMIC);
   // fnic_log_info(fnic->fnic_num, "heap fcoe frame:%d\n", max_framesz);
    if (!frame) {
        fnic_log_alert(fnic->fnic_num,"fnic 0x%p Failed to allocate frame for flogi\n", fnic);
            return -1;
    }
    memset(frame, 0, max_framesz);

    //TBD - cover the case non-hw-inserted vlan

    ethhdr = (fnic_eth_hdr_t *)frame;

    memcpy(frame, (uint8_t*)&fnic_eth_hdr_fcoe, sizeof(fnic_eth_hdr_t));
    len = sizeof(fnic_eth_hdr_t);
    //memcpy(ethhdr->src_mac, fnic->lport.fpma, 6);
    //memcpy(ethhdr->dst_mac, fnic->lport.fcfmac, 6);
    memcpy(ethhdr->src_mac, srcmac, ETH_ALEN);
    memcpy(ethhdr->dst_mac, dstmac, ETH_ALEN);

    memcpy(frame + len, (uint8_t *)&fnic_fcoe_hdr, sizeof(fnic_fcoe_hdr_t));
    len += sizeof(fnic_fcoe_hdr_t);

    memcpy(frame + len, (uint8_t*)payload, payload_sz);
    len += payload_sz;
//#if DBG_FRM
#if 0
        u8arr = payload;
        for (i = 0; i < payload_sz; i = i+8) {
            fnic_log_info(fnic->fnic_num,
                "%02x %02x %02x %02x %02x %02x %02x %02x",
                u8arr[i + 0], u8arr[i + 1], u8arr[i + 2], u8arr[i+ 3],
                u8arr[i + 4], u8arr[i+5], u8arr[i+6], u8arr[i+7]);
        }
#endif

        //review: Handle this with the state in fdls for reg_done, and remove
        //this logic
        /*
         * Queue frame if in a transitional state.
         * This occurs while registering the Port_ID / MAC address after FLOGI.
         */
//    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
//    printk(KERN_DEBUG "fnic state %d", fnic->state);
    if ((fnic->state != FNIC_IN_FC_MODE) &&
        (fnic->state != FNIC_IN_ETH_MODE)) {
	frame_elem = kmalloc(sizeof(struct fnic_frame_list), GFP_ATOMIC);
	if (!frame_elem) {
		fnic_log_alert(fnic->fnic_num, "Mem Alloc failure size:%ld\n",
			sizeof(struct fnic_frame_list));
		return -1;
	}
        memset(frame_elem, 0, sizeof(struct fnic_frame_list));

        fnic_log_info(fnic->fnic_num, "Queueing frame:%p\n", frame);

        frame_elem->fp = frame;
        frame_elem->frame_len = len;
        list_add_tail(&frame_elem->links, &fnic->tx_queue);

//        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
        return 0;
    }
//    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);

    ret = fnic_send_frame(fnic, frame, len);
    return ret;
}

static int
fdls_send_fip_frame(struct fnic *fnic, void *payload, int payload_sz)
{
    uint8_t *frame;
    int max_framesz = FNIC_FCOE_FRAME_MAXSZ;
    int ret;

    frame = (uint8_t *) kmalloc(max_framesz, GFP_ATOMIC);
    if (!frame) {
        fnic_log_alert(fnic->fnic_num,"fnic 0x%p Failed to allocate fip frame\n", fnic);
        return -1;
    }
    memset(frame, 0, max_framesz);

    //TBD - cover the case non-hw-inserted vlan

    memcpy(frame, (uint8_t*)payload, payload_sz);
    ret = fnic_send_frame(fnic, frame, payload_sz);

    return ret;

}

int
fnic_send_fcoe_frame(struct fnic_iport_s *iport, void *payload, int payload_sz)
{
        struct fnic *fnic = iport->fnic;
        uint8_t *dstmac, *srcmac;
        int ret = 0;

	// fnic_log_info(fnic->fnic_num,
        //    "fnic%d: FNIC send FCOE frame: fabric state:%d",
        //    fnic->fnic_num, iport->fabric.state);

        /* If module unload is in-progress, don't send */
        if (fnic->in_remove)
        {
                return -1;
        }

        if (iport->fabric.flags & FNIC_FDLS_FPMA_LEARNT) {
                srcmac = iport->fpma;
                dstmac = iport->fcfmac;
		// fnic_log_info(fnic->fnic_num,
                //    "fnic%d: srcmac:fpma", fnic->fnic_num);
        } else {
                srcmac = iport->hwmac;
                dstmac = fcoe_all_fcf_mac;
        }
	/*
        fnic_log_info(fnic->fnic_num, "fnic%d: srcmac: %02x:%02x:%02x:%02x:%02x:%02x "
            "dstmac: %02x:%02x:%02x:%02x:%02x:%02x", fnic->fnic_num, srcmac[0],
            srcmac[1], srcmac[2], srcmac[3], srcmac[4], srcmac[5], dstmac[0],
            dstmac[1], dstmac[2], dstmac[3], dstmac[4], dstmac[5]);
	*/
        ret = fdls_send_fcoe_frame(fnic, payload, payload_sz, srcmac, dstmac);
        return ret;
}

int
fnic_send_fip_frame(struct fnic_iport_s *iport, void *payload, int payload_sz)
{
    struct fnic *fnic = iport->fnic;

    if (fnic->in_remove) {
        return -1;
    }

    return fdls_send_fip_frame(fnic, payload, payload_sz);
}

/**
 * fnic_flush_tx() - send queued frames.
 * @fnic: fnic device
 *
 * Send frames that were waiting to go out in FC or Ethernet mode.
 * Whenever changing modes we purge queued frames, so these frames should
 * be queued for the stable mode that we're in, either FC or Ethernet.
 *
 * Called without fnic_lock held.
 */
void fnic_flush_tx(struct fnic *fnic)
{
        void *fp;
        struct fnic_frame_list *cur_frame, *next;

        fnic_log_info(fnic->fnic_num, "Flush queued frames %p", fnic);

	list_for_each_entry_safe(cur_frame, next, &fnic->tx_queue, links) {
                fp = cur_frame->fp;
                list_del(&cur_frame->links);
                fnic_send_frame(fnic, fp, cur_frame->frame_len);
        }
}

int
fnic_fdls_register_portid(fnic_iport_t *iport, u32 port_id, void *fp)
{
        struct fnic *fnic = iport->fnic;
        fnic_eth_hdr_t *ethhdr;
        int ret;

        fnic_log_info(fnic->fnic_num,
            "setting port id:%x and fp:%p, fnic state:%d",
            port_id, fp, fnic->state);

        if (fp) {
                ethhdr = (fnic_eth_hdr_t *)fp;
                vnic_dev_add_addr(fnic->vdev, ethhdr->dst_mac);
        }

        /* Change state to reflect transition to FC mode */
        if (fnic->state == FNIC_IN_ETH_MODE || fnic->state == FNIC_IN_FC_MODE)
                fnic->state = FNIC_IN_ETH_TRANS_FC_MODE;
        else {
                fnic_log_info(fnic->fnic_num,"fnic 0x%p Unexpected fnic state while"
                          " processing flogi resp\n", fnic);
                return -1;
        }

        /*
         * Send FLOGI registration to firmware to set up FC mode.
         * The new address will be set up when registration completes.
         */
        ret = fnic_flogi_reg_handler(fnic, port_id);

        if (ret < 0) {

                fnic_log_info(fnic->fnic_num,
                    "flogi reg handeler Error ret:%d, fnic state:%d\n",
                    ret, fnic->state);
                if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE)
                        fnic->state = FNIC_IN_ETH_MODE;

                return -1;
        }
	iport->fabric.flags |= FNIC_FDLS_FPMA_LEARNT;

        fnic_log_info(fnic->fnic_num,"fnic 0x%p flogi registration Success\n", fnic);

        return 0;

}

void fnic_flush_tport_event_list(struct fnic *fnic)
{
	fnic_tport_event_t *cur_evt, *next;
	unsigned long flags;
	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(cur_evt, next, &fnic->tport_event_list, links) {
		list_del(&cur_evt->links);
		kfree(cur_evt);
	}
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
        return;

}
void fnic_delete_fcp_tports(struct fnic *fnic)
{
	fnic_tport_t *tport, *next;
	unsigned long flags;
	
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
        list_for_each_entry_safe(tport, next, &fnic->iport.tport_list, links)   {
                FNIC_FDLS_DBG(KERN_DEBUG, fnic, "removing fcp rport:%x",
                        tport->fcid);
                fdls_set_tport_state(tport, fdls_tgt_state_offlining);
		fnic_fdls_remove_tport(&fnic->iport, tport, flags);
		fnic_del_tport_timer_sync();
        }
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
}

/**
 * fnic_tport_work() - Handler for remote port events in the tport_event_queue
 * @work: Handle to the remote port being dequeued
 */
void fnic_tport_event_handler(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, tport_work);
	fnic_tport_event_t *cur_evt, *next;
	unsigned long flags;
    fnic_tport_t *tport;

    fnic_log_info(fnic->fnic_num, "reached tport_event_handler %p", fnic);
	MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(cur_evt, next, &fnic->tport_event_list, links) {
		tport = cur_evt->arg1;
		switch(cur_evt->event) {
		case TGT_EV_RPORT_ADD:
    		fnic_log_info(fnic->fnic_num, "Add rport event %p", cur_evt->arg1);
			if (tport->state == fdls_tgt_state_ready) {
				fnic_fdls_add_tport(&fnic->iport, (fnic_tport_t *)cur_evt->arg1,flags);
			} else {
    				fnic_log_info(fnic->fnic_num, "Add rport event dropped %x", tport->fcid);
			}
			
			break;
		case TGT_EV_RPORT_DEL:
		    fnic_log_info(fnic->fnic_num, "Remove rport event %p", cur_evt->arg1);
			if (tport->state == fdls_tgt_state_offlining) {
				fnic_fdls_remove_tport(&fnic->iport, (fnic_tport_t *)cur_evt->arg1,flags);
			} else {
    				fnic_log_info(fnic->fnic_num, "remove rport event dropped %x", tport->fcid);
			}
			break;
		case TGT_EV_TPORT_DELETE:
		    fnic_log_info(fnic->fnic_num, "delete tport event %p", cur_evt->arg1);
		    fdls_delete_tport(tport->iport, tport);
		    break;
		default:
			break;
		}
		fnic_log_info(fnic->fnic_num, "freeing up event in tport_event_handler %p", fnic);
		list_del(&cur_evt->links);
		kfree(cur_evt);
	}
	MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
        return;
}
