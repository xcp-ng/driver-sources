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
#include <linux/kthread.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/version.h>
#include <linux/nvme.h>
#include <linux/nvme-fc.h>
#include "fnic.h"
#include "fnic_trace.h"
#include <linux/nvme-fc.h>
#if FNIC_CUMH_IN_KMOD_H
#include <linux/kmod.h>
#elif FNIC_CUMH_IN_UMH_H
#include <linux/umh.h>
#endif

// #define DBG_ABTS 1


/* TBD may be */
fc_hdr_t nvfnic_lsreq_fchdr = {
        .r_ctl = 0x32,
            .type = 0x28, .f_ctl = FNIC_NVME_LS_REQ_FCTL, .rx_id=0xFFFF,
};

enum nvfnic_lsreq_state_e {
	FNIC_LSREQ_CMD_INIT = 0,
	FNIC_LSREQ_CMD_PENDING,
	FNIC_LSREQ_CMD_ABTS_PENDING,
	FNIC_LSREQ_CMD_COMPLETE,
	FNIC_LSREQ_ABTS_COMPLETE,
	FNIC_LSREQ_CMD_ABTS_STARTED,
};

#define FNIC_NVME_TPORT_REMOVE_WAIT (5 * 1000)
#define FNIC_NVME_TPORT_LIST_EMPTY_WAIT (FNIC_NVME_TPORT_REMOVE_WAIT * 2)
#define FNIC_NVME_LPORT_REMOVE_WAIT (2 * 60 * 1000)

extern spinlock_t fnic_list_lock;

int
nvfnic_add_lport(struct fnic *fnic);
struct fnic_io_req*
nvfnic_find_ioreq_by_tag(struct fnic *fnic, uint16_t tag);
static void
nvfnic_dma_unmap_sgl(struct fnic *fnic, struct fnic_io_req *io_req);
static int
fnic_transport_ready(fnic_iport_t *iport, fnic_tport_t *tport);
static uint16_t
nvfnic_alloc_fcpio_tag(fnic_iport_t *iport, struct fnic_io_req *io_req);
int
nvfnic_queuecommand(struct fnic_io_req *io_req,
	void (*done)(struct fnic_io_req *io_req));
static void
nvfnic_free_fcpio_tag(fnic_iport_t *iport, struct fnic_io_req *io_req);
void
nvfnic_fcpio_cmpl(struct fnic_io_req *io_req);
void
nvfnic_dump_nvcmd(struct fnic_io_req *io_req, uint8_t flags);
void fnic_flush_nvme_io_list(struct fnic *fnic);
static void
nvfnic_free_lsreq_oxid(fnic_iport_t *iport, uint16_t oxid);
extern struct workqueue_struct *fnic_event_queue;
void fnic_cleanup_tport_io(struct fnic *fnic, fnic_tport_t *tport);
extern const char *fnic_state_to_str(unsigned int state);
extern const char *fnic_ioreq_state_to_str(unsigned int state);
extern const char *fnic_fcpio_status_to_str(unsigned int status);
extern int free_wq_copy_descs(struct fnic *fnic, struct vnic_wq_copy *wq, int cq_index);
extern void __fnic_set_state_flags(struct fnic *fnic, unsigned long st_flags, unsigned long clearbits);
extern int fnic_fw_reset_handler(struct fnic *fnic);
extern int fnic_flogi_reg_handler(struct fnic *fnic, u32 fc_id);
extern int fnic_fcpio_fw_reset_cmpl_handler(struct fnic *fnic,  struct fcpio_fw_req *desc);
extern int fnic_fcpio_flogi_reg_cmpl_handler(struct fnic *fnic,  struct fcpio_fw_req *desc);
extern int fdls_send_lsreq_abts(fnic_iport_t *iport,fnic_tport_t *tport, unsigned int oxid);
static void nvfnic_lsreq_abort(struct nvme_fc_local_port *lport,
		struct nvme_fc_remote_port *rport, struct nvmefc_ls_req *lsreq);
static int  nvfnic_queue_abort_ioreq(struct fnic *fnic, int tag,
                                          u32 task_req, u8 *fc_lun,
                                          struct fnic_io_req *io_req);
void nvfnic_fcpio_abort(struct nvme_fc_local_port *lport, struct nvme_fc_remote_port *rport,
                       void *hw_queue_handle, struct nvmefc_fcp_req *fcp_req);


struct nvfnic_lsreq {
    struct list_head list;
    struct nvmefc_ls_req *lsreq;
    uint16_t oxid;
    struct timer_list lsreq_timer;
    struct fnic *fnic;
    fnic_tport_t *tport;
    int state;
    unsigned int flags;
};
extern unsigned int nvme_dev_loss_tmo;
extern unsigned int nvme_max_ios_to_process;

#define FNIC_LSREQ_FLAGS_NONE      0x0
#define FNIC_LSREQ_FLAGS_ABORTED   0x1
#define FNIC_LSREQ_FLAGS_DONE      0x2
#define FNIC_LSREQ_ABORT_COMPLETED 0x4
#define FNIC_STATUS_LSREQ_ABORTED 0x1 /* TBD */

#define FNIC_LSREQ_MIN_TMO_SECS (2)
#define FNIC_LSREQ_MAX_TMO_SECS (5)
#define FNIC_LSREQ_TMO_MSECS(tmo)    (((tmo >= FNIC_LSREQ_MIN_TMO_SECS) && \
					(tmo <= FNIC_LSREQ_MAX_TMO_SECS)) ? \
					(tmo * 1000) : (FNIC_LSREQ_MIN_TMO_SECS * 1000))

#define IS_ADMINIO(_io_req)     \
	(NVME_CMD_FLAGS(_io_req) & FNIC_NVME_ADMINIO_TIMER_PENDING)

/*
 * Unmap the data buffer and sense buffer for an io_req,
 * also unmap and free the device-private scatter/gather list.
 */
static void fnic_release_nvme_ioreq_buf(struct fnic *fnic,
				   struct fnic_io_req *io_req)
{
	nvfnic_dma_unmap_sgl(fnic, io_req);

	if (io_req->sgl_cnt)
		mempool_free(io_req->sgl_list_alloc,
			     fnic->io_sgl_pool[io_req->sgl_type]);
	fnic->iport.nvfnic_fcpio_tag[io_req->tag] = NULL;
}

int
fnic_get_nvmef_info(struct fnic *fnic, struct fnic_nvmef_info *info)
{
	fnic_iport_t *iport = &fnic->iport;
	int buf_size = info->buf_size;  
	int len = 0;
	fnic_tport_t *tport, *next;
	unsigned long flags;

	len += snprintf(info->info_buffer + len, buf_size - len,
		"lport wwpn 0x%llx wwnn 0x%llx fcid 0x%06x\n",
		iport->wwpn, iport->wwnn, iport->fcid);
               

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(tport, next, &iport->tport_list, links)   {
		len += snprintf(info->info_buffer + len, buf_size - len,
		"tport wwpn 0x%llx wwnn 0x%llx fcid 0x%06x\n",
		tport->wwpn, tport->wwnn, tport->fcid);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return len;
}

/*
 * fnic_queue_wq_copy_desc
 * Routine to enqueue a wq copy desc
 */
static inline int fnic_queue_wq_nvme_copy_desc(struct fnic *fnic,
					  struct vnic_wq_copy *wq,
					  struct fnic_io_req *io_req,
					  int sg_count)
{
	struct scatterlist *sg;
	fnic_tport_t *tport = io_req->tport;
	struct host_sg_desc *desc;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned int i;
	unsigned long intr_flags;
	int flags;
	u8 exch_flags;
    struct scatterlist *sgl;

	if (sg_count) {
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
                sgl = io_req->fcp_req->first_sgl;
		for_each_sg(sgl, sg, sg_count, i) {
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
	}

	/* Enqueue the descriptor in the Copy WQ */
	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0]) {
		free_wq_copy_descs(fnic, wq, 0);
	}

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
		fnic_printk(KERN_INFO, fnic,
			  "fnic_queue_wq_copy_desc failure - no descriptors\n");
		atomic64_inc(&misc_stats->io_cpwq_alloc_failures);
		return -EBUSY;
	}

	flags = 0;
	if (io_req->fcp_req->io_dir == NVMEFC_FCP_READ)
		flags = FCPIO_ICMND_RDDATA;
	else if (io_req->fcp_req->io_dir == NVMEFC_FCP_WRITE)
		flags = FCPIO_ICMND_WRDATA;

	exch_flags = 0;
#ifdef TODO
	if ((fnic->config.flags & VFCF_FCP_SEQ_LVL_ERR) &&
	    (rp->flags & FC_RP_FLAGS_RETRY))
		exch_flags |= FCPIO_ICMND_SRFLAG_RETRY;
#endif

	fnic_queue_wq_copy_desc_nvme_io(wq, io_req->tag,
		exch_flags, io_req->sgl_cnt, io_req->sgl_list_pa, flags,
		io_req->fcp_req->cmdaddr, io_req->fcp_req->cmdlen,
		io_req->fcp_req->payload_length,
		io_req->port_id, tport->max_payload_size, tport->r_a_tov, tport->e_d_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
	return 0;
}

static int
nvfnic_dma_map_sgl(struct fnic_io_req *io_req)
{
	/* TBD */
	return io_req->fcp_req->sg_cnt;
}

static void
nvfnic_dma_unmap_sgl(struct fnic *fnic, struct fnic_io_req *io_req)
{
	if (io_req->sgl_list_pa)
		pci_unmap_single(fnic->pdev, io_req->sgl_list_pa,
			sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt,
			PCI_DMA_TODEVICE);
}

void
nvfnic_dump_nvcmd(struct fnic_io_req *io_req, uint8_t flags)
{
	struct nvmefc_fcp_req  *fcp_req = io_req->fcp_req;
	struct nvme_fc_cmd_iu *cmd_iu = fcp_req->cmdaddr;
	
#if 0
	printk("**** Dumping nvcmd iu ****\n");
	printk("tag:%d cmd_seq:%d cmd_len %d flags %d payloadlen:%d sg_count:%d rsplen_expected:%d\n", 
		io_req->tag, u32[4], io_req->fcp_req->cmdlen, flags, 
		io_req->fcp_req->payload_length, io_req->sgl_cnt, io_req->fcp_req->rsplen);

	for (i = 0; i < 4; i = i + 4) {
		printk("%02x %02x %02x %02x\n",
			ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
	}
#endif
	printk("cmd_sn:%08x %llx\n", be32_to_cpu(cmd_iu->csn), le64_to_cpu(cmd_iu->sqe.rw.slba));
		
//	printk("cmdiu dump done***\n");
}

void
nvfnic_dump_nvcmd_long(struct fnic_io_req *io_req, uint8_t flags)
{
        struct nvmefc_fcp_req  *fcp_req = io_req->fcp_req;
        int i;
        uint8_t *ptr;

        printk("**** Dumping nvcmd iu ****\n");
        printk("cmd_len %d flags %d payloadlen:%d sg_count:%d rsplen_expected:%d\n",
                io_req->fcp_req->cmdlen, flags, io_req->fcp_req->payload_length, io_req->sgl_cnt, io_req->fcp_req->rsplen);

        ptr = fcp_req->cmdaddr;
        for (i = 0; i < fcp_req->cmdlen; i = i + 4) {
                printk("%02x %02x %02x %02x\n",
                        ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
        }

        printk("cmdiu dump done***\n");
}

#if FNIC_USE_SETUP_TIMER
void
nvfnic_adminIO_timeout(unsigned long arg)
{
	struct fnic_io_req *io_req = (struct fnic_io_req *)arg;
#else
void
nvfnic_adminIO_timeout(struct timer_list *t) {
        struct fnic_io_req *io_req = from_timer(io_req, t, adminIO_timer);
#endif

	fnic_iport_t *iport = io_req->iport;
	struct fnic *fnic = iport->fnic;
	fnic_tport_t *tport = io_req->tport;

	FNIC_NVME_DBG(KERN_DEBUG, fnic, "adminIO timeout fcpreq:%p tag %d\n",
	io_req->fcp_req, io_req->tag);

	nvfnic_fcpio_abort(iport->nv_lport, tport->nv_rport,
	NULL, io_req->fcp_req);
}

static int
nvfnic_fcpio_send(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, void *hw_queue_handle,
    struct nvmefc_fcp_req *fcp_req)
{
    fnic_iport_t *iport = lport->private;
    struct fnic_io_req *io_req;
    int ret;
    struct fnic *fnic = iport->fnic;
    unsigned long flags = 0;
    fnic_tport_t *tport = (fnic_tport_t *)rport->private;
    struct fnic_stats *fnic_stats = &fnic->fnic_stats;

    spin_lock_irqsave(&fnic->fnic_lock, flags);
    atomic64_inc(&fnic_stats->io_stats.io_reqs_rcvd);
    if (fnic_transport_ready(iport, tport)) {
	atomic64_inc(&fnic_stats->io_stats.io_rsps_sent);
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	FNIC_NVME_DBG(KERN_INFO, fnic, "Rejecting IO for tport %p\n", tport);
        return -ENODEV;
    }
    atomic_inc(&fnic->in_flight);

    io_req = (struct fnic_io_req *)fcp_req->private;
    //memset(io_req, 0, sizeof(struct fnic_io_req));
    io_req->iport = iport;
    io_req->tport = (fnic_tport_t *)rport->private;
    io_req->fcp_req = fcp_req;
    io_req->done = nvfnic_fcpio_cmpl;
    io_req->sgl_list_pa = 0;

    io_req->tag = nvfnic_alloc_fcpio_tag(iport, io_req);
    if (io_req->tag == 0xFFFF) {
	fnic_printk(KERN_ERR, fnic, "no free tag available. Failing IO\n");
	atomic64_inc(&fnic_stats->io_stats.alloc_failures);
        atomic_dec(&fnic->in_flight);
	atomic64_inc(&fnic_stats->io_stats.io_rsps_sent);
    	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return -EBUSY;
    }
    	
//    FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_fcpio_send fcpreq:%p, cmdnum:%d, outstanding:%d tag %d\n",
  //      fcp_req, fnic->nvme_fcpio_cmds, fnic->nvme_fcpio_pending, io_req->tag);

    ret = nvfnic_queuecommand(io_req, nvfnic_fcpio_cmpl);

    if (ret) {
	FNIC_NVME_DBG(KERN_ERR, fnic, "queuecommand failed %d\n", io_req->tag);
	nvfnic_free_fcpio_tag(iport, io_req);
    }	
    spin_unlock_irqrestore(&fnic->fnic_lock, flags);
    atomic_dec(&fnic->in_flight);
    return ret;
}


int
nvfnic_queuecommand(struct fnic_io_req *io_req,
    void (*done)(struct fnic_io_req *io_req))
{
        fnic_iport_t *iport = io_req->iport;
        struct fnic *fnic = iport->fnic;
        fnic_tport_t *tport = io_req->tport;
        struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        struct vnic_wq_copy *wq;
        int ret = 0;
        int sg_count = 0;
        unsigned long ptr;
	unsigned char *lba;
	u64 cmd_trace;
	struct nvme_fc_cmd_iu *cmdiu = io_req->fcp_req->cmdaddr;



        NVME_CMD_STATE(io_req) = FNIC_IOREQ_NOT_INITED;
        NVME_CMD_FLAGS(io_req) = FNIC_NO_FLAGS;
        /* Map the data buffer */
        sg_count = nvfnic_dma_map_sgl(io_req);
        if (sg_count < 0) {
                FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
                          io_req->tag, io_req, 0, io_req->fcp_req->io_dir,
                          sg_count, NVME_CMD_STATE(io_req));
                printk("sg count is less-than-zero\n");
                ret = -1;
                goto out;
        }

        /* Determine the type of scatter/gather list we need */
        io_req->sgl_cnt = sg_count;
        io_req->sgl_type = FNIC_SGL_CACHE_DFLT;
        if (sg_count > FNIC_DFLT_SG_DESC_CNT)
                io_req->sgl_type = FNIC_SGL_CACHE_MAX;

        if (sg_count) {
                io_req->sgl_list =
                        mempool_alloc(fnic->io_sgl_pool[io_req->sgl_type],
                                      GFP_ATOMIC);
                if (!io_req->sgl_list) {
                        atomic64_inc(&fnic_stats->io_stats.alloc_failures);
                        printk("unable to alloc SGLs\n");
                        ret = -ENOMEM;
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
        //nvfnic_dump_nvcmd(io_req);
        /*
         * we will acquire lock before setting to IO initialized
        */

        /* initialize rest of io_req */
        io_req->port_id = tport->fcid;
        io_req->start_time = jiffies;
        NVME_CMD_STATE(io_req) = FNIC_IOREQ_CMD_PENDING;
        NVME_CMD_FLAGS(io_req) |= FNIC_IO_INITIALIZED;
        io_req->done = done;

	if (((le16_to_cpu(io_req->fcp_req->sqid) == 0) && (cmdiu->sqe.rw.opcode != 0x0c)) ||
		(cmdiu->sqe.rw.opcode == 0x7f)) {
#if FNIC_USE_SETUP_TIMER
		setup_timer(&io_req->adminIO_timer, nvfnic_adminIO_timeout,
			(unsigned long)io_req);
#else
		timer_setup(&io_req->adminIO_timer, nvfnic_adminIO_timeout, (unsigned long)0);
#endif
		NVME_CMD_FLAGS(io_req) |= FNIC_NVME_ADMINIO_TIMER_PENDING;
       }

        /* create copy wq desc and enqueue it */
        wq = &fnic->wq_copy[0];
        ret = fnic_queue_wq_nvme_copy_desc(fnic, wq, io_req, sg_count);
        if (ret) {
                fnic_printk(KERN_ERR, fnic, "can't q frame\n");
                /*
                 * In case another thread cancelled the request,
                 * refetch the pointer under the lock.
                 */
		fnic_release_nvme_ioreq_buf(fnic, io_req);
                FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
                          io_req->tag, io_req->fcp_req, 0, 0, 0,
                          (((u64)NVME_CMD_FLAGS(io_req) << 32) | NVME_CMD_STATE(io_req)));
                return ret;
        } else {
		if (NVME_CMD_FLAGS(io_req) & FNIC_NVME_ADMINIO_TIMER_PENDING) {
			mod_timer(&io_req->adminIO_timer,
			round_jiffies(jiffies +
			msecs_to_jiffies(FNIC_NVME_ADMINIO_TIMEOUT)));
		}

                atomic64_inc(&fnic_stats->io_stats.active_ios);
                atomic64_inc(&fnic_stats->io_stats.num_ios);
                if (atomic64_read(&fnic_stats->io_stats.active_ios) >
                        atomic64_read(&fnic_stats->io_stats.max_active_ios))
                        atomic64_set(&fnic_stats->io_stats.max_active_ios ,
                             atomic64_read(&fnic_stats->io_stats.active_ios));
                /* REVISIT: Use per IO lock in the final code */
                NVME_CMD_FLAGS(io_req) |= FNIC_IO_ISSUED;
        }
out:
	lba = (char *)&cmdiu->sqe.rw.slba;
        cmd_trace = ((u64)cmdiu->sqe.rw.opcode << 56 | (u64)lba[4] << 40 |
                        (u64)lba[5] << 32 | (u64)lba[0] << 24 |
                        (u64)lba[1] << 16 | (u64)lba[2] << 8 |
                        lba[3]);

        FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
                  io_req->tag, 0, io_req,
                  sg_count, cmd_trace,
                  (((u64)NVME_CMD_FLAGS(io_req) >> 32) | NVME_CMD_STATE(io_req)));

        /* if only we issued IO will we have the io lock */

        return ret;
}

/* TBD: run this under a different thread? */
static void
nvfnic_usermode_connect(struct fnic *fnic, fnic_tport_t *tport)
{
#if FNIC_HAVE_NVME_AUTOCONNECT
    return;
#else
	fnic_iport_t *iport = &fnic->iport;
	int ret = 0;
	char *argv[] = { "/usr/bin/nvmef-connect", iport->str_wwpn, iport->str_wwnn,
		tport->str_wwpn, tport->str_wwnn, NULL };
	char *envp[] = {"HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };

	FNIC_NVME_DBG(KERN_INFO, fnic,
		"nvfnic_usermode_connect tport:%x\n", tport->fcid);

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);

	if (ret) {
		printk("fnic Error[%d]: Unable to run /nvme/connect automatically. Scheduling after 5 seconds\n",
		ret);
		schedule_delayed_work(&tport->tport_scan_work,
				msecs_to_jiffies(5000));
		return;
	}
	FNIC_NVME_DBG(KERN_INFO, fnic,
		"fnic: nvme-connect automatically executed tport:%x\n", tport->fcid);
#endif
}

void nvfnic_tport_scan_work(struct work_struct *wk)
{
	struct delayed_work *d_work = to_delayed_work(wk);	
	fnic_tport_t *tport = container_of(d_work, struct fnic_tport_s, tport_scan_work);	

	fnic_iport_t *iport = tport->iport;
	struct fnic *fnic = iport->fnic;


	FNIC_NVME_DBG(KERN_INFO, fnic, "reached nvfnic_scan_work %x\n", tport->fcid);

	if ((tport->state == fdls_tgt_state_offlining) ||
		(tport->state == fdls_tgt_state_offline)) {
		FNIC_NVME_DBG(KERN_INFO, fnic, "tport scan_work %x called after deletion\n", tport->fcid);
		return;
	}

	nvfnic_usermode_connect(fnic, tport);
}


#define SCSI_NO_TAG 0 /* TBD */


void
nvfnic_dump_nvrsp(u8 hdr_status, u32 id, struct fcpio_nvme_cmpl *nvme_cmpl)
{
	int i;
	uint8_t *ptr;
	uint32_t *u32;


	ptr = nvme_cmpl->resp_bytes;
	u32 = (uint32_t *)ptr;

	printk("**** Dumping the nvrsp *****\n");

	printk("hdr_status:%d, tag:%d, recvd_rsp_size:%d rsp_seq_num:%d\n",
		hdr_status, id, nvme_cmpl->resp_size, u32[1]);

	for (i = 0; i < 4; i = i + 4) {
		printk("%02x %02x %02x %02x\n",
			ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
	}
	printk("nvrsp dump done ****\n");
}

void
nvfnic_dump_nvrsp_long(u8 hdr_status, u32 id, struct fcpio_nvme_cmpl *nvme_cmpl)
{
        int i;
        uint8_t *ptr;

        printk("**** Dumping the nvrsp *****\n");

        printk("hdr_status:%d, id:%d, recvd_rsp_size:%d\n",
                hdr_status, id, nvme_cmpl->resp_size);
        ptr = nvme_cmpl->resp_bytes;

        for (i = 0; i < nvme_cmpl->resp_size; i = i + 4) {
                printk("%02x %02x %02x %02x\n",
                        ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
        }
        printk("nvrsp dump done ****\n");
}

/*
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle nvme io completions
 */
void fnic_fcpio_nvme_fast_cmpl_handler(struct fnic *fnic,
                                         struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        unsigned long start_time;
        unsigned long io_duration_time;
#ifdef DBG_ABTS
	static int count = 0;
#endif
	u64 cmd_trace;
	char *lba;
	struct nvme_fc_cmd_iu *cmdiu;
	fnic_tport_t *tport;

        /* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

	if (id >= fnic->fnic_max_tag_id) {
		fnic_printk(KERN_ERR, fnic, "Tag out of range tag %x hdr status = %s\n",
			id, fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_ioreq_by_tag(fnic, id);

	WARN_ON_ONCE(!io_req);
	
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		fnic_printk(KERN_ERR, fnic, "fast icmnd_cmpl sc is null - "
			"hdr status = %s tag = 0x%x desc = 0x%p\n",
			fnic_fcpio_status_to_str(hdr_status), id, desc);
		fnic_printk(KERN_ERR, fnic, "type:%x, status:%x, rsvd:%x, tag:%x\n",
			desc->hdr.type, desc->hdr.status, desc->hdr._resvd, id); 
        	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	cmdiu = io_req->fcp_req->cmdaddr;
        if (NVME_CMD_STATE(io_req) != FNIC_IOREQ_CMD_PENDING || io_req->tag != id) {
                fnic_printk(KERN_INFO, fnic, 
			"IO already being freed by abort. ignore it %d, tag:%d, st:%x cmd_sn:0x%08x\n", 
			id, io_req->tag, NVME_CMD_STATE(io_req), be32_to_cpu(cmdiu->csn));
                spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
                return;
        }
	tport = io_req->tport;



        start_time = io_req->start_time;

        /* firmware completed the io */
        io_req->io_completed = 1;
        if (NVME_CMD_STATE(io_req) == FNIC_IOREQ_ABTS_PENDING) {

                /*
                 * set the FNIC_IO_DONE so that this doesn't get
                 * flagged as 'out of order' if it was not aborted
                 */
                NVME_CMD_FLAGS(io_req) |= FNIC_IO_DONE;
                NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABTS_PENDING;
                if(FCPIO_ABORTED == hdr_status)
                        NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABORTED;
                spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

                FNIC_NVME_DBG(KERN_INFO, fnic,
                        "icmnd_cmpl abts pending "
                          "hdr status(%d) = %s tag = 0x%x io_req = 0x%p",
                          hdr_status, fnic_fcpio_status_to_str(hdr_status),
                          id, io_req);
                return;
        }

        /* Mark the IO as complete */
        NVME_CMD_STATE(io_req) = FNIC_IOREQ_CMD_COMPLETE;
        switch (hdr_status) {
        case FCPIO_SUCCESS:

                io_req->fcp_req->status = 0;

                io_req->fcp_req->transferred_length = io_req->fcp_req->payload_length;
                io_req->fcp_req->rcv_rsplen = 12;
                break;
        default:
                fnic_printk(KERN_ERR, fnic, "HDR success NOOOOT 0\n");
                io_req->fcp_req->status = NVME_SC_INTERNAL;
                break;
        }

        if (hdr_status != FCPIO_SUCCESS) {
                atomic64_inc(&fnic_stats->io_stats.io_failures);
                fnic_printk(KERN_ERR, fnic, "hdr status = %s\n",
                             fnic_fcpio_status_to_str(hdr_status));
        }

        NVME_CMD_FLAGS(io_req) |= FNIC_IO_DONE;

	cmdiu = io_req->fcp_req->cmdaddr;
        lba = (char *)&cmdiu->sqe.rw.slba;
        cmd_trace = ((u64)hdr_status << 56) |
                  (u64)cmdiu->sqe.rw.opcode << 32 |
                  (u64)lba[0] << 24 | (u64)lba[1] << 16 |
                  (u64)lba[2] << 8 | lba[3];

        FNIC_TRACE(fnic_fcpio_nvme_fast_cmpl_handler,
                  fnic->fnic_num, id, io_req,
                  jiffies_to_msecs(jiffies - start_time),
                  desc, cmd_trace,
                  (((u64)NVME_CMD_FLAGS(io_req) << 32) | NVME_CMD_STATE(io_req)));

	if (cmdiu->sqe.rw.opcode == nvme_cmd_read) {
		atomic64_inc(&fnic_stats->nvme_stats.nvme_input_requests);
		fnic->fcp_input_bytes += io_req->fcp_req->transferred_length;
	} else if (cmdiu->sqe.rw.opcode == nvme_cmd_write) {
		atomic64_inc(&fnic_stats->nvme_stats.nvme_output_requests);
		fnic->fcp_output_bytes += io_req->fcp_req->transferred_length;
	} else
		atomic64_inc(&fnic_stats->nvme_stats.nvme_control_requests);

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
	fnic_release_nvme_ioreq_buf(fnic, io_req);
        if (io_req->done)
                io_req->done(io_req);
        spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}


/*
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle nvme io completions
 */
void fnic_fcpio_ersp_cmpl_handler(struct fnic *fnic,
					 struct fcpio_fw_req *desc, int sw_flag)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	struct fcpio_nvme_cmpl *nvme_cmpl;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long start_time;
	uint32_t rsplen;
	struct nvme_fc_ersp_iu *ersp, *nrsp;
	char *byte;
	struct nvme_fc_cmd_iu *cmdiu;
	struct nvme_command *sqe;
	struct nvme_completion *cqe;
	u64 cmd_trace;
	unsigned int io_duration_time;
	fnic_tport_t *tport;
	char *lba;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);
	nvme_cmpl = &desc->u.nvme_cmpl;
	ersp = (struct nvme_fc_ersp_iu *)nvme_cmpl->resp_bytes;

	if (id >= fnic->fnic_max_tag_id) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "Tag out of range tag %x hdr status = %s\n",
			     id, fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_ioreq_by_tag(fnic, id );
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		fnic_printk(KERN_ERR, fnic, "ersp icmnd_cmpl sc is null - "
			  "hdr status = %s tag = 0x%x desc = 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, desc);
		fnic_printk(KERN_ERR, fnic, "type:%x, status:%x, rsvd:%x, tag:%x\n",
			desc->hdr.type, desc->hdr.status, desc->hdr._resvd, id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	tport = io_req->tport;

	cmdiu = io_req->fcp_req->cmdaddr;

	if (NVME_CMD_STATE(io_req) != FNIC_IOREQ_CMD_PENDING || io_req->tag != id) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "IO already being freed by abort. ignore it %d\n", id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	nrsp = (struct nvme_fc_ersp_iu *)io_req->fcp_req->rspaddr;
	cmdiu = (struct nvme_fc_cmd_iu *)io_req->fcp_req->cmdaddr;
	sqe = &cmdiu->sqe;
	cqe = &nrsp->cqe;

	start_time = io_req->start_time;

	/* firmware completed the io */
	io_req->io_completed = 1;

	/*
	 *  if SCSI-ML has already issued abort on this command,
	 *  set completion of the IO. The abts path will clean it up
	 */
	if (NVME_CMD_STATE(io_req) == FNIC_IOREQ_ABTS_PENDING) {

		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */ 
		NVME_CMD_FLAGS(io_req) |= FNIC_IO_DONE;
		NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABTS_PENDING;
		if(FCPIO_ABORTED == hdr_status)
			NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABORTED;
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

		FNIC_NVME_DBG(KERN_INFO, fnic,
			"icmnd_cmpl abts pending "
			  "hdr status = %s tag = 0x%x io_req = 0x%p", 
			  fnic_fcpio_status_to_str(hdr_status),
			  id, io_req);
		return;
	}

	/* Mark the IO as complete */
	NVME_CMD_STATE(io_req) = FNIC_IOREQ_CMD_COMPLETE;

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		io_req->fcp_req->status = 0;
		if (!sw_flag) {
			io_req->fcp_req->transferred_length = io_req->fcp_req->payload_length;
 			rsplen = 32;
			nrsp->iu_len = cpu_to_be16(sizeof(struct nvme_fc_ersp_iu)/4);
			nrsp->xfrd_len = cpu_to_be32(io_req->fcp_req->payload_length);

#if FNIC_HAVE_ERSP_RESULT
			nrsp->ersp_result = 0;
#else
			nrsp->status_code = 0;
#endif
			cqe->command_id = sqe->common.command_id;
			cqe->status = 0;
			cqe->result.u64 = 0;
		} else {
			io_req->fcp_req->transferred_length = be32_to_cpu(ersp->xfrd_len);
			rsplen = be16_to_cpu(ersp->iu_len * 4);
			memcpy(io_req->fcp_req->rspaddr, ersp, rsplen);
			byte = (char *)ersp;
#if FNIC_HAVE_ERSP_RESULT
			if (ersp->ersp_result == NVME_STAT_ERROR)
#else
			if (ersp->status_code == NVME_STAT_ERROR)
#endif
				atomic64_inc(&fnic_stats->misc_stats.check_condition);

#if FNIC_HAVE_ERSP_RESULT
			if (ersp->ersp_result == NVME_STAT_TASK_SET_FULL)
#else
			if (ersp->status_code == NVME_STAT_TASK_SET_FULL)
#endif
				atomic64_inc(&fnic_stats->misc_stats.queue_fulls);
		}
		atomic64_inc(&fnic_stats->nvme_stats.nvme_ersps);
		io_req->fcp_req->rcv_rsplen = rsplen;

		break;
	default:
		printk("HDR success NOOOOT 0\n");
		io_req->fcp_req->status = NVME_SC_INTERNAL;
		break;
	}

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
		fnic_printk(KERN_ERR, fnic, "hdr status = %s tag %d\n",
			     fnic_fcpio_status_to_str(hdr_status), id);
	}

	NVME_CMD_FLAGS(io_req) |= FNIC_IO_DONE;

        lba = (char *)&cmdiu->sqe.rw.slba;
	cmd_trace = ((u64)hdr_status << 56) |
#if FNIC_HAVE_ERSP_RESULT
		  (u64)ersp->ersp_result << 48 |
#else
		  (u64)ersp->status_code << 48 |
#endif
		  (u64)cmdiu->sqe.rw.opcode << 32 |
		  (u64)lba[0] << 24 | (u64)lba[1] << 16 |
		  (u64)lba[2] << 8 | lba[3];

	FNIC_TRACE(fnic_fcpio_ersp_cmpl_handler,
		  fnic->fnic_num, id, io_req,
		  ((u64)nvme_cmpl->resvd[1] << 56 |
		  (u64)nvme_cmpl->resvd[0] << 48 |
		  jiffies_to_msecs(jiffies - start_time)),
		  desc, cmd_trace,
		  (((u64)NVME_CMD_FLAGS(io_req) << 32) | NVME_CMD_STATE(io_req)));

	if (cmdiu->sqe.rw.opcode == nvme_cmd_read) {
		atomic64_inc(&fnic_stats->nvme_stats.nvme_input_requests);
		fnic->fcp_input_bytes += io_req->fcp_req->transferred_length;
	} else if (cmdiu->sqe.rw.opcode == nvme_cmd_write) {
		atomic64_inc(&fnic_stats->nvme_stats.nvme_output_requests);
		fnic->fcp_output_bytes += io_req->fcp_req->transferred_length;
	} else
		atomic64_inc(&fnic_stats->nvme_stats.nvme_control_requests);

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
	fnic_release_nvme_ioreq_buf(fnic, io_req);
	/* Call NVME completion function to complete the IO */
	if (io_req->done)
		io_req->done(io_req);
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}


/* fnic_fcpio_itmf_cmpl_handler
 * Routine to handle itmf completions
 */
void fnic_fcpio_nvme_itmf_cmpl_handler(struct fnic *fnic,
					struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	struct fnic_io_req *io_req;
	struct nvme_fc_cmd_iu *cmd_iu;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long start_time;
	fnic_tport_t *tport;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

	if ((id & FNIC_TAG_MASK) >= fnic->fnic_max_tag_id) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "Tag out of range tag %x hdr status = %s\n",
		id, fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_ioreq_by_tag(fnic, ((int)id) & FNIC_TAG_MASK);
	
        WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABT_TERM_REQ_NULL;
		FNIC_NVME_DBG(KERN_ERR,fnic, "itmf icmnd_cmpl sc is null - "
			"hdr status = %s tag = 0x%x desc = 0x%p\n",
		fnic_fcpio_status_to_str(hdr_status), id, desc);
		FNIC_NVME_DBG(KERN_ERR,fnic, "type:%x, status:%x, rsvd:%x, tag:%x\n",
			desc->hdr.type, desc->hdr.status, desc->hdr._resvd, id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	cmd_iu = io_req->fcp_req->cmdaddr;
	FNIC_NVME_DBG(KERN_INFO, fnic, "recvd itmf_cmpl_handler tag %x hdr_sts:%d, 0x%08x\n",
		(unsigned int)(id & FNIC_TAG_MASK), hdr_status, be32_to_cpu(cmd_iu->csn));

	tport = io_req->tport;

	start_time = io_req->start_time;

	/* Completion of abort cmd */
	switch (hdr_status) {
	case FCPIO_SUCCESS:
		FNIC_NVME_DBG(KERN_DEBUG,fnic, "abort success recd. id %x\n", id);
		break;
	case FCPIO_TIMEOUT:
		FNIC_NVME_DBG(KERN_DEBUG, fnic, "abort timeout recd. id %x\n", id);
		if (NVME_CMD_FLAGS(io_req) & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_fw_timeouts);
		else
			atomic64_inc(
				&term_stats->terminate_fw_timeouts);
		break;
	case FCPIO_ITMF_REJECTED:
		FNIC_NVME_DBG(KERN_DEBUG, fnic, "abort reject recd. id %x\n", id);
		break;

	case FCPIO_IO_NOT_FOUND:
		FNIC_NVME_DBG(KERN_DEBUG, fnic, "abort io not found recd. id %x\n", id);
		if (NVME_CMD_FLAGS(io_req) & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_io_not_found);
		else
			atomic64_inc(
				&term_stats->terminate_io_not_found);
		break;
	default:
		FNIC_NVME_DBG(KERN_DEBUG, fnic, "abort unknown recd. id %x\n", id);
		if (NVME_CMD_FLAGS(io_req) & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_failures);
		else
			atomic64_inc(
				&term_stats->terminate_failures);
		break;
	}

	if (NVME_CMD_STATE(io_req) != FNIC_IOREQ_ABTS_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "abort late cmpl recd. id %x\n", id);
		/* This is a late completion. Ignore it */
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	NVME_CMD_ABTS_STATUS(io_req) = hdr_status;

	/* If the status is IO not found consider it as success */
	/* NVME sends abort even if rport is down in which case
	  we will get FCPIO_TIMEOUT. Consider this as success */
	if ((hdr_status == FCPIO_IO_NOT_FOUND) ||
	    (hdr_status == FCPIO_TIMEOUT) ||
	    (hdr_status == FCPIO_ITMF_REJECTED))
		NVME_CMD_ABTS_STATUS(io_req) = FCPIO_SUCCESS;

	NVME_CMD_FLAGS(io_req) |= FNIC_IO_ABT_TERM_DONE;

	if(!(NVME_CMD_FLAGS(io_req) & (FNIC_IO_ABORTED | FNIC_IO_DONE)))
		atomic64_inc(&misc_stats->no_icmnd_itmf_cmpls);


	if (NVME_CMD_ABTS_STATUS(io_req) == FCPIO_SUCCESS) {
		io_req->fcp_req->transferred_length = 0;
		io_req->fcp_req->rcv_rsplen = 0;
		io_req->fcp_req->status = NVME_SC_ABORT_REQ;
		atomic64_dec(&fnic_stats->io_stats.active_ios);
		if (atomic64_read(&fnic->io_cmpl_skip))
			atomic64_dec(&fnic->io_cmpl_skip);
		else
			atomic64_inc(&fnic_stats->io_stats.io_completions);

		fnic_release_nvme_ioreq_buf(fnic, io_req);
		if (io_req->done)
			io_req->done(io_req);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

/* This function is a catch-all that frees any I/Os after target has
been freed by nvme core. At this point no I/Os should exist
*/
void
fnic_cleanup_tport_io(struct fnic *fnic, fnic_tport_t *tport)
{
        int i;
        struct fnic_io_req *io_req;
	unsigned long flags;
	struct nvfnic_lsreq *nvfnic_lsreq, *next;
	struct nvmefc_ls_req *lsreq;
	enum fnic_ioreq_state old_ioreq_state;
	
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(nvfnic_lsreq, next, &(tport->lsreq_list), list) {
		lsreq = nvfnic_lsreq->lsreq;
		if (!lsreq || (lsreq->private == NULL)) {
			printk(KERN_INFO "fnic_cleanup_tport_io lsreq NULL\n");
			continue;
		}
		list_del(&nvfnic_lsreq->list);
		lsreq->private = NULL;
		nvfnic_free_lsreq_oxid(&fnic->iport, nvfnic_lsreq->oxid);
		nvfnic_lsreq->state = FNIC_LSREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		del_timer_sync(&nvfnic_lsreq->lsreq_timer);
		lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
	}

	// For link-down IOs are freed by firmware reset completion
	if (fdls_get_state(&fnic->iport.fabric) == FDLS_STATE_LINKDOWN) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	for (i = 1; i < fnic->fnic_max_tag_id; i++) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		io_req = nvfnic_find_ioreq_by_tag(fnic, i);
	
		if (!io_req || io_req->tport != tport) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			continue;
		}


		if ((NVME_CMD_STATE(io_req) == FNIC_IOREQ_ABTS_PENDING) ||
		(NVME_CMD_STATE(io_req) == FNIC_DEV_RST_TERM_ISSUED)) {
			FNIC_NVME_DBG(KERN_INFO, fnic, "abort already pending %x\n", io_req->tag);
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			continue;
		}

		if (NVME_CMD_FLAGS(io_req) & FNIC_NVME_ADMINIO_TIMER_PENDING) {
			del_timer_sync(&io_req->adminIO_timer);
			NVME_CMD_FLAGS(io_req) &= ~FNIC_NVME_ADMINIO_TIMER_PENDING;
		}

		FNIC_NVME_DBG(KERN_ERR, fnic, 
			"NVME IO to be cleaned up after unregister timeout terminating tag:%d(0x%x)\n",
			io_req->tag, io_req->tag);

		old_ioreq_state = NVME_CMD_STATE(io_req);
		NVME_CMD_STATE(io_req) = FNIC_IOREQ_ABTS_PENDING;
		NVME_CMD_ABTS_STATUS(io_req) = FCPIO_INVALID_CODE;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		if (nvfnic_queue_abort_ioreq(fnic, io_req->tag, FCPIO_ITMF_ABT_TASK_TERM,
                       	NULL, io_req)) {
			fnic_printk(KERN_ERR, fnic,
			    "fnic_queue_abort_io_req failed\n");
			spin_lock_irqsave(&fnic->fnic_lock, flags);
			NVME_CMD_STATE(io_req) = old_ioreq_state;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		}
	}

}


void
fnic_terminate_tport_lsreqs(struct fnic *fnic, fnic_tport_t *tport)
{
	struct nvmefc_ls_req *lsreq;
	struct nvfnic_lsreq *nvfnic_lsreq, *next;
	int count = 0;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(nvfnic_lsreq, next,
		&(tport->lsreq_list), list) {

		lsreq = nvfnic_lsreq->lsreq;
		if (!lsreq || (lsreq->private == NULL)) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"fnic_cleanup_tport_io lsreq NULL\n");
			continue;
		}
		list_del(&nvfnic_lsreq->list);
		lsreq->private = NULL;
		nvfnic_free_lsreq_oxid(&fnic->iport, nvfnic_lsreq->oxid);
		nvfnic_lsreq->state = FNIC_LSREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		del_timer_sync(&nvfnic_lsreq->lsreq_timer);
		lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);
		count++;
		spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	
	FNIC_NVME_DBG(KERN_INFO, fnic,
		"fnic_terminate_tport_lsreqs tport[%x]: freed lsreq:%d\n",
		tport->fcid, count);
}

void
fnic_terminate_tport_admin_ios(struct fnic *fnic, fnic_tport_t *tport)
{       
	int i;
	fnic_iport_t *iport = &fnic->iport;
	struct fnic_io_req *io_req;
	int total_io= 0, admin_io = 0;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	for (i = 1; i < fnic->fnic_max_tag_id; i++) {
		io_req = nvfnic_find_ioreq_by_tag(fnic, i);
		if (!io_req || io_req->tport != tport) {
			continue;
		}
		total_io++;
		if (IS_ADMINIO(io_req)) {
			admin_io++;
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			FNIC_NVME_DBG(KERN_INFO, fnic, 
				"Terminate Admin IO:tag %d(tport fcid %x)\n",
				io_req->tag, io_req->tport->fcid);
			nvfnic_fcpio_abort(iport->nv_lport,
				tport->nv_rport, NULL, io_req->fcp_req);
			spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		"fnic_terminate_tport_adminio tport[%x]: freed adminios:%d, total_ios:%d\n",
		tport->fcid, admin_io, total_io);
}

void fnic_cleanup_all_nvme_io(struct fnic *fnic)
{
        int i;
        struct fnic_io_req *io_req;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
        for (i = 1; i < fnic->fnic_max_tag_id; i++) {
                io_req = nvfnic_find_ioreq_by_tag(fnic, i);

                if (!io_req) {
                        continue;
                }
                FNIC_NVME_DBG(KERN_ERR, fnic, 
			"Cleanup IO:tag %d(tport fcid %x)\n",
			io_req->tag, io_req->tport->fcid);
                NVME_CMD_STATE(io_req) = FNIC_DEV_RST_TERM_ISSUED;
                io_req->fcp_req->status = NVME_SC_INTERNAL;
                io_req->fcp_req->transferred_length = 0;
                io_req->fcp_req->rcv_rsplen = 0;
                fnic_release_nvme_ioreq_buf(fnic, io_req);
		nvfnic_fcpio_cmpl(io_req);
        }
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

static void fnic_nvme_zero_devloss_tports(struct fnic *fnic)
{
	fnic_tport_t *tport, *next;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(tport, next, &fnic->iport.tport_list, links)   {
		if (tport->flags & FNIC_FDLS_NVME_REGISTERED) {
        		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			nvme_fc_set_remoteport_devloss(tport->nv_rport, 0);
			spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
		}
        }
        spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

/* 
 * FC_NVME specific unload
 * TBD: Instead of fw reset, follow the tport_delete procedure? 
 */
void
fnic_nvme_unload(struct fnic *fnic)
{
	int ret = 0;
	fnic_iport_t *iport = &fnic->iport;
	unsigned long flags;
	unsigned int time_wait =  FNIC_NVME_LPORT_REMOVE_WAIT;
	unsigned int time_remain;
	DECLARE_COMPLETION_ONSTACK(nvme_lport_unreg_done);

	/* Mark iport state as INIT, so that no IOs can be issued from this point */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->in_remove = 1;
	fnic->iport.state = FNIC_IPORT_STATE_LINK_WAIT;
	fnic->nvme_lport_unreg_done = &nvme_lport_unreg_done;

	/*
	 * If fnic is already processing link-down or fnic is held 
	 * in disabled state following a reboot we dont need to issue
	 * firmware reset and unregister remote ports as it is already
	 * done as part of link down handling.
	*/ 
	if (fdls_get_state(&iport->fabric) == FDLS_STATE_LINKDOWN) {
        	while (fnic->reset_in_progress == IN_PROGRESS) {
        		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
                	wait_for_completion_timeout(&fnic->reset_completion_wait,
                        	msecs_to_jiffies(5000));
			spin_lock_irqsave(&fnic->fnic_lock, flags);
                        fnic_log_info(fnic->fnic_num,
                        "rmmod thread waiting for reset completion %p\n", fnic);
                }
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	} else if (fdls_get_state(&iport->fabric) != FDLS_STATE_INIT) {

		fdls_set_state((&fnic->iport.fabric), FDLS_STATE_INIT);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		/*
	 	* Mark state so that the workqueue thread stops forwarding
	 	* received frames and link events to the local port. ISR and
	 	* other threads that can queue work items will also stop
	 	* creating work items on the fnic workqueue
	 	*/
		fnic_nvme_zero_devloss_tports(fnic);
		nvfnic_delete_lport(&fnic->iport);
		fnic_scsi_fcpio_reset(fnic); 
	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}

	if (iport->flags & FNIC_LPORT_NVME_REGISTERED) {
		ret = nvme_fc_unregister_localport(fnic->iport.nv_lport);

		if (ret) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"Unregister nvme localport failed:%d\n", ret);
			return;
		}

		time_remain = wait_for_completion_timeout(fnic->nvme_lport_unreg_done,
			msecs_to_jiffies(time_wait));
		if (!time_remain) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"Timed out waiting for local port removed\n");
			BUG_ON(1);
		}
	}


	fnic_flush_nvme_io_list(fnic);
	if (fnic->io_tag_pool) {
		kfree(fnic->io_tag_pool);
		fnic->io_tag_pool = NULL;
	}
	
	FNIC_NVME_DBG(KERN_INFO, fnic,
		"Waiting for all remoteport to deleted by nvmecore\n");
	/* Block the unload in case tports were not unregistered properly */
	while (!list_empty(&fnic->iport.tport_list_pending_del)) {
		msleep(5000);
	}
	FNIC_NVME_DBG(KERN_INFO, fnic,
		"All remoteport are deleted by nvmecore\n");

	ret = kthread_stop(fnic->kthread);
	if (ret == -EINTR) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			"WARNING kthread could not stopped during unload\n");	
	}
	 
}

static struct nvfnic_lsreq*
nvfnic_find_lsreq(fnic_tport_t *tport, uint16_t oxid)
{
    struct nvfnic_lsreq *nvfnic_lsreq, *next;

    list_for_each_entry_safe(nvfnic_lsreq, next, &(tport->lsreq_list), list) {
        if (nvfnic_lsreq->oxid == oxid)
            return nvfnic_lsreq;
    }
    return NULL;

}

struct fnic_io_req*
nvfnic_find_ioreq_by_tag(struct fnic *fnic, uint16_t tag)
{
    fnic_iport_t *iport = &fnic->iport;
    if (tag < 1 || tag >= NVFNIC_FCPIO_TAG_POOL_SZ)
	return NULL;

    return iport->nvfnic_fcpio_tag[tag];    	
}

static void
nvfnic_init_lsreq_oxid_pool(fnic_iport_t *iport)
{
        memset(iport->lsreq_oxid_pool, 0, NVFNIC_LSREQ_OXID_POOL_SZ);
}

static uint16_t
nvfnic_alloc_lsreq_oxid(fnic_iport_t *iport)
{
        int i;

        for (i = 0; i < NVFNIC_LSREQ_OXID_POOL_SZ; i++) {
                if (iport->lsreq_oxid_pool[i] == 0) {
                        iport->lsreq_oxid_pool[i] = 1;
                        return (i + NVFNIC_LSREQ_OXID_BASE);
                }
        }
        return 0xFFFF;
}

static void
nvfnic_free_lsreq_oxid(fnic_iport_t *iport, uint16_t oxid)
{
	struct fnic *fnic = iport->fnic;
        if (iport->lsreq_oxid_pool[oxid - NVFNIC_LSREQ_OXID_BASE] != 1) {
                FNIC_NVME_DBG(KERN_ERR, fnic, "Freeing unused OXID: 0x%x", oxid);
        }
        iport->lsreq_oxid_pool[oxid - NVFNIC_LSREQ_OXID_BASE] = 0;
}

/* TBD */
void
nvfnic_init_fcpio_tag_pool(fnic_iport_t *iport)
{
	struct fnic *fnic = iport->fnic;
	int tag = 0;
	struct fnic_tag_t *tag_data;

	memset(iport->nvfnic_fcpio_tag, 0, sizeof(void *) * NVFNIC_FCPIO_TAG_POOL_SZ);


	/* Initialize free list */
	INIT_LIST_HEAD(&fnic->io_tag_free);

	tag_data = &fnic->io_tag_pool[0];
	for (tag = 1; tag < NVFNIC_FCPIO_TAG_POOL_SZ; tag++, tag_data++) {
		tag_data->tag_id = tag;
		list_add_tail(&tag_data->free_list, &fnic->io_tag_free);
	}
}

static uint16_t
nvfnic_alloc_fcpio_tag(fnic_iport_t *iport, struct fnic_io_req *io_req)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_tag_t *free_tag;

	free_tag = list_first_entry_or_null(&fnic->io_tag_free,
		struct fnic_tag_t, free_list);
 
	if (free_tag) {
		list_del(&free_tag->free_list);
		iport->nvfnic_fcpio_tag[free_tag->tag_id] = io_req;
		io_req->tag_data = free_tag;
		return free_tag->tag_id;
	}
	return 0xFFFF;
}
static void
nvfnic_free_fcpio_tag(fnic_iport_t *iport, struct fnic_io_req *io_req)
{
	struct fnic *fnic = iport->fnic;
	io_req->tag = -1;
	list_add_tail(&io_req->tag_data->free_list, &fnic->io_tag_free);
}

/* IO completion response */
void
nvfnic_fcpio_cmpl(struct fnic_io_req *io_req)
{
	struct fnic *fnic = io_req->iport->fnic;
	struct nvmefc_fcp_req *fcp_req = io_req->fcp_req;
	fnic_io_event_t *io_evt = &io_req->io_evt;
    	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	memset(io_evt, 0, sizeof(fnic_io_event_t));	
	io_evt->arg1 = (void *)fcp_req;
	nvfnic_free_fcpio_tag(io_req->iport, io_req);
	atomic64_inc(&fnic_stats->io_stats.ios_queued_for_rsp);
	list_add_tail(&io_evt->links, &fnic->nvme_io_event_list);
	fnic->rsp_cnt++;
	wake_up(&fnic->rsp_wait);
	atomic64_inc(&fnic_stats->io_stats.num_ios_in_waitq);
	io_req->waitq_start_time = jiffies;
}

void
nvfnic_ls_abts_recv(fnic_iport_t *iport, fc_hdr_t *fchdr)
{
    uint32_t tport_fcid;
    fnic_tport_t *tport;
    struct nvfnic_lsreq *nvfnic_lsreq;
    struct nvmefc_ls_req *lsreq;
    uint16_t oxid = FNIC_GET_OX_ID(fchdr);	
    struct fnic *fnic = iport->fnic;
    struct fnic_stats *fnic_stats = &fnic->fnic_stats;

    /* TBD Validate and check header */
    tport_fcid = ntoh24(fchdr->sid);

    tport = fnic_find_tport_by_fcid(iport, tport_fcid);
    if (tport == NULL)
    {
        FNIC_NVME_DBG(KERN_ERR, fnic, "tport is NULL is ls_resp lookup\n");
	return;
    }

    nvfnic_lsreq = nvfnic_find_lsreq(tport, oxid);
    if (nvfnic_lsreq == NULL) {
        FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq is NULL is ls_resp lookup\n");
	return;
    }

    lsreq = nvfnic_lsreq->lsreq;

    if ((lsreq == NULL) || (lsreq->private == NULL)) {
        FNIC_NVME_DBG(KERN_INFO, fnic, "lsreq NULL when abort received\n");
        return;
    }

    atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_abort_responses);	
    nvfnic_lsreq->state = FNIC_LSREQ_ABTS_COMPLETE;

    FNIC_NVME_DBG(KERN_DEBUG, fnic, "LS abort rspnum:%lld\n",
        (u64)atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));

    list_del(&nvfnic_lsreq->list);
    nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
    lsreq->private = NULL;
    MY_SPIN_UNLOCK_IRQRESTORE(&fnic->fnic_lock, fnic->lock_flags);
    del_timer_sync(&nvfnic_lsreq->lsreq_timer);
    lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);
    MY_SPIN_LOCK_IRQ_SAVE(&fnic->fnic_lock, fnic->lock_flags);	
}


/*
 * ls_req completion from transport
 * For now, assume queuing in WQ means send completed
 * Also, sending and completion run in a same context 
 *
 */
void
nvfnic_lsrsp_recv(fnic_iport_t *iport, fc_hdr_t *fchdr,
    int len)
{
    uint32_t tport_fcid;
    fnic_tport_t *tport;
    struct nvfnic_lsreq *nvfnic_lsreq;
    struct nvmefc_ls_req *lsreq;
    uint16_t oxid;
    struct fnic *fnic = iport->fnic;
    unsigned long flags;
    struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	
    /* TBD Validate and check header */
    tport_fcid = ntoh24(fchdr->sid);

    spin_lock_irqsave(&fnic->fnic_lock, flags);
    tport = fnic_find_tport_by_fcid(iport, tport_fcid);
    if (!tport) {
	FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsrsp_recv NULL tport\n");
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
    }

    oxid = ntohs(fchdr->ox_id);
    nvfnic_lsreq = nvfnic_find_lsreq(tport, oxid);
    if (!nvfnic_lsreq) {
	FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsrsp_recv no nvfnic_lsreq for oxid:%x\n", oxid);
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
    }

    lsreq = nvfnic_lsreq->lsreq;
    if (!lsreq || (lsreq->private == NULL)) {
	FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsrsp_recv lsreq NULL oxid:%x\n", oxid);
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
    }
    if (!lsreq->rspaddr) {
	FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsrsp_recv lsreq->rspaddr NULL oxid:%x\n", oxid);
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
    }

    if ((nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_PENDING) ||
	(nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_STARTED)) {
	FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_lsrsp_recv lsreq abts pending oxid:%x\n", oxid);
        spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return;
    }

    nvfnic_lsreq->state = FNIC_LSREQ_CMD_COMPLETE;
    atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_responses);

    list_del(&nvfnic_lsreq->list);
    lsreq->private = NULL;
    nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
    spin_unlock_irqrestore(&fnic->fnic_lock, flags);
    del_timer_sync(&nvfnic_lsreq->lsreq_timer);

    FNIC_NVME_DBG(KERN_DEBUG, fnic, "NVME LS rsp rspnum:%lld\n",
	(u64)atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));


    /* Copy the Response */
    memcpy(lsreq->rspaddr, (void *)((uint8_t*)fchdr + sizeof(fc_hdr_t)),
	len - sizeof(fc_hdr_t));

    lsreq->done(lsreq, 0);
}

static int
fnic_transport_ready(fnic_iport_t *iport, fnic_tport_t *tport)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	if (tport == NULL) {
		return -1;
	}
	if (fdls_get_state(&iport->fabric) == FDLS_STATE_LINKDOWN ||
	   iport->state != FNIC_IPORT_STATE_READY) {
		atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		return -1;	
	}
	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED)))
		return -1;
 
	if ((tport->state == fdls_tgt_state_offlining) ||
		(tport->state == fdls_tgt_state_offline)) {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		return -1;
	}
	return 0;
}

#if FNIC_USE_SETUP_TIMER
static void
fnic_lsreq_timeout(unsigned long arg)
{
	struct nvfnic_lsreq *nvfnic_lsreq = (struct nvfnic_lsreq *)arg;
#else
static void
fnic_lsreq_timeout(struct timer_list *t) {
	struct nvfnic_lsreq *nvfnic_lsreq = from_timer(nvfnic_lsreq, t, lsreq_timer);
#endif
	struct fnic *fnic = nvfnic_lsreq->fnic;
	struct nvmefc_ls_req *lsreq = nvfnic_lsreq->lsreq;
	fnic_iport_t *iport =&fnic->iport;
	fnic_tport_t *tport = (fnic_tport_t *)nvfnic_lsreq->tport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	int timeout;

	FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_lsreq_timeout\n");
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	if ((lsreq->private == NULL) ||
	    (nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_STARTED)) {

		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_timeout :%p "
			"ls_req already in clean up by midlayer abort\n",nvfnic_lsreq);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	if (nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_timeout :%p abort timeout."
			"Completing the abort\n",nvfnic_lsreq);

		list_del(&nvfnic_lsreq->list);
		lsreq = nvfnic_lsreq->lsreq;
		nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
		lsreq->private = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);
		return;
	} else if ((nvfnic_lsreq->state == FNIC_LSREQ_CMD_PENDING) &&
		  (fnic_transport_ready(iport, tport) == 0)) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_timeout :%p sending abort\n",nvfnic_lsreq);
		nvfnic_lsreq->state = FNIC_LSREQ_CMD_ABTS_PENDING;
		atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_aborts);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		if (fdls_send_lsreq_abts(iport,tport, nvfnic_lsreq->oxid) == 0) {

			timeout = FNIC_LSREQ_TMO_MSECS(lsreq->timeout);
			mod_timer(&nvfnic_lsreq->lsreq_timer,
				round_jiffies(jiffies + msecs_to_jiffies(timeout)));
			return;
		}
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_timeout cannot send abort %p\n",nvfnic_lsreq);
		spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	}
	if (lsreq->private == NULL) {
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	list_del(&nvfnic_lsreq->list);
	lsreq = nvfnic_lsreq->lsreq;
	nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
	lsreq->private = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);

}

/* Callbacks from nvme host fc layer */
/* todo: init lsreq_oxid pool and free */
static int
nvfnic_lsreq_send(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, struct nvmefc_ls_req *lsreq)
{
	int ret = 0;
	fnic_iport_t *iport = lport->private;
	struct nvfnic_lsreq *nvfnic_lsreq;
	void *src;
	uint8_t *frame;
	fc_hdr_t *fchdr;
	fnic_tport_t *tport = (fnic_tport_t*)rport->private;
	struct fnic *fnic = iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags = 0;
	int timeout;
	nvfnic_lsreq = lsreq->private;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic_transport_ready(iport, tport)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -1;
	}

	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_requests);
	nvfnic_lsreq->oxid = nvfnic_alloc_lsreq_oxid(iport);
	if (nvfnic_lsreq->oxid == 0xFFFF) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_send Error oxid unavailable numlsreq:%lld\n",
			(u64)atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -1;
	}
#if FNIC_USE_SETUP_TIMER
	setup_timer(&nvfnic_lsreq->lsreq_timer, fnic_lsreq_timeout, (unsigned long)nvfnic_lsreq);
#else
	timer_setup(&nvfnic_lsreq->lsreq_timer, fnic_lsreq_timeout, (unsigned long)0);
#endif

	nvfnic_lsreq->fnic = fnic;
	nvfnic_lsreq->tport = tport;

	nvfnic_lsreq->state = FNIC_LSREQ_CMD_INIT;

	nvfnic_lsreq->lsreq = lsreq;

	/* Build fc/fcoe header or Copy the pre-build FC/FCoE hdr and 
	 * send Src and Dst MAC addresses 
	 */
	frame = (uint8_t *) kmalloc(lsreq->rqstlen + sizeof(fc_hdr_t), __GFP_NORETRY);
	if (!frame) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_send kmalloc failed\n");
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -1;
	}
	memset(frame, 0, lsreq->rqstlen + sizeof(fc_hdr_t));

	fchdr = (fc_hdr_t *)(frame);
	src = &nvfnic_lsreq_fchdr;

	memcpy(fchdr, src, sizeof(fc_hdr_t));
	hton24(fchdr->sid, iport->fcid);
	hton24(fchdr->did, tport->fcid);
	fchdr->ox_id = htons(nvfnic_lsreq->oxid);

	memcpy(frame + sizeof(fc_hdr_t), lsreq->rqstaddr, lsreq->rqstlen);

	FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_lsreq_send type:0x%02x len:%d, lsreq_num:%lld\n", 
		*((uint8_t*)lsreq->rqstaddr), lsreq->rqstlen, (u64)atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));
	//fnic_dump_frame((uint8_t *)fchdr, lsreq->rqstlen + sizeof(fc_hdr_t));
	/* Call fnic_send_frame TBD use host provided dma or map it in fdls */

	/* TBD. Fix it. 
	* Response could come back before we acqurie the lock and the rest 
	* That's why we do the bookkeeping before sending the frame. 
	* But send_frame could fail. Fix it in the right way
	*/
	list_add_tail(&nvfnic_lsreq->list, &tport->lsreq_list);
	nvfnic_lsreq->state = FNIC_LSREQ_CMD_PENDING;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	ret = fnic_send_fcoe_frame(iport, fchdr, lsreq->rqstlen + sizeof(fc_hdr_t));
	if (ret) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		list_del(&nvfnic_lsreq->list);
		nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
		lsreq->private = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	} else {
		timeout = FNIC_LSREQ_TMO_MSECS(lsreq->timeout);
		mod_timer(&nvfnic_lsreq->lsreq_timer,
			round_jiffies(jiffies + msecs_to_jiffies(timeout)));
	}
	kfree(frame);

	return ret;

}

void 
nvfnic_localport_delete(struct nvme_fc_local_port *lport)
{
	fnic_iport_t *iport = (fnic_iport_t *)lport->private;
	struct fnic *fnic = iport->fnic;
	unsigned long flags = 0;

	FNIC_NVME_DBG(KERN_INFO, fnic, "lport delete %s\n", __FUNCTION__);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->nvme_lport_unreg_done)
		complete(fnic->nvme_lport_unreg_done);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void
nvfnic_remoteport_delete(struct nvme_fc_remote_port *rport)
{
	fnic_tport_t *tport;
	fnic_iport_t *iport;
	struct fnic *fnic = NULL;
	unsigned long flags = 0, flags_listlock = 0;

	/* Acquire fnic_list_lock to protect from simultaneous delete calls */
	spin_lock_irqsave(&fnic_list_lock, flags_listlock);

	if(rport->private == NULL) {
		printk(KERN_ERR "NVME tport callback after setting to NULL %p\n", rport);
		spin_unlock_irqrestore(&fnic_list_lock, flags_listlock);
		return;
	}

	tport = (fnic_tport_t *)rport->private;
	iport = (fnic_iport_t *)tport->iport;
	fnic = iport->fnic;
	printk("nvfnic_remoteport_delete: tport :%p\n", tport);

	/* rport->private is used with fnic_lock in other places */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	rport->private = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_unlock_irqrestore(&fnic_list_lock, flags_listlock);


	spin_lock_irqsave(&fnic->fnic_lock, flags);
	BUG_ON(!(tport->flags & FNIC_FDLS_NVME_REGISTERED));

	FNIC_NVME_DBG(KERN_DEBUG, fnic, 
		"tport %8x freed in the callback\n", tport->fcid);

	if (tport->flags & NVME_TPORT_CLEANUP_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic, 
			"tport %8x delete_callback waiting on clean pending\n", tport->fcid);
	}
	while (tport->flags & NVME_TPORT_CLEANUP_PENDING) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		msleep(2000);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
	}
	list_del(&tport->links);

	if (tport->tport_del_done)
		complete(tport->tport_del_done);

	/* If this is the last element and someone waiting for the list to become 
         * empty, wake them
         */ 
	if ((fnic->in_remove) ||
		(fdls_get_state(&iport->fabric) == FDLS_STATE_LINKDOWN)) {
		if (list_empty(&fnic->iport.tport_list_pending_del) && 
			(fnic->nvme_tport_empty_wait))
			complete(fnic->nvme_tport_empty_wait);
	}
	tport->flags |= FNIC_TPORT_CAN_BE_FREED;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}


static int
nvfnic_create_queue(struct nvme_fc_local_port *lport,
	unsigned int idx, u16 size, void **handle)
{
	fnic_iport_t *iport = (fnic_iport_t *)lport->private;
	struct fnic *fnic  = iport->fnic;

	FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_create_queue: %d, %d\n", idx, size);

	*handle = iport->fnic;
	return 0; 
}

static void
nvfnic_lsreq_abort(struct nvme_fc_local_port *lport, struct nvme_fc_remote_port *rport,
	struct nvmefc_ls_req *lsreq)
{
	fnic_iport_t *iport = lport->private;
	struct fnic *fnic = iport->fnic;
	fnic_tport_t *tport = (fnic_tport_t*)rport->private;
	struct nvfnic_lsreq *nvfnic_lsreq;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	int timeout;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	/* find the request */
	nvfnic_lsreq = lsreq->private;

	if ((nvfnic_lsreq == NULL) ||
		(nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_PENDING)) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "lsreq already scheduled for completion."
			"or abort.Returning from midlayer lsreq abort\n");
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	FNIC_NVME_DBG(KERN_INFO, fnic,
		"lsreq abts:oxid :%x\n", nvfnic_lsreq->oxid);

	nvfnic_lsreq->state = FNIC_LSREQ_CMD_ABTS_STARTED;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
    	del_timer_sync(&nvfnic_lsreq->lsreq_timer);

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	nvfnic_lsreq = lsreq->private;

	if ((nvfnic_lsreq == NULL) ||
		(nvfnic_lsreq->state == FNIC_LSREQ_CMD_ABTS_PENDING)) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "lsreq handled by timeout while"
			" NVME midlayer abort is running\n");
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	/* Basic validations of the state */
	if (fnic_transport_ready(iport, tport)) {
		/* If iport or tport offline, it will be handled from that event */
		lsreq->private = NULL;
		list_del(&nvfnic_lsreq->list);
		nvfnic_free_lsreq_oxid(iport, nvfnic_lsreq->oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		lsreq->done(lsreq, FNIC_STATUS_LSREQ_ABORTED);
		FNIC_NVME_DBG(KERN_ERR, fnic, "nvfnic_lsreq_abort transport not ready\n");
		return;
	}

	/* Mark the state and flags */
	nvfnic_lsreq->state = FNIC_LSREQ_CMD_ABTS_PENDING;
	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_aborts);
	timeout = FNIC_LSREQ_TMO_MSECS(lsreq->timeout);
	mod_timer(&nvfnic_lsreq->lsreq_timer,
		round_jiffies(jiffies + msecs_to_jiffies(timeout)));
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

	fdls_send_lsreq_abts(iport,tport, nvfnic_lsreq->oxid);
	return;
}

static int 
nvfnic_queue_abort_ioreq(struct fnic *fnic, int tag,
                                          u32 task_req, u8 *fc_lun,
                                          struct fnic_io_req *io_req)
{
        struct vnic_wq_copy *wq = &fnic->wq_copy[0];
        struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
        unsigned long flags;

        atomic_inc(&fnic->in_flight);

        spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

        if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
                free_wq_copy_descs(fnic, wq, 0);

        if (!vnic_wq_copy_desc_avail(wq)) {
                spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
                atomic_dec(&fnic->in_flight);
		fnic_printk(KERN_ERR, fnic, "fnic_queue_abort_io_req: failure: no descriptors\n");
                atomic64_inc(&misc_stats->abts_cpwq_alloc_failures);
                return 1;
        }
        fnic_queue_wq_copy_desc_itmf(wq, tag | FNIC_TAG_ABORT,
                                     0, task_req, tag, fc_lun, io_req->port_id,
                                     fnic->config.ra_tov, fnic->config.ed_tov);

        atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
        if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
                atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
                atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
                atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

        spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
        atomic_dec(&fnic->in_flight);

        return 0;
}

/* Debug code */
void
fnic_iterate_respone_queue(struct fnic *fnic, struct fnic_io_req *match_ioreq)
{
	fnic_io_event_t *io_evt = NULL, *next;
	struct nvmefc_fcp_req *fcp_req = NULL;
	struct fnic_io_req *io_req = NULL;
	int null_entries = 0, count = 0;

	fnic_printk(KERN_INFO, fnic,
		"Walking through response queue\n");

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(io_evt, next, &fnic->nvme_io_event_list, links) {
		if (!io_evt) {
			/* should not hit the case though */
			null_entries++;
		}

		fcp_req = io_evt->arg1;
		BUG_ON(!fcp_req);
		io_req = (struct fnic_io_req *)fcp_req->private;
		BUG_ON(!io_req);

		count++;
		fnic_printk(KERN_INFO, fnic,
			"io_req:%p, tport_fcid:%x\n", io_req, io_req->tport->fcid);

		if (match_ioreq) {
			if (io_req == match_ioreq)
				fnic_printk(KERN_INFO, fnic, "io_req found in rsp q\n");
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags); 

	fnic_printk(KERN_INFO, fnic,
		"iterate_rsp_q entries in queue:%d\n", count);
}

void nvfnic_fcpio_abort(struct nvme_fc_local_port *lport, struct nvme_fc_remote_port *rport,
			void *hw_queue_handle, struct nvmefc_fcp_req *fcp_req)
{
	fnic_iport_t *iport = lport->private;
	struct fnic *fnic = iport->fnic;
        struct nvme_fc_cmd_iu *cmd_iu = fcp_req->cmdaddr;
	struct fnic_io_req *io_req = (struct fnic_io_req *)fcp_req->private;
	unsigned int tag = io_req->tag;
        struct fnic_stats *fnic_stats = &fnic->fnic_stats;
        struct abort_stats *abts_stats;
	struct terminate_stats *term_stats;
	unsigned long flags = 0;
	unsigned long abt_issued_time;
	unsigned int task_req;
	enum fnic_ioreq_state old_ioreq_state;
	unsigned long num_ios_waitq, waitq_2sec, waitq_max_time;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (io_req->tag == 0) {
		BUG_ON(1);
	}
	if (io_req->tag == -1) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			"fcpio_abort. (tag = -1), tport_fcid:%x\n",
			io_req->tport->fcid);
#if 0
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fnic_iterate_respone_queue(fnic, io_req);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
#endif
	}
	if (io_req != nvfnic_find_ioreq_by_tag(fnic, io_req->tag)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			"command tag freed and reissued or not-issued %d sn:0x%08x\n", 
			io_req->tag, be32_to_cpu(cmd_iu->csn));
		num_ios_waitq = atomic64_read(&fnic_stats->io_stats.num_ios_in_waitq);
		waitq_2sec = atomic64_read(&fnic_stats->io_stats.io_in_waitq_3000_msec);
		waitq_max_time = atomic64_read(&fnic_stats->io_stats.io_in_waitq_max_time);
		FNIC_NVME_DBG(KERN_INFO, fnic,
			"num_ios_waitq: %ld, waitq_2sec: %ld, waitq_max_time: %ld\n",
			num_ios_waitq, waitq_2sec, waitq_max_time);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (NVME_CMD_STATE(io_req) == FNIC_IOREQ_CMD_COMPLETE) {
		FNIC_NVME_DBG(KERN_INFO, fnic, "IO already completed before abort %d\n", tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	if ((NVME_CMD_STATE(io_req) == FNIC_IOREQ_ABTS_PENDING) ||
	    (NVME_CMD_STATE(io_req) == FNIC_DEV_RST_TERM_ISSUED)) {
		FNIC_NVME_DBG(KERN_INFO, fnic, "abort already pending %d\n", tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	if (NVME_CMD_STATE(io_req) != FNIC_IOREQ_CMD_PENDING) {	
		FNIC_NVME_DBG(KERN_INFO, fnic, "io_req is NULL completed or aborted already for abort tag %d\n", tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
        FNIC_NVME_DBG(KERN_INFO, fnic, "in abort cmd_sn:%08x %llx tag %d\n", 
		be32_to_cpu(cmd_iu->csn), le64_to_cpu(cmd_iu->sqe.rw.slba), io_req->tag);

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED))) {
        	FNIC_NVME_DBG(KERN_INFO, fnic, "returning abort tag %d as fw reset in progress\n", 
			io_req->tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
    	atomic_inc(&fnic->in_flight);

	if ((fdls_get_tport_state(io_req->tport) == fdls_tgt_state_offline) ||
		(fdls_get_tport_state(io_req->tport) == fdls_tgt_state_offlining) ||
		(NVME_CMD_STATE(io_req) == FNIC_IOREQ_RESET_TERM)) {
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	} else {
		task_req = FCPIO_ITMF_ABT_TASK;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	abts_stats = &fnic->fnic_stats.abts_stats;
	term_stats = &fnic->fnic_stats.term_stats;

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

	old_ioreq_state = NVME_CMD_STATE(io_req);
	NVME_CMD_STATE(io_req) = FNIC_IOREQ_ABTS_PENDING;
	NVME_CMD_ABTS_STATUS(io_req) = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (nvfnic_queue_abort_ioreq(fnic, io_req->tag, task_req,
		NULL, io_req)) {
		fnic_printk(KERN_ERR, fnic, "fnic_queue_abort_io_req failed\n");
		spin_lock_irqsave(&fnic->fnic_lock, flags);
                NVME_CMD_STATE(io_req) = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
        }
    	atomic_dec(&fnic->in_flight);
	return;
}

/* TBD for the actual parameter values in this struct */
static struct
nvme_fc_port_template nvfnic_port = {
    .localport_delete  = nvfnic_localport_delete,
    .remoteport_delete = nvfnic_remoteport_delete,
    .create_queue      = nvfnic_create_queue,
    .delete_queue      = NULL,
    .ls_req            = nvfnic_lsreq_send,
    .ls_abort          = nvfnic_lsreq_abort,
    .fcp_io            = nvfnic_fcpio_send,
    .fcp_abort         = nvfnic_fcpio_abort,
#if FNIC_HAVE_NVME_FC_TEMPLATE_POLL_QUEUE
    .poll_queue        = NULL,
#endif
    .max_hw_queues     = 512,
    .max_sgl_segments  = 256,
    .max_dif_sgl_segments = 64, /* TBD */
    .dma_boundary         = 0xFFFFFFFF,
    .local_priv_sz        = sizeof(fnic_iport_t *),
    .remote_priv_sz       = sizeof(fnic_tport_t *),
    .lsrqst_priv_sz       = sizeof(struct nvfnic_lsreq),
    .fcprqst_priv_sz      = sizeof(struct fnic_io_req),
};
/*
 * This is called from module remove context to free io event list
*/
void fnic_flush_nvme_io_list(struct fnic *fnic)
{
	unsigned long flags;
	fnic_io_event_t *cur_evt, *next;
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (list_empty(&fnic->nvme_io_event_list))
		printk("fnic_flush_nvme_io_list: io_event list empty\n");
	else
		printk("fnic_flush_nvme_io_list: io_event list NOT empty\n");

	list_for_each_entry_safe(cur_evt, next, &fnic->nvme_io_event_list, links) {
		list_del(&cur_evt->links);
		//kfree(cur_evt);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

/*
 * fnic_nvme_iodone_handler
 * This kthread is woken up conditionally when there are 1 or more io resp
 * to be sent. 
 * Runs until kthread_stop is invoked during module unload
 */
int
fnic_nvme_iodone_handler(void *arg)
{
	struct fnic *fnic = (struct fnic *)arg;
	struct nvmefc_fcp_req *fcp_req;
	unsigned long flags;
	fnic_io_event_t *cur_evt;
	unsigned int num_ios_processed =  0;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	while (1) {
		wait_event_cmd(fnic->rsp_wait,
			(!list_empty(&fnic->nvme_io_event_list) || (kthread_should_stop())),
			spin_unlock_irqrestore(&fnic->fnic_lock, flags),
			spin_lock_irqsave(&fnic->fnic_lock, flags));

		if (kthread_should_stop()) {
			FNIC_NVME_DBG(KERN_INFO, fnic,
				"fnic nvme-fc iodone kthread stopped\n");
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			break;	
		}

		num_ios_processed = 0;          
		do {
			cur_evt = list_first_entry_or_null(&fnic->nvme_io_event_list,
				fnic_io_event_t, links);
			if (cur_evt == NULL) {
				break;
			}
			atomic64_inc(&fnic_stats->io_stats.io_rsps_unqueued);
 
			list_del(&cur_evt->links);
			fnic->rsp_cnt--;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);

			fcp_req = cur_evt->arg1;
			io_req = (struct fnic_io_req *)fcp_req->private;

			if (NVME_CMD_FLAGS(io_req) & FNIC_NVME_ADMINIO_TIMER_PENDING) {
				del_timer_sync(&io_req->adminIO_timer);
				NVME_CMD_FLAGS(io_req) &= ~FNIC_NVME_ADMINIO_TIMER_PENDING;
			}
			atomic64_inc(&fnic_stats->io_stats.io_rsps_sending);
			fcp_req->done(fcp_req);
			atomic64_inc(&fnic_stats->io_stats.io_rsps_sent);
			spin_lock_irqsave(&fnic->fnic_lock, flags);

			num_ios_processed++;
			if (num_ios_processed >= nvme_max_ios_to_process) {
				num_ios_processed = 0;
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				schedule();
				spin_lock_irqsave(&fnic->fnic_lock, flags);
			}

		} while (1);
	}
	do_exit(0);
	return 0;
}

void nvfnic_delete_tport(fnic_tport_t *tport)
{
	struct fnic *fnic = (struct fnic *)((fnic_iport_t *)tport->iport)->fnic;
	fnic_iport_t * iport = tport->iport;
	int ret;
	fnic_tport_t *tport1 = NULL, *next;
	unsigned long flags = 0;
	unsigned int time_wait = FNIC_NVME_TPORT_REMOVE_WAIT;
	unsigned int time_remain;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	unsigned int fcid = tport->fcid;

    	FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_delete_tport scheduled for [%x]\n",
		tport->fcid);

	/* cleanup lsreq and adminios before unregistering */
	fnic_terminate_tport_lsreqs(fnic, tport);
	fnic_terminate_tport_admin_ios(fnic, tport);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	/* This rather should be a BUG_ON. TBD */
	if (tport->flags & FNIC_FDLS_NVME_REGISTERED) {
		tport->tport_del_done = &tm_done;

		
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		ret = nvme_fc_unregister_remoteport(tport->nv_rport);
		if (ret) {
			fnic_printk(KERN_ERR, fnic,
				"nvfnic_delete_tport(%x) unregister failed %d\n", 
				tport->fcid, ret);
			return;
		}
		time_remain = wait_for_completion_timeout(tport->tport_del_done,
				msecs_to_jiffies(time_wait));

    		FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_delete_tport wait for "
			" callback woken up or timeout [%x]\n", tport->fcid);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		tport->tport_del_done = NULL;

		if (!time_remain) {
			list_for_each_entry_safe(tport1, next, 
				&iport->tport_list_pending_del, links)   {
				if (tport1 == tport) {
					tport->flags |= NVME_TPORT_CLEANUP_PENDING;
					break;
				}
                        }
			if (tport1 != tport) {
    				FNIC_NVME_DBG(KERN_DEBUG, fnic, "tport not found in pending"
				" list following a timeout. NVME callback may be running "
				" [%x]\n", fcid);
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				return;
			}

			fnic_printk(KERN_ERR, fnic,
				"nvfnic_delete_tport(%x) nvme midlayer completion timed out\n",
				tport->fcid);

			fnic_printk(KERN_ERR, fnic,
				"del_tport rx:%lld, queued:%lld, unq:%lld, "
				"sending:%lld, rsps_sent:%lld\n",
				atomic64_read(&fnic->fnic_stats.io_stats.io_reqs_rcvd),
				atomic64_read(&fnic->fnic_stats.io_stats.ios_queued_for_rsp),
				atomic64_read(&fnic->fnic_stats.io_stats.io_rsps_unqueued),
				atomic64_read(&fnic->fnic_stats.io_stats.io_rsps_sending),
				atomic64_read(&fnic->fnic_stats.io_stats.io_rsps_sent));

			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			fnic_cleanup_tport_io(fnic, tport);
			spin_lock_irqsave(&fnic->fnic_lock, flags);
			tport->flags &= ~NVME_TPORT_CLEANUP_PENDING;
		} else {
			int count = 0;
			while(!(tport->flags & FNIC_TPORT_CAN_BE_FREED) && (count < 8)){
				count++;
				msleep(2000);
			}
			if (tport->flags & FNIC_TPORT_CAN_BE_FREED)
				kfree(tport);
		}
		printk("nvfnic_delete_tport(%x) delete complete\n",
			fcid);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void
nvfnic_delete_tport_work(struct work_struct *work)
{
	fnic_tport_t *tport = container_of(work, fnic_tport_t,
		tport_del_work);

	nvfnic_delete_tport(tport);
}

/* Interface to FDLS/fnic driver */
int
nvfnic_add_tport(struct fnic *fnic, fnic_tport_t *tport)
{
    fnic_iport_t *iport = &fnic->iport;
    struct nvme_fc_port_info pinfo;
    int ret = 0;

    FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_add_tport Adding tport tp nvme wwpn[%llx]\n",
	tport->wwpn);

    memset(&pinfo, 0, sizeof(struct nvme_fc_port_info));

    pinfo.port_name = tport->wwpn;
    pinfo.node_name = tport->wwnn;
    pinfo.port_role = FC_PORT_ROLE_NVME_DISCOVERY | FC_PORT_ROLE_NVME_TARGET;
    pinfo.port_id = tport->fcid;
    pinfo.dev_loss_tmo = nvme_dev_loss_tmo;

	printk("add_tport register remoteport\n");	
    ret = nvme_fc_register_remoteport(iport->nv_lport, &pinfo,
        &tport->nv_rport);
	printk("register remoteport ret:%d\n", ret);
    if (ret) {
	fnic_printk(KERN_ERR, fnic, "register_tport FAILED wwpn:%llx, ret:%d\n", 
		tport->wwpn, ret);
	return 0;
    }
    tport->flags |= FNIC_FDLS_NVME_REGISTERED;
    tport->nv_rport->private = tport;

    /* TBD: run this under a different thread? */
    sprintf(tport->str_wwpn, "0x%llx", tport->wwpn);
    sprintf(tport->str_wwnn, "0x%llx", tport->wwnn);
    schedule_delayed_work(&tport->tport_scan_work, 0);
	
    return ret;
}

void
fnic_iport_event_handler(struct work_struct *work)
{
        struct fnic *fnic = container_of(work, struct fnic,
                iport_work);

	nvfnic_add_lport(fnic);
}



int
nvfnic_add_lport(struct fnic *fnic)
{
    struct nvme_fc_port_template *tmpl;
    struct nvme_fc_port_info pinfo;
    fnic_iport_t *iport = &fnic->iport;
    int ret = 0;

    FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_add_lport Adding lport nvme fnic wwpn[%llx]\n",
	iport->wwpn);

    tmpl = &nvfnic_port;

    pinfo.node_name = iport->wwnn;
    pinfo.port_name = iport->wwpn;
    pinfo.port_role = FC_PORT_ROLE_NVME_INITIATOR;
    pinfo.port_id   = iport->fcid;

    nvfnic_init_lsreq_oxid_pool(iport);
    nvfnic_init_fcpio_tag_pool(iport);

    if (!(iport->flags & FNIC_LPORT_NVME_REGISTERED))
    {		
    	init_completion(&fnic->nvme_del_done);

	printk("add_lport register localport\n");
    	ret = nvme_fc_register_localport(&pinfo, tmpl,
        	get_device(&fnic->pdev->dev), &iport->nv_lport);
	printk("register localport ret:%d\n", ret);
	
    	if (ret) {
		fnic_printk(KERN_ERR, fnic, "register_localport FAILED "
			"wwpn:%llx, ret:%d\n", iport->wwpn, ret);
		return 0;
    	}
	iport->flags |= FNIC_LPORT_NVME_REGISTERED;
    	iport->nv_lport->private = iport;
    }
    /* added for usermode_helper... Revisit */
    sprintf(iport->str_wwpn, "0x%llx", iport->wwpn);
    sprintf(iport->str_wwnn, "0x%llx", iport->wwnn);

    FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvfnic_add_lport Adding lport succeeded wwpn[%llx]\n",
                iport->wwpn);

    return 0;
}

void
nvfnic_delete_lport(fnic_iport_t *iport)
{
	struct fnic *fnic = iport->fnic;
	fnic_tport_t *tport, *next;
	unsigned long flags;
	int wait_for_cmpl = 0;
	unsigned int time_wait = FNIC_NVME_TPORT_LIST_EMPTY_WAIT;
	unsigned int time_remain;
	DECLARE_COMPLETION_ONSTACK(nvme_tport_empty_wait);

	FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_delete_lport Deleting lport wwpn[%llx]\n",
		iport->wwpn);


	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->nvme_tport_empty_wait = &nvme_tport_empty_wait;
 	list_for_each_entry_safe(tport, next, &iport->tport_list, links)   {

		if ((tport->state == fdls_tgt_state_offlining) ||
			(tport->state == fdls_tgt_state_offline))
			continue;
		FNIC_NVME_DBG(KERN_INFO, fnic, "removing rport:%x",
			tport->fcid);
		fdls_set_tport_state(tport, fdls_tgt_state_offlining);
		tport->flags |= FNIC_FDLS_TPORT_TERMINATING;

		if (tport->timer_pending)
			del_timer_sync(&tport->retry_timer);

		list_del(&tport->links);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		cancel_delayed_work_sync(&tport->tport_scan_work);
		spin_lock_irqsave(&fnic->fnic_lock, flags);

		if (tport->flags & FNIC_FDLS_NVME_REGISTERED) {
			FNIC_NVME_DBG(KERN_INFO, fnic, "nvfnic_delete_lport schedule to delete %x\n",
				tport->fcid);
			wait_for_cmpl = 1;
			list_add_tail(&tport->links, &iport->tport_list_pending_del);
			schedule_work(&tport->tport_del_work);
		} else {
			kfree(tport);
		}
	}
	
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (wait_for_cmpl) {
		time_remain = 
			wait_for_completion_timeout(fnic->nvme_tport_empty_wait,
			msecs_to_jiffies(time_wait));
		if (!time_remain) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"Timed out waiting for remote ports deleted\n");
			//BUG_ON(1);
		}
	}

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->nvme_tport_empty_wait = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}
