/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2024 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2011 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#ifndef _LPFC_COMPAT_H
#define _LPFC_COMPAT_H

/*
 * This file provides macros to aid compilation in the Linux 2.4 kernel
 * over various platform architectures.
 */

/*******************************************************************
Note: HBA's SLI memory contains little-endian LW.
Thus to access it from a little-endian host,
memcpy_toio() and memcpy_fromio() can be used.
However on a big-endian host, copy 4 bytes at a time,
using writel() and readl().
 *******************************************************************/
#include <asm/byteorder.h>
#include <scsi/scsi_eh.h>
#include <linux/version.h>

#if defined __has_attribute
   #if __has_attribute(__fallthrough__)
      # define fallthrough                    __attribute__((__fallthrough__))
   #else
      # define fallthrough                    do {} while (0)  /* fallthrough */
   #endif
#else
   #define fallthrough      do {} while (0)  /* fallthrough */
#endif

/* Needed for 3.10 kernel */
#if !defined(MIN_NICE)
#define MIN_NICE	-20
#endif
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif

#ifndef T10_PI_APP_ESCAPE
#define T10_PI_APP_ESCAPE cpu_to_be16(0xffff)
#endif
#ifndef T10_PI_REF_ESCAPE
#define T10_PI_REF_ESCAPE cpu_to_be32(0xffffffff)
#endif

#ifndef PCI_HEADER_TYPE_MFD
#define PCI_HEADER_TYPE_MFD 0x80 /* Multi-Function Device (possible) */
#endif

#ifndef BUILD_USE_STRUCT_GRP
/**
 * __struct_group() - Create a mirrored named and anonyomous struct
 *
 * @TAG: The tag name for the named sub-struct (usually empty)
 * @NAME: The identifier name of the mirrored sub-struct
 * @ATTRS: Any struct attributes (usually empty)
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical layout
 * and size: one anonymous and one named. The former's members can be used
 * normally without sub-struct naming, and the latter can be used to
 * reason about the start, end, and size of the group of struct members.
 * The named struct can also be explicitly tagged for layer reuse, as well
 * as both having struct attributes appended.
 */
#define __struct_group(TAG, NAME, ATTRS, MEMBERS...) \
	union { \
		struct { MEMBERS } ATTRS; \
		struct TAG { MEMBERS } ATTRS NAME; \
	}

/**
 * struct_group() - Wrap a set of declarations in a mirrored struct
 *
 * @NAME: The identifier name of the mirrored sub-struct
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members.
 */
#define struct_group(NAME, MEMBERS...) \
	__struct_group(/* no tag */, NAME, /* no attrs */, MEMBERS)

/**
 * struct_group_attr() - Create a struct_group() with trailing attributes
 *
 * @NAME: The identifier name of the mirrored sub-struct
 * @ATTRS: Any struct attributes to apply
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members. Includes structure attributes argument.
 */
#define struct_group_attr(NAME, ATTRS, MEMBERS...) \
	__struct_group(/* no tag */, NAME, ATTRS, MEMBERS)

/**
 * struct_group_tagged() - Create a struct_group with a reusable tag
 *
 * @TAG: The tag name for the named sub-struct
 * @NAME: The identifier name of the mirrored sub-struct
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members. Includes struct tag argument for the named copy,
 * so the specified layout can be reused later.
 */
#define struct_group_tagged(TAG, NAME, MEMBERS...) \
	__struct_group(TAG, NAME, /* no attrs */, MEMBERS)

#endif

#ifndef BUILD_DC_SCSI_DONE
#define scsi_done(cmd) ((cmd)->scsi_done(cmd))
#endif

#ifndef BUILD_USE_ML_PI_AVAIL
#define scsi_cmd_to_rq(scmd) ((scmd)->request)

static inline unsigned int scsi_logical_block_count(struct scsi_cmnd *scmd)
{
	unsigned int shift = ilog2(scmd->device->sector_size) - SECTOR_SHIFT;

	return blk_rq_bytes(scsi_cmd_to_rq(scmd)) >> shift;
}
#endif

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
#define lpfc_strscpy(dest, src, count) strlcpy(dest, src, count)
#else
#define lpfc_strscpy(dest, src, count) strscpy(dest, src, count)
#endif

#if KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE
#define lpfc_get_random_u16() (0xFFFF & prandom_u32())
#else
#define lpfc_get_random_u16() get_random_u16()
#endif

#if KERNEL_VERSION(5, 14, 0) > LINUX_VERSION_CODE

#define lpfc_set_driver_byte(cmd, status) \
	((cmd)->result = ((cmd)->result & 0x00ffffff) | ((status) << 24))

#ifndef BUILD_UB2110
/**
 * scsi_build_sense - build sense data for a command
 * @scmd:	scsi command for which the sense should be formatted
 * @desc:	Sense format (non-zero == descriptor format,
 *              0 == fixed format)
 * @key:	Sense key
 * @asc:	Additional sense code
 * @ascq:	Additional sense code qualifier
 *
 **/
static inline void
scsi_build_sense(struct scsi_cmnd *scmd, int desc, u8 key, u8 asc, u8 ascq)
{
	scsi_build_sense_buffer(desc, scmd->sense_buffer, key, asc, ascq);
	scmd->result = SAM_STAT_CHECK_CONDITION;
}
#endif

#else

#if !defined(DRIVER_SENSE)
#define DRIVER_SENSE 0x08
#endif

#define lpfc_set_driver_byte(cmd, status) {}

#endif

#if KERNEL_VERSION(3, 18, 0) > LINUX_VERSION_CODE
static inline unsigned int scsi_prot_interval(struct scsi_cmnd *scmd)
{
	return scmd->device->sector_size;
}
#endif

#ifndef memset_startat
/**
 * memset_startat - Set a value starting at a member to the end of a struct
 *
 * @obj: Address of target struct instance
 * @v: Byte value to repeatedly write
 * @member: struct member to start writing at
 *
 * Note that if there is padding between the prior member and the target
 * member, memset_after() should be used to clear the prior padding.
 */
#define memset_startat(obj, v, member)					\
({									\
	u8 *__ptr = (u8 *)(obj);					\
	typeof(v) __val = (v);						\
	memset(__ptr + offsetof(typeof(*(obj)), member), __val,		\
	       sizeof(*(obj)) - offsetof(typeof(*(obj)), member));	\
})
#endif

#ifdef __BIG_ENDIAN

static inline void
lpfc_memcpy_to_slim(void __iomem *dest, void *src, unsigned int bytes)
{
	uint32_t __iomem *dest32;
	uint32_t *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t __iomem *) dest;
	src32  = (uint32_t *) src;

	/* write input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		writel( *src32, dest32);
		readl(dest32); /* flush */
		dest32++;
		src32++;
	}

	return;
}

static inline void
lpfc_memcpy_from_slim( void *dest, void __iomem *src, unsigned int bytes)
{
	uint32_t *dest32;
	uint32_t __iomem *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t *) dest;
	src32  = (uint32_t __iomem *) src;

	/* read input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		*dest32 = readl( src32);
		dest32++;
		src32++;
	}

	return;
}

#else

static inline void
lpfc_memcpy_to_slim( void __iomem *dest, void *src, unsigned int bytes)
{
	/* convert bytes in argument list to word count for copy function */
	__iowrite32_copy(dest, src, bytes / sizeof(uint32_t));
}

static inline void
lpfc_memcpy_from_slim( void *dest, void __iomem *src, unsigned int bytes)
{
	/* actually returns 1 byte past dest */
	memcpy_fromio( dest, src, bytes);
}

#endif	/* __BIG_ENDIAN */
#endif	/* _LPFC_COMPAT_H */
