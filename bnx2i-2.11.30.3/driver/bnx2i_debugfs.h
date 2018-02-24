#ifndef _BNX2I_DEBUGFS_H_
#define _BNX2I_DEBUGFS_H_
#include <linux/debugfs.h>
#include <linux/err.h>
#include "bnx2i.h"


#define bnx2i_dbg_fileops(drv, ops) \
{ \
	.owner  = THIS_MODULE, \
	.open   = simple_open, \
	.read   = drv##_dbg_##ops##_cmd_read, \
	.write  = drv##_dbg_##ops##_cmd_write \
}

/* Used for debugfs sequential files */
#define bnx2i_dbg_fileops_seq(drv, ops) \
{ \
	.owner = THIS_MODULE, \
	.open = drv##_dbg_##ops##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

void bnx2i_dbg_init(char *drv_name);
void bnx2i_dbg_exit(void);
void bnx2i_login_stats_init(struct Scsi_Host *shost);
void bnx2i_login_stats_exit(void);
void bnx2i_rdp_stats_init(struct Scsi_Host *shost);
void bnx2i_rdp_stats_exit(struct Scsi_Host *shost);
#endif /* _BNX2I_DEBUGFS_H_ */
