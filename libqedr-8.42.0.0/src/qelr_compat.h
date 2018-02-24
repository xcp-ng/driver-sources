/* QLogic (R)NIC Driver/Library
 * Copyright (c) 2010-2017  Cavium, Inc.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __QELR_COMPAT_H__
#define __QELR_COMPAT_H__

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define dma_addr_t uint64_t
#define cpu_to_le32 htole32
#define cpu_to_le16 htole16

#define min_t(type, x, y) ({		\
	type __max1 = (x);		\
	type __max2 = (y);		\
	__max1 < __max2 ? __max1: __max2; })

#define max_t(type, x, y) ({		\
	type __max1 = (x);		\
	type __max2 = (y);		\
	__max1 > __max2 ? __max1: __max2; })

#define QED_U32_MAX		((u32) ~0U)

#define get_qelr_xxx(xxx, type)						\
	((struct qelr_##type *)						\
	((void *) ib##xxx - offsetof(struct qelr_##type, ibv_##xxx)))

#ifndef container_of
#define container_of(ptr, type, member) ({				\
		const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
		(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef min
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#endif

#endif /* __QELR_COMPAT_H__ */
