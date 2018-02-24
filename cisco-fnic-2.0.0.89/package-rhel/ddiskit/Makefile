KVERSIONS = 4.19.0+1
KMOD_REL=CitrixHypervisoru
KARCH := $(shell uname -m)
FSTYPE=ext2

export

.PHONY: all prepare clean

all: prepare
	$(MAKE) -f Build.rules diskiso
	gunzip images/dd.iso.gz
	mv images/dd.iso images/fnic-2.0.0.89-$(KMOD_REL)-dd.iso
	@echo -e "\nfnic driver disk built successfully!"
	@echo -e "ISO is images/fnic-2.0.0.89-$(KMOD_REL)-dd.iso\n"

prepare: clean
ifeq ($(KMOD_REL), unsupported)
	$(error "fnic driver disk cannot be built for this unsupported distribution")
endif
	chown -R $(shell whoami):$(shell id -gn) *

clean:
	$(MAKE) -f Build.rules distclean
