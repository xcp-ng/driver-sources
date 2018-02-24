# SPDX-License-Identifier: GPL-2.0
################################################################################
#
# Driver for Solarflare and Xilinx network controllers and boards
# Copyright 2019 Solarflare Communications Inc.
# Copyright 2019-2020 Xilinx Inc.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, incorporated herein by reference.
#
################################################################################


all modules:
	$(MAKE) -C drivers/net/ethernet/sfc $@
	$(build_selftests)

install modules_install:
	@echo INSTALL_MOD_PATH=$(INSTALL_MOD_PATH)
	@echo INSTALL_MOD_DIR=$(INSTALL_MOD_DIR)
	$(MAKE) -C drivers/net/ethernet/sfc modules_install
	$(build_selftests)
	depmod

export-srpm export:
	$(MAKE) -C drivers/net/ethernet/sfc $@
	$(MAKE) -C include/linux $@
	$(build_selftests)

clean:
	$(MAKE) -C drivers/net/ethernet/sfc $@
	$(RM) -r deb rpm .version
	$(build_selftests)

version:
	@if [ -d .git ] && [ "$(USE_BASE_DRIVER_VERSION)" == "" ]; then \
	  git describe --tags --match v* HEAD | sed 's/^v//; s/_/./g'; \
	else \
	  grep EFX_DRIVER_VERSION drivers/net/ethernet/sfc/net_driver.h | cut -d\" -f2; \
	fi

