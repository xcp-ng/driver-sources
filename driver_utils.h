/*
 * Copyright 2008-2013 Cisco Systems, Inc.  All rights reserved.
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
#ifndef __DRIVER_UTILS_H__
#define __DRIVER_UTILS_H__

#include <linux/string.h>

static inline int driver_encode_asic_info(char *str, int strlen, u16 asic_type,
				u16 asic_rev)
{
	if (strlen < sizeof(asic_type) + sizeof(asic_rev))
		return -EINVAL;

	memcpy(str, &asic_type, sizeof(asic_type));
	memcpy(str + sizeof(asic_type), &asic_rev, sizeof(asic_rev));

	return 0;
}

static inline int driver_decode_asic_info(char *str, int strlen, u16 *asic_type,
						u16 *asic_rev)
{
	if (strlen < sizeof(*asic_type) + sizeof(*asic_rev))
		return -EINVAL;

	if (asic_type)
		memcpy(asic_type, str, sizeof(*asic_type));
	if (asic_rev)
		memcpy(asic_rev, str + sizeof(*asic_type), sizeof(*asic_rev));
	return 0;
}

#endif /*!__DRIVER_UTILS_H__*/
