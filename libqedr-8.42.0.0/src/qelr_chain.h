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

#ifndef __QELR_CHAIN_H__
#define __QELR_CHAIN_H__

/* fast path functions are inline */

static inline uint32_t qelr_chain_get_cons_idx_u32(struct qelr_chain *p_chain)
{
	return p_chain->cons_idx;
}

static inline void *qelr_chain_produce(struct qelr_chain *p_chain)
{
	void *p_ret = NULL;

	p_chain->prod_idx++;

	p_ret = p_chain->p_prod_elem;

	if (likely(p_chain->p_prod_elem != p_chain->last_addr))
		p_chain->p_prod_elem = (void *)(((uint8_t *)p_chain->p_prod_elem) +
				       p_chain->elem_size);
	else
		p_chain->p_prod_elem = p_chain->first_addr;

	return p_ret;
}

/* qelr_chain_produce_cons_bytes returns a pointer to consecutive number of
 * bytes, equal or smaller or larger to the requested number of bytes. Prior to
 * this call the user must already check that the chain has enough bytes.
 * The only reason the function may return less bytes is if the it reached the
 * end of the SQ i.e. wrap.
 * The only reason the the function may return more bytes is due to alignment to
 * element size. Note that aligment size is passed as a field and not used as
 * field of the chain so the division will be faster (it is expected the
 * parameter will be passed as a constant).
 */
static inline void *qelr_chain_produce_bytes(struct qelr_chain *p_chain,
					     uint32_t bytes_requested,
					     uint32_t *bytes_granted,
					     uint32_t elem_size)
{
	uint32_t bytes_available, bytes_to_grant;
	void *p_ret;

	/* Cast to void to get the number of bytes */
	bytes_available = ((uint8_t *)p_chain->last_addr -
			   (uint8_t *)p_chain->p_prod_elem) + elem_size;

	bytes_to_grant = min_t(uint32_t, bytes_available, bytes_requested);
	bytes_to_grant = ALIGN(bytes_to_grant, elem_size);

	p_chain->prod_idx += bytes_to_grant / elem_size;
	p_ret = p_chain->p_prod_elem;

	/* if prod_elem pointed to last_addr then only 1 element was granted */
	if (likely(bytes_to_grant != bytes_available))
		p_chain->p_prod_elem = (void *)
				       (((uint8_t *)p_chain->p_prod_elem) +
				       bytes_to_grant);
	else
		p_chain->p_prod_elem = p_chain->first_addr;

	*bytes_granted = bytes_to_grant;

	return p_ret;
}

static inline void *qelr_chain_consume(struct qelr_chain *p_chain)
{
	void *p_ret = NULL;

	p_chain->cons_idx++;

	p_ret = p_chain->p_cons_elem;

	if (p_chain->p_cons_elem == p_chain->last_addr)
		p_chain->p_cons_elem = p_chain->first_addr;
	else
		p_chain->p_cons_elem	= (void *)
					  (((uint8_t *)p_chain->p_cons_elem) +
					   p_chain->elem_size);

	return p_ret;
}

static inline void *qelr_chain_consume_n(struct qelr_chain *p_chain, int n)
{
	void *p_ret = NULL;
	int n_wrap;

	p_chain->cons_idx += n;
	p_ret = p_chain->p_cons_elem;

	n_wrap = p_chain->cons_idx % p_chain->n_elems;
	if (n_wrap < n)
		p_chain->p_cons_elem = (void *)
				       (((uint8_t *)p_chain->first_addr) +
					(p_chain->elem_size * n_wrap));
	else
		p_chain->p_cons_elem = (void *)(((uint8_t *)p_chain->p_cons_elem) +
				       (p_chain->elem_size * n));

	return p_ret;
}

static inline uint32_t qelr_chain_get_elem_left_u32(struct qelr_chain *p_chain)
{
	uint32_t used;

	used = (uint32_t)(((uint64_t)((uint64_t) ~0U) + 1 +
			  (uint64_t)(p_chain->prod_idx)) -
			  (uint64_t)p_chain->cons_idx);

	return p_chain->n_elems - used;
}

static inline uint8_t qelr_chain_is_full(struct qelr_chain *p_chain)
{
	return qelr_chain_get_elem_left_u32(p_chain) == p_chain->n_elems;
}

static inline void qelr_chain_set_prod(
		struct qelr_chain *p_chain,
		uint32_t prod_idx,
		void *p_prod_elem)
{
	p_chain->prod_idx = prod_idx;
	p_chain->p_prod_elem = p_prod_elem;
}

void *qelr_chain_get_last_elem(struct qelr_chain *p_chain);
void qelr_chain_reset(struct qelr_chain *p_chain);
int qelr_chain_alloc(struct qelr_chain *chain, int chain_size, int page_size,
		     uint16_t elem_size);
void qelr_chain_free(struct qelr_chain *buf);

#endif /* __QELR_CHAIN_H__ */
