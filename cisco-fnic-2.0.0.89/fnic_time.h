/*
 * Copyright 2013 Cisco Systems, Inc.  All rights reserved.
 * 
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Contributed by Paul Eggert (eggert@twinsun.com).
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
This program is free software; you may redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#ifndef __FNIC_TIME_H__
#define __FNIC_TIME_H__

#include <linux/time.h>
/*
 * Similar to the struct tm in userspace <time.h>,
 */
struct tm {
        /*
         * the number of seconds after the minute, normally in the range
         * 0 to 59, but can be up to 60 to allow for leap seconds
         */
        int tm_sec;
        /* the number of minutes after the hour, in the range 0 to 59*/
        int tm_min;
        /* the number of hours past midnight, in the range 0 to 23 */
        int tm_hour;
        /* the day of the month, in the range 1 to 31 */
        int tm_mday;
        /* the number of months since January, in the range 0 to 11 */
        int tm_mon;
        /* the number of years since 1900 */
        long tm_year;
        /* the number of days since Sunday, in the range 0 to 6 */
        int tm_wday;
        /* the number of days since January 1, in the range 0 to 365 */
        int tm_yday;
};

void time_to_tm(time_t totalsecs, int offset, struct tm *result);

#endif
