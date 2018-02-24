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

/*! \file */ 
#include "fnic_config.h"

#include "fnic.h"
#include "fip.h"

extern struct workqueue_struct *fnic_fip_queue;

void fnic_fcoe_send_vlan_req(struct fnic *fnic);
void fnic_fcoe_start_fcf_discovery(struct fnic *fnic);
void fnic_fcoe_start_flogi(struct fnic *fnic);
void fnic_fcoe_process_cvl(struct fnic *fnic, fip_header_t *fiph);
void fnic_vlan_discovery_timeout(struct fnic *fnic);

#define TRUE 1
#define FALSE 0

int drop_rsp = TRUE;

#define MAX_RESET_WAIT_COUNT 15
// TBD TO COMPILE TNIC

#define htonll(x) cpu_to_be64(x)

/****************************** Functions ***********************************/

/**
 * fnic_fcoe_reset_vlans
 *
 * Frees up the list of discovered vlans
 *
 * @param fnic fnic driver instance
 */

void fnic_fcoe_reset_vlans (struct fnic *fnic)
{
	unsigned long flags;
        struct fcoe_vlan *vlan, *next;

	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p enter reset vlan\n", fnic);
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->vlans_lock, flags);
        if (!list_empty(&fnic->vlan_list)) {
            list_for_each_entry_safe(vlan, next, &fnic->vlan_list, list) {
                list_del(&vlan->list);
                kfree(vlan);
            }
        }

        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);
	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p reset vlan done\n", fnic);
}

/**
 * fnic_fcoe_send_vlan_req
 *
 * Sends FIP vlan request to all FCFs MAC
 *
 * @param fnic fnic driver instance
 */

void fnic_fcoe_send_vlan_req(struct fnic *fnic)
{
    fnic_iport_t *iport= &fnic->iport;
    struct fnic_stats *fnic_stats = &fnic->fnic_stats;
    u64 vlan_tov;

    int fr_len;
    fip_vlan_req_t vlan_req;

    FNIC_FIP_DBG(KERN_INFO, fnic, "Enter send vlan req\n");
    fnic_fcoe_reset_vlans(fnic);

    fnic->set_vlan(fnic, 0);
    FNIC_FIP_DBG(KERN_INFO, fnic, "set vlan done\n");

    fr_len = sizeof(fip_vlan_req_t);

    FNIC_FIP_DBG(KERN_INFO, fnic, "got MAC %x %x %x %x %x %x\n",
		    iport->hwmac[0], iport->hwmac[1],iport->hwmac[2],
		    iport->hwmac[3],iport->hwmac[4],iport->hwmac[5]);

    memcpy(&vlan_req, &fip_vlan_req_tmpl, fr_len);
    memcpy(vlan_req.eth.smac, iport->hwmac, ETH_ALEN);
    memcpy(vlan_req.mac_desc.mac, iport->hwmac, ETH_ALEN);

    atomic64_inc(&fnic_stats->vlan_stats.vlan_disc_reqs);

    iport->fip.state = FDLS_FIP_VLAN_DISCOVERY_STARTED;

    fnic_send_fip_frame(iport, &vlan_req, fr_len);
    FNIC_FIP_DBG(KERN_INFO, fnic, "vlan req sent\n");


    vlan_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_FIPVLAN_TOV);
    mod_timer(&fnic->retry_fip_timer, round_jiffies(vlan_tov));
    FNIC_FIP_DBG(KERN_INFO, fnic, "fip timer set\n");
}

/**
 * fnic_fcoe_process_vlan_resp
 *
 * Processes the vlan response from one FCF and populates VLAN list.
 * Will wait for responses from multiple FCFs until timeout.
 *
 * @param fnic fnic driver instance
 * @param fiph received fip frame
 */

void fnic_fcoe_process_vlan_resp (struct fnic *fnic, fip_header_t *fiph)
{
        fip_vlan_notif_t *vlan_notif = (fip_vlan_notif_t *) fiph;

        struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        u16 vid;
        int num_vlan = 0;
        int cur_desc, desc_len;
        struct fcoe_vlan *vlan;
        fip_vlan_desc_t *vlan_desc;
        unsigned long flags;

	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p got vlan resp\n", fnic);

        desc_len = ntohs(vlan_notif->fip.desc_len);
	FNIC_FIP_DBG(KERN_INFO, fnic, "desc_len %d\n", desc_len);

        MY_SPIN_LOCK_IRQ_SAVE(&fnic->vlans_lock, flags);

        cur_desc = 0;
        while (desc_len > 0) {
                vlan_desc = (fip_vlan_desc_t *)
                    (((char *)vlan_notif->vlans_desc) + cur_desc * 4);

                if (vlan_desc->type == FIP_TYPE_VLAN) {
                        if (vlan_desc->len != 1) {
				FNIC_FIP_DBG(KERN_INFO, fnic,
                                    "Invalid descriptor length(%x) in VLan "
                                    "response\n", vlan_desc->len);

                        }
                        num_vlan++;
                        vid = ntohs(vlan_desc->vlan);
			FNIC_FIP_DBG(KERN_INFO, fnic,
                            "process_vlan_resp: FIP VLAN %d\n", vid);
                        vlan = kmalloc(sizeof(*vlan), GFP_ATOMIC);

                        if (!vlan) {
                                /* retry from timer */
                                FNIC_FIP_DBG(KERN_ALERT, fnic, "Mem Alloc failure\n");
                                MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);
                                goto out;
                        }
                        memset(vlan, 0, sizeof(struct fcoe_vlan));
                        vlan->vid = vid & 0x0fff;
                        vlan->state = FIP_VLAN_AVAIL;
                        list_add_tail(&vlan->list, &fnic->vlan_list);
                        break;
                } else {
			FNIC_FIP_DBG(KERN_INFO, fnic, "Invalid descriptor "
                            "type(%x) in VLan response\n",vlan_desc->type);
                        // Note : received a type=2 descriptor here i.e. FIP
                        // MAC Address Descriptor
                }
                cur_desc += vlan_desc->len;
                desc_len -= vlan_desc->len;
        }

        /* any VLAN descriptors present ? */
        if (num_vlan == 0) {
                atomic64_inc(&fnic_stats->vlan_stats.resp_withno_vlanID);
		FNIC_FIP_DBG(KERN_INFO, fnic,
                    "fnic 0x%p No VLAN descriptors in FIP VLAN response\n", fnic);
        }

        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);

out:
        return;
}

/**
 * fnic_fcoe_start_fcf_discovery
 *
 * Starts FIP FCF discovery in a selected vlan
 *
 * @param fnic fnic driver instance
 */

void fnic_fcoe_start_fcf_discovery (struct fnic *fnic)
{
    fnic_iport_t *iport= &fnic->iport;
    u64 fcs_tov;
    
    int fr_len;
    fip_discovery_t disc_sol;

    FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p start fcf discovery\n", fnic);
    fr_len = sizeof(fip_discovery_t);
    memset(iport->selected_fcf.fcf_mac, 0, ETH_ALEN);

    memcpy(&disc_sol, &fip_discovery_tmpl, fr_len);
    memcpy(disc_sol.eth.smac, iport->hwmac, ETH_ALEN);
    memcpy(disc_sol.mac_desc.mac, iport->hwmac, ETH_ALEN);
    iport->selected_fcf.fcf_priority = 0xFF;

    disc_sol.name_desc.name = htonll(iport->wwnn);
    fnic_send_fip_frame(iport, &disc_sol, fr_len);

    iport->fip.state = FDLS_FIP_FCF_DISCOVERY_STARTED;

    fcs_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_FCS_TOV);
    mod_timer(&fnic->retry_fip_timer, round_jiffies(fcs_tov));
    
    FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p Started FCF discovery", fnic);
    
}

/**
 * fnic_fcoe_fip_discovery_resp
 *
 * Processes FCF advertisements.
 * They can be:
 * solicited   Sent in response of a discover FCF FIP request
 *             We will only store the informations of the FCF with
 *             highest priority.
 *             We wait until timeout in case of multiple FCFs.
 * unsolicited Sent periodically by the FCF for keep alive.
 *             If FLOGI is in progress or completed and the advertisement is
 *             received by our selected FCF, refresh the keep alive timer.
 *
 * @param fnic fnic driver instance
 * @param fiph received frame
 */

void fnic_fcoe_fip_discovery_resp(struct fnic *fnic, fip_header_t *fiph)
{
        fnic_iport_t *iport= &fnic->iport;
        fip_disc_adv_t *disc_adv = (fip_disc_adv_t *) fiph;
        u64 fcs_ka_tov;
        int desc_len = ntohs(disc_adv->fip.desc_len);
	int fka_has_changed;

        // fnic_log_info(fnic->fnic_num,"fnic 0x%p In fcf discovery resp\n", fnic);
        // fnic_log_info(fnic->fnic_num,"fip state %d\n", iport->fip.state);

        if (!(desc_len == 12)) {
                // fnic_log_info(fnic->fnic_num, "fip_disc_adv_t invalid "
                //   "Descriptor List len (%x)\n", desc_len);
        }
        if (!((disc_adv->prio_desc.type==1) && (disc_adv->prio_desc.len==1)) ||
            !((disc_adv->mac_desc.type==2) && (disc_adv->mac_desc.len==2)) ||
            !((disc_adv->name_desc.type==4) && (disc_adv->name_desc.len==3)) ||
            !((disc_adv->fabric_desc.type==5) &&
            (disc_adv->fabric_desc.len==4)) ||
            !((disc_adv->fka_adv_desc.type==12) &&
            (disc_adv->fabric_desc.len==2))) {// this len comes 4 ??
                /* fnic_log_info(fnic->fnic_num,
                    " fip_disc_adv_t invalid Descriptor type and len mix:  "
                    "type(%x) len(%x)|type(%x) len(%x)|type(%x) "
                    "len(%x)|type(%x) len(%x)|type(%x) len(%x) \n",
                    disc_adv->prio_desc.type, disc_adv->prio_desc.len,
                    disc_adv->mac_desc.type,disc_adv->mac_desc.len,
                    disc_adv->name_desc.type,disc_adv->name_desc.len,
                    disc_adv->fabric_desc.type,disc_adv->fabric_desc.len,
                    disc_adv->fka_adv_desc.type,disc_adv->fabric_desc.len);
		*/
        }

        if (iport->fip.state == FDLS_FIP_FCF_DISCOVERY_STARTED) {
            if (ntohs(disc_adv->fip.flags) & FIP_FLAG_S) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p Solicited adv\n", fnic);

                if ((disc_adv->prio_desc.priority <
                    iport->selected_fcf.fcf_priority) &&
                    (ntohs(disc_adv->fip.flags) & FIP_FLAG_A)) {

		    FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p FCF Available\n", fnic);
                    memcpy(iport->selected_fcf.fcf_mac,
                            disc_adv->mac_desc.mac, ETH_ALEN);
                    iport->selected_fcf.fcf_priority =
                        disc_adv->prio_desc.priority;
                    iport->selected_fcf.fka_adv_period =
                        ntohl(disc_adv->fka_adv_desc.fka_adv);
		    FNIC_FIP_DBG(KERN_INFO, fnic, "adv time %d",
                                 iport->selected_fcf.fka_adv_period);
                    iport->selected_fcf.ka_disabled =
                        (disc_adv->fka_adv_desc.rsvd_D & 1);
                }
            } else {
            // ignore
            }
        } else if ((iport->fip.state == FDLS_FIP_FLOGI_STARTED) ||
                   (iport->fip.state == FDLS_FIP_FLOGI_COMPLETE)) {
            if (!(ntohs(disc_adv->fip.flags) & FIP_FLAG_S)) {
                //  same fcf
                if (memcmp(iport->selected_fcf.fcf_mac,
                    disc_adv->mac_desc.mac, ETH_ALEN) == 0) {
                    if (iport->selected_fcf.fka_adv_period !=
                        ntohl(disc_adv->fka_adv_desc.fka_adv)) {
                            iport->selected_fcf.fka_adv_period =
                            ntohl(disc_adv->fka_adv_desc.fka_adv);
			    FNIC_FIP_DBG(KERN_INFO, fnic,
                                "change fka to %d",
                            iport->selected_fcf.fka_adv_period);
                     }

		     fka_has_changed = (iport->selected_fcf.ka_disabled == 1) &&
                                       ((disc_adv->fka_adv_desc.rsvd_D & 1) == 0);

		     iport->selected_fcf.ka_disabled =
                        (disc_adv->fka_adv_desc.rsvd_D & 1);
                     if (!((iport->selected_fcf.ka_disabled) ||
                         (iport->selected_fcf.fka_adv_period == 0))) {
                         
                         fcs_ka_tov = jiffies +
                         3* msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
                         mod_timer(&fnic->fcs_ka_timer, round_jiffies(fcs_ka_tov));
                         
                     } else {
                         if (timer_pending(&fnic->fcs_ka_timer))
			     del_timer_sync(&fnic->fcs_ka_timer);
		     }

		     if (fka_has_changed) {
		         u64 tov;
	
			 if (iport->selected_fcf.fka_adv_period != 0) {

                             tov = jiffies + msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
                             mod_timer(&fnic->enode_ka_timer, round_jiffies(tov));

                             tov = jiffies + msecs_to_jiffies(FCOE_CTLR_VN_KA_TOV);
                             mod_timer(&fnic->vn_ka_timer, round_jiffies(tov));
			}	
		     }
                 }
             }
        }
}

/**
 * fnic_fcoe_start_flogi
 *
 * Sends FIP FLOGI to the selected FCF
 *
 * @param fnic fnic driver instance
 */

void fnic_fcoe_start_flogi(struct fnic *fnic)
{
        fnic_iport_t *iport= &fnic->iport;

        int fr_len;
        fip_flogi_t flogi_req;
        u64 flogi_tov;

        fr_len = sizeof(fip_flogi_t);
	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p Start fip FLOGI\n", fnic);

        memcpy(&flogi_req, &fip_flogi_tmpl, fr_len);
        memcpy(flogi_req.eth.smac, iport->hwmac, ETH_ALEN);
        if (iport->usefip) {
            memcpy(flogi_req.eth.dmac, iport->selected_fcf.fcf_mac, ETH_ALEN);
        }
        flogi_req.flogi_desc.flogi.nport_name = htonll(iport->wwpn);
        flogi_req.flogi_desc.flogi.node_name = htonll(iport->wwnn);

        fnic_send_fip_frame(iport, &flogi_req, fr_len);
        iport->fip.flogi_retry++;

        iport->fip.state = FDLS_FIP_FLOGI_STARTED;
        flogi_tov = jiffies +msecs_to_jiffies(fnic->config.flogi_timeout);
        mod_timer(&fnic->retry_fip_timer, round_jiffies(flogi_tov));
}

/**
 * fnic_fcoe_process_flogi_resp
 *
 * Processes FLOGI response from FCF.
 * If successfull saves assigned fc_id and MAC, programs firmware
 * and starts fdls discovery.
 * Else restarts vlan discovery.
 *
 * @param fnic fnic driver instance
 * @param fiph received frame
 */

void fnic_fcoe_process_flogi_resp(struct fnic *fnic, fip_header_t *fiph)
{
        fnic_iport_t *iport= &fnic->iport;
        fip_flogi_rsp_t *flogi_rsp = (fip_flogi_rsp_t *) fiph;
        int desc_len;
        uint32_t s_id;
    
        struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p FIP FLOGI rsp\n", fnic);
        desc_len = ntohs(flogi_rsp->fip.desc_len);
        if (desc_len != 38) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "fnic_fcoe_process_flogi_resp"
                             " invalid Descriptor List len (%x)\n", desc_len );
        }
        if (!((flogi_rsp->rsp_desc.type==7) && (flogi_rsp->rsp_desc.len==36)) ||
            !((flogi_rsp->mac_desc.type==2) && (flogi_rsp->mac_desc.len==2))) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "dropping frame.."
                    "fnic_fcoe_process_flogi_resp invalid Descriptor "
                    "type and len mix : \n"
                    "flogi_rsp->rsp_desc.type(%x) flogi_rsp->rsp_desc.len(%x) "
                    "flogi_rsp->mac_desc.type(%x) flogi_rsp->mac_desc.len(%x) ",
                    flogi_rsp->rsp_desc.type, flogi_rsp->rsp_desc.len,
                    flogi_rsp->mac_desc.type, flogi_rsp->mac_desc.len);

        }
        s_id = ntoh24(flogi_rsp->rsp_desc.els.fchdr.sid);
        if ((flogi_rsp->rsp_desc.els.fchdr.f_ctl != 0x98) ||
            (flogi_rsp->rsp_desc.els.fchdr.r_ctl !=0x23) ||
            (s_id != 0xFFFFFE) ||
            (flogi_rsp->rsp_desc.els.fchdr.ox_id != FNIC_FLOGI_OXID) ||
            (flogi_rsp->rsp_desc.els.fchdr.type !=0x01)) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "fnic_fcoe_process_flogi_resp"
                    " received Flogi resp with some Invalid fc frame bits "
                    "s_id(%x)  FCTL(%x) R_CTL(%x) type(%x) OX_ID(%x)... "
                    "Dropping the frame..\n", s_id,
                    flogi_rsp->rsp_desc.els.fchdr.f_ctl,
                    flogi_rsp->rsp_desc.els.fchdr.r_ctl,
                    flogi_rsp->rsp_desc.els.fchdr.type,
                    flogi_rsp->rsp_desc.els.fchdr.ox_id);
                return;
        }

        if (iport->fip.state == FDLS_FIP_FLOGI_STARTED) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p rsp for pending FLOGI\n", fnic);

                del_timer_sync(&fnic->retry_fip_timer);

                if ((ntohs(flogi_rsp->fip.desc_len) == 38) &&
                    (flogi_rsp->rsp_desc.els.command == FC_LS_ACC)) {

			FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p FLOGI success\n", fnic);
                    memcpy (iport->fpma, flogi_rsp->mac_desc.mac, ETH_ALEN);
                    iport->fcid = ntoh24(flogi_rsp->rsp_desc.els.fchdr.did);

                    iport->r_a_tov =
                        ntohl(flogi_rsp->rsp_desc.els.u.csp_flogi.r_a_tov);
                    iport->e_d_tov =
                        ntohl(flogi_rsp->rsp_desc.els.u.csp_flogi.e_d_tov);
                    memcpy(fnic->iport.fcfmac, iport->selected_fcf.fcf_mac,
                        ETH_ALEN);
                    vnic_dev_add_addr(fnic->vdev,flogi_rsp->mac_desc.mac);

		    if (fnic_fdls_register_portid(iport, iport->fcid, NULL) !=
                        0 ) {
			FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p flogi registration failed\n", fnic);
                            return;
                    }
	
                    iport->fip.state = FDLS_FIP_FLOGI_COMPLETE;
                    iport->state = FNIC_IPORT_STATE_FABRIC_DISC;
		    FNIC_FIP_DBG(KERN_INFO, fnic, "iport->state:%d\n",
                                iport->state);
                    fnic_fdls_disc_start(iport);
                    if (!((iport->selected_fcf.ka_disabled) ||
                         (iport->selected_fcf.fka_adv_period == 0))) { 
                        u64 tov;
                        
                        tov = jiffies + msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
                        mod_timer(&fnic->enode_ka_timer, round_jiffies(tov));
                        
                        tov = jiffies + msecs_to_jiffies(FCOE_CTLR_VN_KA_TOV);
                        mod_timer(&fnic->vn_ka_timer, round_jiffies(tov));
                        
                    }
                } else {
                        /*
                         * If there's FLOGI rejects - clear all
                         * fcf's & restart from scratch
                         */
                        atomic64_inc(&fnic_stats->vlan_stats.flogi_rejects);
                        // shost_printk(KERN_INFO, fnic->iport->host,
                        //     "Trigger a Link down - VLAN Disc \n");
                        // fcoe_ctlr_link_down(&fnic->ctlr);
                        /* start FCoE VLAN discovery */
                        fnic_fcoe_send_vlan_req(fnic);

                        iport->fip.state = FDLS_FIP_VLAN_DISCOVERY_STARTED;
                }
        }
}

/**
 * fnic_common_fip_cleanup
 *
 * Cleans up FCF info and timers in case of link down/CVL
 *
 * @param fnic fnic driver instance
 */

void fnic_common_fip_cleanup(struct fnic *fnic) {

        fnic_iport_t *iport= &fnic->iport;

        if (!iport->usefip)
                return;
	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p fip cleanup\n", fnic);


        iport->fip.state = FDLS_FIP_INIT;
    
        del_timer_sync(&fnic->retry_fip_timer);
        del_timer_sync(&fnic->fcs_ka_timer);
        del_timer_sync(&fnic->enode_ka_timer);
        del_timer_sync(&fnic->vn_ka_timer);

        if (!is_zero_ether_addr(iport->fpma))
            vnic_dev_del_addr(fnic->vdev, iport->fpma);

        memset (iport->fpma, 0, ETH_ALEN);
        iport->fcid = 0;
        iport->r_a_tov = 0;
        iport->e_d_tov = 0;
        memset(fnic->iport.fcfmac, 0, ETH_ALEN);
        memset(iport->selected_fcf.fcf_mac, 0, ETH_ALEN);
        iport->selected_fcf.fcf_priority = 0;
        iport->selected_fcf.fka_adv_period = 0;
        iport->selected_fcf.ka_disabled = 0;

        fnic_fcoe_reset_vlans(fnic);
}

/**
 * fnic_fcoe_process_cvl
 *
 * Processes Clear Virtual Link from FCF
 * Verifies that cvl is received from our current FCF for our assigned MAC
 * Cleans up and restarts the vlan discovery
 *
 * @param fnic fnic driver instance
 * @param fiph received frame
 */

void fnic_fcoe_process_cvl(struct fnic *fnic, fip_header_t *fiph)
{
        fnic_iport_t *iport= &fnic->iport;
        fip_cvl_t *cvl_msg = (fip_cvl_t *) fiph;
        int i;
        int found = FALSE;
	int max_count = 0;

	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p clear virtual link handler\n", fnic);

        if (!((cvl_msg->fcf_mac_desc.type == 2) &&
            (cvl_msg->fcf_mac_desc.len == 2)) ||
            !((cvl_msg->name_desc.type == 4) &&
            (cvl_msg->name_desc.len == 3))) {
                
	    FNIC_FIP_DBG(KERN_INFO, fnic, " fnic_fcoe_process_cvl "
                    "invalid Descriptor type and len mix : "
                    "fcf_mac_desc.type(%x) fcf_mac_desc.len(%x)  "
                    "cvl_msg->name_desc.type(%x) cvl_msg->name_desc.len(%x) ",
                    cvl_msg->fcf_mac_desc.type, cvl_msg->fcf_mac_desc.len,
                    cvl_msg->name_desc.type, cvl_msg->name_desc.len);
        }

        if (memcmp(iport->selected_fcf.fcf_mac, cvl_msg->fcf_mac_desc.mac,
            ETH_ALEN) == 0) {
                for (i = 0; i < ((ntohs(fiph->desc_len) / 5) - 1); i++) {
                        if (!((cvl_msg->vn_ports_desc[i].type == 11) &&
                            (cvl_msg->vn_ports_desc[i].len == 5))) {
                            
				FNIC_FIP_DBG(KERN_INFO, fnic,
                                    "fnic_fcoe_process_cvl invalid Descriptor "
                                    "type and len mix: vn_ports_desc[i].type"
                                    "(%d) vn_ports_desc[i].len(%d)\n",
                                    cvl_msg->vn_ports_desc[i].type,
                                    cvl_msg->vn_ports_desc[i].len);
                        }
                        if (memcmp(iport->fpma,
                            cvl_msg->vn_ports_desc[i].vn_port_mac,
                            ETH_ALEN) == 0) {
                                found = TRUE;
                                break;
                        }
                }
                if (!found)
                        return;
                fnic_common_fip_cleanup(fnic);
		while (fnic->reset_in_progress == IN_PROGRESS) {
                        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
                        wait_for_completion_timeout(&fnic->reset_completion_wait,
                                msecs_to_jiffies(5000));
                        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);
                        max_count++;
                        if (max_count >= MAX_RESET_WAIT_COUNT) {
                                fnic_log_info(fnic->fnic_num,
                                "Reset thread waited for too long. Skipping handle link event %p\n", fnic);
                                return;
                        }
                        fnic_log_info(fnic->fnic_num,
                        "fnic reset in progress.link event needs to wait %p", fnic);
                }
		fnic->reset_in_progress = IN_PROGRESS;
                fnic_fdls_link_down(iport);
		fnic->reset_in_progress  = NOT_IN_PROGRESS;
		complete(&fnic->reset_completion_wait);

                fnic_fcoe_send_vlan_req(fnic);
        }
}

/**
 * fdls_fip_recv_frame
 *
 * Demultiplexer for FIP frames
 *
 * @param fnic driver instance
 * @param frame received ethernet frame
 * @return Frame processed by FIP
 */

int fdls_fip_recv_frame(struct fnic *fnic, void *frame)
{
    eth_hdr_t *eth = (eth_hdr_t *) frame;
    fip_header_t *fiph;
    u16 protocol;
    u8 sub;

    if (eth->eth_type == ntohs(FIP_ETH_TYPE)) {

        fiph = (fip_header_t *) (eth+1);
        protocol = ntohs(fiph->protocol);
        sub = ntohs(fiph->subcode);

        // fnic_log_info(fnic->fnic_num, " recv fip frame op %x sub %x\n",
        //                                 protocol, sub);

        if (protocol == FIP_DISCOVERY && sub == FIP_SUBCODE_RESP) {
                fnic_fcoe_fip_discovery_resp(fnic, fiph);
        } else if (protocol == FIP_VLAN_DISC && sub == FIP_SUBCODE_RESP) {
                fnic_fcoe_process_vlan_resp(fnic, fiph);
        } else if (protocol == FIP_KA_CVL && sub == FIP_SUBCODE_RESP) {
                fnic_fcoe_process_cvl(fnic, fiph);
        } else if (protocol == FIP_FLOGI && sub == FIP_SUBCODE_RESP) {
                fnic_fcoe_process_flogi_resp(fnic, fiph);
        }
        return 1;
    }
    return 0;
}

void fnic_work_on_fip_timer(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, fip_timer_work);

	fnic_iport_t *iport= &fnic->iport;
	FNIC_FIP_DBG(KERN_INFO, fnic, "fip timeout\n");

        if (iport->fip.state == FDLS_FIP_VLAN_DISCOVERY_STARTED) {
                // fnic_log_info(fnic->fnic_num, "vlan discovey timeout\n");
                fnic_vlan_discovery_timeout(fnic);
        } else if (iport->fip.state == FDLS_FIP_FCF_DISCOVERY_STARTED) {
            u8 zmac[ETH_ALEN] = {0,0,0,0,0,0};

	    FNIC_FIP_DBG(KERN_INFO, fnic, "FCF Discovey timeout\n");
	    if (memcmp(iport->selected_fcf.fcf_mac, zmac, ETH_ALEN) != 0) {

                    if(iport->flags & FNIC_FIRST_LINK_UP){
                          fnic_scsi_fcpio_reset(iport->fnic);
                          iport->flags &= ~FNIC_FIRST_LINK_UP;
                     }

                    fnic_fcoe_start_flogi(fnic);
                    if (!((iport->selected_fcf.ka_disabled) ||
                        (iport->selected_fcf.fka_adv_period == 0))) {
                        u64 fcf_tov;

                        fcf_tov = jiffies +
                                  3 * msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
                        mod_timer(&fnic->fcs_ka_timer, round_jiffies(fcf_tov));
                    }
            } else {
		FNIC_FIP_DBG(KERN_INFO, fnic, "FCF Discovey timeout\n");
		    fnic_vlan_discovery_timeout(fnic);
            }
        } else if (iport->fip.state == FDLS_FIP_FLOGI_STARTED) {
		FNIC_FIP_DBG(KERN_INFO, fnic, "FLOGI timeout\n");
		if (iport->fip.flogi_retry < fnic->config.flogi_retries) {
                        fnic_fcoe_start_flogi(fnic);
                } else {
                        fnic_vlan_discovery_timeout(fnic);
                }
        }
}

/**
 * fnic_handle_fip_timer
 *
 * Timeout handler for FIP discover phase.
 * Based on the current state, starts next phase or restarts discovery
 *
 * @param data Opaque pointer to fnic structure
 */

#if FNIC_USE_SETUP_TIMER
void fnic_handle_fip_timer(unsigned long data) {
	struct fnic *fnic = (struct fnic *)data;
#else
void fnic_handle_fip_timer(struct timer_list *t) {
	struct fnic *fnic = from_timer(fnic, t, retry_fip_timer);
#endif
	INIT_WORK(&fnic->fip_timer_work, fnic_work_on_fip_timer);
        queue_work(fnic_fip_queue, &fnic->fip_timer_work);
}

/**
 * fnic_handle_enode_ka_timer
 *
 * FIP node keep alive.
 *
 * @param data Opaque pointer to fnic struct
 */
#if FNIC_USE_SETUP_TIMER
void fnic_handle_enode_ka_timer(unsigned long data)
{
    struct fnic *fnic = (struct fnic *)data;
#else
void fnic_handle_enode_ka_timer (struct timer_list *t)
    {
	            struct fnic *fnic = from_timer(fnic, t, enode_ka_timer);
#endif
    fnic_iport_t *iport= &fnic->iport;
    int fr_len;
    fip_enode_ka_t enode_ka;
    u64 enode_ka_tov;
    
    // fnic_log_info(fnic->fnic_num,"fnic 0x%p ka timer\n", fnic);

    if (iport->fip.state != FDLS_FIP_FLOGI_COMPLETE) {
        return;
    }

    if ((iport->selected_fcf.ka_disabled) ||
        (iport->selected_fcf.fka_adv_period == 0)) {
        return;
    }

    fr_len = sizeof(fip_enode_ka_t);

    memcpy(&enode_ka, &fip_enode_ka_tmpl, fr_len);
    memcpy(enode_ka.eth.smac, iport->hwmac, ETH_ALEN);
    memcpy(enode_ka.eth.dmac,iport->selected_fcf.fcf_mac, ETH_ALEN);
    memcpy(enode_ka.mac_desc.mac, iport->hwmac, ETH_ALEN);

    fnic_send_fip_frame(iport, &enode_ka, fr_len);
    enode_ka_tov = jiffies +
		   msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
    mod_timer(&fnic->enode_ka_timer, round_jiffies(enode_ka_tov));
}

/**
 * fnic_handle_vn_ka_timer
 *
 * FIP virtual port keep alive.
 *
 * @param data Opaque pointer to fnic structure
 */

#if FNIC_USE_SETUP_TIMER
void fnic_handle_vn_ka_timer(unsigned long data)
{
        struct fnic *fnic = (struct fnic *)data;
#else
void fnic_handle_vn_ka_timer (struct timer_list *t)
    {
	            struct fnic *fnic = from_timer(fnic, t, vn_ka_timer);
#endif
        fnic_iport_t *iport= &fnic->iport;
        int fr_len;
        fip_vn_port_ka_t vn_port_ka;
        u64 vn_ka_tov;
        uint8_t fcid[3];

        // fnic_log_info(fnic->fnic_num,"fnic 0x%p vn port ka timer\n", fnic);

        if (iport->fip.state != FDLS_FIP_FLOGI_COMPLETE) {
                return;
        }

	if ((iport->selected_fcf.ka_disabled) ||
            (iport->selected_fcf.fka_adv_period == 0)) {
		return;
	}

        fr_len = sizeof(fip_vn_port_ka_t);

        memcpy(&vn_port_ka, &fip_vn_port_ka_tmpl, fr_len);
        memcpy(vn_port_ka.eth.smac, iport->fpma, ETH_ALEN);
        memcpy(vn_port_ka.eth.dmac,iport->selected_fcf.fcf_mac, ETH_ALEN);
        memcpy(vn_port_ka.mac_desc.mac, iport->hwmac, ETH_ALEN);
        memcpy(vn_port_ka.vn_port_desc.vn_port_mac, iport->fpma, ETH_ALEN);
        hton24(fcid, iport->fcid);
        memcpy(vn_port_ka.vn_port_desc.vn_port_id, fcid, 3);
        vn_port_ka.vn_port_desc.vn_port_name = htonll(iport->wwpn);

        fnic_send_fip_frame(iport, &vn_port_ka, fr_len);
        vn_ka_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_VN_KA_TOV);
        mod_timer(&fnic->vn_ka_timer, round_jiffies(vn_ka_tov));
}

/**
 * fnic_vlan_discovery_timeout
 *
 * End of VLAN discovery or FCF discovery time window
 * Start the FCF discovery if VLAN was never used
 * Retry in case of FCF not responding or move to next VLAN
 *
 * @param fnic fnic driver instance
 */

void fnic_vlan_discovery_timeout(struct fnic *fnic)
{
        struct fcoe_vlan *vlan;
        fnic_iport_t *iport= &fnic->iport;
        struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        unsigned long flags;
    
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, flags);
        if (fnic->stop_rx_link_events) {
                MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);
                return;
        }
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, flags);

        if (!iport->usefip)
                 return;

        MY_SPIN_LOCK_IRQ_SAVE(&fnic->vlans_lock, flags);
        if (list_empty(&fnic->vlan_list)) {
                /* no vlans available, try again */
                MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);
                fnic_fcoe_send_vlan_req(fnic);
                return;
        }
    
        vlan = list_first_entry(&fnic->vlan_list, struct fcoe_vlan, list);

        if (vlan->state == FIP_VLAN_SENT) {
                if (vlan->sol_count >= FCOE_CTLR_MAX_SOL) {
                        /*
                         * no response on this vlan, remove  from the list.
                         * Try the next vlan
                         */
                        list_del(&vlan->list);
                        kfree(vlan);
                        vlan = NULL;
                        if (list_empty(&fnic->vlan_list)) {
                            /* we exhausted all vlans, restart vlan disc */
                            MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);
                            fnic_fcoe_send_vlan_req(fnic);
                            return;
                        }
                        /* check the next vlan */
                        vlan = list_first_entry(&fnic->vlan_list,struct fcoe_vlan, list);
                    
                        fnic->set_vlan(fnic, vlan->vid);
                        vlan->state = FIP_VLAN_SENT; /* sent now */

                }
                atomic64_inc(&fnic_stats->vlan_stats.sol_expiry_count);

        } else {
                fnic->set_vlan(fnic, vlan->vid);
                vlan->state = FIP_VLAN_SENT; /* sent now */
        }
        vlan->sol_count++;
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->vlans_lock, flags);
        fnic_fcoe_start_fcf_discovery(fnic);
}

/**
 * fnic_work_on_fcs_ka_timer - finish handling fcs_ka_timer in process context
 * We need to finish this timer in a process context so that we do
 * not hand in fip_common_cleanup. Here we clean up, bring the link down
 * and restart all FIP discovery.
 *
 * @work - the work queue that we will be servicing
 */

void fnic_work_on_fcs_ka_timer(struct work_struct *work)
{
        struct fnic *fnic = container_of(work, struct fnic, fip_timer_work);
        fnic_iport_t *iport= &fnic->iport;

	FNIC_FIP_DBG(KERN_INFO, fnic, "fnic 0x%p fcs ka timeout\n", fnic);

        fnic_common_fip_cleanup(fnic);
        spin_lock_irqsave(&fnic->fnic_lock,fnic->lock_flags);
        fnic_fdls_link_down(iport);
	iport->state = FNIC_IPORT_STATE_FIP;
        spin_unlock_irqrestore(&fnic->fnic_lock,fnic->lock_flags);

        fnic_fcoe_send_vlan_req(fnic);
}

/**
 * fnic_handle_fcs_ka_timer
 *
 * No keep alives received from FCF. Clean up, bring the link down
 * and restart all the FIP discovery.
 *
 * @param data Opaque pointer to fnic structure
 */

#if FNIC_USE_SETUP_TIMER
void fnic_handle_fcs_ka_timer(unsigned long data) {
	struct fnic *fnic = (struct fnic *)data;
#else
void fnic_handle_fcs_ka_timer(struct timer_list *t) {
        struct fnic *fnic = from_timer(fnic, t, fcs_ka_timer);
#endif
	INIT_WORK(&fnic->fip_timer_work, fnic_work_on_fcs_ka_timer);
        queue_work(fnic_fip_queue, &fnic->fip_timer_work);
}
