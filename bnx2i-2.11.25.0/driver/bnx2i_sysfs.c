/* bnx2i_sysfs.c: QLogic iSCSI driver.
 *
 * Copyright (c) 2004-2014 Broadcom Corporation
 * Copyright (c) 2014 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 * Maintained by: Eddie Wai (eddie.wai@broadcom.com)
 */

#include "bnx2i.h"

/**
 * bnx2i_dev_to_hba - maps dev pointer to adapter struct
 * @dev:	device pointer
 *
 * Map device to hba structure
 */
static inline struct bnx2i_hba *bnx2i_dev_to_hba(struct device *dev)
{
#if (defined(__RHEL_DISTRO__) && (__RHEL_DISTRO__ < 0x0600))
	/* TODO: is the shost_gendev what we want here?  or
		 do we want the actual class_dev */
	struct Scsi_Host *shost = dev_to_shost(dev);
#else
	struct Scsi_Host *shost = class_to_shost(dev);
#endif
	return iscsi_host_priv(shost);
}

static ssize_t
bnx2i_sysfs_read_subnet(struct file *filep, struct kobject *kobj,
			struct bin_attribute *ba, char *buf, loff_t off,
			size_t count)
{
	struct bnx2i_hba *hba = NULL;
	struct list_head *active_list;
	ssize_t ret = 0;

	if (off != 0)
		return ret;

	hba = iscsi_host_priv(dev_to_shost(container_of(kobj, struct device,
					kobj)));

	active_list = &hba->ep_active_list;
	read_lock_bh(&hba->ep_rdwr_lock);
	if (!list_empty(&hba->ep_active_list)) {
		struct bnx2i_endpoint *bnx2i_ep;
		struct cnic_sock *csk;

		bnx2i_ep = list_first_entry(active_list,
				struct bnx2i_endpoint,
				link);
		csk = bnx2i_ep->cm_sk;
		if (csk->subnet_mask)
			ret = sprintf(buf, "%pI4\n", csk->subnet_mask);
	}
	read_unlock_bh(&hba->ep_rdwr_lock);
	return ret;
}

static ssize_t
bnx2i_sysfs_read_gw(struct file *filep, struct kobject *kobj,
			struct bin_attribute *ba, char *buf, loff_t off,
			size_t count)
{
	struct bnx2i_hba *hba = NULL;
	struct list_head *active_list = NULL;
	ssize_t ret = 0;

	if (off != 0)
		return ret;

	hba = iscsi_host_priv(dev_to_shost(container_of(kobj, struct device,
					kobj)));
	active_list = &hba->ep_active_list;
	read_lock_bh(&hba->ep_rdwr_lock);
	if (!list_empty(&hba->ep_active_list)) {
		struct bnx2i_endpoint *bnx2i_ep;
		struct cnic_sock *csk;

		bnx2i_ep = list_first_entry(active_list,
				struct bnx2i_endpoint,
				link);
		csk = bnx2i_ep->cm_sk;
		if (csk->gateway)
			ret = sprintf(buf, "%pI4\n", csk->gateway);
	}
	read_unlock_bh(&hba->ep_rdwr_lock);
	return ret;
}


static struct bin_attribute sysfs_subnet_attr = {
	.attr = {       
		.name = "subnet",
		.mode = S_IRUSR,
	},
	.size = 0,
	.read = bnx2i_sysfs_read_subnet,
	.write = 0,
};      
        
static struct bin_attribute sysfs_gw_attr = {
	.attr = {       
		.name = "gateway",
		.mode = S_IRUSR,
	},
	.size = 0,
	.read = bnx2i_sysfs_read_gw,
	.write = 0,
};      
        
static struct sysfs_bin_attrs {
	char *name;
	struct bin_attribute *attr;
} bin_file_entries[] = {
	{"subnet", &sysfs_subnet_attr},            
	{"gateway", &sysfs_gw_attr},            
	{NULL}, 
};

int                             
bnx2i_create_sysfs_attr(struct bnx2i_hba *hba)
{
	struct Scsi_Host *host = hba->shost;
	struct sysfs_bin_attrs *iter;
	int ret = 0;

	for (iter = bin_file_entries; iter->name; iter++) {
		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
				iter->attr);
		if (ret)
			pr_err("Unable to create sysfs %s attr, err(%d).\n",
					iter->name, ret);
	}       
	return ret;       
}               
        
void    
bnx2i_remove_sysfs_attr(struct bnx2i_hba *hba)
{
	struct Scsi_Host *host = hba->shost;
	struct sysfs_bin_attrs *iter;
	for (iter = bin_file_entries; iter->name; iter++)
		sysfs_remove_bin_file(&host->shost_gendev.kobj, iter->attr);
}


/**
 * bnx2i_show_sq_info - return(s currently configured send queue (SQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 *
 * Returns current SQ size parameter, this paramater determines the number
 * outstanding iSCSI commands supported on a connection
 */
static ssize_t bnx2i_show_sq_info(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	return sprintf(buf, "0x%x\n", hba->max_sqes);
}


/**
 * bnx2i_set_sq_info - update send queue (SQ) size parameter
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * Interface for user to change shared queue size allocated for each conn
 * Must be within SQ limits and a power of 2. For the latter this is needed
 * because of how libiscsi preallocates tasks.
 */
static ssize_t bnx2i_set_sq_info(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);
	u32 val;
	int max_sq_size;

	if (hba->ofld_conns_active)
		goto skip_config;

	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type))
		max_sq_size = BNX2I_5770X_SQ_WQES_MAX;
	else
		max_sq_size = BNX2I_570X_SQ_WQES_MAX;

	if (sscanf(buf, " 0x%x ", &val) > 0) {
		if ((val >= BNX2I_SQ_WQES_MIN) && (val <= max_sq_size) &&
		    (is_power_of_2(val)))
			hba->max_sqes = val;
	}

	return count;

skip_config:
	printk(KERN_ERR "bnx2i: device busy, cannot change SQ size\n");
	return 0;
}


/**
 * bnx2i_show_ccell_info - returns command cell (HQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 *
 * returns per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_show_ccell_info(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	return sprintf(buf, "0x%x\n", hba->num_ccell);
}


/**
 * bnx2i_get_link_state - set command cell (HQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * updates per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_set_ccell_info(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u32 val;
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	if (hba->ofld_conns_active)
		goto skip_config;

	if (sscanf(buf, " 0x%x ", &val) > 0) {
		if ((val >= BNX2I_CCELLS_MIN) &&
		    (val <= BNX2I_CCELLS_MAX)) {
			hba->num_ccell = val;
		}
	}

	return count;

skip_config:
	printk(KERN_ERR "bnx2i: device busy, cannot change CCELL size\n");
	return 0;
}


/**
 * bnx2i_show_ooo_count - reads the OOO counter
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * updates per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_show_ooo_tx_count(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	if (hba->cnic)
		return sprintf(buf, "0x%llu\n", hba->cnic->ooo_tx_count);
	else
		return -EINVAL;
}

/**
 * bnx2i_reset_ooo_count - reads the OOO counter
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * updates per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_reset_ooo_tx_count(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	if (hba->cnic)
		hba->cnic->ooo_tx_count = 0;

	return 0;
}


static DEVICE_ATTR(sq_size, S_IRUGO | S_IWUSR,
		   bnx2i_show_sq_info, bnx2i_set_sq_info);
static DEVICE_ATTR(num_ccell, S_IRUGO | S_IWUSR,
		   bnx2i_show_ccell_info, bnx2i_set_ccell_info);
static DEVICE_ATTR(ooo_tx_count, S_IRUGO | S_IWUSR,
		   bnx2i_show_ooo_tx_count, bnx2i_reset_ooo_tx_count);

struct device_attribute *bnx2i_dev_attributes[] = {
	&dev_attr_sq_size,
	&dev_attr_num_ccell,
	&dev_attr_ooo_tx_count,
	NULL
};
