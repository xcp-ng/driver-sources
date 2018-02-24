buildtree=$(PWD)
export buildtree

all:
	make -C /lib/modules/${KVER}/build SUBDIRS=$(PWD)/drivers/net/ethernet/chelsio/cxgb4 modules
	make -C /lib/modules/${KVER}/build SUBDIRS=$(PWD)/drivers/scsi/csiostor modules
	make -C /lib/modules/${KVER}/build SUBDIRS=$(PWD)/drivers/scsi/cxgbi modules

modules_install:
	mkdir -p ${INSTALL_MOD_PATH}/lib/modules/${KVER}/${INSTALL_MOD_DIR}
	cp $(PWD)/drivers/net/ethernet/chelsio/cxgb4/*.ko ${INSTALL_MOD_PATH}/lib/modules/${KVER}/${INSTALL_MOD_DIR}
	cp $(PWD)/drivers/scsi/csiostor/*.ko              ${INSTALL_MOD_PATH}/lib/modules/${KVER}/${INSTALL_MOD_DIR}
	cp $(PWD)/drivers/scsi/cxgbi/*.ko                 ${INSTALL_MOD_PATH}/lib/modules/${KVER}/${INSTALL_MOD_DIR}
	cp $(PWD)/drivers/scsi/cxgbi/cxgb4i/*.ko          ${INSTALL_MOD_PATH}/lib/modules/${KVER}/${INSTALL_MOD_DIR}

clean:
	rm $(PWD)/drivers/net/ethernet/chelsio/cxgb4/*.o
	rm $(PWD)/drivers/net/ethernet/chelsio/cxgb4/*.ko
	rm $(PWD)/drivers/scsi/csiostor/*.ko
	rm $(PWD)/drivers/scsi/csiostor/*.o
	rm $(PWD)/drivers/scsi/cxgbi/*.o
	rm $(PWD)/drivers/scsi/cxgbi/*.ko
	rm $(PWD)/drivers/scsi/cxgbi/cxgb4i/*.o
	rm $(PWD)/drivers/scsi/cxgbi/cxgb4i/*.ko
	
