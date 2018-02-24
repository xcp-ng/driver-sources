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

#ifndef FNIC_CONFIG_H
#error "This file is not meant to be included directly."
#endif

/* Sanity check: ensure that we got at least one of the timers */
#if !FNIC_HAVE_TIMER_SETUP && !FNIC_HAVE_SETUP_TIMER
#error "Do not have any known version of kernel timer setup function"
#endif

/* We want to prefer setup_timer() */
#if FNIC_HAVE_SETUP_TIMER
#define FNIC_USE_SETUP_TIMER 1
#else
#define FNIC_USE_SETUP_TIMER 0
#endif

/* Sanity check: ensure that we got at least one of the strtouls */
#if !FNIC_HAVE_KSTRTOUL && !FNIC_HAVE_STRICT_STRTOUL
#error "Do not have any known version of *strtoul()"
#endif

/* We want to prefer kstrtoul */
#if FNIC_HAVE_KSTRTOUL
#define FNIC_USE_KSTRTOUL 1
#else
#define FNIC_USE_KSTRTOUL 0
#endif

/* Sanity check: ensure that we have at least one of the FIP_SC_VL_*s */
#if !FNIC_HAVE_SC_VL_NOTE && !FNIC_HAVE_SC_VL_REP
#error "Do not have any known version of FIC_SC_VL_[NOTE|REP]"
#endif

#if FNIC_HAVE_SC_VL_NOTE
#define FNIC_USE_FIP_SC_VL_NOTE 1
#else
#define FNIC_USE_FIP_SC_VL_NOTE 0
#endif

/* Sanity check: ensure that we have at least one of the ether checks */
#if !FNIC_HAVE_COMPARE_ETHER_ADDR && !FNIC_HAVE_ETHER_ADDR_EQUAL
#error "Do not have any known version of ether_addr comparison function"
#endif

#if FNIC_HAVE_COMPARE_ETHER_ADDR
#define FNIC_USE_COMPARE_ETHER_ADDR 1
#else
#define FNIC_USE_COMPARE_ETHER_ADDR 0
#endif

/* Sanity check: ensure that we have at least one of the
   pci_enable_msix functions */
#if !FNIC_HAVE_PCI_ENABLE_MSIX_EXACT && !FNIC_HAVE_PCI_ENABLE_MSIX
#error "Do not have any known version of pci_enable_msix*()"
#endif

#if FNIC_HAVE_PCI_ENABLE_MSIX_EXACT
#define FNIC_USE_PCI_ENABLE_MSIX_EXACT 1
#else
#define FNIC_USE_PCI_ENABLE_MSIX_EXACT 0
#endif

/* Sanity check: enusre that we have one of the "current time"
   functions */
#if !FNIC_HAVE_CURRENT_KERNEL_TIME && !FNIC_HAVE_KTIME_GET_REAL_TS64 && !defined(CURRENT_TIME)
#error "Do not have any known version of current kernel time function"
#endif

/* Sanity check: ensure that we have one of the time-to-tm
   functions */
#if !FNIC_HAVE_TIME_TO_TM && !FNIC_HAVE_TIME64_TO_TM
#error "Do not have any known version of time*_to_tm()"
#endif

#if FNIC_HAVE_TIME_TO_TM
#define FNIC_USE_TIME_TO_TM 1
#else
#define FNIC_USE_TIME_TO_TM 0
#endif
