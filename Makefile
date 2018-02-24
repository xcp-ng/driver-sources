#
# Copyright 2008-2019 Cisco Systems, Inc.  All rights reserved.
# Copyright 2007 Nuova Systems, Inc.  All rights reserved.
#
# This program is free software; you may redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

KDIR=/lib/modules/4.19.0+1/build
LINUX_DISTRO=CitrixHypervisor
FNIC_VERSION=2.0.0.89
PACKAGE_NAME=fnic
PACKAGE_VERSION=2.0.0.89-243.0
LINUX_DISTRO_DISK=xs80
VIC_EXTRA_KCFLAGS=-mindirect-branch-register -mindirect-branch=thunk-inline 
TAR_BALL=fnic-${FNIC_VERSION}.tar.bz2
TAR_DIR=fnic-${FNIC_VERSION}
VIC_SWIMS_TICKET_FILE=
VIC_SWIMS_KEY_TYPE=
VIC_SWIMS_SIGNING_SCRIPT=
VIC_SWIMS_PYTHON=

# Verbosity output
VIC_V_SIGN = $(vic__v_SIGN_$V)
vic__v_SIGN_ = $(vic__v_SIGN_0)
vic__v_SIGN_0 = @echo "  VIC SIGN [M]" $@;

module=$(PACKAGE_NAME).ko

abs_srcdir = ${M}
abs_builddir = ${M}

obj-$(CONFIG_FCOE_FNIC) += fnic.o
fnic-objs := \
       fip.o \
       fdls_if.o \
       fdls_disc.o \
       fnic_attrs.o \
       fnic_debugfs.o \
       fnic_isr.o \
       fnic_main.o \
       fnic_nvme.o \
       fnic_res.o \
       fnic_scsi.o \
       fnic_trace.o \
       vnic_cq.o \
       vnic_dev.o \
       vnic_intr.o \
       vnic_rq.o \
       vnic_wq_copy.o \
       vnic_wq.o \
       fnic_pci_subsys_devid.o

CPPFLAGS += $(addprefix -I ,$(INCLUDE_DIRECTORIES))
ccflags-y := $(ccflags-y) $(CPPFLAGS) $(VIC_EXTRA_KCFLAGS)

CORE_SOURCE_FILES := \
        cq_desc.h \
        cq_enet_desc.h \
        cq_exch_desc.h \
        fdls_disc.c \
        fdls_fc.h \
        fdls_if.c \
        fcpio.h \
        fip.c \
        fip.h \
        fnic_attrs.c \
        fnic_config_bottom.h \
        fnic_debugfs.c \
        fnic_fdls.h \
        fnic_fip.h \
        fnic.h \
        fnic_ioctl.c \
        fnic_ioctl.h \
        fnic_io.h \
        fnic_isr.c \
        fnic_main.c \
        fnic_nvme.c \
        fnic_procfs.c \
        fnic_res.c \
        fnic_res.h \
        fnic_scsi.c \
        fnic_stats.h \
        fnic_tag_map.c \
        fnic_tag_map.h \
        fnic_timeconv.c \
        fnic_time.h \
        fnic_trace.c \
        fnic_trace.h \
        rq_enet_desc.h \
        vnic_cq.c \
        vnic_cq_copy.h \
        vnic_cq.h \
        vnic_dev.c \
        vnic_devcmd.h \
        vnic_dev.h \
        vnic_intr.c \
        vnic_intr.h \
        vnic_nic.h \
        vnic_resource.h \
        vnic_rq.c \
        vnic_rq.h \
        vnic_scsi.h \
        vnic_stats.h \
        vnic_wq.c \
        vnic_wq_copy.c \
        vnic_wq_copy.h \
        vnic_wq.h \
        wq_enet_desc.h \
        fnic_pci_subsys_devid.c

AUTOGEN_FILES := \
	config/config.guess \
	config/config.sub \
	config/install-sh

AUTOCONF_SRC_FILES := \
	configure.ac \
	aclocal.m4 \
	vic-common.m4 \
	Makefile.in \
	make_package.sh \
	fnic_config.h.in \
	package-rhel/fnic.spec.in \
	package-rhel/ddiskit/Makefile.in \
	package-oel/fnic.spec.in \
        package-oel/ddiskit/Makefile.in \
	package-sles/cisco-fnic.spec.in \
	package-sles/Makefile.in \
	package-xs/cisco-fnic.spec.in \
	package-xs/Makefile.in

EXTRA_DIST := \
	LICENSE \
	version.sh

CLEANFILES := \
	*.o .*.cmd *.ko *.ko.unsigned \
	fnic.mod.c \
	Module.symvers \
	package-*/*spec \
	package-*/Makefile \
	fnic-${FNIC_VERSION}-${LINUX_DISTRO_DISK} \
	${TAR_BALL} \
	${TAR_DIR}

DISTCLEAN_FILES := \
	Makefile \
	config.log \
	config.status \
	fnic_config.h \
	.tmp_versions

WDIR := $(shell pwd)

SRC_TAR_FILES := \
	${AUTOGEN_FILES} \
	${CORE_SOURCE_FILES} \
	${EXTRA_DIST} \
	configure

.PHONY: default
default: all

.PHONY: all
all: modules

.PHONY: modules
modules: $(module)

# Makes sym links for VPATH builds
.PHONY: src_symlinks
src_symlinks:
	@ if test $(abs_srcdir) != $(abs_builddir); then \
		for f in $(SRC_TAR_FILES); do \
			dir=$$(dirname $$f); \
			if test -n "$$dir"; then \
				mkdir -p $(abs_builddir)/$$dir; \
			fi; \
			if test ! -r $$f; then \
				ln -sf $(abs_srcdir)/$$f $(abs_builddir)/$$f; \
			fi; \
		done; \
	fi

$(SRC_TAR_FILES): src_symlinks

$(module): ${SRC_TAR_FILES}
	$(MAKE) -C ${KDIR} M=${WDIR} modules
	@if test -n "$(VIC_SWIMS_TICKET_FILE)"; then \
	    $(MAKE) vic-sign; \
	fi

.PHONY: vic-sign
vic-sign:
	@if test -z "$(VIC_SWIMS_SIGNING_SCRIPT)"; then \
	     echo "This build not configured for signing support."; \
	     exit 1; \
	fi
	@rm -f $(module).signed
	$(VIC_V_SIGN) \
	 dir=`readlink -f $(VIC_SWIMS_SIGNING_SCRIPT)`; \
	 dir=`dirname $$dir`; \
	 export PYTHONPATH=$$dir:$$PYTHONPATH; \
	 $(VIC_SWIMS_PYTHON) $(VIC_SWIMS_SIGNING_SCRIPT) \
		--ImageName=$(module) \
		--TicketFile=$(VIC_SWIMS_TICKET_FILE) \
		--KeyType=$(VIC_SWIMS_KEY_TYPE)
	 @mv $(module).signed $(module)

.PHONY: install
install:
	$(MAKE) -C ${KDIR} M=${WDIR} modules_install
	sudo /sbin/depmod -a >/dev/null

.PHONY: package
package: dist
	${WDIR}/make_package.sh ${LINUX_DISTRO_DISK} ${PACKAGE_VERSION} \
	4.19.0+1

.PHONY: clean
clean:
	rm -rf ${CLEANFILES} &> /dev/null

.PHONY: packclean
packclean:
	rm -rf	fnic-${FNIC_VERSION}-${LINUX_DISTRO_DISK} \
	fnic-${FNIC_VERSION}-${LINUX_DISTRO_DISK}.tar.bz2

.PHONY: distclean
distclean: clean packclean
	rm -rf ${DISTCLEAN_FILES} &> /dev/null

.PHONY: help
help:
	@echo 'Targets'
	@echo '-------------------------------------------------------------------------------'
	@echo '  all			- Build fnic.ko'
	@echo '  install		- Install fnic.ko'
	@echo '  dist	         	- Build source tarball'
	@echo '  package	        - Build fnic rpm and iso'
	@echo '  clean			- Remove all compiled files'
	@echo '  distclean		- Make tree just like it was extracted from tarball'
	@echo '-------------------------------------------------------------------------------'

.PHONY: dist
dist:
	rm -rf ${TAR_BALL} ${TAR_DIR}
	mkdir -p ${TAR_DIR}
	@for f in ${SRC_TAR_FILES} ${AUTOCONF_SRC_FILES}; do \
		dir=$$(dirname $$f); \
		file=$$(basename $$f); \
		if test -n $$dir; then \
			mkdir -p ${TAR_DIR}/$$dir; \
		fi;\
		if test ! -e ${TAR_DIR}/$$dir/$$f; then \
			cp $$f ${TAR_DIR}/$$dir; \
		fi \
	done
	cd ${TAR_DIR}; \
	rm -f ${CLEANFILES}; \
	sed -i \
	    -e '/\[Insert appropriate license here/r LICENSE' \
	    -e '/\[Insert appropriate license here/d' ${SRC_TAR_FILES}
	tar cmjf ${TAR_BALL} ${TAR_DIR}
	rm -rf ${TAR_DIR}
