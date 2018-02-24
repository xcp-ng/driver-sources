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
#ifndef _FNIC_H_
#define _FNIC_H_

#include "fnic_config.h"

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/version.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc_frame.h>
#include <linux/etherdevice.h>
#include "fnic_fdls.h"
#include "fnic_io.h"
#include "fnic_res.h"
#include "fnic_trace.h"
#include "fnic_stats.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_wq_copy.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_scsi.h"

#define MY_SPIN_LOCK_IRQ_SAVE(arg1, arg2)  spin_lock_irqsave(arg1,arg2);
/*	{printk("fnic : <%d> ++++ trying fnic lock at ++++ %s:%s:%d\n",fnic->fnic_num, __func__, __FILE__,__LINE__); spin_lock_irqsave(arg1,arg2); \
		printk("fnic :<%d>locked at %s:%s:%d\n",fnic->fnic_num, __func__, __FILE__,__LINE__);} 
*/

#define MY_SPIN_UNLOCK_IRQRESTORE(arg1, arg2) spin_unlock_irqrestore(arg1,arg2);
/*	{printk("fnic : <%d> ---- fnic UNLOCKED at ----- %s:%s:%d\n", fnic->fnic_num,  __func__, __FILE__,__LINE__); spin_unlock_irqrestore(arg1,arg2);} 
*/
#define DRV_NAME		"fnic"
#define DRV_DESCRIPTION		"Cisco MQ FNIC FC and NVME HBA Driver"
#define DRV_VERSION             PACKAGE_VERSION

#define PFX			DRV_NAME ": "
#define DFX                     DRV_NAME "%d: "

#define FABRIC_LOGO_MAX_RETRY 3

#define DESC_CLEAN_LOW_WATERMARK	8
#define FNIC_UCSM_DFLT_THROTTLE_CNT_BLD	16 /* UCSM default throttle count */
#define FNIC_MIN_IO_REQ			256 /* Min IO throttle count */
#define FNIC_MAX_IO_REQ			1024 /* scsi_cmnd tag map entries */
#define FNIC_DFLT_IO_REQ                256 /* scsi_cmnd tag map entries */
#define FNIC_IO_LOCKS			64 /* IO locks: power of 2 */
#define FNIC_DFLT_QUEUE_DEPTH		256
#define FNIC_STATS_RATE_LIMIT		4 /* limit rate at which stats are pulled up */
#if !defined(__OFC__) || defined(__RHEL56__)
#define FNIC_MAX_CMD_LEN		16 /* Supported CDB length */
#endif

/*
 * Tag bits used for special requests.
 */
#define FNIC_TAG_ABORT		BIT(30)		/* tag bit indicating abort */
#define FNIC_TAG_DEV_RST	BIT(29)		/* indicates device reset */
#define FNIC_TAG_MASK		(BIT(24) - 1)	/* mask for lookup */
#define FNIC_NO_TAG             -1

/*
 * Command flags to identify the type of command and for other future
 * use.
 */
#define FNIC_NO_FLAGS                   0
#define FNIC_IO_INITIALIZED             BIT(0)
#define FNIC_IO_ISSUED                  BIT(1)
#define FNIC_IO_DONE                    BIT(2)
#define FNIC_IO_REQ_NULL                BIT(3)
#define FNIC_IO_ABTS_PENDING            BIT(4)
#define FNIC_IO_ABORTED                 BIT(5)
#define FNIC_IO_ABTS_ISSUED             BIT(6)
#define FNIC_IO_TERM_ISSUED             BIT(7)
#define FNIC_IO_INTERNAL_TERM_ISSUED    BIT(8)
#define FNIC_IO_ABT_TERM_DONE           BIT(9)
#define FNIC_IO_ABT_TERM_REQ_NULL       BIT(10)
#define FNIC_IO_ABT_TERM_TIMED_OUT      BIT(11)
#define FNIC_DEVICE_RESET               BIT(12)  /* Device reset request */
#define FNIC_DEV_RST_ISSUED             BIT(13)
#define FNIC_DEV_RST_TIMED_OUT          BIT(14)
#define FNIC_DEV_RST_ABTS_ISSUED        BIT(15)
#define FNIC_DEV_RST_TERM_ISSUED        BIT(16)
#define FNIC_DEV_RST_DONE               BIT(17)
#define FNIC_DEV_RST_REQ_NULL           BIT(18)
#define FNIC_DEV_RST_ABTS_DONE          BIT(19)
#define FNIC_DEV_RST_TERM_DONE          BIT(20)
#define FNIC_DEV_RST_ABTS_PENDING       BIT(21)
#define FNIC_NVME_ADMINIO_TIMER_PENDING BIT(22)
#define FNIC_NVME_ADMIN_IO              BIT(23)
/*
 * Usage of the scsi_cmnd scratchpad.
 * These fields are locked by the hashed io_req_lock.
 */
#define CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)
#define CMD_STATE(Cmnd)		((Cmnd)->SCp.phase)
#define CMD_ABTS_STATUS(Cmnd)	((Cmnd)->SCp.Message)
#define CMD_LR_STATUS(Cmnd)	((Cmnd)->SCp.have_data_in)
#define CMD_TAG(Cmnd)           ((Cmnd)->SCp.sent_command)
#define CMD_FLAGS(Cmnd)         ((Cmnd)->SCp.Status)

#define NVME_CMD_SP(Cmnd)            ((Cmnd)->fcp_req) /* TBD */
#define NVME_CMD_STATE(Cmnd)         ((Cmnd)->cmd_state)
#define NVME_CMD_ABTS_STATUS(Cmnd)   ((Cmnd)->abts_state)
#define NVME_CMD_LR_STATUS(Cmnd)     ((Cmnd)->SCp.have_data_in)
#define NVME_CMD_TAG(Cmnd)           ((Cmnd)->tag)
#define NVME_CMD_FLAGS(Cmnd)         ((Cmnd)->status)

#define FCPIO_INVALID_CODE 0x100 /* hdr_status value unused by firmware */

#define FNIC_LUN_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_HOST_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_RMDEVICE_TIMEOUT        1000       /* mSec */
#define FNIC_HOST_RESET_SETTLE_TIME  30         /* Sec */
#define FNIC_ABT_TERM_DELAY_TIMEOUT  500        /* mSec */
#define FNIC_FW_RESET_TIMEOUT        60000     /* mSec   */
#define FNIC_MAX_FCP_TARGET     256
#define FNIC_NVME_ADMINIO_TIMEOUT 30000 /* mSec */

/**
 * state_flags to identify host state along along with fnic's state
 **/
#define __FNIC_FLAGS_FWRESET		BIT(0) /* fwreset in progress */
#define __FNIC_FLAGS_BLOCK_IO		BIT(1) /* IOs are blocked */

#define FNIC_FLAGS_NONE			(0)
#define FNIC_FLAGS_FWRESET		(__FNIC_FLAGS_FWRESET | \
					__FNIC_FLAGS_BLOCK_IO)

#define FNIC_FLAGS_IO_BLOCKED		(__FNIC_FLAGS_BLOCK_IO)

#define fnic_set_state_flags(fnicp, st_flags)	\
	__fnic_set_state_flags(fnicp, st_flags, 0)

#define fnic_clear_state_flags(fnicp, st_flags)  \
	__fnic_set_state_flags(fnicp, st_flags, 1)

extern unsigned int fnic_log_level;
extern unsigned int fnic_fdmi_support;
extern unsigned int io_completions;

enum reset_states{
	NOT_IN_PROGRESS = 0,
	IN_PROGRESS,
	RESET_ERROR
};

enum rscn_type {
    NOT_PC_RSCN = 0,
    PC_RSCN
};

enum pc_rscn_handling_status {
    PC_RSCN_HANDLING_NOT_IN_PROGRESS = 0,
    PC_RSCN_HANDLING_IN_PROGRESS
};

enum pc_rscn_handling_feature {
    PC_RSCN_HANDLING_FEATURE_OFF = 0,
    PC_RSCN_HANDLING_FEATURE_ON
};

#define FNIC_MAIN_LOGGING	0x01
#define FNIC_FCS_LOGGING	0x02
#define FNIC_SCSI_LOGGING	0x04
#define FNIC_ISR_LOGGING	0x08
#define FNIC_FDLS_LOGGING	0x10
#define FNIC_NVME_LOGGING	0x20
#define FNIC_FIP_LOGGING	0x40

#define FNIC_CHECK_LOGGING(LEVEL, CMD)				\
do {								\
	if (unlikely(fnic_log_level & LEVEL))			\
		do {						\
			CMD;					\
		} while (0);					\
} while (0)

#define fnic_printk(kern_level,fnic, fmt, ...)           \
                         fnic->role?printk(kern_level "fnic: <%d> %s %d" fmt, fnic->fnic_num, __FUNCTION__, __LINE__, ##__VA_ARGS__):shost_printk(kern_level, fnic->host, fmt, ##__VA_ARGS__)

#define FNIC_MAIN_DBG(kern_level, fnic, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_MAIN_LOGGING,			\
			 fnic_printk(kern_level, fnic, fmt, ##args);)

#define FNIC_FCS_DBG(kern_level, fnic, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_FCS_LOGGING,			\
			 fnic_printk(kern_level, fnic, fmt, ##args);)

#define FNIC_SCSI_DBG(kern_level, fnic, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_SCSI_LOGGING,			\
			 fnic_printk(kern_level, fnic, fmt, ##args);)

#define FNIC_ISR_DBG(kern_level, fnic, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_ISR_LOGGING,			\
			 fnic_printk(kern_level, fnic,fmt, ##args);)

#define FNIC_MAIN_NOTE(kern_level, fnic, fmt, args...)		\
			 shost_printk(kern_level, host, fmt, ##args)

#define FNIC_FDLS_DBG(kern_level, fnic, fmt, args...)               \
        FNIC_CHECK_LOGGING(FNIC_FDLS_LOGGING,                   \
                         fnic_printk(kern_level, fnic, fmt, ##args);)

#define FNIC_NVME_DBG(kern_level, fnic, fmt, args...)               \
        FNIC_CHECK_LOGGING(FNIC_NVME_LOGGING,                   \
                         fnic_printk(kern_level, fnic, fmt, ##args);)

#define FNIC_FIP_DBG(kern_level, fnic, fmt, args...)               \
        FNIC_CHECK_LOGGING(FNIC_FIP_LOGGING,                   \
                         fnic_printk(kern_level, fnic, fmt, ##args);)

#define dbgprintk(level, _fmt, ...) printk(level _fmt, __VA_ARGS__)
#define fnic_log_emerg(hostnum, _fmt, ...) printk(KERN_EMERG "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_alert(hostnum, _fmt, ...) printk(KERN_ALERT "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_crit(hostnum, _fmt, ...) printk(KERN_CRIT "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_err(hostnum, _fmt, ...) printk(KERN_ERR "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_warning(hostnum, _fmt, ...) printk(KERN_WARNING "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_notice(hostnum, _fmt, ...) printk(KERN_NOTICE "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_info(hostnum, _fmt, ...) printk(KERN_INFO "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)
#define fnic_log_debug(hostnum, _fmt, ...) printk(KERN_DEBUG "fnic: <%d>: %s: %d: " _fmt, hostnum, __FUNCTION__, __LINE__, __VA_ARGS__)

#define FNIC_WQ_COPY_MAX 64
#define FNIC_WQ_MAX 1
#define FNIC_RQ_MAX 1
#define FNIC_CQ_MAX (FNIC_WQ_COPY_MAX + FNIC_WQ_MAX + FNIC_RQ_MAX)
#define FNIC_DFLT_IO_COMPLETIONS 256


extern const char *fnic_state_str[];

enum fnic_intx_intr_index {
    FNIC_INTX_WQ_RQ_COPYWQ,
    FNIC_INTX_DUMMY,
    FNIC_INTX_NOTIFY,
    FNIC_INTX_ERR,
    FNIC_INTX_INTR_MAX,
};

enum fnic_msix_intr_index {
	FNIC_MSIX_RQ,
	FNIC_MSIX_WQ,
	FNIC_MSIX_WQ_COPY,
	FNIC_MSIX_ERR_NOTIFY=FNIC_MSIX_WQ_COPY+FNIC_WQ_COPY_MAX,
	FNIC_MSIX_INTR_MAX,
};

struct fnic_msix_entry {
	int requested;
	char devname[IFNAMSIZ];
	irqreturn_t (*isr)(int, void *);
	void *devid;
	int irq_num;
};

enum fnic_state {
	FNIC_IN_FC_MODE = 0,
	FNIC_IN_FC_TRANS_ETH_MODE,
	FNIC_IN_ETH_MODE,
	FNIC_IN_ETH_TRANS_FC_MODE,
};

enum fnic_role_e {
        FNIC_ROLE_FCP_INITIATOR = 0,
        FNIC_ROLE_FCP_TARGET,
        FNIC_ROLE_NVME_INITIATOR,
        FNIC_ROLE_NVME_TARGET,
};


struct mempool;

enum fnic_evt {
	FNIC_EVT_START_VLAN_DISC = 1,
	FNIC_EVT_START_FCF_DISC = 2,
	FNIC_EVT_MAX,
};

struct fnic_event {
	struct list_head list;
	struct fnic *fnic;
	enum fnic_evt event;
};

struct fnic_cpy_wq {
	unsigned long hw_lock_flags;
	u16 active_ioreq_count;
	u16 ioreq_table_size;
	____cacheline_aligned struct fnic_io_req **io_req_table;
};

struct fnic_frame_list {
        /*
         * Link to frame lists
         */
        struct list_head links;
        void *fp;
        int frame_len;
        int rx_ethhdr_stripped;
};

struct fnic_tag_t {
        struct list_head        free_list;
        int tag_id;
};

/* Per-instance private data structure */
struct fnic {
	int fnic_num;
	enum fnic_role_e role;
	fnic_iport_t   iport;
	struct Scsi_Host *host;
	struct vnic_dev_bar bar0;

#if !FNIC_HAVE_PCI_IRQ_VECTOR
	struct msix_entry msix_entry[FNIC_MSIX_INTR_MAX];
#endif

	struct fnic_msix_entry msix[FNIC_MSIX_INTR_MAX];

	struct vnic_stats *stats;
	unsigned long stats_time;	/* time of stats update */
	unsigned long stats_reset_time;	/* time of stats reset */
	struct vnic_nic_cfg *nic_cfg;
	char name[IFNAMSIZ];
	struct timer_list notify_timer; /* used for MSI interrupts */

	unsigned int fnic_max_tag_id;
	unsigned int err_intr_offset;
	unsigned int link_intr_offset;

	unsigned int wq_count;
	unsigned int cq_count;

	struct dentry *fnic_stats_debugfs_host;
	struct dentry *fnic_stats_debugfs_file;
	struct dentry *fnic_reset_debugfs_file;
	struct dentry *fnic_debug_flags_file;
	u64 fnic_debug_flags;
	struct dentry *fnic_nvmef_debugfs_host;
	struct dentry *fnic_nvmef_debugfs_file;

	struct completion *fw_reset_done;
	struct completion reset_completion_wait;
	unsigned int reset_stats;
	atomic64_t io_cmpl_skip;
	struct fnic_stats fnic_stats;
	u32 vlan_hw_insert:1;	        /* let hw insert the tag */
	u32 in_remove:1;                /* fnic device in removal */
	u32 stop_rx_link_events:1;      /* stop proc. rx frames, link events */

//	struct completion *remove_wait; /* device remove thread blocks */

	atomic_t in_flight;		/* io counter */
	u32 reset_in_progress;
	u32 _reserved;			/* fill hole */
	unsigned long state_flags;	/* protected by host lock */
	enum fnic_state state;
	spinlock_t fnic_lock;
	unsigned long lock_flags;
	u16 vlan_id;	                /* VLAN tag including priority */
	u8 data_src_addr[ETH_ALEN];
	u64 fcp_input_bytes;		/* internal statistic */
	u64 fcp_output_bytes;		/* internal statistic */
	u32 link_down_cnt;
	u32 soft_reset_count;
	int link_status;

	struct list_head list;
	struct list_head links;
	struct pci_dev *pdev;
	struct vnic_fc_config config;
	struct vnic_dev *vdev;
	unsigned int raw_wq_count;
	unsigned int wq_copy_count;
	unsigned int rq_count;
	int fw_ack_index[FNIC_WQ_COPY_MAX];
	unsigned short fw_ack_recd[FNIC_WQ_COPY_MAX];
	unsigned short wq_copy_desc_low[FNIC_WQ_COPY_MAX];
	unsigned int intr_count;
	u32 __iomem *legacy_pba;
	struct fnic_host_tag *tags;
	mempool_t *io_req_pool;
	mempool_t *io_sgl_pool[FNIC_SGL_NUM_CACHES];

	unsigned int cpy_wq_base;

	struct work_struct link_work;
	struct work_struct frame_work;
	struct work_struct iport_work;

	struct list_head nvme_io_event_list;
	unsigned int rsp_cnt;
	wait_queue_head_t rsp_wait;
	struct task_struct *kthread;

	struct work_struct tport_work;
	struct list_head frame_queue;
	struct list_head tx_queue;
	struct list_head tport_event_list;
        struct list_head io_tag_free;
        struct fnic_tag_t *io_tag_pool;
	struct mutex sg3utils_devreset_mutex;
	/*** FIP related data members  -- start ***/
	void (*set_vlan)(struct fnic *, u16 vlan);
	struct work_struct      fip_frame_work;
	struct work_struct      fip_timer_work;
	struct list_head 	fip_frame_queue;
	struct list_head        vlans;
	spinlock_t              vlans_lock;
	struct timer_list       retry_fip_timer;
        struct timer_list       fcs_ka_timer;
        struct timer_list       enode_ka_timer;
        struct timer_list       vn_ka_timer;
	struct list_head        vlan_list;

	struct work_struct      event_work;
	struct list_head        evlist;
	struct completion nvme_del_done;
	struct completion *nvme_tport_empty_wait;
	struct completion *nvme_lport_unreg_done;
	/*** FIP related data members  -- end ***/

	/* copy work queue cache line section */
	____cacheline_aligned struct vnic_wq_copy wq_copy[FNIC_WQ_COPY_MAX];
	____cacheline_aligned struct fnic_cpy_wq fnic_cpy_wq[FNIC_WQ_COPY_MAX];
	/* completion queue cache line section */
	____cacheline_aligned struct vnic_cq cq[FNIC_CQ_MAX];

	spinlock_t wq_copy_lock[FNIC_WQ_COPY_MAX];

	/* work queue cache line section */
	____cacheline_aligned struct vnic_wq wq[FNIC_WQ_MAX];
	spinlock_t wq_lock[FNIC_WQ_MAX];

	/* receive queue cache line section */
	____cacheline_aligned struct vnic_rq rq[FNIC_RQ_MAX];

	/* interrupt resource cache line section */
	____cacheline_aligned struct vnic_intr intr[FNIC_MSIX_INTR_MAX];

    char subsys_desc[14];
    int subsys_desc_len;
    int pc_rscn_handling_status;
};

#define IS_FNIC_FCP_INITIATOR(fnic) (fnic->role == FNIC_ROLE_FCP_INITIATOR)
#define IS_FNIC_NVME_INITIATOR(fnic) (fnic->role == FNIC_ROLE_NVME_INITIATOR)


extern struct workqueue_struct *fnic_event_queue;
extern struct workqueue_struct *fnic_fip_queue;
extern struct device_attribute *fnic_attrs[];


void fnic_clear_intr_mode(struct fnic *fnic);
int fnic_set_intr_mode(struct fnic *fnic);
void fnic_free_intr(struct fnic *fnic);
int fnic_request_intr(struct fnic *fnic);

int fnic_send(struct fc_lport *, struct fc_frame *);
void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf);
void fnic_handle_frame(struct work_struct *work);
void fnic_tport_event_handler(struct work_struct *work);
void fnic_handle_link(struct work_struct *work);
void nvfnic_tport_scan_work(struct work_struct *work);
void fnic_handle_event(struct work_struct *work);
int fnic_rq_cmpl_handler(struct fnic *fnic, int);
int fnic_alloc_rq_frame(struct vnic_rq *rq);
void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf);
void fnic_flush_tx(struct fnic *);
//void fnic_eth_send(struct fcoe_ctlr *, struct sk_buff *skb);
void fnic_update_mac(struct fc_lport *, u8 *new);
void fnic_update_mac_locked(struct fnic *, u8 *new);

int fnic_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
int fc_change_queue_depth(struct scsi_device *, int);
int fnic_abort_cmd(struct scsi_cmnd *);
int fnic_device_reset(struct scsi_cmnd *);
int fnic_eh_host_reset_handler(struct scsi_cmnd *);
int fnic_host_reset(struct Scsi_Host *);
int fnic_reset(struct Scsi_Host *);
int fnic_issue_fc_host_lip(struct Scsi_Host *);
void fnic_scsi_fcpio_reset(struct fnic *);
void fnic_scsi_abort_io(struct fc_lport *);
void fnic_empty_scsi_cleanup(struct fc_lport *);
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int, unsigned int);
int fnic_wq_cmpl_handler(struct fnic *fnic, int);
int fnic_flogi_reg_handler(struct fnic *fnic, u32);
void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq,
				  struct fcpio_host_req *desc);
int fnic_fw_reset_handler(struct fnic *fnic);
void fnic_terminate_rport_io(struct fc_rport *);
const char *fnic_state_to_str(unsigned int state);

void fnic_log_q_error(struct fnic *fnic);
void fnic_handle_link_event(struct fnic *fnic);

void fnic_handle_fip_frame(struct work_struct *work);
void reset_fnic_work_handler(struct work_struct *work);
void fnic_handle_fip_event(struct fnic *fnic);
void fnic_fcoe_reset_vlans(struct fnic *fnic);
void fnic_fcoe_evlist_free(struct fnic *fnic);
#if FNIC_USE_SETUP_TIMER
extern void fnic_handle_fip_timer(unsigned long);
#else
extern void fnic_handle_fip_timer (struct timer_list *t);
#endif
int fnic_mq_map_queues_cpus(struct Scsi_Host *host);
int fnic_stats_debugfs_init(struct fnic *);
void fnic_stats_debugfs_remove(struct fnic *);
int fnic_nvmef_debugfs_init(struct fnic *);
void fnic_nvmef_debugfs_remove(struct fnic *);
void fnic_get_host_port_state(struct Scsi_Host *shost);

void nvfnic_delete_lport(fnic_iport_t *iport);
void nvfnic_delete_tport(fnic_tport_t *tport);

static inline int
fnic_chk_state_flags_locked(struct fnic *fnic, unsigned long st_flags)
{
	return ((fnic->state_flags & st_flags) == st_flags);
}
void __fnic_set_state_flags(struct fnic *, unsigned long, unsigned long);
void fnic_dump_fchost_stats(struct Scsi_Host *, struct fc_host_statistics *);

#endif /* _FNIC_H_ */
