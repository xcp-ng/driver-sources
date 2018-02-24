/*
 * Copyright 2008-2019 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _VNIC_DEVCMD_H_
#define _VNIC_DEVCMD_H_

#define _CMD_NBITS      14
#define _CMD_VTYPEBITS	10
#define _CMD_FLAGSBITS  6
#define _CMD_DIRBITS	2

#define _CMD_NMASK      ((1 << _CMD_NBITS)-1)
#define _CMD_VTYPEMASK  ((1 << _CMD_VTYPEBITS)-1)
#define _CMD_FLAGSMASK  ((1 << _CMD_FLAGSBITS)-1)
#define _CMD_DIRMASK    ((1 << _CMD_DIRBITS)-1)

#define _CMD_NSHIFT     0
#define _CMD_VTYPESHIFT (_CMD_NSHIFT+_CMD_NBITS)
#define _CMD_FLAGSSHIFT (_CMD_VTYPESHIFT+_CMD_VTYPEBITS)
#define _CMD_DIRSHIFT   (_CMD_FLAGSSHIFT+_CMD_FLAGSBITS)

/*
 * Direction bits (from host perspective).
 */
#define _CMD_DIR_NONE   0U
#define _CMD_DIR_WRITE  1U
#define _CMD_DIR_READ   2U
#define _CMD_DIR_RW     (_CMD_DIR_WRITE | _CMD_DIR_READ)

/*
 * Flag bits.
 */
#define _CMD_FLAGS_NONE 0U
#define _CMD_FLAGS_NOWAIT 1U

/*
 * vNIC type bits.
 */
#define _CMD_VTYPE_NONE  0U
#define _CMD_VTYPE_ENET  1U
#define _CMD_VTYPE_FC    2U
#define _CMD_VTYPE_SCSI  4U
#define _CMD_VTYPE_ALL   (_CMD_VTYPE_ENET | _CMD_VTYPE_FC | _CMD_VTYPE_SCSI)

/*
 * Used to create cmds..
 */
#define _CMDCF(dir, flags, vtype, nr)  \
	(((dir)   << _CMD_DIRSHIFT) | \
	((flags) << _CMD_FLAGSSHIFT) | \
	((vtype) << _CMD_VTYPESHIFT) | \
	((nr)    << _CMD_NSHIFT))
#define _CMDC(dir, vtype, nr)    _CMDCF(dir, 0, vtype, nr)
#define _CMDCNW(dir, vtype, nr)  _CMDCF(dir, _CMD_FLAGS_NOWAIT, vtype, nr)

/*
 * Used to decode cmds..
 */
#define _CMD_DIR(cmd)            (((cmd) >> _CMD_DIRSHIFT) & _CMD_DIRMASK)
#define _CMD_FLAGS(cmd)          (((cmd) >> _CMD_FLAGSSHIFT) & _CMD_FLAGSMASK)
#define _CMD_VTYPE(cmd)          (((cmd) >> _CMD_VTYPESHIFT) & _CMD_VTYPEMASK)
#define _CMD_N(cmd)              (((cmd) >> _CMD_NSHIFT) & _CMD_NMASK)

enum vnic_devcmd_cmd {
	CMD_NONE                = _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_NONE, 0),

	/*
	 * mcpu fw info in mem:
	 * in:
	 *   (u64)a0=paddr to struct vnic_devcmd_fw_info
	 * action:
	 *   Fills in struct vnic_devcmd_fw_info (128 bytes)
	 * note:
	 *   An old definition of CMD_MCPU_FW_INFO
	 */
	CMD_MCPU_FW_INFO_OLD    = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 1),

	/*
	 * mcpu fw info in mem:
	 * in:
	 *   (u64)a0=paddr to struct vnic_devcmd_fw_info
	 *   (u16)a1=size of the structure
	 * out:
	 *	 (u16)a1=0                          for in:a1 = 0,
	 *		 data size actually written for other values.
	 * action:
	 *   Fills in first 128 bytes of vnic_devcmd_fw_info for in:a1 = 0,
	 *            first in:a1 bytes               for 0 < in:a1 <= 132,
	 *            132 bytes                       for other values of in:a1.
	 * note:
	 *   CMD_MCPU_FW_INFO and CMD_MCPU_FW_INFO_OLD have the same enum 1
	 *   for source compatibility.
	 */
	CMD_MCPU_FW_INFO        = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 1),

	/* dev-specific block member:
	 *    in: (u16)a0=offset,(u8)a1=size
	 *    out: a0=value
	 */
	CMD_DEV_SPEC            = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 2),

	/* stats clear */
	CMD_STATS_CLEAR         = _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 3),

	/* stats dump in mem: (u64)a0=paddr to stats area,
	 *                    (u16)a1=sizeof stats area */
	CMD_STATS_DUMP          = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 4),

	/* set Rx packet filter: (u32)a0=filters (see CMD_PFILTER_*) */
	CMD_PACKET_FILTER	= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 7),

	/* set Rx packet filter for all: (u32)a0=filters (see CMD_PFILTER_*) */
	CMD_PACKET_FILTER_ALL   = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 7),

	/* hang detection notification */
	CMD_HANG_NOTIFY         = _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 8),

	/* MAC address in (u48)a0 */
	CMD_MAC_ADDR            = _CMDC(_CMD_DIR_READ,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 9),
#define CMD_GET_MAC_ADDR CMD_MAC_ADDR   /* some uses are aliased */

	/* add addr from (u48)a0 */
	CMD_ADDR_ADD            = _CMDCNW(_CMD_DIR_WRITE,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 12),

	/* del addr from (u48)a0 */
	CMD_ADDR_DEL            = _CMDCNW(_CMD_DIR_WRITE,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 13),

	/* add VLAN id in (u16)a0 */
	CMD_VLAN_ADD            = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 14),

	/* del VLAN id in (u16)a0 */
	CMD_VLAN_DEL            = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 15),

	/*
	 * nic_cfg  (no wait, always succeeds)
	 * in (u32)a0 - NIC_CFG word (see vnic_set_nic_cfg())
	 *
	 * Capability query:
	 * out: (u64) a0= 1 if a1 is valid
	 *      (u64) a1= (NIC_CFG bits supported) | (flags << 32)
	 *                              (flags are CMD_NIC_CFG_CAPF_xxx)
	 */
	CMD_NIC_CFG             = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 16),

	/*
	 * nic_cfg_chk  (same as nic_cfg, but may return error)
	 * in (u32)a0 - NIC_CFG word (see vnic_set_nic_cfg())
	 *
	 * Capability query:
	 * out: (u64) a0= 1 if a1 is valid
	 *      (u64) a1= (NIC_CFG bits supported) | (flags << 32)
	 *                              (flags are CMD_NIC_CFG_CAPF_xxx)
	 */
	CMD_NIC_CFG_CHK         = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 16),

#define CMD_NIC_CFG_CAPF_UDP_WEAK (1ULL << 0)    /* 14xx-style UDP RSS */

	/* union vnic_rss_key in mem: (u64)a0=paddr, (u16)a1=len */
	CMD_RSS_KEY             = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 17),

	/* union vnic_rss_cpu in mem: (u64)a0=paddr, (u16)a1=len */
	CMD_RSS_CPU             = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 18),

	/* initiate softreset */
	CMD_SOFT_RESET          = _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 19),

	/* softreset status:
	 *    out: a0=0 reset complete, a0=1 reset in progress */
	CMD_SOFT_RESET_STATUS   = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 20),

	/* set struct vnic_devcmd_notify buffer in mem:
	 * in:
	 *   (u64)a0=paddr to notify (set paddr=0 to unset)
	 *   (u32)a1 & 0x00000000ffffffff=sizeof(struct vnic_devcmd_notify)
	 *   (u16)a1 & 0x0000ffff00000000=intr num (-1 for no intr)
	 * out:
	 *   (u32)a1 = effective size
	 *
	 * Capability query:
	 * out: (u32)a0=errno, 0:valid cmd,
	 *           a1=supported VNIC_STF_* bits
	 */
	CMD_NOTIFY              = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 21),

	/* UNDI API: (u64)a0=paddr to s_PXENV_UNDI_ struct,
	 *           (u8)a1=PXENV_UNDI_xxx */
	CMD_UNDI                = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 22),

	/* initiate open sequence (u32)a0=flags (see CMD_OPENF_*) */
	CMD_OPEN		= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 23),

	/* open status:
	 *    out: a0=0 open complete, a0=1 open in progress */
	CMD_OPEN_STATUS		= _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 24),

	/* close vnic */
	CMD_CLOSE		= _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 25),

	/* initialize virtual link: (u32)a0=flags (see CMD_INITF_*) */
/***** Replaced by CMD_INIT *****/
	CMD_INIT_v1		= _CMDCNW(_CMD_DIR_READ, _CMD_VTYPE_ALL, 26),

	/* variant of CMD_INIT, with provisioning info
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info */
	CMD_INIT_PROV_INFO	= _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 27),

	/* enable virtual link */
	CMD_ENABLE		= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 28),

	/* enable virtual link, waiting variant. */
	CMD_ENABLE_WAIT		= _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 28),

	/* disable virtual link */
	CMD_DISABLE		= _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 29),

	/* stats dump sum of all vnic stats on same uplink in mem:
	 *     (u64)a0=paddr
	 *     (u16)a1=sizeof stats area */
	CMD_STATS_DUMP_ALL	= _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 30),

	/* init status:
	 *    out: a0=0 init complete, a0=1 init in progress
	 *         if a0=0, a1=errno */
	CMD_INIT_STATUS		= _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 31),

	/* INT13 API: (u64)a0=paddr to vnic_int13_params struct
	 *            (u32)a1=INT13_CMD_xxx */
	CMD_INT13               = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_FC, 32),

	/* logical uplink enable/disable: (u64)a0: 0/1=disable/enable */
	CMD_LOGICAL_UPLINK      = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 33),

	/* undo initialize of virtual link */
	CMD_DEINIT		= _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 34),

	/* initialize virtual link: (u32)a0=flags (see CMD_INITF_*) */
	CMD_INIT		= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 35),

	/* check fw capability of a cmd:
	 * in:  (u32)a0=cmd
	 * out: (u32)a0=errno, 0:valid cmd
	 *      Look at the description of corresponding cmd
	 *                      for additional output
	 */
	CMD_CAPABILITY		= _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 36),

	/* persistent binding info
	 * in:  (u64)a0=paddr of arg
	 *      (u32)a1=CMD_PERBI_XXX */
	CMD_PERBI		= _CMDC(_CMD_DIR_RW, _CMD_VTYPE_FC, 37),

	/* Interrupt Assert Register functionality
	 * in: (u16)a0=interrupt number to assert
	 */
	CMD_IAR			= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 38),

	/* initiate hangreset, like softreset after hang detected */
	CMD_HANG_RESET		= _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 39),

	/* hangreset status:
	 *    out: a0=0 reset complete, a0=1 reset in progress */
	CMD_HANG_RESET_STATUS   = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 40),

	/*
	 * Set hw ingress packet vlan rewrite mode:
	 * in:  (u32)a0=new vlan rewrite mode
	 * out: (u32)a0=old vlan rewrite mode */
	CMD_IG_VLAN_REWRITE_MODE = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 41),

	/*
	 * in:  (u16)a0=bdf of target vnic
	 *      (u32)a1=cmd to proxy
	 *      a2-a15=args to cmd in a1
	 * out: (u32)a0=status of proxied cmd
	 *      a1-a15=out args of proxied cmd */
	CMD_PROXY_BY_BDF =	_CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 42),

	/*
	 * As for BY_BDF except a0 is index of hvnlink subordinate vnic
	 * or SR-IOV virtual vnic
	 */
	CMD_PROXY_BY_INDEX =    _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 43),

	/*
	 * For HPP toggle:
	 * adapter-info-get
	 * in:  (u64)a0=phsical address of buffer passed in from caller.
	 *      (u16)a1=size of buffer specified in a0.
	 * out: (u64)a0=phsical address of buffer passed in from caller.
	 *      (u16)a1=actual bytes from VIF-CONFIG-INFO TLV, or
	 *              0 if no VIF-CONFIG-INFO TLV was ever received. */
	CMD_CONFIG_INFO_GET = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 44),

	/*
	 * INT13 API: (u64)a0=paddr to vnic_int13_params struct
	 *            (u32)a1=INT13_CMD_xxx
	 */
	CMD_INT13_ALL = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 45),

	/*
	 * Set default vlan:
	 * in: (u16)a0=new default vlan
	 *     (u16)a1=zero for overriding vlan with param a0,
	 *		       non-zero for resetting vlan to the default
	 * out: (u16)a0=old default vlan
	 */
	CMD_SET_DEFAULT_VLAN = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 46),

	/* init_prov_info2:
	 * Variant of CMD_INIT_PROV_INFO, where it will not try to enable
	 * the vnic until CMD_ENABLE2 is issued.
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info */
	CMD_INIT_PROV_INFO2  = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 47),

	/* enable2:
	 *      (u32)a0=0                  ==> standby
	 *             =CMD_ENABLE2_ACTIVE ==> active
	 */
	CMD_ENABLE2 = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 48),

	/*
	 * cmd_status:
	 *     Returns the status of the specified command
	 * Input:
	 *     a0 = command for which status is being queried.
	 *          Possible values are:
	 *              CMD_SOFT_RESET
	 *              CMD_HANG_RESET
	 *              CMD_OPEN
	 *              CMD_INIT
	 *              CMD_INIT_PROV_INFO
	 *              CMD_DEINIT
	 *              CMD_INIT_PROV_INFO2
	 *              CMD_ENABLE2
	 * Output:
	 *     if status == STAT_ERROR
	 *        a0 = ERR_ENOTSUPPORTED - status for command in a0 is
	 *                                 not supported
	 *     if status == STAT_NONE
	 *        a0 = status of the devcmd specified in a0 as follows.
	 *             ERR_SUCCESS   - command in a0 completed successfully
	 *             ERR_EINPROGRESS - command in a0 is still in progress
	 */
	CMD_STATUS = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 49),

	/*
	 * Returns interrupt coalescing timer conversion factors.
	 * After calling this devcmd, ENIC driver can convert
	 * interrupt coalescing timer in usec into CPU cycles as follows:
	 *
	 *   intr_timer_cycles = intr_timer_usec * multiplier / divisor
	 *
	 * Interrupt coalescing timer in usecs can be be converted/obtained
	 * from CPU cycles as follows:
	 *
	 *   intr_timer_usec = intr_timer_cycles * divisor / multiplier
	 *
	 * in: none
	 * out: (u32)a0 = multiplier
	 *      (u32)a1 = divisor
	 *      (u32)a2 = maximum timer value in usec
	 */
	CMD_INTR_COAL_CONVERT = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 50),

	/*
	 * ISCSI DUMP API:
	 * in: (u64)a0=paddr of the param or param itself
	 *     (u32)a1=ISCSI_CMD_xxx
	 */
	CMD_ISCSI_DUMP_REQ = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 51),

	/*
	 * ISCSI DUMP STATUS API:
	 * in: (u32)a0=cmd tag
	 * in: (u32)a1=ISCSI_CMD_xxx
	 * out: (u32)a0=cmd status
	 */
	CMD_ISCSI_DUMP_STATUS = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 52),

	/*
	 * Subvnic migration from MQ <--> VF.
	 * Enable the LIF migration from MQ to VF and vice versa. MQ and VF
	 * indexes are statically bound at the time of initialization.
	 * Based on the direction of migration, the resources of either MQ or
	 * the VF shall be attached to the LIF.
	 * in:        (u32)a0=Direction of Migration
	 *					0=> Migrate to VF
	 *					1=> Migrate to MQ
	 *            (u32)a1=VF index (MQ index)
	 */
	CMD_MIGRATE_SUBVNIC = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 53),

	/*
	 * Register / Deregister the notification block for MQ subvnics
	 * in:
	 *   (u64)a0=paddr to notify (set paddr=0 to unset)
	 *   (u32)a1 & 0x00000000ffffffff=sizeof(struct vnic_devcmd_notify)
	 *   (u16)a1 & 0x0000ffff00000000=intr num (-1 for no intr)
	 * out:
	 *   (u32)a1 = effective size
	 */
	CMD_SUBVNIC_NOTIFY = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 54),

	/*
	 * Set the predefined mac address as default
	 * in:
	 *   (u48)a0=mac addr
	 */
	CMD_SET_MAC_ADDR = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 55),

	/* Update the provisioning info of the given VIF
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info */
	CMD_PROV_INFO_UPDATE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 56),

	/*
	 * Initialization for the devcmd2 interface.
	 * in: (u64) a0=host result buffer physical address
	 * in: (u16) a1=number of entries in result buffer
	 */
	CMD_INITIALIZE_DEVCMD2 = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 57),

	/*
	 * Add a filter.
	 * in: (u64) a0= filter address
	 *     (u32) a1= size of filter
	 * out: (u32) a0=filter identifier
	 *
	 * Capability query:
	 * out: (u64) a0= 1 if capability query supported
	 *      (u64) a1= MAX filter type supported
	 */
	CMD_ADD_FILTER = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 58),

	/*
	 * Delete a filter.
	 * in: (u32) a0=filter identifier
	 */
	CMD_DEL_FILTER = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 59),

	/*
	 * Enable a Queue Pair in User space NIC
	 * in: (u32) a0=Queue Pair number
	 *     (u32) a1= command
	 */
	CMD_QP_ENABLE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 60),

	/*
	 * Disable a Queue Pair in User space NIC
	 * in: (u32) a0=Queue Pair number
	 *     (u32) a1= command
	 */
	CMD_QP_DISABLE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 61),

	/*
	 * Stats dump Queue Pair in User space NIC
	 * in: (u32) a0=Queue Pair number
	 *     (u64) a1=host buffer addr for status dump
	 *     (u32) a2=length of the buffer
	 */
	CMD_QP_STATS_DUMP = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 62),

	/*
	 * Clear stats for Queue Pair in User space NIC
	 * in: (u32) a0=Queue Pair number
	 */
	CMD_QP_STATS_CLEAR = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 63),

	/*
	 * UEFI BOOT API: (u64)a0= UEFI FLS_CMD_xxx
	 * (ui64)a1= paddr for the info buffer
	 */
	CMD_FC_REQ = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_FC, 64),

	/*
	 * Return the iSCSI config details required by the EFI Option ROM
	 * in:  (u32) a0=0 Get Boot Info for PXE eNIC as per pxe_boot_config_t
	 *            a0=1 Get Boot info for iSCSI enic as per
	 *            iscsi_boot_efi_cfg_t
	 * in:  (u64) a1=Host address where iSCSI config info is returned
	 */
	CMD_VNIC_BOOT_CONFIG_INFO = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 65),

	/*
	 * Create a Queue Pair (RoCE)
	 * in: (u32) a0 = Queue Pair number
	 *     (u32) a1 = Remote QP
	 *     (u32) a2 = RDMA-RQ
	 *     (u16) a3 = RQ Res Group
	 *     (u16) a4 = SQ Res Group
	 *     (u32) a5 = Protection Domain
	 *     (u64) a6 = Remote MAC
	 *     (u32) a7 = start PSN
	 *     (u16) a8 = MSS
	 *     (u32) a9 = protocol version
	 */
	CMD_RDMA_QP_CREATE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 66),

	/*
	 * Delete a Queue Pair (RoCE)
	 * in: (u32) a0 = Queue Pair number
	 */
	CMD_RDMA_QP_DELETE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 67),

	/*
	 * Retrieve a Queue Pair's status information (RoCE)
	 * in: (u32) a0 = Queue Pair number
	 *     (u64) a1 = host buffer addr for QP status struct
	 *     (u32) a2 = length of the buffer
	 */
	CMD_RDMA_QP_STATUS = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 68),

	/*
	 * Use this devcmd for agreeing on the highest common version supported
	 * by both driver and fw for by features who need such a facility.
	 *  in:  (u64) a0 = feature (driver requests for the supported versions on
	 *                  this feature)
	 *  out: (u64) a0 = bitmap of all supported versions for that feature
	 *       (u64) a1 = feature specific flags
	 */
	CMD_GET_SUPP_FEATURE_VER = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 69),

	/*
	 * Initialize the RDMA notification work queue
	 * in: (u64) a0 = host buffer address
	 * in: (u16) a1 = number of entries in buffer
	 * in: (u16) a2 = resource group number
	 * in: (u16) a3 = CQ number to post completion
	 */
	CMD_RDMA_INIT_INFO_BUF = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 70),

	/*
	 * De-init the RDMA notification work queue
	 * in: (u64) a0=resource group number
	 */
	CMD_RDMA_DEINIT_INFO_BUF = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 71),

	/*
	 * Control (Enable/Disable) overlay offloads on the given vnic
	 * in: (u8) a0 = OVERLAY_FEATURE_NVGRE : NVGRE
	 *          a0 = OVERLAY_FEATURE_VXLAN : VxLAN
	 *          a0 = OVERLAY_FEATURE_GENEVE : GENEVE
	 * in: (u8) a1 = OVERLAY_OFFLOAD_ENABLE : Enable or
	 *          a1 = OVERLAY_OFFLOAD_DISABLE : Disable or
	 *          a1 = OVERLAY_OFFLOAD_ENABLE_V2 : Enable with version 2
	 * Capability check return:
	 * out:     a0 = 0
	 *          a1 = Max number of supported Geneve option bytes
	 */
	CMD_OVERLAY_OFFLOAD_CTRL =
				_CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 72),

	/*
	 * Configuration of overlay offloads feature on a given vNIC
	 * in: (u8) a0 = OVERLAY_CFG_VXLAN_PORT_UPDATE : VxLAN
	 *               OVERLAY_CFG_GENEVE_PORT_UPDATE : Geneve
	 * in: (u16) a1 = unsigned short int port information
	 */
	CMD_OVERLAY_OFFLOAD_CFG = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 73),

	/*
	 * Return the configured name for the device
	 * in: (u64) a0=Host address where the name is copied
	 *     (u32) a1=Size of the buffer
	 */
	CMD_GET_CONFIG_NAME = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 74),

	/*
	 * Enable group interrupt for the VF
	 * in: (u32) a0 = GRPINTR_ENABLE : enable
	 *           a0 = GRPINTR_DISABLE : disable
	 *           a0 = GRPINTR_UPD_VECT: update group vector addr
	 * in: (u32) a1 = interrupt group count
	 * in: (u64) a2 = Start of host buffer address for DMAing group
	 *           vector bitmap
	 * in: (u64) a3 = Stride between group vectors
	 */
	CMD_CONFIG_GRPINTR = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 75),

	/*
	 * Set cq arrary base and size in a list of consective wqs and
	 * rqs for a device
	 * in: (u16) a0 = the wq relative index in the device.
	 *		-1 indicates skipping wq configuration
	 * in: (u16) a1 = the wcq relative index in the device
	 * in: (u16) a2 = the rq relative index in the device
	 *		-1 indicates skipping rq configuration
	 * in: (u16) a3 = the rcq relative index in the device
	 */
	CMD_CONFIG_CQ_ARRAY = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 76),

	/*
	 * Add an advanced filter.
	 * in: (u64) a0= filter address
	 *     (u32) a1= size of filter
	 * out: (u32) a0=filter identifier
	 *
	 * Capability query:
	 * in:  (u64) a1= supported filter capability exchange modes
	 * out: (u64) a0= 1 if capability query supported
	 *      if (u64) a1 = 0: a1 = MAX filter type supported
	 *      if (u64) a1 & FILTER_CAP_MODE_V1_FLAG:
	 *                       a1 = bitmask of supported filters
	 *                       a2 = FILTER_CAP_MODE_V1
	 *                       a3 = bitmask of supported actions
	 */
	CMD_ADD_ADV_FILTER = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 77),

	/*
	 * Add a MAC address and VLAN pair to a LIF. This is like CMD_ADDR_ADD
	 * but with the ability to specify a VLAN as well.
	 * in: (u64) a0 = MAC address
	 *     (u16) a1 = VLAN (0 means default VLAN)
	 *     (u32) a2 = flags (see AVF_xxx below)
	 */
	CMD_ADDR_VLAN_ADD = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 78),

	/*
	 * Delete a MAC address and VLAN pair from a LIF. This is like
	 * CMD_ADDR_DEL but with the ability to specify a VLAN as well.
	 * in: (u64) a0 = MAC address
	 *     (u16) a1 = VLAN (0 means default VLAN)
	 *     (u32) a2 = flags (see AVF_xxx below)
	 */
	CMD_ADDR_VLAN_DEL = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 79),

	/*
	 * Bind resources to an MQ sub-vnic.  To detach a sub-vnic from all
	 * resources, call with all 0s.  A sub-vnic may not be attached to
	 * different resources until it is detached from current resources.
	 * This may only be issued as proxy-by-index on a MQ sub-vnic
	 * in: (u32) a0 = WQ base (relative)
	 *     (u32) a1 = WQ count
	 *     (u32) a2 = RQ base
	 *     (u32) a3 = RQ count
	 *     (u32) a4 = CQ base
	 *     (u32) a5 = CQ count
	 */
	CMD_SUBVNIC_RES_BIND = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 80),

	/*
	 * Configure RDMA Resource
	 * in:  (u32) a0 = sub-command
	 *      (u32) a1 = resource domain, 0xffffffff for domain-less commands
	 *      (u32) a2 = (command-specific)
	 *      ...
	 *
	 * All arguments that have not been assigned a meaning should be
	 * initialized to 0 to allow for better driver forward compatibility.
	 */
	CMD_RDMA_CTRL = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 81),

	/*
	 * Set a rate limit on a vnic
	 * in: (u32) a0 = rate limit in units of Mb/s
	 *     (u32) a1 = traffic class
	 */
	CMD_RATE_LIMIT_SET = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 82),

	/*
	 * Query rate limit on a vnic
	 * in: (u32) a0 = traffic class
	 * out:(u32) a0 = latest devcmd specified rate limit (Mb/s)
	 *           a1 = current actual rate limit (Mb/s)
	 */
	CMD_RATE_LIMIT_GET = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ENET, 82),

	/*
	 * Write QoS settings to a vnic
	 * in: (u32) a0 = flags
	 *     (u32) a1 = PFC map | (pause_mode << 8)
	 *     (u32) a2 = PGS grouping
	 *         ((a2 >> (PRI * 4)) & 0xf) = pri group (15 = strict priority)
	 *     (u64) a3 = PGS BW allocation
	 *         ((a3 >> (PG * 8)) & 0xff) = BW % for priority group
	 *                                     (must sum to 100)
	 */
	CMD_QOS_SET = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 83),

	/*
	 * Read QoS settings from a vnic
	 * out:(u32) a0 = flags
	 *     (u32) a1 = PFC map | (pause_mode << 8)
	 *     (u32) a2 = PGS grouping
	 *         ((a2 >> (PRI * 4)) & 0xf) = pri group (15 = strict priority)
	 *     (u64) a3 = PGS BW allocation
	 *         ((a3 >> (PG * 8)) & 0xff) = BW % for priority group
	 *     (u32) a4 = Remote flags
	 *     (u32) a5 = Remote PFC map | (pause_mode << 8)
	 *     (u32) a6 = Remote PGS grouping
	 *         ((a6 >> (PRI * 4)) & 0xf) = pri group (15 = strict priority)
	 *     (u64) a7 = Remote PGS BW allocation
	 *         ((a7 >> (PG * 8)) & 0xff) = BW % for priority group
	 *     (u64) a8 = Max supported independent traffic classes
	 *     (u32) a9 = Mapping of PRI to Traffic Class, 4 bits per PRI
	 *     (u32) a10= FCoE COS | (MGMT COS << 8)
	 */
	CMD_QOS_GET = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ENET, 83),

	/*
	 * Bind RDMA resources to an MQ sub-vnic.  To detach a sub-vnic from all
	 * resources, call with count == 0.  A sub-vnic may not be attached to
	 * different resources until it is detached from current resources.
	 * This may only be issued as proxy-by-index on a MQ sub-vnic
	 * in: (u32) a0 = RDMA engine index
	 *     (u32) a1 = count
	 */
	CMD_SUBVNIC_RDMA_BIND = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 84),

	/*
	 * Notify firmware about an hardware error on a queue.
	 *
	 * in: (u64) a0 = host buffer address for queue error TLVs
	 *     (u32) a1 = length of the host buffer
	 *
	 * The data in the host buffer has to be encoded in
	 * "struct qerror_s" format.
	 *
	 * The queue data is versioned so that compatibility between
	 * old/new firmware/driver can be ensured.
	 *
	 * The supported versions can be queried using capability devcmd:
	 * out: (u32) a0 = bitmap of firmware supported versions.
	 */
	CMD_QUEUE_ERROR = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 85),

	/*
	 * Assign a WQ to a traffic class.  In Bodega, driver must assign TCs
	 * such that the property "all WQs belonging to the same TC are
	 * contiguous"
	 * Any command whose result would be to violate this property will fail
	 * with an error code of ERR_EINVAL.
	 * in: (u32) a0 = vnic-relative WQ index
	 *     (u32) a1 = Traffic Class
	 *
	 * Capability check will return flags in a[1]
	 *  QOS_TC_ARBITRARY_MAP - arbitrary WQ:TC mapping is allowed
	 */
	CMD_QOS_WQ_TC_SET = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 86),

	/*
	 * Query the traffic class for a WQ
	 * in: (u32) a0 = vnic-relative WQ index
	 * out:(u32) a0 = Traffic Class
	 */
	CMD_QOS_WQ_TC_GET = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 87),

	/*
	 * Perform a Flow Manager Operation (see flowman_api.h)
	 * in:  (u32) a0 = sub-command
	 *      (u64) a1..15 = (sub-command specific)
	 *
	 * All arguments that have not been assigned a meaning should be
	 * initialized to 0 to allow for better driver forward compatibility.
	 */
	CMD_FLOW_MANAGER_OP = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 88),

	/*
	 * RDMA v3 control
	 * in:  (u32) a0 = sub-command
	 *      (u32) a1 = (command-specific)
	 *      ...
	 *
	 * All arguments that have not been assigned a meaning should be
	 * initialized to 0 to allow for better driver forward compatibility.
	 */
	CMD_RDMA_V4_CTRL_OLD = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 86),
	CMD_RDMA_V4_CTRL = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 89),

	/*
	 * Set extended CQ field in MREGS of RQ (or all RQs)
	 * for given vNIC
	 * in: (u64) a0 = RQ selection (VNIC_RQ_ALL for all RQs)
	 *     (u32) a1 = CQ entry size
	 *         VNIC_RQ_CQ_ENTRY_SIZE_16 --> 16 bytes
	 *         VNIC_RQ_CQ_ENTRY_SIZE_32 --> 32 bytes
	 *         VNIC_RQ_CQ_ENTRY_SIZE_64 --> 64 bytes
	 *
	 * Capability query:
	 * out: (u32) a0 = errno, 0:valid cmd
	 *      (u32) a1 = value consisting of supported entries
	 *         bit 0: 16 bytes
	 *         bit 1: 32 bytes
	 *         bit 2: 64 bytes
	 */
	CMD_CQ_ENTRY_SIZE_SET_OLD = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 87),
	CMD_CQ_ENTRY_SIZE_SET = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 90),
};

enum queue_error_qtype {
	QUEUE_ERROR_QTYPE_WQ = 0,
	QUEUE_ERROR_QTYPE_RQ,
};

#define MK_QUEUE_ERROR_TYPE(type)       (1 << (type))
enum queue_error_type {
	QUEUE_ERROR_TYPE_V0 = 0,
	QUEUE_ERROR_TYPE_MAX,
};


/*
 * The queue error data is organised in TLVs as below:
 *
 * Header:
 * +--------+------+------+-----+------+
 * | # TLVs | TLV0 | TLV1 | ... | TLVn |
 * +--------+------+------+-----+------+
 *
 * Each TLV:
 * +------+--------+------------+
 * | Type | Length | Queue Data |
 * +------+--------+------------+
 *
 * Each TLV's data:
 * +-------+-----+--------+---------------+-------------+
 * | Qtype | Qid | Qerror | # Descriptors | Descriptors |
 * +-------+-----+--------+---------------+-------------+
 */

struct qerror_data_v0_s {
	u_int16_t qed_qtype;	/* queue type -- WQ/RQ */
	u_int16_t qed_id;	/* relative queue ID */
	u_int16_t qed_error;	/* error type */
	u_int16_t qed_ndescs;	/* # of descriptors below */
	u_int8_t qed_desc[0];	/* last qed_ndescs descriptros */
};

struct qerror_data_s {
	union {
		struct qerror_data_v0_s qed_v0;
	} u;
};

struct qerror_tlv_s {
	u_int16_t qet_type;
	u_int16_t qet_len;
	u_int8_t qet_data[0];
};

struct qerror_s {
	u_int32_t qe_ntlvs;	/* # queue error TLVs below */
	u_int8_t qe_tlvs[0];	/* queue error TLVs start here */
};

/* Modes for exchanging advanced filter capabilities. The modes supported by
 * the driver are passed in the CMD_ADD_ADV_FILTER capability command and the
 * mode selected is returned.
 *    V0: the maximum filter type supported is returned
 *    V1: bitmasks of supported filters and actions are returned
 */
enum filter_cap_mode {
	FILTER_CAP_MODE_V0 = 0,  /* Must always be 0 for legacy drivers */
	FILTER_CAP_MODE_V1 = 1,
};
#define FILTER_CAP_MODE_V1_FLAG (1 << FILTER_CAP_MODE_V1)

/* CMD_ENABLE2 flags */
#define CMD_ENABLE2_STANDBY 0x0
#define CMD_ENABLE2_ACTIVE  0x1

/* flags for CMD_OPEN */
#define CMD_OPENF_OPROM               0x1 /* open coming from option rom */
#define CMD_OPENF_RQ_ENABLE_THEN_POST 0x2 /* driver enables RQ before posting */

/* flags for CMD_INIT */
#define CMD_INITF_DEFAULT_MAC	0x1	/* init with default mac addr */

/* flags for CMD_PACKET_FILTER */
#define CMD_PFILTER_DIRECTED		0x01
#define CMD_PFILTER_MULTICAST		0x02
#define CMD_PFILTER_BROADCAST		0x04
#define CMD_PFILTER_PROMISCUOUS		0x08
#define CMD_PFILTER_ALL_MULTICAST	0x10

/* Commands for CMD_QP_ENABLE/CM_QP_DISABLE */
#define CMD_QP_RQWQ                     0x0

/* rewrite modes for CMD_IG_VLAN_REWRITE_MODE */
#define IG_VLAN_REWRITE_MODE_DEFAULT_TRUNK              0
#define IG_VLAN_REWRITE_MODE_UNTAG_DEFAULT_VLAN         1
#define IG_VLAN_REWRITE_MODE_PRIORITY_TAG_DEFAULT_VLAN  2
#define IG_VLAN_REWRITE_MODE_PASS_THRU                  3

enum vnic_devcmd_status {
	STAT_NONE = 0,
	STAT_BUSY = 1 << 0,	/* cmd in progress */
	STAT_ERROR = 1 << 1,	/* last cmd caused error (code in a0) */
	STAT_FAILOVER = 1 << 2, /* always set on vnics in pci standby state
				  if seen a failover to the standby happened */
};

enum vnic_devcmd_error {
	ERR_SUCCESS = 0,
	ERR_EINVAL = 1,
	ERR_EFAULT = 2,
	ERR_EPERM = 3,
	ERR_EBUSY = 4,
	ERR_ECMDUNKNOWN = 5,
	ERR_EBADSTATE = 6,
	ERR_ENOMEM = 7,
	ERR_ETIMEDOUT = 8,
	ERR_ELINKDOWN = 9,
	ERR_EMAXRES = 10,
	ERR_ENOTSUPPORTED = 11,
	ERR_EINPROGRESS = 12,
	ERR_EEXISTS = 13,
	ERR_MAX
};

/*
 * note: hw_version and asic_rev refer to the same thing,
 *       but have different formats. hw_version is
 *       a 32-byte string (e.g. "A2") and asic_rev is
 *       a 16-bit integer (e.g. 0xA2).
 */
struct vnic_devcmd_fw_info {
	char fw_version[32];
	char fw_build[32];
	char hw_version[32];
	char hw_serial_number[32];
	u16 asic_type;
	u16 asic_rev;
	u64 ptp_clock_freq;
};

#ifndef FOR_UPSTREAM_KERNEL
enum fwinfo_asic_type {
	FWINFO_ASIC_TYPE_UNKNOWN,
	FWINFO_ASIC_TYPE_PALO,
	FWINFO_ASIC_TYPE_SERENO,
	FWINFO_ASIC_TYPE_CRUZ,
	FWINFO_ASIC_TYPE_BODEGA,
	FWINFO_ASIC_TYPE_BEVERLY,
};
#endif

struct vnic_devcmd_notify {
	u32 csum;		/* checksum over following words */

	u32 link_state;		/* link up == 1 */
	u32 port_speed;		/* effective port speed (rate limit) */
	u32 mtu;		/* MTU */
	u32 msglvl;		/* requested driver msg lvl */
	u32 uif;		/* uplink interface */
	u32 status;		/* status bits (see VNIC_STF_*) */
	u32 error;		/* error code (see ERR_*) for first ERR */
	u32 link_down_cnt;	/* running count of link down transitions */
	u32 perbi_rebuild_cnt;	/* running count of perbi rebuilds */
	u32 dcbx_update_seq;    /* latest DCBX update sequence # */
};

#define VNIC_STF_FATAL_ERR	0x0001	/* fatal fw error */
#define VNIC_STF_STD_PAUSE	0x0002	/* standard link-level pause on */
#define VNIC_STF_PFC_PAUSE	0x0004	/* priority flow control pause on */
#define VNIC_STF_WQ_TC_REMAPPED 0x0008	/* Traffic classes have been remapped */
/* all supported status flags */
#define VNIC_STF_ALL		(VNIC_STF_FATAL_ERR |\
				 VNIC_STF_STD_PAUSE |\
				 VNIC_STF_PFC_PAUSE |\
				 VNIC_STF_WQ_TC_REMAPPED |\
				 0)

struct vnic_devcmd_provinfo {
	u8 oui[3];
	u8 type;
	u8 data[0];
};

/*
 * These are used in flags field of different filters to denote
 * valid fields used.
 */
#define FILTER_FIELD_VALID(fld) (1 << (fld - 1))

#define FILTER_FIELD_USNIC_VLAN    FILTER_FIELD_VALID(1)
#define FILTER_FIELD_USNIC_ETHTYPE FILTER_FIELD_VALID(2)
#define FILTER_FIELD_USNIC_PROTO   FILTER_FIELD_VALID(3)
#define FILTER_FIELD_USNIC_ID      FILTER_FIELD_VALID(4)

#define FILTER_FIELDS_USNIC (FILTER_FIELD_USNIC_VLAN | \
                             FILTER_FIELD_USNIC_ETHTYPE | \
                             FILTER_FIELD_USNIC_PROTO | \
                             FILTER_FIELD_USNIC_ID)

struct filter_usnic_id {
	u32 flags;
	u16 vlan;
	u16 ethtype;
	u8 proto_version;
	u32 usnic_id;
} __attribute__((packed));

#define FILTER_FIELD_5TUP_PROTO  FILTER_FIELD_VALID(1)
#define FILTER_FIELD_5TUP_SRC_AD FILTER_FIELD_VALID(2)
#define FILTER_FIELD_5TUP_DST_AD FILTER_FIELD_VALID(3)
#define FILTER_FIELD_5TUP_SRC_PT FILTER_FIELD_VALID(4)
#define FILTER_FIELD_5TUP_DST_PT FILTER_FIELD_VALID(5)

#define FILTER_FIELDS_IPV4_5TUPLE (FILTER_FIELD_5TUP_PROTO | \
                                   FILTER_FIELD_5TUP_SRC_AD | \
                                   FILTER_FIELD_5TUP_DST_AD | \
                                   FILTER_FIELD_5TUP_SRC_PT | \
                                   FILTER_FIELD_5TUP_DST_PT)

/* Enums for the protocol field. */
enum protocol_e {
	PROTO_UDP = 0,
	PROTO_TCP = 1,
	PROTO_IPV4 = 2,
	PROTO_IPV6 = 3
};

struct filter_ipv4_5tuple {
	u32 flags;
	u32 protocol;
	u32 src_addr;
	u32 dst_addr;
	u16 src_port;
	u16 dst_port;
} __attribute__((packed));

#define FILTER_FIELD_VMQ_VLAN   FILTER_FIELD_VALID(1)
#define FILTER_FIELD_VMQ_MAC    FILTER_FIELD_VALID(2)

#define FILTER_FIELDS_MAC_VLAN (FILTER_FIELD_VMQ_VLAN | \
                                FILTER_FIELD_VMQ_MAC)

#define FILTER_FIELDS_NVGRE    FILTER_FIELD_VMQ_MAC

struct filter_mac_vlan {
	u32 flags;
	u16 vlan;
	u8 mac_addr[6];
} __attribute__((packed));

#define FILTER_FIELD_VLAN_IP_3TUP_VLAN      FILTER_FIELD_VALID(1)
#define FILTER_FIELD_VLAN_IP_3TUP_L3_PROTO  FILTER_FIELD_VALID(2)
#define FILTER_FIELD_VLAN_IP_3TUP_DST_AD    FILTER_FIELD_VALID(3)
#define FILTER_FIELD_VLAN_IP_3TUP_L4_PROTO  FILTER_FIELD_VALID(4)
#define FILTER_FIELD_VLAN_IP_3TUP_DST_PT    FILTER_FIELD_VALID(5)

#define FILTER_FIELDS_VLAN_IP_3TUP (FILTER_FIELD_VLAN_IP_3TUP_VLAN | \
                                    FILTER_FIELD_VLAN_IP_3TUP_L3_PROTO | \
                                    FILTER_FIELD_VLAN_IP_3TUP_DST_AD | \
                                    FILTER_FIELD_VLAN_IP_3TUP_L4_PROTO | \
                                    FILTER_FIELD_VLAN_IP_3TUP_DST_PT)

struct filter_vlan_ip_3tuple {
	u32 flags;
	u16 vlan;
	u16 l3_protocol;
	union {
		u32 dst_addr_v4;
		u8 dst_addr_v6[16];
	} u;
	u32 l4_protocol;
	u16 dst_port;
} __attribute__((packed));

#define FILTER_GENERIC_1_BYTES 64

enum filter_generic_1_layer {
	FILTER_GENERIC_1_L2,
	FILTER_GENERIC_1_L3,
	FILTER_GENERIC_1_L4,
	FILTER_GENERIC_1_L5,
	FILTER_GENERIC_1_NUM_LAYERS
};

#define FILTER_GENERIC_1_IPV4       (1 << 0)
#define FILTER_GENERIC_1_IPV6       (1 << 1)
#define FILTER_GENERIC_1_UDP        (1 << 2)
#define FILTER_GENERIC_1_TCP        (1 << 3)
#define FILTER_GENERIC_1_TCP_OR_UDP (1 << 4)
#define FILTER_GENERIC_1_IP4SUM_OK  (1 << 5)
#define FILTER_GENERIC_1_L4SUM_OK   (1 << 6)
#define FILTER_GENERIC_1_IPFRAG     (1 << 7)

#define FILTER_GENERIC_1_KEY_LEN 64

/*
 * Version 1 of generic filter specification
 * position is only 16 bits, reserving positions > 64k to be used by firmware
 */
struct filter_generic_1 {
	u16 position;       // lower position comes first
	u32 mask_flags;
	u32 val_flags;
	u16 mask_vlan;
	u16 val_vlan;
	struct {
		u8 mask[FILTER_GENERIC_1_KEY_LEN];    // 0 bit means "don't care"
		u8 val[FILTER_GENERIC_1_KEY_LEN];
	} __attribute__((packed)) layer[FILTER_GENERIC_1_NUM_LAYERS];
} __attribute__((packed));

/* Specifies the filter_action type. */
enum {
	FILTER_ACTION_RQ_STEERING = 0,
	FILTER_ACTION_V2 = 1,
	FILTER_ACTION_MAX
};

struct filter_action {
	u32 type;
	union {
		u32 rq_idx;
	} u;
} __attribute__((packed));

#define FILTER_ACTION_RQ_STEERING_FLAG (1 << 0)
#define FILTER_ACTION_FILTER_ID_FLAG   (1 << 1)
#define FILTER_ACTION_DROP_FLAG        (1 << 2)

/* Version 2 of filter action must be a strict extension of struct filter_action
 * where the first fields exactly match in size and meaning.
 */
struct filter_action_v2 {
	u32 type;
	u32 rq_idx;
	u32 flags;                     // use FILTER_ACTION_XXX_FLAG defines
	u16 filter_id;
	u_int8_t reserved[32];         // for future expansion
} __attribute__((packed));

/* Specifies the filter type. */
enum filter_type {
	FILTER_USNIC_ID = 0,
	FILTER_IPV4_5TUPLE = 1,
	FILTER_MAC_VLAN = 2,
	FILTER_VLAN_IP_3TUPLE = 3,
	FILTER_NVGRE_VMQ = 4,
	FILTER_USNIC_IP = 5,
	FILTER_DPDK_1 = 6,
	FILTER_MAX
};

#define FILTER_USNIC_ID_FLAG       (1 << FILTER_USNIC_ID)
#define FILTER_IPV4_5TUPLE_FLAG    (1 << FILTER_IPV4_5TUPLE)
#define FILTER_MAC_VLAN_FLAG       (1 << FILTER_MAC_VLAN)
#define FILTER_VLAN_IP_3TUPLE_FLAG (1 << FILTER_VLAN_IP_3TUPLE)
#define FILTER_NVGRE_VMQ_FLAG      (1 << FILTER_NVGRE_VMQ)
#define FILTER_USNIC_IP_FLAG       (1 << FILTER_USNIC_IP)
#define FILTER_DPDK_1_FLAG         (1 << FILTER_DPDK_1)

struct filter {
	u32 type;
	union {
		struct filter_usnic_id usnic;
		struct filter_ipv4_5tuple ipv4;
		struct filter_mac_vlan mac_vlan;
		struct filter_vlan_ip_3tuple vlan_3tuple;
	} u;
} __attribute__((packed));

/*
 * This is a strict superset of "struct filter" and exists only
 * because many drivers use "sizeof(struct filter)" in deciding TLV size.
 * This new, larger struct filter would cause any code that uses that method
 * to not work with older firmware, so we add filter_v2 to hold the
 * new filter types.  Drivers should use vnic_filter_size() to determine
 * the TLV size instead of sizeof(struct fiter_v2) to guard against future
 * growth.
 */
struct filter_v2 {
	u32 type;
	union {
		struct filter_usnic_id usnic;
		struct filter_ipv4_5tuple ipv4;
		struct filter_mac_vlan mac_vlan;
		struct filter_vlan_ip_3tuple vlan_3tuple;
		struct filter_generic_1 generic_1;
	} u;
} __attribute__((packed));

enum {
	CLSF_TLV_FILTER = 0,
	CLSF_TLV_ACTION = 1,
};

struct filter_tlv {
	u_int32_t type;
	u_int32_t length;
	u_int32_t val[0];
};

/* Data for CMD_ADD_FILTER is 2 TLV and filter + action structs */
#define FILTER_MAX_BUF_SIZE 100
#define FILTER_V2_MAX_BUF_SIZE (sizeof(struct filter_v2) + \
	sizeof(struct filter_action_v2) + \
	(2 * sizeof(struct filter_tlv)))

/*
 * Compute actual structure size given filter type.  To be "future-proof,"
 * drivers should use this instead of "sizeof(struct filter_v2)" when
 * computing length for TLV.
 */
static inline u_int32_t
vnic_filter_size(
	struct filter_v2 *fp)
{
	u_int32_t size;

	switch (fp->type) {
	case FILTER_USNIC_ID:
		size = sizeof(fp->u.usnic);
		break;
	case FILTER_IPV4_5TUPLE:
		size = sizeof(fp->u.ipv4);
		break;
	case FILTER_MAC_VLAN:
	case FILTER_NVGRE_VMQ:
		size = sizeof(fp->u.mac_vlan);
		break;
	case FILTER_VLAN_IP_3TUPLE:
		size = sizeof(fp->u.vlan_3tuple);
		break;
	case FILTER_USNIC_IP:
	case FILTER_DPDK_1:
		size = sizeof(fp->u.generic_1);
		break;
	default:
		size = sizeof(fp->u);
		break;
	}
	size += sizeof(fp->type);
	return size;
}


enum {
	CLSF_ADD = 0,
	CLSF_DEL = 1,
};

/*
 * Get the action structure size given action type. To be "future-proof,"
 * drivers should use this instead of "sizeof(struct filter_action_v2)"
 * when computing length for TLV.
 */
static inline u_int32_t
vnic_action_size(struct filter_action_v2 *fap)
{
	u_int32_t size;

	switch (fap->type) {
	case FILTER_ACTION_RQ_STEERING:
		size = sizeof(struct filter_action);
		break;
	case FILTER_ACTION_V2:
		size = sizeof(struct filter_action_v2);
		break;
	default:
		/* this should never happen and will cause a devcmd error */
		size = sizeof(struct filter_action);
		break;
	}
	return size;
}

/*
 * Writing cmd register causes STAT_BUSY to get set in status register.
 * When cmd completes, STAT_BUSY will be cleared.
 *
 * If cmd completed successfully STAT_ERROR will be clear
 * and args registers contain cmd-specific results.
 *
 * If cmd error, STAT_ERROR will be set and args[0] contains error code.
 *
 * status register is read-only.  While STAT_BUSY is set,
 * all other register contents are read-only.
 */

/* Make sizeof(vnic_devcmd) a power-of-2 for I/O BAR. */
#define VNIC_DEVCMD_NARGS 15
struct vnic_devcmd {
	u32 status;			/* RO */
	u32 cmd;			/* RW */
	u64 args[VNIC_DEVCMD_NARGS];	/* RW cmd args (little-endian) */
};

/*
 * Version 2 of the interface.
 *
 * Some things are carried over, notably the vnic_devcmd_cmd enum.
 */

/*
 * Flags for vnic_devcmd2.flags
 */

#define DEVCMD2_FNORESULT       0x1     /* Don't copy result to host */

#define VNIC_DEVCMD2_NARGS      VNIC_DEVCMD_NARGS
struct vnic_devcmd2 {
	u16 pad;
	u16 flags;
	u32 cmd;                /* same command #defines as original */
	u64 args[VNIC_DEVCMD2_NARGS];
};

#define VNIC_DEVCMD2_NRESULTS   VNIC_DEVCMD_NARGS
struct devcmd2_result {
	u64 results[VNIC_DEVCMD2_NRESULTS];
	u32 pad;
	u16 completed_index;    /* into copy WQ */
	u8  error;              /* same error codes as original */
	u8  color;              /* 0 or 1 as with completion queues */
};

#define DEVCMD2_RING_SIZE   32
#define DEVCMD2_DESC_SIZE   128

#define DEVCMD2_RESULTS_SIZE_MAX   ((1 << 16) - 1)

/* Overlay related definitions */

/*
 * This enum lists the flag associated with each of the overlay features
 */
typedef enum {
	OVERLAY_FEATURE_NVGRE = 1,
	OVERLAY_FEATURE_VXLAN,
	OVERLAY_FEATURE_GENEVE,
	OVERLAY_FEATURE_MAX,
} overlay_feature_t;

typedef enum {
	OVERLAY_OFFLOAD_ENABLE,
	OVERLAY_OFFLOAD_DISABLE,
	OVERLAY_OFFLOAD_ENABLE_V2,
	OVERLAY_OFFLOAD_MAX,
} overlay_ofld_cmd;

#define OVERLAY_CFG_VXLAN_PORT_UPDATE 0
#define OVERLAY_CFG_GENEVE_PORT_UPDATE 1

/*
 * Use this enum to get the supported versions for each of these features
 * If you need to use the devcmd_get_supported_feature_version(), add
 * the new feature into this enum and install function handler in devcmd.c
 */
typedef enum {
	VIC_FEATURE_VXLAN,
	VIC_FEATURE_RDMA,
	VIC_FEATURE_GENEVE,
	VIC_FEATURE_PTP,
	VIC_FEATURE_MAX,
} vic_feature_t;

/*
 * These flags are used in args[1] of devcmd CMD_GET_SUPP_FEATURE_VER
 * to indicate the host driver about the VxLAN and Multi WQ features
 * supported
 */
#define ENIC_VXLAN_INNER_IPV6 (1 << 0)
#define ENIC_VXLAN_OUTER_IPV6 (1 << 1)
#define ENIC_VXLAN_MULTI_WQ   (1 << 2)

#define FEATURE_VXLAN_IPV6		(FEATURE_VXLAN_IPV6_INNER | \
					 FEATURE_VXLAN_IPV6_OUTER)
#define FEATURE_GENEVE_OPTIONS (1 << 0)  // supports Geneve option bytes

#define FEATUREF_RDMA_ON_PF (1 << 0)    // RDMA on PFs is enabled
#define FEATUREF_RDMA_ON_MQ (1 << 1)    // RDMA on MQs is enabled
#define FEATUREF_RDMA_IPV6  (1 << 2)    // supports IPv6 & mod_qp_edma devcmd
#define FEATUREF_RDMA_SGL_BUF (1 << 3)  // supports SGL buffer in VIC memory

/*
 * RDMA version represents the fimrware supported APIs for RDMA.
 *
 * Any incompatible changes between the firmware/driver interface would
 * result in a new version.
 */
#define MK_RDMA_FW_VER(ver) (1 << (u_int64_t)(ver))
enum vnic_rdma_fw_versions {
	RDMA_FW_VER_1,      // Cruz
	RDMA_FW_VER_2,      // Bodega pre-release (before Jalama Beach)
	RDMA_FW_VER_3,      // Bodega (Jalama Beach and later)
	RDMA_FW_VER_4,      // Beverly
};

enum {
	VIC_FEATURE_PTP_VER_1 = 1,	// Beverly PTP
	VIC_FEATURE_PTP_VER_MAX,
};

/*
 * CMD_CONFIG_GRPINTR subcommands
 */
typedef enum {
	GRPINTR_ENABLE = 1,
	GRPINTR_DISABLE,
	GRPINTR_UPD_VECT,
} grpintr_subcmd_t;

/*
 * These defines are used by RDMA V2 and V3
 */
#define RDMA_QP_TYPE_INVALID 0
#define RDMA_QP_TYPE_RC      1
#define RDMA_QP_TYPE_UD      2

#define RDMA_INTR_NULL_IDX   0xffffffff
#define RDMA_ANY_QPN         0xffffffff
#define RDMA_NULL_QP_ID      0xffffffff
#define RDMA_PSN_UNCHANGED   0xffffffff

#define RDMA_PROTO_ROCEV2      0

/*
 * CMD_RDMA_CTRL subcommands for RDMA v2
 *
 * Unless otherwise stated, all arguments are in little endian (as with regular
 * devcmds).
 *
 * MAC address arguments are encoded in u64 arguments.  A little endian host
 * should encode 11:22:33:44:55:66 as 0x0000112233445566.  The high order bytes
 * of the u64 value must be 0 or the argument will be considered an invalid MAC
 * address.
 */

/* RDMA resource domain reset flags */

/* Do not reset MRs in a dom reset */
#define RDMA_DOM_RESETF_NO_MRS  (1 << 0)

#define RDMA_QP_STATE_INVALID  0
#define RDMA_QP_STATE_RESET    (1<<0)
#define RDMA_QP_STATE_INIT     (1<<1)
#define RDMA_QP_STATE_RTR      (1<<2)
#define RDMA_QP_STATE_RTS      (1<<3)
#define RDMA_QP_STATE_SQD      (1<<4)
#define RDMA_QP_STATE_SQE      (1<<5)
#define RDMA_QP_STATE_ERR      (1<<6)
#define RDMA_QP_STATE_RSRV1    (1<<7)

#define RDMA_QP_STATE_VALID_RQ \
	(RDMA_QP_STATE_INIT | \
	 RDMA_QP_STATE_RTR | \
	 RDMA_QP_STATE_RTS | \
	 RDMA_QP_STATE_SQD | \
	 RDMA_QP_STATE_SQE)

#define RDMA_QP_STATE_VALID_RESP \
	(RDMA_QP_STATE_RTR | \
	 RDMA_QP_STATE_RTS | \
	 RDMA_QP_STATE_SQD)

#define RDMA_RQ_STATE_VALID_RESP \
	(RDMA_QP_STATE_RTR | \
	 RDMA_QP_STATE_RTS | \
	 RDMA_QP_STATE_SQD)

#define RDMA_QP_STATE_SQD_SQE \
	(RDMA_QP_STATE_SQD | \
	 RDMA_QP_STATE_SQE)

/*
 * Initialize a specific resource domain associated with the current vNIC.  The
 * number of resource domains for the current vNIC is specified in the vNIC
 * devspec.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_CFG_RESOURCE_DOMAIN
 *      (u32) a1 = resource domain id (0-indexed)
 *      (u32) a2 = protocol type (only RDMA_PROTO_ROCEV2 for now)
 *      (u64) a3 = source MAC address (see note above about MAC encoding)
 *      (u64) a4 = ring base addr of rdma_reg_cmd_result ring
 *      (u32) a5 = result ring size, should equal command WQ ring size
 *      (u32) a6 = rcmd soft cq interrupt vector (idx w/in vnic's intr range)
 *                 (pass RDMA_INTR_NULL_IDX for no interrupt)
 */
#define RDMA_SUBCMD_CFG_RESOURCE_DOMAIN   0

/*
 * Allocate a soft CQ from the resource domain.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_CREATE_CQ
 *      (u32) a1 = resource domain ID
 *      (u64) a2 = ring base address
 *      (u32) a3 = ring size
 *      (u32) a4 = interrupt vector (idx w/in vnic's intr range)
 *                 (pass RDMA_INTR_NULL_IDX for no interrupt)
 *
 * out: (u32) a0 = CQ ID
 */
#define RDMA_SUBCMD_CREATE_CQ             1

/*
 * Deallocate a soft CQ.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_DESTROY_CQ
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = CQ ID
 */
#define RDMA_SUBCMD_DESTROY_CQ            2

/*
 * Allocate a QP (with one SQ and one RQ) from the resource domain.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_CREATE_QP
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP type (see RDMA_QP_TYPE_xxx)
 *      (u32) a3 = max SQ WRs
 *      (u32) a4 = max RQ WRs
 *      (u32) a5 = SQ CQ ID
 *      (u32) a6 = RQ CQ ID
 *      (u32) a7 = desired QPN (or RDMA_ANY_QPN if don't care)
 *      (u32) a8 = QP flags
 *      (u64) a9 = SQ ring base ptr
 *      (u64) a10 = RQ ring base ptr
 *
 * out: (u32) a0 = QP ID
 *      (u32) a1 = actual QPN (XXX could just obtain from QUERY_QP)
 */
#define RDMA_SUBCMD_CREATE_QP             3

/*
 * Modify the state of an existing QP.  This is primarily used to transition
 * the QP from one state to the next.  The "current state" argument must match
 * the QP's actual current state or the command will fail.  If the driver and
 * firmware get out of sync, the actual current state can be queried with
 * RDMA_SUBCMD_QUERY_QP.
 *
 * The next-hop MAC, peer IP, and peer QPN arguments are ignored if the new
 * state is not RTR.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_MODIFY_QP
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP ID
 *      (u32) a3 = current state
 *      (u32) a4 = new state
 *      (u64) a5 = next-hop MAC to destination IP (see MAC encoding note above)
 *      (u64) a6 = peer IP address
 *      (u32) a7 = peer QPN
 *      (u32) a8 = path MTU (one of: 512/1024/2048/4096, 0 means no change)
 *      (u64) a9 = upper 32-bits: SQ PSN (RDMA_PSN_UNCHANGED means no change)
 *                 lower 32-bits: RQ PSN (RDMA_PSN_UNCHANGED means no change)
 *      (u32) a10 = Q_Key (UD QPs only)
 *      (u32) a11 = source IPv4 address in network byte order
 *      (u32) a12 = udp src port in packets for this QP
 */
#define RDMA_SUBCMD_MODIFY_QP             4

/*
 * Query current QP status.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_QUERY_QP
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP ID
 *
 * out: (u32) a0 = QPN
 *      (u32) a1 = current QP state
 *      (u32) a2 = path MTU
 *      (u32) a3 = current SQ PSN
 *      (u32) a4 = current RQ PSN
 */
#define RDMA_SUBCMD_QUERY_QP              5

/*
 * Deallocate a QP.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_DESTROY_QP
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP ID
 */
#define RDMA_SUBCMD_DESTROY_QP            6

/*
 * Retrieve a snapshot of current statistics for this vnic's
 * rdma engine
 *
 * in:  (u32) a0 = RDMA_SUBCMD_GET_STATS
 *
 * out: (u64) a0 = IG packet count
 *      (u64) a1 = IG byte count
 *      (u64) a2 = EG packet count
 *      (u64) a3 = EG byte count
 */
#define RDMA_SUBCMD_GET_STATS             7

/*
 * in:  (u32) a0 = RDMA_SUBCMD_RST_RESOURCE_DOMAIN
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = bitmap of RDMA_DOM_RESETF_XXX flags
 */
#define RDMA_SUBCMD_RST_RESOURCE_DOMAIN   8

/*
 * Status for deallocate QP dev_cmd.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_DESTROY_QP_STATUS
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP ID
 *
 * out: (u32) a0 = ERR_EINPROGRESS/ERR_EBADSTATE/ERR_SUCCESS
 */
#define RDMA_SUBCMD_DESTROY_QP_STATUS     9

/*
 * Status for modify QP dev_cmd.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_MODIFY_QP_STATUS
 *      (u32) a1 = resource domain ID
 *      (u32) a2 = QP ID
 *
 * out: (u32) a0 = ERR_EINPROGRESS/ERR_EBADSTATE/ERR_SUCCESS
 */
#define RDMA_SUBCMD_MODIFY_QP_STATUS      10

/*
 * in:  (u32) a0 = RDMA_SUBCMD_RST_RES_DOM_STATUS
 *      (u32) a1 = resource domain ID
 */
#define RDMA_SUBCMD_RST_RES_DOM_STATUS    11

#define RDMA_NOTIFY_TYPE_REG 0
#define RDMA_NOTIFY_TYPE_DEREG 1

/*
 * Initialize (and enable) or disable RDMA notify block, and the associated
 * interrupt.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_NOTIFY
 *      (u32) a1 = Notify type:
 *                  RDMA_NOTIFY_TYPE_REG = Register notifications.
 *                  RDMA_NOTIFY_TYPE_DEREG = De-register notifications.
 *      (u64) a2 = Host DMAable address for notify block (0x0 = disable notify).
 *      (u32) a3 = Size of the notify block.
 *      (u32) a4 = Interrupt number realtive to the vNIC's interrupts.
 *      (u64) a5 = Cookie to be used in future notifications.
 */
#define RDMA_SUBCMD_NOTIFY 12

/*
 * Ack for RDMA_SUBCMD_NOTIFY devcmd.
 *
 * in:  (u64) a0 = sequence number being acknowledged.
 */
#define RDMA_SUBCMD_NOTIFY_ACK 13

/*
 * Query the status of previously issuesd notify devcmd.
 *
 * Notification de-registraiton is asynchronous, and thus a status command is
 * required.
 *
 * in:  (u32) a0: RDMA_SUBCMD_NOTIFY_STATUS
 *      (u32) a1: Query status of registration or de-registration:
 *                  RDMA_NOTIFY_TYPE_REG: status of registration
 *                  RDMA_NOTIFY_TYPE_DE_REG: status of de-registration
 *
 * out: (u64) a0: Status of previously issued (de-)register.
 *                  ERR_SUCCESS: Complete. Safe to free host notify buffer.
 *                  ERR_EINPROGRESS: Still in progress. Query again.
 */
#define RDMA_SUBCMD_NOTIFY_STATUS 14

/*
 * Softreset a RDMA vNIC (just RDMA side of things -- the underlying enic vNIC
 * is left untouched). This is an asynchronous devcmd; i.e., driver has to
 * issue a RDMA_SUBCMD_SOFTRESET_STATUS devcmd to find the status of softreset.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_SOFTRESET
 */
#define RDMA_SUBCMD_SOFTRESET 15

/*
 * Status for softreset devcmd.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_SOFTRESET_STATUS
 * out: (u32) a0 = ERR_SUCCESS: softreset succeeded (or)
 *                 ERR_EINPROGRESS: softreset is still in progress (or)
 *                 ERR_EBADSTATE: softreset failed.
 */
#define RDMA_SUBCMD_SOFTRESET_STATUS 16

/*
 * Status for set RDMA cos.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_SET_COS
 *      (u31) a1 = cos
 */
#define RDMA_SUBCMD_SET_COS 17

/*
 * Modify the state of an existing QP. This is similar to
 * RDMA_SUBCMD_MODIFY_QP, but firmware eDMAs the QP attributes from host
 * buffer.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_MODIFY_QP_EDMA
 *      (u64) a1 = host PA that has struct rdma_modify_qp_attr
 *      (u32) a2 = length of host buffer that holds rdma_modify_qp_attr
 */
#define RDMA_SUBCMD_MODIFY_QP_EDMA 18

#define RDMA_SUBCMD_MAX 256

// RDMA RC QP network address types to use in struct rdma_modify_qp_attr
enum {
	RDMA_QP_ADDR_TYPE_IPV4,
	RDMA_QP_ADDR_TYPE_IPV6,
};

/*
 * GID is valid. Data contained within GID table (src MAC/IP) will be used from
 * GID table, and not from rdma_modify_qp_attr.
 */
#define RDMA_MOD_QPF_GID    (1 << 0)    /* GID is valid */
#define RDMA_MOD_QPF_ALL    (RDMA_MOD_QPF_GID)

struct enic_rdma_modify_qp_attr {
	u_int16_t rmq_dom_id;			// resource domain ID
	u_int32_t rmq_qpn;			// QPN being modified

	u_int8_t rmq_qp_cur_state;		// QP's current state
	u_int8_t rmq_qp_new_state;		// QP's new state

	u_int64_t rmq_nh_mac;			// nexthop MAC address
	u_int8_t rmq_qp_addr_type;		// QP address type -- v4/v6
	union {
		struct {
			u_int32_t sip_be;	// source IPv4 address
			u_int32_t pip_be;	// peer IPv4 address
		} __attribute__ ((__packed__)) rmq_v4;

		struct {
			u_int8_t sip[16];	// source IPv6 address
			u_int8_t pip[16];	// peer IPv6 address
		} __attribute__ ((__packed__)) rmq_v6;
	} __attribute__ ((__packed__));
	u_int16_t rmq_udp_sport_be;		// QP's source UDP port number

	u_int16_t rmq_path_mtu;			// QP's path MTU
	u_int32_t rmq_peer_qpn;			// QP's peer QPN

	// RDMA_PSN_UNCHANGED means no change
	u_int32_t rmq_sq_psn;			// QP's SQ PSN
	u_int32_t rmq_rq_psn;			// QP's RQ PSN

	u_int32_t rmq_ud_qkey;			// UD QP's qkey

	/*
	 * rmq_gid_index and rmq_flags are only supported in Beverly.
	 * Bodega also uses this struct, but wouldn't touch these fields.
	 */
	u_int32_t rmq_gid_index;		// index into engine's GID table
	u_int32_t rmq_flags;			// flags

	u_int8_t pad[51];                       // pad to 128B for future use
} __attribute__ ((__packed__));

/*
 * Flags for CMD_ADDR_VLAN_ADD and CMD_ADDR_VLAN_DEL
 */
#define AVF_VLAN_VALID      0x0001  // use VLAN from a1 in match
                                    //  (else VLAN is wildcard)
#define AVF_INNER_PKT       0x0002  // match on inner packet

/*
 * Flags for CMD_QOS_SET
 */
#define QOSF_WILLING        0x0001
#define QOSF_PFC_VALID      0x0002
#define QOSF_PGS_VALID      0x0004

#define QOS_PAUSE_NONE      0
#define QOS_PAUSE_PFC       1
#define QOS_PAUSE_STD       2

/*
 * Flags for CMD_QOS_WQ_TC_SET capability check
 */
#define QOS_TC_ARBITRARY_MAP 0x0001    // Arbitrary WQ:TC mapping is allowed

/*
 * CMD_RDMA_V4_CTRL subcommands for RDMA v4
 *
 * Unless otherwise stated, all arguments are in little endian (as with regular
 * devcmds).
 */

/*
 *  Host visible QP states
 */
#define RDMA_V4_QP_STATE_RESET      0
#define RDMA_V4_QP_STATE_INIT       1
#define RDMA_V4_QP_STATE_RTR        2
#define RDMA_V4_QP_STATE_RTS        3
#define RDMA_V4_QP_STATE_SQD        4
#define RDMA_V4_QP_STATE_SQE        5
#define RDMA_V4_QP_STATE_ERR        6

/*
 * Initialize a specific resource domain associated with the current vNIC.  The
 * number of resource domains for the current vNIC is specified in the vNIC
 * devspec.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_CFG_RESOURCE_DOMAIN
 *      (u32) a1 = resource domain id (0-indexed)
 *      (u32) a2 = protocol type (only RDMA_PROTO_ROCEV2 for now)
 *      (u64) a3 = ring base addr of rdma_reg_cmd_result ring
 *      (u32) a4 = result ring size, should equal command WQ ring size
 *      (u32) a5 = rcmd soft cq interrupt vector (idx w/in vnic's intr range)
 *                 (pass RDMA_INTR_NULL_IDX for no interrupt)
 */
#define RDMA_V4_SUBCMD_CFG_RESOURCE_DOMAIN  0

/*
 * Reset a resource domain on a RDMA-enabled vNIC.
 * Reset the command WQs and soft result CQ assoicated with the resource domain
 *
 * in:  (u32) a0 = RDMA_SUBCMD_RST_RESOURCE_DOMAIN
 *      (u32) a1 = resource domain ID
 */
#define RDMA_V4_SUBCMD_RST_RESOURCE_DOMAIN  1

/*
 * Allocate a soft event CQ for a RDMA-enabled vnic (Future implementation)
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_CREATE_EVTQ
 *      (u64) a1 = ring base address
 *      (u32) a2 = ring size
 *      (u32) a3 = interrupt vector (idx w/in vnic's intr range)
 *                 (pass RDMA_INTR_NULL_IDX for no interrupt)
 *
 * out: (u32) a0 = event CQ ID
 */
#define RDMA_V4_SUBCMD_CREATE_EVTQ          2

/*
 * Dealloc soft event CQ for a RDMA-enabled vnic (Future implementation)
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_DESTROY_EVTQ
 *      (u64) a1 = event CQ ID
 *
 */
#define RDMA_V4_SUBCMD_DESTROY_EVTQ         3

/*
 * Allocate a soft CQ for a RDMA-enabled vnic
 *
 * in:  (u32) a0 = RDMA_SUBCMD_CREATE_CQ
 *      (u64) a1 = ring base address
 *      (u32) a2 = ring size
 *      (u32) a3 = event CQ ID. 0xFFFF if not associated with an event CQ
 *      (u32) a4 = interrupt vector (idx w/in vnic's intr range)
 *                 (pass RDMA_INTR_NULL_IDX for no interrupt)
 *
 * out: (u32) a0 = CQ ID
 */
#define RDMA_V4_SUBCMD_CREATE_CQ            4

/*
 * Deallocate a soft CQ.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_DESTROY_CQ
 *      (u32) a1 = CQ ID
 */
#define RDMA_V4_SUBCMD_DESTROY_CQ           5

/*
 * Allocate a PD in a RDMA-enabled vNIC.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_ALLOC_PD
 *
 * out: (u32) a0 = PD ID
 */
#define RDMA_V4_SUBCMD_ALLOC_PD             6

/*
 * De-allocate a PD in a RDMA-enabled vNIC.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_ALLOC_PD
 *      (u32) a1 = PD ID
 *
 */
#define RDMA_V4_SUBCMD_DEALLOC_PD           7

/*
 * Allocate a QP (with one SQ and one RQ).
 *
 * in:  (u32) a0 = RDMA_SUBCMD_V4_CREATE_QP
 *      (u32) a1 = PD ID
 *      (u32) a2 = QP type (see RDMA_QP_TYPE_xxx)
 *      (u32) a3 = max SQ WRs
 *      (u32) a4 = max RQ WRs
 *      (u32) a5 = SQ CQ ID
 *      (u32) a6 = RQ CQ ID
 *      (u32) a7 = desired QPN (or RDMA_ANY_QPN if don't care)
 *      (u32) a8 = QP flags
 *      (u64) a9 = SQ ring base ptr
 *      (u64) a10 = RQ ring base ptr
 *
 * out: (u32) a0 = QPN
 */
#define RDMA_V4_SUBCMD_CREATE_QP            8

/*
 * Modify the state of an existing QP.  This is primarily used to transition
 * the QP from one state to the next.  The "current state" argument must match
 * the QP's actual current state or the command will fail.  If the driver and
 * firmware get out of sync, the actual current state can be queried with
 * RDMA_SUBCMD_QUERY_QP.
 *
 * The next-hop MAC, peer IP, and peer QPN arguments are ignored if the new
 * state is not RTR.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_MODIFY_QP
 *      (u32) a1 = QPN
 *      (u32) a2 = current state
 *      (u32) a3 = new state
 *      (u64) a4 = next-hop MAC to destination IP (see MAC encoding note above)
 *      (u64) a5 = destination IP address in network byte order
 *      (u32) a6 = peer QPN
 *      (u32) a7 = path MTU (one of: 512/1024/2048/4096, 0 means no change)
 *      (u64) a8 = upper 32-bits: SQ PSN (RDMA_PSN_UNCHANGED means no change)
 *                 lower 32-bits: RQ PSN (RDMA_PSN_UNCHANGED means no change)
 *      (u32) a9 = Q_Key (UD QPs only)
 *      (u32) a10 = source IPv4 address in network byte order
 *      (u32) a11 = udp src port in packets for this QP
 *      (u32) a12 = flags (see RDMA_MOD_QPF_XXX)
 *      (u32) a13 = GID index (valid if RDMA_MOD_QPF_GID is set in flags)
 */
#define RDMA_V4_SUBCMD_MODIFY_QP            9

/*
 * Status for modify QP dev_cmd.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_MODIFY_QP_STATUS
 *      (u32) a1 = QPN
 *
 * out: (u32) a0 = ERR_EINPROGRESS/ERR_EBADSTATE/ERR_SUCCESS
 */
#define RDMA_V4_SUBCMD_MODIFY_QP_STATUS     10

/*
 * Query current QP status.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_QUERY_QP
 *      (u32) a1 = QPN
 *
 * out: (u32) a0 = QPN
 *      (u32) a1 = current QP state
 *      (u32) a2 = path MTU
 *      (u32) a3 = current SQ PSN
 *      (u32) a4 = current RQ PSN
 */
#define RDMA_V4_SUBCMD_QUERY_QP             11

/*
 * Deallocate a QP.
 *
 * in:  (u32) a0 = RDMA_SUBCMD_DESTROY_QP
 *      (u32) a1 = QPN
 */
#define RDMA_V4_SUBCMD_DESTROY_QP           12

/*
 * Status for deallocate QP dev_cmd.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_DESTROY_QP_STATUS
 *      (u32) a1 = QPN
 *
 * out: (u32) a0 = ERR_EINPROGRESS/ERR_EBADSTATE/ERR_SUCCESS
 */
#define RDMA_V4_SUBCMD_DESTROY_QP_STATUS    13

/*
 * Retrieve a snapshot of current statistics for this vnic's
 * rdma engine
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_GET_STATS
 *
 * out: (u64) a0 = IG packet count
 *      (u64) a1 = IG byte count
 *      (u64) a2 = EG packet count
 *      (u64) a3 = EG byte count
 */
#define RDMA_V4_SUBCMD_GET_STATS            14

/*
 * Softreset a RDMA vNIC (just RDMA -- the underlying enic vNIC is left
 * untouched). This is an asynchronous devcmd; i.e., driver should poll
 * for completion via RDMA_V4_SUBCMD_SOFTRESET_STATUS.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_SOFTRESET
 */
#define RDMA_V4_SUBCMD_SOFTRESET 15

/*
 * Check the status of previously issued softreset.
 *
 * in:  (u32): a0 = RDMA_V4_SUBCMD_SOFTRESET_STATUS
 * out: (u32): a0 = ERR_SUCCESS: softreset succeeded (or)
 *		    ERR_EINPROGRESS: softreset is still in progress (or)
 *		    ERR_EBADSTATE: softreset failed.
 */
#define RDMA_V4_SUBCMD_SOFTRESET_STATUS 16

/*
 * Modify the state of an existing QP. This is similar to
 * RDMA_V4_SUBCMD_MODIFY_QP, but firmware eDMAs the QP attributes from
 * host buffer.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_MODIFY_QP_EDMA
 *      (u64) a1 = host PA that has struct rdma_modify_qp_attr
 *      (u32) a2 = length of host buffer that holds rdma_modify_qp_attr
 */
#define RDMA_V4_SUBCMD_MODIFY_QP_EDMA 17

/*
 * Initialize (and enable) or disable RDMA notify block, and the associated
 * interrupt.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_NOTIFY
 *      (u32) a1 = Notify type:
 *                  RDMA_NOTIFY_TYPE_REG = Register notifications.
 *                  RDMA_NOTIFY_TYPE_DEREG = De-register notifications.
 *      (u64) a2 = Host DMAable address for notify block (0x0 = disable notify).
 *      (u32) a3 = Size of the notify block.
 *      (u32) a4 = Interrupt number realtive to the vNIC's interrupts.
 *      (u64) a5 = Cookie to be used in future notifications.
 */
#define RDMA_V4_SUBCMD_NOTIFY 18

/*
 * Ack for RDMA_V4_SUBCMD_NOTIFY devcmd.
 *
 * in:  (u64) a0 = sequence number being acknowledged.
 */
#define RDMA_V4_SUBCMD_NOTIFY_ACK 19

/*
 * Query the status of previously issuesd notify devcmd.
 *
 * Notification de-registraiton is asynchronous, and thus a status command is
 * required.
 *
 * in:  (u32) a0: RDMA_V4_SUBCMD_NOTIFY_STATUS
 *      (u32) a1: Query status of registration or de-registration:
 *                  RDMA_NOTIFY_TYPE_REG: status of registration
 *                  RDMA_NOTIFY_TYPE_DE_REG: status of de-registration
 *
 * out: (u64) a0: Status of previously issued (de-)register.
 *                  ERR_SUCCESS: Complete. Safe to free host notify buffer.
 *                  ERR_EINPROGRESS: Still in progress. Query again.
 */
#define RDMA_V4_SUBCMD_NOTIFY_STATUS 20

/*
 * Status for set RDMA cos.
 *
 * in:  (u32) a0 = RDMA_V4_SUBCMD_SET_COS
 *      (u31) a1 = cos
 */
#define RDMA_V4_SUBCMD_SET_COS 21

/*
 * Query firmware about SGL buffer's offset and length within the memory
 * buffer (resource type RES_TYPE_MEM) exposed via BAR.
 *
 * in:  (u32) a0 = RDMA_V4_SGL_BUF_QUERY
 *
 * out: (u32) a0 = Offset of SGL buffer within BAR's memory buffer.
 *      (u32) a1 = Length of SGL buffer in bytes.
 *
 * errors: ERR_ENOTSUPPORTED if firmware does not support SGL buffer.
 */
#define RDMA_V4_SGL_BUF_QUERY 22

#define RDMA_V4_SUBCMD_MAX 256

/*
 * Defines and Capabilities for CMD_CQ_ENTRY_SIZE_SET
 */
#define VNIC_RQ_ALL                     (~0ULL)

#define VNIC_RQ_CQ_ENTRY_SIZE_16        0
#define VNIC_RQ_CQ_ENTRY_SIZE_32        1
#define VNIC_RQ_CQ_ENTRY_SIZE_64        2

#define VNIC_RQ_CQ_ENTRY_SIZE_16_CAPABLE        (1 << VNIC_RQ_CQ_ENTRY_SIZE_16)
#define VNIC_RQ_CQ_ENTRY_SIZE_32_CAPABLE        (1 << VNIC_RQ_CQ_ENTRY_SIZE_32)
#define VNIC_RQ_CQ_ENTRY_SIZE_64_CAPABLE        (1 << VNIC_RQ_CQ_ENTRY_SIZE_64)

#define VNIC_RQ_CQ_ENTRY_SIZE_ALL_BIT	(VNIC_RQ_CQ_ENTRY_SIZE_16_CAPABLE | \
					 VNIC_RQ_CQ_ENTRY_SIZE_32_CAPABLE | \
					 VNIC_RQ_CQ_ENTRY_SIZE_64_CAPABLE)

#endif /* _VNIC_DEVCMD_H_ */
