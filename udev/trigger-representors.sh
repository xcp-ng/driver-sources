#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

# Copyright 2021 Xilinx Inc.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, incorporated herein by reference.


# This file normally goes in the /usr/lib/sfc directory.

# $1 - current name of interface
# $2 - phys_port_id of interface
orig_name="$1"

LOCKDIR=/tmp/sfc_interface_renaming.lock

# Change to a proper file name in order to enable debug logging.
#LOGFILE=/tmp/sfc_trigger.log
LOGFILE=/dev/null

# Don't forget to change udev's log level via udevadm when changing this line.
# Change to anything other than an empty value to enable logging to udev.
LOG_TO_STDERR=

function dbgPrint {
	echo "$$ $orig_name: $*" >> $LOGFILE
	[ -n "$LOG_TO_STDERR" ] && echo "$$ $orig_name: $*" 1>&2
}

# How long to sleep between mutex directory checks. Value is in seconds.
# Do not set to a value lower than 0.001 seconds (1 millisecond)
SLEEP_DURATION=0.1
# Maximum amount of time in seconds to wait for mutex.
SLEEP_MAX=30
SLEEP_TIMER=0

# Conversion done in order to be able to use arithmetic operations.
SLEEP_DURATION_IN_MS="$(printf %.0f "${SLEEP_DURATION}e3")"

while ! mkdir "$LOCKDIR" > /dev/null 2> /dev/null; do
	sleep "$SLEEP_DURATION"

	if (( (SLEEP_TIMER += SLEEP_DURATION_IN_MS) >= (SLEEP_MAX * 1000) )); then
		dbgPrint "Timed out while waiting for mutex directory."
		break
	fi
done

if (( SLEEP_TIMER < (SLEEP_MAX * 1000) )); then
	# If the loop above timed out, removing the lock directory is not a sensible idea.
	trap 'rm -rf "$LOCKDIR"' EXIT KILL TERM INT
fi

dbgPrint ==================================
dbgPrint $(date)
dbgPrint "$2"

if [ -n "$LOG_TO_STDERR" ]; then
	udevadm trigger -v -c add -s net -a "phys_switch_id=$2" 1>&2
elif [ -n "$LOGFILE" ]; then
	udevadm trigger -v -c add -s net -a "phys_switch_id=$2" >> "$LOGFILE" 2>> "$LOGFILE"
else
	udevadm trigger -v -c add -s net -a "phys_switch_id=$2" > /dev/null 2>> /dev/null
fi

exit 0
