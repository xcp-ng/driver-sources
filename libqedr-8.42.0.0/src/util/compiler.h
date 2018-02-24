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

/* GPLv2 or OpenIB.org BSD (MIT) See COPYING file */
#ifndef UTIL_COMPILER_H
#define UTIL_COMPILER_H

/* Use to tag a variable that causes compiler warnings. Use as:
    int uninitialized_var(sz)

   This is only enabled for old compilers. gcc 6.x and beyond have excellent
   static flow analysis. If code solicits a warning from 6.x it is almost
   certainly too complex for a human to understand.
*/
#if __GNUC__ >= 6 || defined(__clang__)
#define uninitialized_var(x) x
#else
#define uninitialized_var(x) x = x
#endif

#ifndef likely
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#else
#define likely(x)      (x)
#endif
#endif

#ifndef unlikely
#ifdef __GNUC__
#define unlikely(x)      __builtin_expect(!!(x), 0)
#else
#define unlikely(x)    (x)
#endif
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* Use to mark fall through on switch statements as desired. */
#if __GNUC__ >= 7
#define SWITCH_FALLTHROUGH __attribute__ ((fallthrough))
#else
#define SWITCH_FALLTHROUGH
#endif

#endif
