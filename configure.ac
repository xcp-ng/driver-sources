dnl
dnl Copyright 2019 Cisco Systems, Inc.  All rights reserved.
dnl
dnl This program is free software; you may redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; version 2 of the License.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
dnl EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
dnl MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
dnl NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
dnl BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
dnl ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
dnl CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
dnl SOFTWARE.
dnl
dnl configure.ac script for Linux fnic driver
dnl

AC_PREREQ(2.53)
AC_INIT(fnic, m4_normalize(esyscmd([./version.sh --full])))

AC_CONFIG_AUX_DIR(config)
AC_CANONICAL_SYSTEM
AC_PROG_CC

#
# Find our own version numbers
#
FNIC_VERSION=`$srcdir/version.sh --version`
FNIC_BUILD_ID=`$srcdir/version.sh --build-id`

AC_SUBST(FNIC_VERSION)
AC_SUBST(FNIC_BUILD_ID)

AC_ARG_VAR([KNAME], [If defined, fnic is built against this kernel])
AC_MSG_CHECKING([KNAME value])
u=$(uname -r)
KNAME=${KNAME:-$u}
AC_MSG_RESULT([$KNAME])

AC_ARG_ENABLE(kname-in-pkg-filename,
AS_HELP_STRING([--enable-kname-in-pkg-filename],
	       [enable-kname-in-pkg-filename default: no]),
[case "${enableval}" in
	     yes) KNAME_PKGNAME=$(echo -$KNAME | sed 's/.x86_64//') ;;
	     no)  KNAME_PKGNAME="" ;;
	     *)   AC_MSG_ERROR([bad value ${enableval} for --enable-kname-in-pkg-name]) ;;
esac],
[KNAME_PKGNAME=""])

AC_SUBST([KNAME_PKGNAME])

#
# Find out what linux distribution we are on
#
VIC_LINUX_DISTRO

KDIR=/lib/modules/${KNAME}/build

AC_SUBST(KNAME)
AC_SUBST(KDIR)

AC_CONFIG_HEADER([fnic_config.h])
AH_TOP(
[/*
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

])

AH_BOTTOM([

#include "fnic_config_bottom.h"

#endif /* FNIC_CONFIG_H */
])

#
# Setup crypto signing
#
VIC_SWIMS_SETUP

#
# Does gcc support the retpoline CLI options?
#
VIC_CHECK_GCC_RETPOLINE

#
# Check for various Linux kernel headers
#
VIC_AC_CHECK_HEADER_DEFINE([scsi/scsi_device.h], [FNIC_HAVE_SCSI_DEVICE_H])
VIC_AC_CHECK_HEADER_DEFINE([linux/vmalloc.h], [FNIC_HAVE_VMALLOC_H])

#
# Check for various Linux kernel functions
#
VIC_AC_CHECK_DECL_DEFINE([scsi/libiscsi.h],
    [scsi_change_queue_depth],
    [FNIC_HAVE_SCSI_CHANGE_QUEUE_DEPTH])
VIC_AC_CHECK_DECL_DEFINE([scsi/scsi_transport_fc.h],
    [fc_eh_timed_out],
    [FNIC_HAVE_FC_EH_TIMED_OUT])
VIC_AC_CHECK_DECL_DEFINE([linux/irq.h],
    [irq_get_effective_affinity_mask],
    [FNIC_HAVE_IRQ_GET_EFFECTIVE_AFFINITY_MASK])
VIC_AC_CHECK_DECL_DEFINE([linux/pci.h],
    [pci_enable_msix_exact],
    [FNIC_HAVE_PCI_ENABLE_MSIX_EXACT])
VIC_AC_CHECK_DECL_DEFINE([linux/pci.h],
    [pci_enable_msix],
    [FNIC_HAVE_PCI_ENABLE_MSIX])
VIC_AC_CHECK_DECL_DEFINE([linux/time.h],
    [time64_to_tm],
    [FNIC_HAVE_TIME64_TO_TM])
VIC_AC_CHECK_DECL_DEFINE([linux/time.h],
    [time_to_tm],
    [FNIC_HAVE_TIME_TO_TM])
VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>],
    [ktime_get_real_ts64],
    [FNIC_HAVE_KTIME_GET_REAL_TS64])
VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>],
    [current_kernel_time],
    [FNIC_HAVE_CURRENT_KERNEL_TIME])
VIC_AC_CHECK_DECL_DEFINE([linux/kernel.h],
    [kstrtoul],
    [FNIC_HAVE_KSTRTOUL])
VIC_AC_CHECK_DECL_DEFINE([linux/kernel.h],
    [strict_strtoul],
    [FNIC_HAVE_STRICT_STRTOUL])
VIC_AC_CHECK_DECL_DEFINE([linux/etherdevice.h],
    [compare_ether_addr],
    [FNIC_HAVE_COMPARE_ETHER_ADDR])
VIC_AC_CHECK_DECL_DEFINE([linux/etherdevice.h],
    [ether_addr_equal],
    [FNIC_HAVE_ETHER_ADDR_EQUAL])
VIC_AC_CHECK_DECL_DEFINE([linux/timer.h],
    [timer_setup],
    [FNIC_HAVE_TIMER_SETUP])
VIC_AC_CHECK_DECL_DEFINE([linux/timer.h],
    [setup_timer],
    [FNIC_HAVE_SETUP_TIMER])
VIC_AC_CHECK_DECL_DEFINE([linux/pci.h],
    [pci_irq_vector],
    [FNIC_HAVE_PCI_IRQ_VECTOR])
VIC_AC_CHECK_DECL_DEFINE([scsi/scsi_tcq.h],
    [scsi_activate_tcq],
    [FNIC_HAVE_SCSI_TCQ])
VIC_AC_CHECK_DECL_DEFINE([scsi/scsi_host.h],
    [shost_use_blk_mq],
    [FNIC_HAVE_SHOST_USE_BLK_MQ])
VIC_AC_CHECK_DECL_DEFINE([scsi/scsi_host.h],
    [scsi_cmd_get_serial],
    [FNIC_HAVE_SCSI_GET_SERIAL])
VIC_AC_CHECK_DECL_DEFINE([scsi/scsi_cmnd.h],
    [scsi_cmd_to_rq],
    [FNIC_HAVE_SCSI_CMD_TO_RQ])
VIC_AC_CHECK_DECL_DEFINE([linux/jiffies.h],
    [jiffies_to_timespec64],
    [FNIC_HAVE_JIFFIES_TO_TIMESPEC64])
VIC_AC_CHECK_DECL_DEFINE([linux/time.h],
    [timespec64_sub],
    [FNIC_HAVE_TIMESPEC64_SUB])
VIC_AC_CHECK_DECL_DEFINE([linux/time.h],
    [timespec_sub],
    [FNIC_HAVE_TIMESPEC_SUB])
dnl Do this manually because VIC AC_CHECK_MEMBER cannot handle
dnl more than one header file at a time.
VIC_WRAP_AC([AC_CHECK_MEMBER(
    [struct nvme_fc_ersp_iu.ersp_result],
    [newf=1], [newf=0],
    [#include "linux/nvme.h"
#include "linux/nvme-fc.h"])
AC_DEFINE_UNQUOTED([FNIC_HAVE_ERSP_RESULT], [$newf], [Whether we have nvme_fc_ersp_iu.ersp_result or not])])
VIC_AC_CHECK_MEMBER_DEFINE([scsi/scsi_host.h],
    [struct scsi_host_template.use_clustering],
    [FNIC_HAVE_SCSI_USE_CLUSTERING])
AC_CHECK_FILE(/usr/lib/systemd/system/nvmf-connect.target, [autoconn=1], [autoconn=0])
AC_DEFINE_UNQUOTED(FNIC_HAVE_NVME_AUTOCONNECT, [$autoconn], [Whether nvme does autoconnect or not])
# Is call_usermodehelper in <linux/kmod.h> or <linux/umh.h>?
AC_MSG_NOTICE([looking in <linux/kmod.h>])
VIC_AC_CHECK_DECL_DEFINE([linux/kmod.h],
    [call_usermodehelper],
    [FNIC_CUMH_IN_KMOD_H])
# Reset the Autoconf cache variable between these two checks
AC_MSG_NOTICE([looking in <linux/umh.h>])
unset ac_cv_have_decl_call_usermodehelper
VIC_AC_CHECK_DECL_DEFINE([linux/umh.h],
    [call_usermodehelper],
    [FNIC_CUMH_IN_UMH_H])


VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <uapi/linux/if_ether.h>
#include <scsi/fc/fc_fip.h>],
    [FIP_SC_VL_NOTE],
    [FNIC_HAVE_SC_VL_NOTE])
VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <uapi/linux/if_ether.h>
#include <scsi/fc/fc_fip.h>],
    [FIP_SC_VL_REP],
    [FNIC_HAVE_SC_VL_REP])
VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <scsi/scsi_transport_fc.h>],
    [FC_PORTSPEED_64GBIT],
    [FNIC_HAVE_FC_PORTSPEED_64GBIT])
VIC_AC_CHECK_DECL_INCLUDES_DEFINE([#include <scsi/scsi_transport_fc.h>],
    [FC_PORTSPEED_128GBIT],
    [FNIC_HAVE_FC_PORTSPEED_128GBIT])

#
# Check for various Linux kernel types
#
VIC_AC_CHECK_TYPE_DEFINE([struct fip_vlan_desc],
    [FNIC_HAVE_FIP_VLAN_DESC],
    [#include <uapi/linux/if_ether.h>
#include <scsi/fc/fc_fip.h>])

#
# Check for various Linux kernel structure members
#
VIC_AC_CHECK_MEMBER_DEFINE([scsi/scsi_host.h],
    [struct scsi_host_template.eh_timed_out],
    [FNIC_HAVE_SCSI_HOST_TEMPLATE_EH_TIMED_OUT])
VIC_AC_CHECK_MEMBER_DEFINE([scsi/scsi_host.h],
    [struct scsi_host_template.track_queue_depth],
    [FNIC_HAVE_SCSI_HOST_TEMPLATE_TRACK_QUEUE_DEPTH])
VIC_AC_CHECK_MEMBER_DEFINE([scsi/scsi_host.h],
    [struct scsi_host_template.map_queues],
    [FNIC_HAVE_SCSI_HOST_TEMPLATE_MAP_QUEUES])
VIC_AC_CHECK_MEMBER_DEFINE([linux/blk-mq.h],
    [struct blk_mq_tag_set.mq_map],
    [FNIC_HAVE_BLK_MQ_TAG_SET_MQ_MAP])
VIC_AC_CHECK_MEMBER_DEFINE([linux/nvme-fc-driver.h],
    [struct nvme_fc_port_template.poll_queue],
    [FNIC_HAVE_NVME_FC_TEMPLATE_POLL_QUEUE])

#
# Cleanup the kernel snarfing tree
#
VIC_SETUP_KERNEL_SNARFING_CLEANUP

#
# Files to generate
#
AC_CONFIG_FILES([
    Makefile

    package-rhel/fnic.spec
    package-rhel/ddiskit/Makefile
    package-oel/fnic.spec
    package-oel/ddiskit/Makefile
    package-sles/cisco-fnic.spec
    package-sles/Makefile
    package-xs/cisco-fnic.spec
    package-xs/Makefile
    ])

AC_OUTPUT
