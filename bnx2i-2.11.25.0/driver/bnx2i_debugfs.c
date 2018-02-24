#include "bnx2i_debugfs.h"


static struct dentry *bnx2i_dbg_root = NULL;
static struct dentry *bnx2i_login_stats_root = NULL;
static int bnx2i_dbg_rdp_open(struct inode *inode, struct file *file);

#define BNX2I_LOGIN_STATS_ATTR_U32(_name, _mode, _parent) \
	attr = debugfs_create_u32(#_name, _mode, _parent, &hba->login_stats._name); \
	if (!attr) {	\
		pr_err("bnx2i: failed to create debugfs attr " #_name "\n");	\
		debugfs_remove_recursive(hba->login_stats_dir);	\
		return;	\
	}	

struct file_operations bnx2i_dbg_fops[] = {
	bnx2i_dbg_fileops_seq(bnx2i, rdp),
#if defined(RHEL8_DEBUGFS) || defined(UBUNTU_DEBUGFS) || defined(SLES15_DEBUGFS)

#else
	{ NULL, NULL },
#endif
};

static int bnx2i_rdp_show(struct seq_file *s, void *unused)
{
	struct bnx2i_hba *hba = s->private;
	struct sfp_diagnostic sfp_diag;
	struct link_error_status lesb;

	memset(&sfp_diag, 0, sizeof(sfp_diag));
	memset(&lesb, 0, sizeof(lesb));

	/* Pull the RDP data and fill it up here */
	if (hba->cnic && hba->cnic->drv_get_sfp_diagnostic)
		hba->cnic->drv_get_sfp_diagnostic(hba->cnic, &sfp_diag);

	seq_printf(s, "3001: %lu: %pM\n", sizeof(hba->cnic->mac_addr),
		   hba->cnic->mac_addr);
	seq_printf(s, "4001: %lu: 0x%08x\n", sizeof(sfp_diag.temperature),
		   sfp_diag.temperature);
	seq_printf(s, "4002: %lu: 0x%08x\n", sizeof(sfp_diag.vcc),
		   sfp_diag.vcc);
	seq_printf(s, "4003: %lu: 0x%08x\n", sizeof(sfp_diag.tx_bias),
		   sfp_diag.tx_bias);
	seq_printf(s, "4004: %lu: 0x%08x\n", sizeof(sfp_diag.tx_power),
		   sfp_diag.tx_power);
	seq_printf(s, "4005: %lu: 0x%08x\n", sizeof(sfp_diag.rx_power),
		   sfp_diag.rx_power);
	seq_printf(s, "4006: %lu: 0x%08x\n", sizeof(sfp_diag.speed_cap),
		   sfp_diag.speed_cap);
	seq_printf(s, "4007: %lu: 0x%08x\n", sizeof(sfp_diag.opr_speed),
		   sfp_diag.opr_speed);

	if (hba->cnic && hba->cnic->drv_get_link_error)
		hba->cnic->drv_get_link_error(hba->cnic, &lesb);

	seq_printf(s, "4012: %lu: 0x%08x\n", sizeof(lesb.inval_crc_cnt),
		   lesb.inval_crc_cnt);

	return 0;
}

static int bnx2i_dbg_rdp_open(struct inode *inode, struct file *file)
{
	struct bnx2i_hba *hba = inode->i_private;

	return single_open(file, bnx2i_rdp_show, hba);
}

static int bnx2i_create_rdp_file(struct bnx2i_hba *hba)
{
	struct dentry *file_dentry = NULL;
	int rc = -EINVAL;

	if (!bnx2i_ssan_feature)
		return rc;

	file_dentry = debugfs_create_file("ssan_rdp", 0600,
					  hba->debugfs_host, hba, bnx2i_dbg_fops);
	if (!file_dentry)
		return rc;

	return 0;
}

/**
 * bnx2i_rdp_stats_init - start up debugfs for bnx2i hosts RDP stats
 **/
void bnx2i_rdp_stats_init(struct Scsi_Host *shost)
{
	int rc = -EINVAL;

	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	char host_dir[32];

	if (!bnx2i_dbg_root) {
		pr_err("bnx2i: debugfs root entry not present!\n");
		return;
	}

	sprintf(host_dir, "host%u", shost->host_no);

	hba->debugfs_host = debugfs_create_dir(host_dir, bnx2i_dbg_root);
	if (!hba->debugfs_host) {
		pr_err("bnx2i: iscsi stats debugfs init failed for host %u\n",
		       shost->host_no);
		return;
	}

	rc = bnx2i_create_rdp_file(hba);
	if (rc) {
		pr_err("bnx2i: RDP debugfs init failed for host %u\n",
		       shost->host_no);
	}
}

/**
 * bnx2i_dbg_rdp_exit - remove debugfs entries for the driver
 **/
void bnx2i_rdp_stats_exit(struct Scsi_Host *shost)
{
	struct bnx2i_hba *hba = iscsi_host_priv(shost);

	debugfs_remove_recursive(hba->debugfs_host);
	hba->debugfs_host = NULL;
}

/**
 * bnx2i_create_login_stats_attrs - create the login stats debugfs attrs
 **/ 
static void bnx2i_create_login_stats_attrs(struct bnx2i_hba *hba)
{
	struct dentry *attr;

	BNX2I_LOGIN_STATS_ATTR_U32(login_successes, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(login_failures, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(login_negotiation_failures, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(login_authentication_failures, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(login_authorization_failures, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(login_redirect_responses, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(logout_normals, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(logout_others, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(connection_timeouts, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(session_failures, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(digest_errors, 0400,
				   hba->login_stats_dir);
	BNX2I_LOGIN_STATS_ATTR_U32(format_errors, 0400,
				   hba->login_stats_dir);
}

/**
 * bnx2i_login_stats_init - start up debugfs for bnx2i hosts iscsi stats
 **/
void bnx2i_login_stats_init(struct Scsi_Host *shost)
{
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	char host_dir[32];

	if (!bnx2i_dbg_root) {
		pr_err("bnx2i: debugfs root entry not present!\n");
		return;
	}

	/* create iscsi stats root for bnx2i hosts in debugfs. */
	if (!bnx2i_login_stats_root)
		bnx2i_login_stats_root = debugfs_create_dir("iscsi_stats",
							    bnx2i_dbg_root);
	if (!bnx2i_login_stats_root)
		pr_err("bnx2i: iscsi stats debugfs init failed!\n");

	sprintf(host_dir, "host%u", shost->host_no);

	hba->login_stats_dir = debugfs_create_dir(host_dir,
						  bnx2i_login_stats_root);
	if (!hba->login_stats_dir) {
		pr_err("bnx2i: iscsi stats debugfs init failed for host %u\n",
		       shost->host_no);
		return;
	}

	bnx2i_create_login_stats_attrs(hba);
}

/**
 * bnx2i_login_stats_exit - start up debugfs for bnx2i hosts iscsi stats
 **/
void bnx2i_login_stats_exit(void)
{
	pr_info("bnx2i: remove iscsi stats debugfs node\n");
	debugfs_remove_recursive(bnx2i_login_stats_root);
	bnx2i_login_stats_root = NULL;
}

/**
 * bnx2i_dbg_init - start up debugfs for the driver
 **/
void bnx2i_dbg_init(char *drv_name)
{
	/* create bnx2i dir in root of debugfs. NULL means debugfs root */
	bnx2i_dbg_root = debugfs_create_dir(drv_name, NULL);
	if (!bnx2i_dbg_root) {
		pr_err("bnx2i: debugfs init failed!\n");
		return;
	}

	pr_info("bnx2i: created debugfs root node!\n");
}

/**
 * bnx2i_dbg_exit - remove debugfs entries for the driver
 **/
void bnx2i_dbg_exit(void)
{
	pr_info("bnx2i: remove debugfs root entry\n");
	debugfs_remove_recursive(bnx2i_dbg_root);
	bnx2i_dbg_root = NULL;
}
