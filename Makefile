#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Fibre Channel Host Bus Adapters.                                *
# * Copyright (C) 2017-2018 Broadcom. All Rights Reserved. The term *
# * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
# * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
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
EXTRAVERSION = $(shell echo ${KERNELVERSION} | \
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

# If the kernel sublevel is 16, 36, or 39, assume UEK R2 and do not check
# any additional extraversion values. Even though the version of the UEK
# kernel is actually 2.6.39-200.*, it reports as 3.0.16-200.* or 3.0.36-300*
# from the KERNELVERSION variable provided by the kernel makefile
#
# DISTRO	KERNELVERSION		    SUBLEVEL	EXTRAVERSION
# rhel6.4	2.6.32-358.el6			32	358
# rhel6.3	2.6.32-279.el6			32	279
# rhel6.2	2.6.32-220.el6			32	220
# rhel6.1	2.6.32-131.0.15.el6		32	131
# rhel7         3.10.0-54.0.1.el7               0       54
# sles11-sp1	2.6.32.12-0.7			32	12
# sles11-sp2	3.0.13-0.27			13	0
# sles11-sp3    3.0.76-0.11                     76      0
# oel6.4        2.6.39-400.17.1.el6uek          39      400
# oel6.3	2.6.39-200.24.1.el6uek		39	200
# oel6.2	2.6.32-300.3.1.el6uek		32	300
# oel6.1	2.6.32-100.34.1.el6uek		32	100
# oel5.9	2.6.39-300.26.1.el5uek		39	300
# oel5.8	2.6.32-300.10.1.el5uek		32	300
# oel5.7	2.6.32-200.13.1.el5uek		32	200
# ovm3.1.1	2.6.39-200.1.1.el5uek		39	200
# xenserver6.1	2.6.32.43-0.4.1.xs1.6.10.734	32	43
# xenserver6.0	2.6.32.12-0.7.1.xs6.0.0.529	32	12
#
ifeq ($(SUBLEVEL),16)
   EXTRA_CFLAGS += -DBUILD_UEK_R2
else ifeq ($(SUBLEVEL),36)
   EXTRA_CFLAGS += -DBUILD_UEK_R2
else ifeq ($(SUBLEVEL),39)
   EXTRA_CFLAGS += -DBUILD_UEK_R2
else ifeq ($(EXTRAVERSION),100)
   EXTRA_CFLAGS += -DBUILD_UEK_R1_100
else ifeq ($(EXTRAVERSION),200)
   EXTRA_CFLAGS += -DBUILD_UEK_R1_200
else ifeq ($(EXTRAVERSION),300)
   EXTRA_CFLAGS += -DBUILD_UEK_R1_300
else ifeq ($(EXTRAVERSION),400)
   EXTRA_CFLAGS += -DBUILD_UEK_R1_400
   EXTRA_CFLAGS += -DBUILD_UEK_R2
else ifeq ($(EXTRAVERSION),12)
   EXTRA_CFLAGS += -DBUILD_SLES11_SPX
else ifeq ($(EXTRAVERSION),36)
   EXTRA_CFLAGS += -DBUILD_SLES11_SPX
else ifeq ($(EXTRAVERSION),43)
   EXTRA_CFLAGS += -DBUILD_CITRIX_TAMPA
else
   EXTRA_CFLAGS += -DBUILD_RHEL6
endif

ifeq ($(MAJORVERSION),3)
ifeq ($(MINORVERSION),10)
   EXTRA_CFLAGS += -DBUILD_3_10_KERN
endif
   EXTRA_CFLAGS += -DDEBUGFS_FLAG
endif

ifeq ($(MAJORVERSION),2)
ifeq ($(MINORVERSION),6)
   EXTRA_CFLAGS += -DDEBUGFS_FLAG
endif
endif

# Determine if the compiler supports retpoline
RETPOLINE_GCC=$(shell gcc -Q --target-help | grep -q mindirect-branch-register && echo 1 || echo 0)
INBOX_RETPOLINE_THUNK_SUP = 0
ifdef CONFIG_RETPOLINE
   INBOX_RETPOLINE_THUNK_SUP = 1
endif

ifneq ("$(wildcard /etc/SuSE-release)","")
   SLES_VERSION = $(shell head -n 1 /etc/SuSE-release | awk '{print $$5}')
ifeq ($(SLES_VERSION),12)
   EXTRA_CFLAGS += -DBUILD_SLES12
endif
endif

ifneq ("$(wildcard /etc/redhat-release)","")
   RHEL_MAJOR_VERSION = $(shell cat /etc/redhat-release | awk -F'.' '{print $$1}' | awk '{print $$NF}')
   IS_SUPPORTED_MINOR = $(shell if [ `cat /etc/redhat-release | awk -F'.' '{print $$2}' | cut -d' ' -f1` -ge 5 ]; then echo "1"; else echo "0"; fi)
ifeq ($(RHEL_MAJOR_VERSION),7)
ifeq ($(IS_SUPPORTED_MINOR),1)
ifeq (,$(findstring -DBUILD_BRCMFCOE,$(BUILD_FLAGS)))
   EXTRA_CFLAGS += -DBUILD_NVME -DCONFIG_NVME_FC -DCONFIG_NVME_TARGET_FC
endif
endif
endif
endif

# Add the retpoline compiler flags only if supported by the compiler
ifeq ($(RETPOLINE_GCC),1)
   EXTRA_CFLAGS += -DENABLE_RETPOLINE
ifeq ($(INBOX_RETPOLINE_THUNK_SUP),0)
   EXTRA_CFLAGS += -mindirect-branch=thunk-inline -mindirect-branch-register
endif
endif

# This will pick out a Citrix kernel, xs5.6 / xs6.0 / xs6.1
EXTRAXSINFO = $(shell echo ${KERNELVERSION} | \
grep 'xs[156].[016]' | sed -e 's/.*xs.*/xs/')

ifeq ($(EXTRAXSINFO),xs)
   EXTRA_CFLAGS += -DBUILD_CITRIX_XS
endif

# This will pick out versions that support setting affinity hints
# rhel6.4, rhel6.3, rhel6.2, rhel6.1
ifeq ($(SUBLEVEL),32)
ifeq ($(EXTRAVERSION),358)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
ifeq ($(EXTRAVERSION),279)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
ifeq ($(EXTRAVERSION),220)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
ifeq ($(EXTRAVERSION),131)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
endif
# oel6.4 oel6.3, ovm3.1.1, oel5.9
ifeq ($(SUBLEVEL),39)
ifeq ($(EXTRAVERSION),200)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
ifeq ($(EXTRAVERSION),300)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
ifeq ($(EXTRAVERSION),400)
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
endif

# This will pick out a asmio kernel
EXTRAASMIOINFO = $(shell echo ${KERNELVERSION} | \
grep 'di' | sed -e 's/.*di.*/di/')

ifeq ($(EXTRAVERSION),400)
ifeq ($(EXTRAASMIOINFO),di)
   EXTRA_CFLAGS += -DBUILD_ASMIO
endif
endif


# This will pick out a xen kernel
EXTRAXENINFO = $(shell echo ${KERNELVERSION} | \
grep 'xen' | sed -e 's/.*xen.*/xen/')

# ALL kernel versions of 3.X or greater, EXCEPT xen, should support this
ifeq ($(MAJORVERSION),3)
ifeq ($(EXTRAXENINFO),xen)
   EXTRA_CFLAGS += -DBUILD_XEN
else
   EXTRA_CFLAGS += -DBUILD_AFFINITY_HINT
endif
endif

EXTRA_CFLAGS += ${BUILD_FLAGS}

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-objs := lpfc_mem.o lpfc_sli.o lpfc_ct.o lpfc_els.o \
	lpfc_hbadisc.o	lpfc_init.o lpfc_mbox.o lpfc_nportdisc.o   \
	lpfc_scsi.o lpfc_attr.o lpfc_vport.o lpfc_debugfs.o lpfc_bsg.o \
	lpfc_nvme.o lpfc_nvmet.o

obj-$(CONFIG_SCSI_BRCMFCOE) := brcmfcoe.o

brcmfcoe-objs := brcmfcoe_mem.o brcmfcoe_sli.o brcmfcoe_ct.o brcmfcoe_els.o brcmfcoe_hbadisc.o	\
	brcmfcoe_init.o brcmfcoe_mbox.o brcmfcoe_nportdisc.o brcmfcoe_scsi.o brcmfcoe_attr.o \
	brcmfcoe_vport.o brcmfcoe_debugfs.o brcmfcoe_bsg.o brcmfcoe_nvme.o brcmfcoe_nvmet.o
