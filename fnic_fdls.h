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

#ifndef _FNIC_FDLS_H_
#define _FNIC_FDLS_H_

#include "fnic_stats.h"
#include "fdls_fc.h"


/* FDLS - Fabric discovery and login services
 * -> VLAN discovery
 *   -> retry every retry delay seconds until it succeeds.
 *                        <- List of VLANs
 *
 * -> Solicitation
 *                        <- Solicitation response (Advertisement)
 *
 * -> FCF selection & FLOGI ( FLOGI timeout - 2 * E_D_TOV)
 *                        <- FLOGI response
 *
 * -> FCF keep alive
 *                         <- FCF keep alive
 *
 * -> PLOGI to FFFFFC (DNS) (PLOGI timeout - 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PLOGI response
 *    -> Retry PLOGI to FFFFFC (DNS) - Number of retries from vnic.cfg
 *
 * -> SCR to FFFFFC (DNS) (SCR timeout - 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- SCR response
 *    -> Retry SCR - Number of retries 2
 *
 * -> GPN_FT to FFFFFC (GPN_FT timeout - 2 * R_A_TOV)a
 *    -> Retry on BUSY until it succeeds
 *    -> Retry on BUSY until it succeeds
 *    -> 2 retries on timeout
 *
 * -> RFT_ID to FFFFFC (DNS)        (RFT_ID timeout - 3 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *    -> Retry RFT_ID to FFFFFC (DNS) (Number of retries 2 )
 *    -> Ignore if both retires fail.
 *
 *        Session establishment with targets
 * For each PWWN
 *   -> PLOGI to FCID of that PWWN (PLOGI timeout 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PLOGI response
 *    -> Retry PLOGI. Num retries using vnic.cfg
 *
 *   -> PRLI to FCID of that PWWN (PRLI timeout 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PRLI response
 *    -> Retry PRLI. Num retries using vnic.cfg
 *
 */
#define FIP 0

#if FIP
#define FNIC_VLAN_RETRY_DELAY     2000
#define FNIC_MAX_FCFS             8
#define FNIC_FIRST_FCF_OXID       FCPIO_HOST_EXCH_RANGE_START
#define FNIC_LAST_FCF_OXID        (FNIC_FIRST_FCF_OXID + FNIC_MAX_FCFS - 1)

#define FNIC_MAX_TGTS             1024 /* Size of the bit map */
#define FNIC_FIRST_TGT_OXID       (FNIC_LAST_FCF_OXID + 1)
                                  /* bit value + base value is the OXID */

/* Timers for FIP */
#define FNIC_VLAN_DISC_TIMER      2000
#define FNIC_SOLICITATION_TIMER   2000
#define FNIC_FKA_ADV_PERIOD       8000
#define FNIC_FKA_VN_PERIOD        90000

typedef uint32_t fc_id_t;

struct fnic_fcf {
           struct list_head links; /* To link FCFs */
/* MAC address */
/* */
};
#endif

#define FDLS_RETRY_COUNT 2

//TBD revisit the whole OXID scheme
#define FDLS_TGT_OXID_POOL_SZ   (0x800)
#define FDLS_TGT_OXID_BLOCK_SZ  (0x200)
#define FDLS_PLOGI_OXID_BASE    (0x2000)
#define FDLS_PRLI_OXID_BASE     (0x2200)
#define FDLS_ADISC_OXID_BASE    (0x2400)
#define FDLS_TGT_OXID_POOL_END  (FDLS_PLOGI_OXID_BASE + FDLS_TGT_OXID_POOL_SZ)

#define NVFNIC_LSREQ_OXID_BASE     (0x2500)
#define NVFNIC_LSREQ_OXID_POOL_SZ   (64)

typedef struct fnic_fip_fcf_s {
        uint16_t  vlan_id;
        uint8_t   fcf_mac[6];
        uint8_t   fcf_priority; //??
        uint32_t  fka_adv_period;
        uint8_t   ka_disabled;
} fnic_fip_fcf_t;

/* FDLS structure - not visible to fnic driver */
typedef enum fnic_fdls_state_e {
        FDLS_STATE_INIT = 0,
        FDLS_STATE_LINKDOWN,    //TBD_REVISIT
        FDLS_STATE_FABRIC_LOGO,
        FDLS_STATE_FLOGO_DONE,
        FDLS_STATE_FABRIC_FLOGI,
        FDLS_STATE_FABRIC_PLOGI,
	FDLS_STATE_RPN_ID,
        FDLS_STATE_REGISTER_FC4_TYPES,
        FDLS_STATE_REGISTER_FC4_FEATURES,
        FDLS_STATE_SCR,
        FDLS_STATE_GPN_FT,
        FDLS_STATE_TGT_DISCOVERY, //review: fdls_state_online
        FDLS_STATE_RSCN_GPN_FT,
	FDLS_STATE_SEND_GPNFT
} fdls_state_t;

#define FNIC_PLOGI_DONE(fnic) (((FDLS_STATE_INIT         == fnic->iport.fabric.state) || \
                                (FDLS_STATE_LINKDOWN     == fnic->iport.fabric.state) || \
                                (FDLS_STATE_FABRIC_LOGO  == fnic->iport.fabric.state) || \
                                (FDLS_STATE_FLOGO_DONE   == fnic->iport.fabric.state) || \
                                (FDLS_STATE_FABRIC_FLOGI == fnic->iport.fabric.state))?0:1)

#define FNIC_FDLS_FABRIC_ABORT_ISSUED     0x1
#define FNIC_FDLS_FPMA_LEARNT             0x2
typedef struct fnic_fdls_s {
        fdls_state_t        state;
        uint32_t            flags;
        struct list_head tport_list; /* List of discovered tports */

        struct timer_list retry_timer;
	int        del_timer_inprogress;
	int        del_fdmi_timer_inprogress;
        int        retry_counter;
        int        timer_pending;
	int 	   fdmi_retry;
	struct timer_list fdmi_timer;
	int 	   fdmi_pending;

} fnic_fdls_fabric_t;

typedef struct fnic_fdls_fip_s {
    uint32_t state;
    uint32_t flogi_retry;
} fnic_fdls_fip_t;

/* Message to tport_event_handler */
enum fnic_tgt_msg_id {
        TGT_EV_NONE = 0,
        TGT_EV_RPORT_ADD,
        TGT_EV_RPORT_DEL,
        TGT_EV_TPORT_DELETE,
        TGT_EV_REMOVE
};

typedef struct fnic_tport_event_s {
        struct list_head	    links;
        enum fnic_tgt_msg_id        event;
	void                        *arg1;
} fnic_tport_event_t;

typedef enum fdls_tgt_state_e {
        fdls_tgt_state_init = 0,
        fdls_tgt_state_plogi,
        fdls_tgt_state_prli,
        fdls_tgt_state_ready,
        fdls_tgt_state_logo_recvd,
        fdls_tgt_state_adisc,
        fdls_tgt_state_plogo,
        fdls_tgt_state_offlining,
        fdls_tgt_state_offline
} fdls_tgt_state_t;

/* tport flags */
#define FNIC_FDLS_TPORT_IN_GPN_FT_LIST 0x1
#define FNIC_FDLS_TGT_ABORT_ISSUED     0x2
#define FNIC_FDLS_TPORT_SEND_ADISC     0x4
#define FNIC_FDLS_RETRY_FRAME          0x8
#define FNIC_FDLS_TPORT_BUSY	       0x10
#define FNIC_FDLS_TPORT_TERMINATING      0x20
#define FNIC_FDLS_TPORT_DELETED        0x40
#define FNIC_FDLS_NVME_REGISTERED      0x80
#define NVME_TPORT_CLEANUP_PENDING     0x100
#define FNIC_FDLS_SCSI_REGISTERED      0x200
#define FNIC_TPORT_CAN_BE_FREED        0x400

#define FC_RP_FLAGS_RETRY 0x1 // Retry supported by rport(returned by prli service parameters)
typedef struct fnic_tport_s {
        struct list_head links; /* To link the tports */

        fdls_tgt_state_t state;
        uint32_t         flags;
        uint32_t         fcid;
        uint64_t         wwpn;
        uint64_t         wwnn;
        uint16_t         oxid_used;
        uint16_t         tgt_flags;
        atomic_t         in_flight;             /* io counter */
        uint16_t         max_payload_size;
        uint16_t         r_a_tov;
        uint16_t         e_d_tov;
        uint16_t         lun0_delay;
        int              max_concur_seqs;
        uint32_t         fcp_csp;

        struct timer_list retry_timer;
	int              del_timer_inprogress;	
        int              retry_counter;
        int              timer_pending;
        unsigned int     num_pending_cmds;
	int nexus_restart_count;
	int exch_reset_in_progress;
        void* iport;
	struct nvme_fc_remote_port *nv_rport;
	struct list_head lsreq_list;
	struct work_struct tport_del_work;
	struct completion *tport_del_done;
	struct delayed_work tport_scan_work;
	struct fc_rport *rport;
	char str_wwpn[20];
	char str_wwnn[20];

} fnic_tport_t;

/* iport */
typedef enum fnic_iport_state_e {
        FNIC_IPORT_STATE_INIT = 0,
        FNIC_IPORT_STATE_LINK_WAIT,
        FNIC_IPORT_STATE_FIP,
        FNIC_IPORT_STATE_FABRIC_DISC,
        FNIC_IPORT_STATE_READY
} fnic_iport_state_t;

#define fdls_set_state(_fdls_fabric, _state)    ((_fdls_fabric)->state = _state)
#define fdls_get_state(_fdls_fabric)            ((_fdls_fabric)->state)

#define FNIC_IPORT_IO_BLOCKED 0x1
#define FNIC_FIRST_LINK_UP    0x2
#define FNIC_LPORT_NVME_REGISTERED 0x4
#define FNIC_FDMI_ACTIVE	0x8

#define NVFNIC_FCPIO_TAG_POOL_SZ (2048)

typedef struct fnic_iport_s {
        fnic_iport_state_t  state;
        struct fnic         *fnic;
	struct nvme_fc_local_port *nv_lport;
        uint64_t            boot_time;
        uint32_t     	    flags;
        int                 usefip;
        uint8_t             hwmac[6];  /* HW MAC Addr */
        uint8_t             fpma[6];   /* Fabric Provided MA */
        uint8_t             fcfmac[6]; /* MAC addr of Fabric */
        uint16_t            vlan_id;
        uint32_t            fcid;
        uint8_t 			tgt_oxid_pool[FDLS_TGT_OXID_POOL_SZ];
        uint8_t             lsreq_oxid_pool[NVFNIC_LSREQ_OXID_POOL_SZ];
        void *              nvfnic_fcpio_tag[NVFNIC_FCPIO_TAG_POOL_SZ];
        struct list_head    fcpio_list; /* nvme */

        fnic_fip_fcf_t      selected_fcf;
        fnic_fdls_fip_t     fip;

        fnic_fdls_fabric_t        fabric; //fc_transport?
        struct list_head     tport_list; /* TBD move to fdls_fabric? */
        struct list_head     tport_list_pending_del; 
        struct list_head     inprocess_tport_list; /* list of tports for which we are yet to send PLOGO */
        struct list_head     deleted_tport_list;

        /* TBD rename them */
        struct work_struct  tport_event_work;
        /* Config block: TBD decide the types here */
        uint32_t            e_d_tov; //msec
        uint32_t            r_a_tov; //msec
        uint32_t            link_supported_speeds;
        uint32_t            max_flogi_retries;
        uint32_t            max_plogi_retries;
        uint32_t            plogi_timeout;
        uint32_t            service_params;
        uint64_t            wwpn;
        uint64_t            wwnn;
        uint16_t            max_payload_size;

	spinlock_t deleted_tport_lst_lock;
        /* TBD_REVISIT Temporary workarounds */
	struct completion *flogi_reg_done;
        //struct timer_list retry_timer;
        struct fnic_iport_stats iport_stats;
	char str_wwpn[20];
	char str_wwnn[20];

} fnic_iport_t;

typedef struct rport_dd_data_s {
    fnic_tport_t *tport;
    fnic_iport_t *iport;
}rport_dd_data_t;

typedef enum fnic_recv_frame_type_e {
        FNIC_FABRIC_FLOGI_RSP = 0,
        FNIC_FABRIC_PLOGI_RSP,
	FNIC_FDMI_PLOGI_RSP,
        FNIC_FABRIC_RPN_RSP,
	FNIC_FABRIC_RFT_RSP,
	FNIC_FABRIC_RFF_RSP,
        FNIC_FABRIC_SCR_RSP,
        FNIC_FABRIC_GPN_FT_RSP,
        FNIC_TPORT_PLOGI_RSP,
        FNIC_TPORT_PRLI_RSP,
        FNIC_TPORT_ADISC_RSP,
        FNIC_TPORT_LOGO_RSP,
	FNIC_BLS_ABTS_REQ,
        FNIC_FABRIC_LOGO_RSP,
        FNIC_BLS_ABTS_RSP,
        FNIC_ELS_PLOGI_REQ,
        FNIC_ELS_RSCN_REQ,
        FNIC_ELS_LOGO_REQ,
	FNIC_ELS_ECHO_REQ,
	FNIC_ELS_ADISC,
	FNIC_ELS_RLS,
	FNIC_ELS_UNSUPPORTED_REQ,
	FNIC_ELS_RRQ,
	FNIC_FDMI_RSP,
} fnic_recv_frame_type_t;

//TBD cleanup and merge with above
#define fdls_set_tport_state(_tport, _state)    (_tport->state = _state)
#define fdls_get_tport_state(_tport)            (_tport->state)



#define fnic_del_fabric_timer_sync()    {                               \
        iport->fabric.del_timer_inprogress = 1;                         \
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);  \
        del_timer_sync(&iport->fabric.retry_timer);                     \
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);      \
        iport->fabric.del_timer_inprogress = 0;                         \
}

#define fnic_del_tport_timer_sync()     {                               \
        tport->del_timer_inprogress = 1;                          \
        MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);  \
        del_timer_sync(&tport->retry_timer);                    \
        MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);      \
        tport->del_timer_inprogress = 0;                                \
}                                       




/* Funtion Declarations */

/* fdls_disc.c */
void fnic_fdls_disc_init(fnic_iport_t *iport);
void fnic_fdls_disc_start(fnic_iport_t *iport);
void fnic_fdls_recv_frame(fnic_iport_t *iport, void *rx_frame, int len,
    int fchdr_offset);
void fnic_fdls_link_down(fnic_iport_t *iport);
void fdls_init_tgt_oxid_pool(fnic_iport_t *iport);
void fdls_tgt_logout(fnic_iport_t *iport, fnic_tport_t *tport);


/* fdls_if.c */
void fnic_fdls_init(struct fnic *fnic, int usefip);
void fnic_fdls_cleanup(struct fnic *fnic);

int fnic_send_fcoe_frame(struct fnic_iport_s *iport,
    void *payload, int payload_sz);
int fnic_send_fip_frame(struct fnic_iport_s *iport,
    void *payload, int payload_sz);
void fnic_fdls_learn_fcoe_macs(fnic_iport_t *iport, void *rx_frame,
    uint8_t *fcid);
void fnic_fdls_add_tport(fnic_iport_t *iport, fnic_tport_t *tport,unsigned long flags);
void fnic_fdls_remove_tport(fnic_iport_t *iport, fnic_tport_t *tport,unsigned long flags);
void fnic_set_port_id(fnic_iport_t *iport, u32 port_id, void *fp);
void fnic_exch_mgr_reset(fnic_iport_t *iport, u32 sid, u32 did);


/* fip.c */
void fnic_fcoe_send_vlan_req(struct fnic *fnic);
void fnic_common_fip_cleanup(struct fnic *fnic);
int fdls_fip_recv_frame(struct fnic *fnic, void *frame);
#if FNIC_USE_SETUP_TIMER
void fnic_handle_fcs_ka_timer(unsigned long data);
void fnic_handle_enode_ka_timer(unsigned long data);
void fnic_handle_vn_ka_timer(unsigned long data);
void fnic_handle_fip_timer(unsigned long data);
extern void fdls_fabric_timer_callback(unsigned long arg);
#else
void fnic_handle_fcs_ka_timer(struct timer_list *t);
void fnic_handle_enode_ka_timer(struct timer_list *t);
void fnic_handle_vn_ka_timer(struct timer_list *t);
void fnic_handle_fip_timer(struct timer_list *t);
extern void fdls_fabric_timer_callback(struct timer_list *t);
#endif
/* fnic_scsi.c */
void fnic_scsi_fcpio_reset(struct fnic *fnic);
void fnic_rport_exch_reset(struct fnic *fnic, u32 fcid);

/* more of utils, TBD_REVIST */
void fnic_fdls_learn_fcoe_macs(fnic_iport_t *iport, void *rx_frame,
    uint8_t *fcid);
int fnic_fdls_register_portid(fnic_iport_t *iport, u32 port_id, void *fp);
fnic_tport_t* fnic_find_tport(fnic_iport_t *iport, uint64_t wwpn);
fnic_tport_t* fnic_find_tport_by_fcid(fnic_iport_t *iport, uint32_t fcid);
fnic_tport_t* fnic_find_tport_by_wwpn(fnic_iport_t *iport,uint64_t  wwpn);
fnic_tport_t* fnic_tport_by_targetid(fnic_iport_t *iport, uint32_t target_id);
fnic_tport_t* fnic_deletedtport_by_targetid(fnic_iport_t *iport, uint32_t target_id);

/* TBD Find home and meaning */
#define fnic_PORTSPEED_10GBIT   1
#define BUF_LEN                 (2240)         /* TBD_REVISIT */

#define FNIC_FRAME_HEADROOM     (32)
#define FNIC_FCOE_FRAME_MAXSZ   (2112)
#define FNIC_FRAME_TAILROOM     (8)

#endif /* _FNIC_FDLS_H_ */

