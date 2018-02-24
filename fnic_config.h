/* fnic_config.h.  Generated from fnic_config.h.in by configure.  */
/* fnic_config.h.in.  Generated from configure.ac by autoheader.  */

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

#ifndef FNIC_CONFIG_H
#define FNIC_CONFIG_H



/* Whether we have declaration call_usermodehelper in <call_usermodehelper> or
   not */
#define FNIC_CUMH_IN_KMOD_H 1

/* Whether we have declaration call_usermodehelper in <call_usermodehelper> or
   not */
#define FNIC_CUMH_IN_UMH_H 1

/* Whether we have member struct blk_mq_tag_set.mq_map in <linux/blk-mq.h> or
   not */
#define FNIC_HAVE_BLK_MQ_TAG_SET_MQ_MAP 1

/* Whether we have declaration compare_ether_addr in <compare_ether_addr> or
   not */
#define FNIC_HAVE_COMPARE_ETHER_ADDR 0

/* Whether we have declaration current_kernel_time in #include <linux/time.h>
   #include <linux/ktime.h> #include <linux/jiffies.h> #include
   <linux/timekeeping.h> or not */
#define FNIC_HAVE_CURRENT_KERNEL_TIME 1

/* Whether we have nvme_fc_ersp_iu.ersp_result or not */
#define FNIC_HAVE_ERSP_RESULT 0

/* Whether we have declaration ether_addr_equal in <ether_addr_equal> or not
   */
#define FNIC_HAVE_ETHER_ADDR_EQUAL 1

/* Whether we have declaration fc_eh_timed_out in <fc_eh_timed_out> or not */
#define FNIC_HAVE_FC_EH_TIMED_OUT 1

/* Whether we have declaration FC_PORTSPEED_128GBIT in #include
   <scsi/scsi_transport_fc.h> or not */
#define FNIC_HAVE_FC_PORTSPEED_128GBIT 1

/* Whether we have declaration FC_PORTSPEED_64GBIT in #include
   <scsi/scsi_transport_fc.h> or not */
#define FNIC_HAVE_FC_PORTSPEED_64GBIT 1

/* Whether we have struct fip_vlan_desc or not */
#define FNIC_HAVE_FIP_VLAN_DESC 1

/* Whether we have declaration irq_get_effective_affinity_mask in
   <irq_get_effective_affinity_mask> or not */
#define FNIC_HAVE_IRQ_GET_EFFECTIVE_AFFINITY_MASK 0

/* Whether we have declaration jiffies_to_timespec64 in
   <jiffies_to_timespec64> or not */
#define FNIC_HAVE_JIFFIES_TO_TIMESPEC64 1

/* Whether we have declaration kstrtoul in <kstrtoul> or not */
#define FNIC_HAVE_KSTRTOUL 1

/* Whether we have declaration ktime_get_real_ts64 in #include <linux/time.h>
   #include <linux/ktime.h> #include <linux/timekeeping.h> or not */
#define FNIC_HAVE_KTIME_GET_REAL_TS64 1

/* Whether nvme does autoconnect or not */
#define FNIC_HAVE_NVME_AUTOCONNECT 0

/* Whether we have member struct nvme_fc_port_template.poll_queue in
   <linux/nvme-fc-driver.h> or not */
#define FNIC_HAVE_NVME_FC_TEMPLATE_POLL_QUEUE 0

/* Whether we have declaration pci_enable_msix in <pci_enable_msix> or not */
#define FNIC_HAVE_PCI_ENABLE_MSIX 0

/* Whether we have declaration pci_enable_msix_exact in
   <pci_enable_msix_exact> or not */
#define FNIC_HAVE_PCI_ENABLE_MSIX_EXACT 1

/* Whether we have declaration pci_irq_vector in <pci_irq_vector> or not */
#define FNIC_HAVE_PCI_IRQ_VECTOR 1

/* Whether we have declaration scsi_change_queue_depth in
   <scsi_change_queue_depth> or not */
#define FNIC_HAVE_SCSI_CHANGE_QUEUE_DEPTH 0

/* Whether we have declaration scsi_cmd_to_rq in <scsi_cmd_to_rq> or not */
#define FNIC_HAVE_SCSI_CMD_TO_RQ 0

/* Whether <scsi/scsi_device.h> exists or not */
#define FNIC_HAVE_SCSI_DEVICE_H 1

/* Whether we have declaration scsi_cmd_get_serial in <scsi_cmd_get_serial> or
   not */
#define FNIC_HAVE_SCSI_GET_SERIAL 1

/* Whether we have member struct scsi_host_template.eh_timed_out in
   <scsi/scsi_host.h> or not */
#define FNIC_HAVE_SCSI_HOST_TEMPLATE_EH_TIMED_OUT 1

/* Whether we have member struct scsi_host_template.map_queues in
   <scsi/scsi_host.h> or not */
#define FNIC_HAVE_SCSI_HOST_TEMPLATE_MAP_QUEUES 1

/* Whether we have member struct scsi_host_template.track_queue_depth in
   <scsi/scsi_host.h> or not */
#define FNIC_HAVE_SCSI_HOST_TEMPLATE_TRACK_QUEUE_DEPTH 1

/* Whether we have declaration scsi_activate_tcq in <scsi_activate_tcq> or not
   */
#define FNIC_HAVE_SCSI_TCQ 0

/* Whether we have member struct scsi_host_template.use_clustering in
   <scsi/scsi_host.h> or not */
#define FNIC_HAVE_SCSI_USE_CLUSTERING 1

/* Whether we have declaration FIP_SC_VL_NOTE in #include
   <uapi/linux/if_ether.h> #include <scsi/fc/fc_fip.h> or not */
#define FNIC_HAVE_SC_VL_NOTE 1

/* Whether we have declaration FIP_SC_VL_REP in #include
   <uapi/linux/if_ether.h> #include <scsi/fc/fc_fip.h> or not */
#define FNIC_HAVE_SC_VL_REP 0

/* Whether we have declaration setup_timer in <setup_timer> or not */
#define FNIC_HAVE_SETUP_TIMER 0

/* Whether we have declaration shost_use_blk_mq in <shost_use_blk_mq> or not
   */
#define FNIC_HAVE_SHOST_USE_BLK_MQ 1

/* Whether we have declaration strict_strtoul in <strict_strtoul> or not */
#define FNIC_HAVE_STRICT_STRTOUL 0

/* Whether we have declaration time64_to_tm in <time64_to_tm> or not */
#define FNIC_HAVE_TIME64_TO_TM 1

/* Whether we have declaration timer_setup in <timer_setup> or not */
#define FNIC_HAVE_TIMER_SETUP 1

/* Whether we have declaration timespec64_sub in <timespec64_sub> or not */
#define FNIC_HAVE_TIMESPEC64_SUB 1

/* Whether we have declaration timespec_sub in <timespec_sub> or not */
#define FNIC_HAVE_TIMESPEC_SUB 1

/* Whether we have declaration time_to_tm in <time_to_tm> or not */
#define FNIC_HAVE_TIME_TO_TM 1

/* Whether <linux/vmalloc.h> exists or not */
#define FNIC_HAVE_VMALLOC_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <linux/netdevice.h> header file. */
#define HAVE_LINUX_NETDEVICE_H 1

/* Define to 1 if you have the <linux/vmalloc.h> header file. */
#define HAVE_LINUX_VMALLOC_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <scsi/scsi_device.h> header file. */
#define HAVE_SCSI_SCSI_DEVICE_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "fnic"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "fnic 2.0.0.89-243.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "fnic"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "2.0.0.89-243.0"

/* defines sles patchlevel if building for a SLES distro */
#define SLES_PATCHLEVEL 0

/* defines sles version if building for a SLES distro */
#define SLES_VERSION 0

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Extra KCFLAGS from VIC_CHECK_GCC_RETPOLINE */
#define VIC_EXTRA_KCFLAGS "-mindirect-branch-register -mindirect-branch=thunk-inline "

/* Whether this driver was compiled with retpoline support or not */
#define VIC_HAVE_RETPOLINE 1



#include "fnic_config_bottom.h"

#endif /* FNIC_CONFIG_H */

