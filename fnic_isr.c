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

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
//#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include <scsi/scsi_transport_fc.h>
#include "fnic_stats.h"
#include "fnic_fdls.h"
#include "fnic_io.h"
#include "fnic.h"

static irqreturn_t fnic_isr_legacy(int irq, void *data)
{
	struct fnic *fnic = data;
	u32 pba;
	unsigned long work_done = 0;

	pba = vnic_intr_legacy_pba(fnic->legacy_pba);
	if (!pba)
		return IRQ_NONE;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	if (pba & (1 << FNIC_INTX_NOTIFY)) {
		vnic_intr_return_all_credits(&fnic->intr[FNIC_INTX_NOTIFY]);
		fnic_handle_link_event(fnic);
	}

	if (pba & (1 << FNIC_INTX_ERR)) {
		vnic_intr_return_all_credits(&fnic->intr[FNIC_INTX_ERR]);
		fnic_log_q_error(fnic);
	}

	if (pba & (1 << FNIC_INTX_DUMMY)) {
	    atomic64_inc(&fnic->fnic_stats.misc_stats.intx_dummy);
	    if((u64) atomic64_read(&fnic->fnic_stats.misc_stats.intx_dummy) <= 3)
              printk("fnic_isr_legacy: FNIC_INTX_DUMMY on irq: %d\n", irq);
	    vnic_intr_return_all_credits(&fnic->intr[FNIC_INTX_DUMMY]);
	}

	if (pba & (1 << FNIC_INTX_WQ_RQ_COPYWQ)) {
		work_done += fnic_wq_copy_cmpl_handler(fnic, io_completions, 2);
		work_done += fnic_wq_cmpl_handler(fnic, -1);
		work_done += fnic_rq_cmpl_handler(fnic, -1);

		vnic_intr_return_credits(&fnic->intr[FNIC_INTX_WQ_RQ_COPYWQ],
					 work_done,
					 1 /* unmask intr */,
					 1 /* reset intr timer */);
	}

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msi(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	work_done += fnic_wq_copy_cmpl_handler(fnic, io_completions, 2);
	work_done += fnic_wq_cmpl_handler(fnic, -1);
	work_done += fnic_rq_cmpl_handler(fnic, -1);

	vnic_intr_return_credits(&fnic->intr[0],
				 work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_rq(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long rq_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	rq_work_done = fnic_rq_cmpl_handler(fnic, -1);
	vnic_intr_return_credits(&fnic->intr[FNIC_MSIX_RQ],
				 rq_work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_wq(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long wq_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	wq_work_done = fnic_wq_cmpl_handler(fnic, -1);
	vnic_intr_return_credits(&fnic->intr[FNIC_MSIX_WQ],
				 wq_work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);
	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_wq_copy(int irq, void *data)
{
	struct fnic *fnic = data;
	int i;
	unsigned long wq_copy_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	i = irq - fnic->msix[0].irq_num;
	
//	printk("fnic cpy wq interrupt on irq:%d wq:%d\n",irq,i);

	if(i >= fnic->wq_copy_count+fnic->cpy_wq_base || i < 0 ||  fnic->msix[i].irq_num != irq)
	{
		for (i = fnic->cpy_wq_base; i < fnic->wq_copy_count + fnic->cpy_wq_base ; i++) {
			if(fnic->msix[i].irq_num == irq)
				break;
		}
	}

	wq_copy_work_done = fnic_wq_copy_cmpl_handler(fnic, io_completions, i);

	/*return credits per cq*/
	vnic_intr_return_credits(&fnic->intr[i],
			 wq_copy_work_done,
			 1 /* unmask intr */,
			 1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_err_notify(int irq, void *data)
{
	struct fnic *fnic = data;

	printk(KERN_ERR "fnic(%d) link event interrupt on irq:%d\n",
		fnic->fnic_num, irq);
	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	vnic_intr_return_all_credits(&fnic->intr[fnic->err_intr_offset]);
	fnic_log_q_error(fnic);
	fnic_handle_link_event(fnic);

	return IRQ_HANDLED;
}

void fnic_free_intr(struct fnic *fnic)
{
	int i;

	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
	case VNIC_DEV_INTR_MODE_MSI:
#if FNIC_HAVE_PCI_IRQ_VECTOR
		free_irq(pci_irq_vector(fnic->pdev, 0), fnic);
#else
		free_irq(fnic->pdev->irq, fnic);
#endif
		break;

	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < ARRAY_SIZE(fnic->msix); i++)
			if (fnic->msix[i].requested)
#if FNIC_HAVE_PCI_IRQ_VECTOR
				free_irq(pci_irq_vector(fnic->pdev, i),
#else
				free_irq(fnic->msix_entry[i].vector,
#endif
					 fnic->msix[i].devid);
		break;

	default:
		break;
	}
}

int fnic_request_intr(struct fnic *fnic)
{
	int err = 0;
	int i;

	switch (vnic_dev_get_intr_mode(fnic->vdev)) {

	case VNIC_DEV_INTR_MODE_INTX:
#if FNIC_HAVE_PCI_IRQ_VECTOR
		err = request_irq(pci_irq_vector(fnic->pdev, 0),
				&fnic_isr_legacy, IRQF_SHARED, DRV_NAME, fnic);
#else
		err = request_irq(fnic->pdev->irq, &fnic_isr_legacy,
				  IRQF_SHARED, DRV_NAME, fnic);
#endif
		break;

	case VNIC_DEV_INTR_MODE_MSI:
        sprintf(fnic->name, "%s", DRV_NAME);
        printk(KERN_DEBUG "fnic_request_intr: fnic->name: (%s)\n", fnic->name);
#if FNIC_HAVE_PCI_IRQ_VECTOR
		err = request_irq(pci_irq_vector(fnic->pdev, 0), &fnic_isr_msi,
#else
		err = request_irq(fnic->pdev->irq, &fnic_isr_msi,
#endif
				  0, fnic->name, fnic);
		break;

	case VNIC_DEV_INTR_MODE_MSIX:

		sprintf(fnic->msix[FNIC_MSIX_RQ].devname,
			"%.11s-fcs-rq", fnic->name);
		fnic->msix[FNIC_MSIX_RQ].isr = fnic_isr_msix_rq;
		fnic->msix[FNIC_MSIX_RQ].devid = fnic;

		sprintf(fnic->msix[FNIC_MSIX_WQ].devname,
			"%.11s-fcs-wq", fnic->name);
		fnic->msix[FNIC_MSIX_WQ].isr = fnic_isr_msix_wq;
		fnic->msix[FNIC_MSIX_WQ].devid = fnic;

		for (i = fnic->cpy_wq_base; i < fnic->wq_copy_count + fnic->cpy_wq_base; i++) {
			sprintf(fnic->msix[i].devname,
				"%.11s-scsi-wq-%d", fnic->name, i-FNIC_MSIX_WQ_COPY);
			fnic->msix[i].isr = fnic_isr_msix_wq_copy;
			fnic->msix[i].devid = fnic;
		}

		sprintf(fnic->msix[fnic->err_intr_offset].devname,
			"%.11s-err-notify", fnic->name);
		fnic->msix[fnic->err_intr_offset].isr =
			fnic_isr_msix_err_notify;
		fnic->msix[fnic->err_intr_offset].devid = fnic;

		for (i = 0; i < fnic->intr_count; i++) {

#if FNIC_HAVE_PCI_IRQ_VECTOR
			fnic->msix[i].irq_num = pci_irq_vector(fnic->pdev, i);

			err = request_irq(fnic->msix[i].irq_num,
					  fnic->msix[i].isr, 0,
					  fnic->msix[i].devname,
					  fnic->msix[i].devid);
#else
		   err = request_irq(fnic->msix_entry[i].vector,
                                          fnic->msix[i].isr, 0,
                                          fnic->msix[i].devname,
                                          fnic->msix[i].devid);

			fnic->msix[i].irq_num = fnic->msix_entry[i].vector;
#endif

			if (err) {
				printk(KERN_ERR 
					     "MSIX: request_irq"
					     " failed %d\n", err);
				fnic_free_intr(fnic);
				break;
			}
			fnic->msix[i].requested = 1;
		}

		break;

	default:
		break;
	}

	return err;
}


int fnic_set_intr_mode_intx(struct fnic *fnic) {

	printk(KERN_INFO "fnic_set_intr_mode_intx: intr mode: %d "
                    "INTX\n", fnic->config.intr_mode);

	/*
         * We need 1 RQ, 1 WQ, 1 WQ_COPY, 3 CQs, and 3 INTRs
         * 1 INTR is used for all 3 queues, 1 INTR for queue errors
         * 1 INTR for notification area
         */

        if (fnic->rq_count >= 1 && fnic->raw_wq_count >= 1
                && fnic->wq_copy_count >= 1 && fnic->cq_count >= 3
                && fnic->intr_count >= 4) {

            fnic->rq_count = 1;
            fnic->raw_wq_count = 1;
            fnic->wq_copy_count = 1;
            fnic->cq_count = 3;
            fnic->intr_count = 4;
            fnic->wq_count = 2;
            fnic->cpy_wq_base = fnic->rq_count + fnic->raw_wq_count;
            fnic->err_intr_offset = 3;

            printk(KERN_DEBUG
                    "Using Legacy Interrupts\n");
            printk(KERN_ERR "fnic_set_intr_mode LEGACY\n");
            vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_INTX);

            return 0;
        }
        return 1;
} //fnic_set_intr_mode_intx


int fnic_set_intr_mode_msi(struct fnic *fnic) {

        printk(KERN_INFO "fnic_set_intr_mode_msi: intr mode: %d "
                    "MSI\n", fnic->config.intr_mode);
	/*
         * We need 1 RQ, 1 WQ, 1 WQ_COPY, 3 CQs, and 1 INTR
         */
        if (fnic->rq_count >= 1 && fnic->raw_wq_count >= 1
                && fnic->wq_copy_count >= 1 && fnic->cq_count >= 3
                && fnic->intr_count >= 1 &&
#ifdef NEW_PCI_INTERFACE
                !pci_enable_msi(fnic->pdev)) {
#else
                pci_alloc_irq_vectors(fnic->pdev, 1, 1, PCI_IRQ_MSI) > 0) {
#endif /*NEW_PCI_INTERFACE*/
            fnic->rq_count = 1;
            fnic->raw_wq_count = 1;
            fnic->wq_copy_count = 1;
            fnic->wq_count = 2;
            fnic->cq_count = 3;
            fnic->intr_count = 1;
            fnic->err_intr_offset = 0;
            fnic->cpy_wq_base = fnic->rq_count + fnic->raw_wq_count;

            printk(KERN_DEBUG
                    "Using MSI Interrupts\n");
            printk(KERN_ERR "fnic_set_intr_mode MSI\n");
            vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_MSI);

            return 0;
        }
	return 1;
}//fnic_set_intr_mode_msi


int fnic_set_intr_mode_msix(struct fnic *fnic)
{
        unsigned int n = ARRAY_SIZE(fnic->rq);
        unsigned int m = ARRAY_SIZE(fnic->wq);
        unsigned int o = ARRAY_SIZE(fnic->wq_copy);
        unsigned int min_irqs = n + m + 1 + 1 ; /*rq, raw wq, wq, err*/
#if !FNIC_HAVE_PCI_IRQ_VECTOR
        unsigned int i;
#endif
        printk(KERN_INFO "fnic_set_intr_mode_msix: intr mode: %d "
                    "MSIX\n", fnic->config.intr_mode);

        /*
         * We need n RQs, m WQs, o Copy WQs, n+m+o CQs, and n+m+o+1 INTRs
         * (last INTR is used for WQ/RQ errors and notification area)
         */
        printk(KERN_DEBUG "rq-array size=%d, wq-array size=%d, copy-wq array size=%d\n", n,m,o);
        printk(KERN_DEBUG "rq_count=%d, raw_wq_count=%d, wq_copy_count=%d, cq_count=%d\n",
               fnic->rq_count, fnic->raw_wq_count, fnic->wq_copy_count, fnic->cq_count);

#if !FNIC_HAVE_PCI_IRQ_VECTOR
        for (i = 0; i < n + m + o + 1; i++)
            fnic->msix_entry[i].entry = i;
#endif

        if (fnic->rq_count <= n && fnic->raw_wq_count <= m
                && fnic->wq_copy_count <= o) {
            int vec_count = 0;
            int vecs = fnic->rq_count + fnic->raw_wq_count
                    + fnic->wq_copy_count + 1;

#if FNIC_HAVE_PCI_IRQ_VECTOR
            vec_count = pci_alloc_irq_vectors(fnic->pdev, min_irqs, vecs, PCI_IRQ_MSIX|PCI_IRQ_AFFINITY);
            printk(KERN_DEBUG "allocated %d MSI-X vectors\n",vec_count);
#else /* FNIC_HAVE_PCI_IRQ_VECTOR */
            printk(KERN_DEBUG "requesting %d MSI-X vectors\n",vec_count);
#if FNIC_USE_PCI_ENABLE_MSIX_EXACT
            vec_count = pci_enable_msix_exact(fnic->pdev, fnic->msix_entry, vecs);
#else
            vec_count = pci_enable_msix(fnic->pdev, fnic->msix_entry, vecs);
#endif /* FNIC_USE_PCI_ENABLE_MSIX */

            printk(KERN_ERR "pci_enable_msix returned %d requested:%d\n", vec_count, vecs);
            if (vec_count < 0) {
                printk(KERN_ERR "Failed to allocate MSIX, falling back to MSI\n");
                return 1;
            } else if (vec_count == 0)
                vec_count = vecs; //for sles12-sp3
#endif /* FNIC_HAVE_PCI_IRQ_VECTOR */

            if (vec_count > 0) {

                if (vec_count < vecs) {
                    printk(KERN_DEBUG "FW sent fewer then promised MSI-X interrupts\n");
                    if (vec_count < min_irqs) {
                        printk(KERN_DEBUG "no interrupts for copy wq\n");
                        return 1;
                    }
                }

                fnic->rq_count = n;
                fnic->raw_wq_count = m;
                fnic->cpy_wq_base = fnic->rq_count + fnic->raw_wq_count;
                fnic->wq_copy_count = vec_count - n - m - 1;
                fnic->wq_count = fnic->raw_wq_count + fnic->wq_copy_count;
                if (fnic->cq_count != vec_count - 1) {
                    printk(KERN_DEBUG "CQ count not matching MSI-X vectors %d:%d\n",fnic->cq_count,vec_count);
                    fnic->cq_count = vec_count - 1;
                }
                fnic->intr_count = vec_count;
                fnic->err_intr_offset = fnic->rq_count + fnic->wq_count;

                printk(KERN_DEBUG
                        "Using MSI-X Interrupts\n");
                printk(KERN_ERR "fnic_set_intr_mode MSIX\n");
                vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_MSIX);
                printk(KERN_DEBUG "fnic using MSI-X \n");
                return 0;
            }
        }
        return 1;
} //fnic_set_intr_mode_msix


int fnic_try_intr_mode_intx(struct fnic *fnic)
{
        int ret_status = 0;

        ret_status = fnic_set_intr_mode_intx(fnic);
        if(ret_status != 0) {
            printk(KERN_INFO "fnic NOT using INTX. Setting intr mode to unknown\n");
            vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);
        }
        return ret_status;
} // fnic_try_intr_mode_intx

int fnic_try_intr_mode_msi(struct fnic *fnic)
{
        int ret_status = 0;

        ret_status = fnic_set_intr_mode_msi(fnic);
        if(ret_status != 0) {
            printk(KERN_INFO "fnic NOT using MSI. Trying INTx\n");
            ret_status = fnic_try_intr_mode_intx(fnic);
        }
        return ret_status;
}//fnic_try_intr_mode_msi

int fnic_try_intr_mode_msix(struct fnic *fnic)
{
        int ret_status = 0;

        ret_status = fnic_set_intr_mode_msix(fnic);
        if(ret_status != 0) {
            printk(KERN_INFO "fnic NOT using MSIX. Trying MSI\n");
            ret_status = fnic_try_intr_mode_msi(fnic);
        }
        return ret_status;

}//fnic_try_intr_mode_msix


int fnic_set_intr_mode(struct fnic *fnic)
{
	int ret_status = 0;

	printk(KERN_INFO "fnic_set_intr_mode: intr mode: %d\n", fnic->config.intr_mode);
	switch (fnic->config.intr_mode) {
            default:
                 ret_status = fnic_try_intr_mode_msix(fnic);
                 break;
            case VNIC_DEV_INTR_MODE_UNKNOWN:
                 ret_status = fnic_try_intr_mode_msix(fnic);
                 break;
            case VNIC_DEV_INTR_MODE_MSIX:
                 ret_status = fnic_try_intr_mode_msix(fnic);
                 break;
            case VNIC_DEV_INTR_MODE_MSI:
                 ret_status = fnic_try_intr_mode_msi(fnic);
                 break;
            case VNIC_DEV_INTR_MODE_INTX:
                 ret_status = fnic_try_intr_mode_intx(fnic);
                 break;
        } //end switch
        return ret_status;
}//fnic_set_intr_mode

void fnic_clear_intr_mode(struct fnic *fnic)
{
#ifdef NEW_PCI_INTERFACE
	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		pci_disable_msix(fnic->pdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		pci_disable_msi(fnic->pdev);
		break;
	default:
		break;
	}
#else
	pci_free_irq_vectors(fnic->pdev);
#endif /*NEW_PCI_INTERFACE*/
	vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_INTX);
}

