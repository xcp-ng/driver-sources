/* bnx2x_debugfs.c: QLogic Everest network driver.
 *
 * Copyright (c) 2018 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/binfmts.h>
#include <linux/vmalloc.h>
#include "bnx2x.h"

static struct dentry *bnx2x_dbg_root;

int bnx2x_str_reg_read_test(struct bnx2x *p_dev, char *params_string);
int bnx2x_str_reg_write_test(struct bnx2x *p_dev, char *params_string);
static ssize_t bnx2x_dbg_tests_cmd_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos);

static ssize_t bnx2x_dbg_tests_cmd_write(struct file *filp,
				 const char __user *buffer,
				 size_t count, loff_t *ppos);

static ssize_t bnx2x_dbg_internal_trace_cmd_read(struct file *filp,
				char __user *buffer,
				size_t count, loff_t *ppos);

static ssize_t bnx2x_dbg_internal_trace_cmd_write(struct file *filp,
				 const char __user *buffer,
				 size_t count, loff_t *ppos);

#define BNX2X_TESTS_NUM_STR_FUNCS 2
struct bnx2x_func_lookup {
	const char *key;
	int (*str_func)(struct bnx2x *bp, char *params_string);
};

static struct bnx2x_func_lookup bnx2x_tests_func[] = {
        {"reg_read", bnx2x_str_reg_read_test},
        {"reg_write", bnx2x_str_reg_write_test},
};

static const char *tests_list = "reg_read\n" "reg_write\n";

static struct file_operations bnx2x_debugfs_fileops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = bnx2x_dbg_tests_cmd_read,
	.write = bnx2x_dbg_tests_cmd_write,
};

static struct file_operations bnx2x_dbg_internal_trace_fileops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = bnx2x_dbg_internal_trace_cmd_read,
	.write = bnx2x_dbg_internal_trace_cmd_write,
};

/**
 * bnx2x_init - start up debugfs for the driver
 **/
void bnx2x_dbg_init(void)
{
	pr_notice("creating debugfs root node\n");

	/* Create bnx2x dir in root of debugfs. NULL means debugfs root. */
	bnx2x_dbg_root = debugfs_create_dir("bnx2x", NULL);
	if (!bnx2x_dbg_root) {
		pr_notice("init of debugfs failed\n");
		return;
	}
}

/**
 * bnx2x_dbg_exit - clean out the driver's debugfs entries
 **/
void bnx2x_dbg_exit(void)
{
	pr_notice("destroying debugfs root entry\n");

	/* remove bnx2x dir in root of debugfs */
#ifdef _HAS_DEBUGFS_REMOVE_RECURSIVE
	debugfs_remove_recursive(bnx2x_dbg_root);
#else
	debugfs_remove(bnx2x_dbg_root);
#endif
	bnx2x_dbg_root = NULL;
}

/**
 * bnx2x_dbg_pf_init - setup the debugfs file for the pf
 * @pf: the pf that is starting up
 **/
void bnx2x_dbg_pf_init(struct bnx2x *bp)
{
	const char *name = pci_name(bp->pdev);
	struct dentry *file_dentry = NULL;

	if (!bnx2x_dbg_root)
		return;

	/* Create pf dir */
	bp->bdf_dentry = debugfs_create_dir(name, bnx2x_dbg_root);
	if (!bp->bdf_dentry) {
		pr_notice("debugfs entry %s creation failed\n", name);
		return;
	}

	/* Create tests debugfs node */
	pr_info("Creating debugfs tests node for %s\n", name);
	file_dentry = debugfs_create_file("tests", 0600, bp->bdf_dentry, bp,
					  &bnx2x_debugfs_fileops);
	if (!file_dentry)
		printk("debugfs tests entry creation failed\n");

	file_dentry = debugfs_create_file("internal_trace", 0600,
					bp->bdf_dentry, bp,
					&bnx2x_dbg_internal_trace_fileops);
	if (!file_dentry)
		printk("debugfs internal_trace entry creation failed\n");

	return;
}

/**
 * bnx2x_dbg_pf_exit - clear out the pf's debugfs entries
 * @pf: the pf that is stopping
 **/
void bnx2x_dbg_pf_exit(struct bnx2x *bp)
{
	/* remove debugfs entries of this PF */
	pr_info("Removing debugfs for PF %d\n", BP_ABS_FUNC(bp));
#ifdef _HAS_DEBUGFS_REMOVE_RECURSIVE
	debugfs_remove_recursive(bp->bdf_dentry);
#else
	debugfs_remove(bp->bdf_dentry);
#endif
	bp->bdf_dentry = NULL;

	if (bp->internal_trace.dump_buf) {
		vfree(bp->internal_trace.dump_buf);
		bp->internal_trace.dump_buf = NULL;
	}
}

int bnx2x_str_reg_read_test(struct bnx2x *bp, char *params_string)
{
	u32 addr, value;
	char canary[4];
	int expected_args = 1, args;

	args = sscanf(params_string, "%i %3s ", &addr, canary);
	if (expected_args != args) {
		printk("Error: Expected %d arguments\n", expected_args);
		return -EINVAL;
	}

	value = REG_RD(bp, addr);

	DP(BNX2X_MSG_SP, "Read value 0x%08x from addr 0x%08x\n", value, addr);

	return value;
}

int bnx2x_str_reg_write_test(struct bnx2x *bp, char *params_string)
{
	u32 addr, value;
	char canary[4];
	int expected_args = 2, args;

	args = sscanf(params_string, "%i %i %3s ", &addr, &value, canary);
	if (expected_args != args) {
		printk("Error: Expected %d arguments\n",
			  expected_args);
		return -EINVAL;
	}

	DP(BNX2X_MSG_SP, "Write value 0x%08x to addr 0x%08x\n", value, addr);

	REG_WR(bp, addr, value);

	return REG_RD(bp, addr);
}

/* function services tests and phy read command */
static ssize_t bnx2x_dbg_external_cmd_read(struct file *filp,
					   char __user * buffer, size_t count,
					   loff_t * ppos, char *data, int len)
{
	struct bnx2x *bp = (struct bnx2x *)filp->private_data;
	int bytes_not_copied;

	/* avoid reading beyond available data */
	len = min_t(int, count, len - *ppos);

	/* copy data to the user */
	bytes_not_copied = copy_to_user(buffer, data + *ppos, len);

	/* notify user of problems */
	if (bytes_not_copied < 0) {
		BNX2X_ERR("failed to copy all bytes: bytes_not_copied %d\n",
			  bytes_not_copied);
		return bytes_not_copied;
	}

	/* mark result as unavailable */
	if (len == 0)
		bp->test_result_available = false;

	*ppos += len;

	return len;
}

static ssize_t bnx2x_dbg_tests_cmd_read(struct file *filp, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct bnx2x *bp = (struct bnx2x *)filp->private_data;
	int len;
	char *data;

	/* if test result is available return it, else print available tests */
	if (bp->test_result_available) {
		data = (char *)&bp->test_result;
		len = sizeof(bp->test_result);
	} else {
		data = (char *)tests_list;
		len = strlen(tests_list);
	}

	return bnx2x_dbg_external_cmd_read(filp, buffer, count, ppos, data,
					   len);
}

static ssize_t bnx2x_dbg_tests_cmd_write(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct bnx2x *bp = (struct bnx2x *)filp->private_data;
	int bytes_not_copied, func_idx;
	char *cmd_buf;
	int rc = 100;
	u8 cmd_len;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	/* strnlen_user() includes the null terminator */
	cmd_len = strnlen_user(buffer, MAX_ARG_STRLEN) - 1;
	cmd_buf = kzalloc(cmd_len, GFP_KERNEL);
	if (!cmd_buf)
		return count;

	/* copy user data to command buffer and perform sanity */
	bytes_not_copied = copy_from_user(cmd_buf, buffer, cmd_len);
	if (bytes_not_copied != 0) {
		BNX2X_ERR("failed to copy all bytes: bytes_not_copied = %d\n",
			  bytes_not_copied);
		kfree(cmd_buf);
		return (bytes_not_copied < 0) ? bytes_not_copied : count;
	}

	/* Fix cmd_len to be the size of the string till the first
	 * occurrence of \n (inclusively), since a multiple-line input
	 * should be processed line by line.
	 */
	cmd_len = strchr(cmd_buf, '\n') - cmd_buf + 1;

	/* Replace the closing \n character with a null terminator */
	cmd_buf[cmd_len - 1] = '\0';

	/* scan lookup table keys for a match to command buffer first arg */
	for (func_idx = 0; func_idx < BNX2X_TESTS_NUM_STR_FUNCS; func_idx++) {
		int keylen = strlen(bnx2x_tests_func[func_idx].key);

		if (strncmp(bnx2x_tests_func[func_idx].key, cmd_buf, keylen) ==
		    0) {
			rc = (bnx2x_tests_func[func_idx].str_func)(bp,
							cmd_buf + keylen);
			break;
		}
	}

	if (func_idx == BNX2X_TESTS_NUM_STR_FUNCS)
		BNX2X_ERR("unknown command: %s\n", cmd_buf);

	memset(bp->test_result, 0, sizeof(bp->test_result));
	snprintf(bp->test_result, BNX2X_TEST_RESULT_LENGTH, "%d\n", rc);
	bp->test_result_available = true;
	kfree(cmd_buf);

	return cmd_len;
}

static ssize_t bnx2x_dbg_internal_trace_cmd_read(struct file *filp,
				char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct bnx2x *bp = (struct bnx2x *)filp->private_data;
	int len;
	char *data = NULL;

	data = (char *)bp->internal_trace.dump_buf;
	len = bp->internal_trace.dumped_bytes;

	/* Already dumped */
	if (*ppos == len)
		return 0;

	/* dump not triggered using keyword */
	if (*ppos > len || !len || !data)
		return 0;

	return bnx2x_dbg_external_cmd_read(filp, buffer,
					   count, ppos, data, len);
}

static int bnx2x_dbg_internal_trace_dump(struct bnx2x *bp)
{
	u32 buf_size;

	if (bp->internal_trace.dump_buf) {
		vfree(bp->internal_trace.dump_buf);
		bp->internal_trace.dump_buf = NULL;
	}

	buf_size = bnx2x_dbg_internal_trace_get_dump_size(bp);
	if (!buf_size)
		return -EINVAL;

	bp->internal_trace.dump_size = buf_size;
	bp->internal_trace.dump_buf = vmalloc(bp->internal_trace.dump_size);
	if (!bp->internal_trace.dump_buf)
		return -ENOMEM;

	/* Dump trace into buffer */
	bnx2x_dbg_internal_trace_result(bp,
					bp->internal_trace.dump_buf,
					&bp->internal_trace.dumped_bytes);

	return 0;
}

static ssize_t bnx2x_dbg_internal_trace_cmd_write(struct file *filp,
				 const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct bnx2x *bp = (struct bnx2x *)filp->private_data;
	int bytes_not_copied, rc;
	char *cmd_buf;
	long cmd_len;

	/* Cmd is small ("dump"). Dont allow partial writes or long cmds */
	if (*ppos != 0)
		return 0;

	cmd_len = count;

	cmd_buf = kzalloc(cmd_len, GFP_KERNEL);
	if (!cmd_buf)
		return count;

	bytes_not_copied = copy_from_user(cmd_buf, buffer, cmd_len);
	if (bytes_not_copied < 0) {
		kfree(cmd_buf);
		return bytes_not_copied;
	}

	if (bytes_not_copied > 0)
		return count;

	cmd_buf[cmd_len - 1] = '\0';

	if (strncmp(cmd_buf, "dump", cmd_len) == 0) {
		/* Perform dump and collect result */
		rc = bnx2x_dbg_internal_trace_dump(bp);
		if (rc)
			BNX2X_ERROR("Failed to dump internal trace, rc %d", rc);
	}

	kfree(cmd_buf);

	return cmd_len;
}
