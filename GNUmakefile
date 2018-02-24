#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Fibre Channel Host Bus Adapters.                                *
# * Copyright (C) 2021-2022 Broadcom. All Rights Reserved. The term *
# * "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.     *
# * Copyright (C) 2004-2009 Emulex.  All rights reserved.           *
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

KERNELVERSION ?= $(shell uname -r)
BASEINCLUDE ?= /lib/modules/$(KERNELVERSION)/build
DRIVERDIR ?= $(CURDIR)
DEBUG_FS = $(shell grep CONFIG_DEBUG_FS=y $(BASEINCLUDE)/.config)

ifeq ($(DEBUG_FS),CONFIG_DEBUG_FS=y)
  EXTRA_CFLAGS += -DCONFIG_SCSI_LPFC_DEBUG_FS
  export EXTRA_CFLAGS
endif

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
  export EXTRA_CFLAGS
endif

clean-files += Modules.symvers Module.symvers Module.markers modules.order

export clean-files

$(info GNUmakefile setting EXTRA_CFLAGS to $(EXTRA_CFLAGS))
default:
	$(MAKE) -C $(BASEINCLUDE) M=$(DRIVERDIR) CONFIG_SCSI_LPFC=m modules

install:
	@rm -f /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc.ko
	install -d /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	install -c lpfc.ko /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	depmod -a

clean:
	$(MAKE) -C $(BASEINCLUDE) M=$(DRIVERDIR) CONFIG_SCSI_LPFC=m clean
