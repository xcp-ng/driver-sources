QED_DIR := ${PWD}/qed-8.42.10.0/src/
QEDE_DIR := ${PWD}/qede-8.42.10.0/src/
QEDR_DIR := ${PWD}/qedr-8.42.10.0/src/
QEDF_DIR := ${PWD}/qedf-8.42.10.0
QEDI_DIR := ${PWD}/qedi-8.42.10.0
LIBQEDR_DIR := ${PWD}/libqedr-8.42.0.0/
SUBDIRS := $(QED_DIR) $(QEDE_DIR) $(QEDR_DIR) $(QEDF_DIR) $(QEDI_DIR)
export QED_DIR
export QEDE_DIR

UBUNTU_DISTRO := $(shell lsb_release -is 2> /dev/null | grep Ubuntu)
ifeq ($(UBUNTU_DISTRO),)
    LIBQEDR_CONFIGURE_CMD := ./configure --prefix=/usr --libdir=${exec_prefix}/lib64 --sysconfdir=/etc
else
    LIBQEDR_CONFIGURE_CMD := ./configure --prefix=/usr --libdir=${exec_prefix}/lib --sysconfdir=/etc
endif

.PHONY: subsystem install clean libqedr libqedr_install

subsystem:
	@for dir in $(SUBDIRS); do		\
		$(MAKE) -C $$dir || exit 1;	\
	done

install: subsystem
	@for dir in $(SUBDIRS); do			\
		$(MAKE) -C $$dir install || exit 1;	\
	done

clean:
	@for dir in $(SUBDIRS); do			\
		$(MAKE) -C $$dir clean || exit 1;	\
	done

libqedr:
	@(cd $(LIBQEDR_DIR) && exec $(LIBQEDR_CONFIGURE_CMD)) && $(MAKE) -C $(LIBQEDR_DIR)

libqedr_install:
	@(cd $(LIBQEDR_DIR) && exec $(LIBQEDR_CONFIGURE_CMD)) && $(MAKE) -C $(LIBQEDR_DIR) install
