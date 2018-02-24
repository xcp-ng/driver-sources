RPM_VERSION     := 2.0.0.85
RPM_RELEASE     := 220.0
FNIC_XS_VERSION := 8.0

SPEC            := cisco-fnic.spec
ARCH            := x86_64

RPM_DEFINE      := --define "_topdir ${FNIC_RPM_ROOT}"

RPMSOURCES      := $(shell rpm ${RPM_DEFINE} --eval %{_sourcedir})
RPMDIR          := $(shell rpm ${RPM_DEFINE} --eval %{_rpmdir})
KERNEL_VERSION  := $(subst xen,,$(shell uname -r))

ifneq ($(FNIC_XS_VERSION),)
XS_RELEASE      := $(FNIC_XS_VERSION)
RPMS            := cisco-fnic-xs$(XS_RELEASE)-$(KERNEL_VERSION)-modules-$(RPM_VERSION)-$(RPM_RELEASE)
else
XS_RELEASE      := xs@XS_MAJOR@.@XS_MINOR@
RPMS            := cisco-fnic-$(XS_RELEASE)-$(KERNEL_VERSION)-modules-$(RPM_VERSION)-$(RPM_RELEASE)

endif

RPM_FILES := $(patsubst %,$(RPMDIR)/$(ARCH)/%.$(ARCH).rpm, $(RPMS))

.PHONY: default clean

default: build-rpms

prepare: clean
ifeq ($(XS_RELEASE), unsupported)
	@echo "fnic driver disk cannot be built for this unsupported distribution"
	exit 1
endif

build-rpms: prepare
	rpmbuild -bb ${RPM_DEFINE} --define "xs_release xs$(XS_RELEASE)" --define "kernel_version $(KERNEL_VERSION)" --define "version $(RPM_VERSION)" --define "release $(RPM_RELEASE)" $(SPEC);
	@for rpm in '$(RPM_FILES)'; do \
		if [ -f $$rpm ] ; \
		then \
			echo -e "\n `basename $$rpm` built successfully..." ; \
		else \
			echo -e "\n `basename $$rpm` build failed..." ; \
			exit 1; \
		fi \
	done
	mkdir -p rpms/$(ARCH)
	cp -fp $(RPM_FILES) rpms/$(ARCH)
	echo -e "\nfnic rpms are available under the rpms/ directory...\n"

clean:
	rm -rf rpms

distclean: clean
