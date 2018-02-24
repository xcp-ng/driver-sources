#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Fibre Channel Host Bus Adapters.                                *
# * Copyright (C) 2017-2024 Broadcom. All Rights Reserved. The term *
# * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
# * Copyright (C) 2004-2012 Emulex.  All rights reserved.           *
# * EMULEX and SLI are trademarks of Emulex.                        *
# * www.broadcom.com                                                *
# *                                                                 *
# * This program is free software; you can redistribute it and/or   *
# * modify it under the terms of version 2 of the GNU General       *
# * Public License as published by the Free Software Foundation.    *
# * This program is distributed in the hope that it will be useful. *
# * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
# * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
# * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
# * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
# * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
# * more details, a copy of which can be found in the file COPYING  *
# * included with this package.                                     *
# *******************************************************************/
######################################################################

SUBLEVEL = $(shell echo ${KERNELVERSION} | \
sed -e 's/[0-9]*.[0-9]*.\([0-9]*\).[0-9]*.*/\1/')
EXTRAVERSION = $(shell echo ${KERNELRELEASE} | \
sed -e 's/[0-9]*.[0-9]*.[0-9]*.\([0-9]*\).*/\1/')
MAJORVERSION = $(shell echo ${KERNELVERSION} | \
sed -e 's/\([0-9]*\).*/\1/')
MINORVERSION = $(shell echo ${KERNELVERSION} | \
sed -e 's/[0-9]*.\([0-9]*\).*/\1/')

EXTRA_CFLAGS += -Werror -DKERNEL_MAJOR=${MAJORVERSION} -DKERNEL_MINOR=${MINORVERSION}

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
endif

#
# DISTRO	KERNELVERSION		    SUBLEVEL	EXTRAVERSION
# sles15-sp6	6.4.0-150600.21-default
# sles15-sp5	5.14.21-150500.53.2
# sles15-sp4	5.14.21-150400.22.1
# sles12-sp5	4.12.14-120                     14	120
# sles12-sp4	4.12.14-94.41.1                 14	94
# rhel8.10	4.18.0-553.el8_10               0	553
# rhel8.9	4.18.0-513.5.1.el8_9            0	513
# rhel8.8	4.18.0-477.10.1.el8_8           0	477
# rhel9.5	5.14.0-503.11.1.el9_5           0	503
# rhel9.4	5.14.0-427.13.1.el9_4           0	427
# rhel9.3	5.14.0-362.8.1.el9_3            0	362
#

ifeq ($(MAJORVERSION),3)
ifeq ($(MINORVERSION),10)
   EXTRA_CFLAGS += -DBUILD_3_10_KERN
endif
endif

ifeq ("$(wildcard /etc/debian_version)","")
   EXTRA_CFLAGS += -DBUILD_NVME
endif

# This will pick out a Citrix kernel, xs5.6 / xs6.0 / xs6.1
EXTRAXSINFO = $(shell echo ${KERNELVERSION} | \
grep 'xs[156].[016]' | sed -e 's/.*xs.*/xs/')

ifeq ($(EXTRAXSINFO),xs)
   EXTRA_CFLAGS += -DBUILD_CITRIX_XS
endif

EXTRAXSINFO = $(shell echo ${KERNELVERSION} | \
grep 'el8' | sed -e 's/.*el8.*/el8/')

EXTRARHEL9 = $(shell echo ${KERNELVERSION} | \
grep 'el9' | sed -e 's/.*el9.*/el9/')

ifeq ($(EXTRAXSINFO),el8)
   EXTRA_CFLAGS += -DBUILD_RHEL8 -DBUILD_MQ_ONLY
   RHEL8_NZ = $(shell echo "${KERNELVERSION} 4.18.0-83.el8.x86_64" | \
                tr " " "\n" | sort -V | head -n1)
ifeq ($(RHEL8_NZ), 4.18.0-83.el8.x86_64)
   EXTRA_CFLAGS += -DBUILD_RHEL8_NZ
endif
ifeq ($(shell test $(EXTRAVERSION) -ge 211; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_RHEL83
endif
# RHEL 8.4 adds NVME1+ Addendum
ifeq ($(shell test $(EXTRAVERSION) -ge 259; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif
ifeq ($(shell test $(EXTRAVERSION) -ge 293; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_FPIN_STATS
endif
# RHEL 8.5 adds nvmet discovery event API
ifeq ($(shell test $(EXTRAVERSION) -ge 348; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_DISC_EVENT_API
endif
# Versions above RHEL 8.5 add VMID support
ifeq ($(shell test $(EXTRAVERSION) -gt 348; echo $$?), 0)
   EXTRA_CFLAGS += -DVMID_SUPPORT
endif
# RHEL 8.7 includes SCSI mid layer PI interface
ifeq ($(shell test $(EXTRAVERSION) -ge 419; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL
endif
# RHEL 8.9 started new fc_host_fpin_rcv API
ifeq ($(shell test $(EXTRAVERSION) -ge 500; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_LINK_FPIN_ACK
endif
endif
# RHEL 9.0 has a 5.14 based kernel
ifeq ($(EXTRARHEL9),el9)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS -DBUILD_RHEL9 -DVMID_SUPPORT
# RHEL 9.1 includes SCSI mid layer PI interface
ifeq ($(shell test $(EXTRAVERSION) -ge 148; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL -DBUILD_SCSI_EH_REWORK
endif
# RHEL 9.3 started new fc_host_fpin_rcv API
ifeq ($(shell test $(EXTRAVERSION) -ge 332; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_LINK_FPIN_ACK
endif
# RHEL 9.4 enables native AER
ifeq ($(shell test $(EXTRAVERSION) -ge 408; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_AUTO_ENABLE_NATIVE_AER
endif

endif

ifneq ("$(wildcard /etc/os-release)","")
   SLES15SP = $(shell grep -q 'sles:15:sp' /etc/os-release && echo 1)
ifeq ($(SLES15SP),1)
   EXTRA_CFLAGS += -DBUILD_SLES15SP
#  Starting in SLES15SP2, NVME1+ Addendum is added
   NVME_PLUS = $(shell echo "${KERNELRELEASE} 5.3.18-22" | \
                 tr " " "\n" | sort -V | head -n1)
ifeq ($(NVME_PLUS), 5.3.18-22)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif
# Some sles15sp3 MU kernels adopted the SCSI mid layer PI interface
   SLES15SP3 = $(shell grep -q 'sles:15:sp3' /etc/os-release && echo 1)
ifeq ($(SLES15SP3),1)
ifeq ($(shell test $(EXTRAVERSION) -ge 150300; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL
endif
endif
   SLES15SP4 = $(shell grep -q 'sles:15:sp4' /etc/os-release && echo 1)
ifeq ($(SLES15SP4),1)
# use SCSI mid layer PI
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL -DVMID_SUPPORT
endif
   SLES15SP5 = $(shell grep -q 'sles:15:sp5' /etc/os-release && echo 1)
ifeq ($(SLES15SP5),1)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL -DBUILD_DC_SCSI_DONE \
		   -DBUILD_USE_ATTR_GRP -DBUILD_USE_STRUCT_GRP \
		   -DBUILD_SCSI_EH_REWORK -DVMID_SUPPORT
endif
endif
   SLES12SP5 = $(shell grep -q 'sles:12:sp5' /etc/os-release && echo 1)
ifeq ($(SLES12SP5),1)
   EXTRA_CFLAGS += -DBUILD_SLES12SP5
# Some sles12sp5 MU kernels adopted the SCSI mid layer PI interface
ifeq ($(shell test $(EXTRAVERSION) -eq 122; echo $$?), 0)
EXTRAMUVERSION = $(shell echo ${KERNELRELEASE} | \
sed -e 's/[0-9]*.[0-9]*.[0-9]*-[0-9]*.\([0-9]*\).*/\1/')
ifeq ($(shell test $(EXTRAMUVERSION) -ge 91; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI_AVAIL
endif
endif
endif
endif

# This will pick out an openEuler kernel
EXTRAOE1INFO = $(shell echo ${KERNELVERSION} | grep oe1 | \
	         sed -e 's/.*oe1.*/oe1/')
ifeq ($(EXTRAOE1INFO),oe1)
ifneq ("$(wildcard /etc/os-release)","")
OE1_20 = $(shell grep -q 'VERSION="20.' /etc/os-release && echo 1)
ifeq ($(OE1_20),1)
   EXTRA_CFLAGS += -DBUILD_OE1_20
endif
endif
endif

# Selections for Ubuntu kernel
ifneq ("$(wildcard /etc/lsb-release)","")
# Identify Ubuntu release 21.10 (5.13.0-22)
   UB2110 = $(shell grep -q 'Ubuntu 21.10' /etc/os-release && echo 1)
ifeq ($(UB2110),1)
   EXTRA_CFLAGS += -DBUILD_UB2110 -DBUILD_NVME_PLUS
endif
endif

# Selections for Kylin kernel
ifneq ("$(wildcard /etc/kylin-release)","")
# Identify Kylin V10 release
# SP1 'Tercel'
# SP2 'Sword'
# SP3 'Lance'
# SP3-2403 'Halberd'
# SP4 'Lithium'
   K10SP3 = $(shell grep -q 'Lance' /etc/kylin-release && echo 1)
ifeq ($(K10SP3),1)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif
   K10SP3-2403 = $(shell grep -q 'Halberd' /etc/kylin-release && echo 1)
ifeq ($(K10SP3-2403),1)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS -DBUILD_MQ_ONLY
endif
   K10SP4 = $(shell grep -q 'Lithium' /etc/kylin-release && echo 1)
ifeq ($(K10SP4),1)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS -DBUILD_MQ_ONLY
endif
endif

# Identify UnionTech release
ifneq ("$(wildcard /etc/UnionTech-release)","")
   UOS20 = $(shell grep -q 'UOS Server release 20' /etc/UnionTech-release && echo 1)
ifeq ($(UOS20),1)
ifneq ("$(wildcard /etc/os-version)","")
   UOS1070 = $(shell grep -q 'MinorVersion=1070' /etc/os-version && echo 1)
ifeq ($(UOS1070),1)
   UOS1070E = $(shell grep -q 'EditionName=e' /etc/os-version && echo 1)
ifeq ($(UOS1070E),1)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif # UOS1070e
endif # UOS1070
endif # os-version
endif # UOS20
endif # UnionTech-release

# For other distros, by kernel version -
# NVME target discovery event API added in 5.3
# NVMe1+ addendum was adopted in 5.8
# Use of SCSI mid layer PI interface was adopted in 5.15
# Use of attribute groups and direct calling scsi_done() introduced in 5.16
# Kernel 5.17 provides new interfaces for affinity hint and scsi "eh rework"
# PCI-AER error reporting is auto enabled, when available natively, in 6.0
# FPIN event acknowledge API added in 6.3
ifeq ($(shell test $(MAJORVERSION) -ge 6; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS -DBUILD_USE_ML_PI -DBUILD_USE_ML_PI_AVAIL \
	-DBUILD_USE_ATTR_GRP  -DBUILD_DC_SCSI_DONE -DBUILD_USE_STRUCT_GRP \
	-DBUILD_NO_IRQ_HINT  -DBUILD_SCSI_EH_REWORK -DBUILD_DISC_EVENT_API \
	-DBUILD_AUTO_ENABLE_NATIVE_AER -DBUILD_MQ_ONLY -DVMID_SUPPORT
ifeq ($(shell test $(MINORVERSION) -ge 3; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_LINK_FPIN_ACK
endif
else
ifeq ($(shell test $(MAJORVERSION) -eq 5; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_MQ_ONLY
ifeq ($(shell test $(MINORVERSION) -ge 3; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_DISC_EVENT_API
endif
ifeq ($(shell test $(MINORVERSION) -ge 8; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif
ifeq ($(shell test $(MINORVERSION) -ge 15; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ML_PI -DBUILD_USE_ML_PI_AVAIL -DVMID_SUPPORT
endif
ifeq ($(shell test $(MINORVERSION) -ge 16; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_USE_ATTR_GRP -DBUILD_DC_SCSI_DONE \
	-DBUILD_USE_STRUCT_GRP
endif
ifeq ($(shell test $(MINORVERSION) -ge 17; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_NO_IRQ_HINT -DBUILD_SCSI_EH_REWORK
endif
endif
endif

# This will pick out a xen kernel
EXTRAXENINFO = $(shell echo ${KERNELVERSION} | \
grep 'xen' | sed -e 's/.*xen.*/xen/')

EXTRA_CFLAGS += ${BUILD_FLAGS}

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-objs := lpfc_mem.o lpfc_sli.o lpfc_ct.o lpfc_els.o \
	lpfc_hbadisc.o	lpfc_init.o lpfc_mbox.o lpfc_nportdisc.o   \
	lpfc_scsi.o lpfc_attr.o lpfc_vport.o lpfc_debugfs.o lpfc_bsg.o \
	lpfc_nvme.o lpfc_nvmet.o lpfc_auth.o lpfc_vmid.o
