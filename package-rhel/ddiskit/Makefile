KVERSIONS = 4.19.0+1
KMOD_REL=
KARCH := $(shell uname -m)
FSTYPE=ext2

export

.PHONY: all prepare clean

all: prepare
	$(MAKE) -f Build.rules diskiso
	gunzip images/dd.iso.gz
	mv images/dd.iso images/enic-4.5.0.7-$(KMOD_REL)-dd.iso
	@echo -e "\nenic driver disk built successfully!"
	@echo -e "ISO is images/enic-4.5.0.7-$(KMOD_REL)-dd.iso\n"

prepare: clean
ifeq ($(KMOD_REL), unsupported)
	$(error "enic driver disk cannot be built for this unsupported distribution")
endif
	chown -R $(shell whoami):$(shell id -gn) *

clean: 
	$(MAKE) -f Build.rules distclean
