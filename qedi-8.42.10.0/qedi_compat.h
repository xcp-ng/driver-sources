/*
 *  QLogic iSCSI Offload Driver
 *  Copyright (c) 2015-2018 Cavium Inc.
 *  
 *  See LICENSE.qedi for copyright and licensing details.
 */

#ifndef ETHER_ADDR_COPY
/**
 ** ether_addr_copy - Copy an Ethernet address
 ** @dst: Pointer to a six-byte array Ethernet address destination
 ** @src: Pointer to a six-byte array Ethernet address source
 **
 ** Please note: dst & src must both be aligned to u16.
 **/
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}
#endif

/* RHEL 7.2 does not have nr_hw_queues in Scsi_Host, so disable MQ */
#if defined(RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(7, 2))
#undef NR_HW_QUEUES
#endif
