/* bnx2x_debugfs.h: QLogic Everest network driver.
 *
 * Copyright (c) 2018 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#ifndef BNX2X_DEBUGFS_H
#define BNX2X_DEBUGFS_H
void bnx2x_dbg_init(void);
void bnx2x_dbg_pf_init(struct bnx2x *dev);
void bnx2x_dbg_exit(void);
void bnx2x_dbg_pf_exit(struct bnx2x *dev);
#endif
