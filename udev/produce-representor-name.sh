#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

# Copyright 2021 Xilinx Inc.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, incorporated herein by reference.

# $1 - current name of representor
# $2 - phys_port_name of representor
# $3 - phys_switch_id of representor
orig_name="$1"
phys_port_name="$2"
phys_switch_id="$3"

# By default, no renaming will happen.
new_name="$orig_name"

# This script's standard output is meant to be read by udev.
# The contents written to stdout will become the name of the interface.

# This file normally goes in the /usr/lib/sfc directory.

LOCKDIR=/tmp/sfc_interface_renaming.lock

# Change to a proper file name in order to enable debug logging.
#LOGFILE=/tmp/sfc_produce_representor_name.log
LOGFILE=/dev/null

# Don't forget to change udev's log level via udevadm when changing this line.
# Change to anything other than an empty value to enable logging to udev.
LOG_TO_STDERR=

# How long to sleep between interface listings. Value is in seconds.
# Do not set to a value lower than 0.001 seconds (1 millisecond)
INTERFACE_ENUMERATION_SLEEP_DURATION=0.1
# Maximum amount of time in seconds to wait for interface to show up, plus one
# second due to lack of precision.
INTERFACE_ENUMERATION_TIME_LIMIT=4
# This is required because sysfs optimizations sometimes cause directories and
# files (attributes) to be invisible to CPUs other than the ones which created
# them in sysfs for a few seconds after creation.
INTERFACE_ENUMERATION_START=0

dbgPrint() {
	echo "$$ $orig_name: $*" >> $LOGFILE
	[ -n "$LOG_TO_STDERR" ] && echo "$$ $orig_name: $*" 1>&2
}

while ! mkdir "$LOCKDIR" > /dev/null 2> /dev/null; do
	sleep 0.1
done

trap 'rm -rf "$LOCKDIR"' EXIT KILL TERM INT

dbgPrint ==================================
dbgPrint $(date)
dbgPrint phys_port_name=$phys_port_name
dbgPrint phys_switch_id=$phys_switch_id

regexPFREP="^p([0-9]+)if([0-9]+)pf([0-9]+)$"
regexVFREP="^p([0-9]+)pf([0-9]+)vf([0-9]+)$"
regexSOCVFREP="^p([0-9]+)if([0-9]+)pf([0-9]+)vf([0-9]+)$"

regexPFNAME0="^eth([0-9]+)$"
regexPFNAME1="^ens([0-9]+)f([0-9]+)(n.+)?$"
regexPFNAME2="^enp([0-9]+)s([0-9]+)f([0-9]+)(n.+)?$"
regexPFNAME3="^enP([0-9]+)p([0-9]+)s([0-9]+)f([0-9]+)(n.+)?$"

# $1 - directory of the PF in sysfs
# $2 - representor if number
# $3 - representor pf number
getPfRepName() {
	local pf="$1"
	local name="$(basename "$pf")"
	local rep_if="$2"
	local rep_pf="$3"

	dbgPrint "Basename of $pf is $name"
	dbgPrint "if $rep_if; pf $rep_pf"

	if [[ $name =~ $regexPFNAME0 ]]; then
		dbgPrint "Matching PF did not get a proper name yet."

		# No name is returned in this situation so the search for a matching PF continues.
		# This is useful, because this interface might get a proper name later.
		return 2
	elif [[ $name =~ $regexPFNAME1 ]]; then
		local slotNumber="${BASH_REMATCH[1]}"
		local portName="${BASH_REMATCH[3]}"

		dbgPrint "PF name match #1: $slotNumber ${BASH_REMATCH[2]} $portName"

		if [ "$rep_if" == "0" ]; then
			# This represents the function on the *current* side
			echo -n "ens${slotNumber}f${rep_pf}${portName}rep"
			dbgPrint "Returning ens${slotNumber}f${rep_pf}${portName}rep"
			return 0
		else
			# This represents the function on the *other* side
			echo -n "ens${slotNumber}f${rep_pf}${portName}soc"
			dbgPrint "Returning ens${slotNumber}f${rep_pf}${portName}soc"
			return 0
		fi
	elif [[ $name =~ $regexPFNAME2 ]]; then
		local portNumber="${BASH_REMATCH[1]}"
		local slotNumber="${BASH_REMATCH[2]}"
		local portName="${BASH_REMATCH[4]}"

		dbgPrint "PF name match #2: $portNumber $slotNumber ${BASH_REMATCH[3]} $portName"

		if [ "$rep_if" == "0" ]; then
			echo -n "enp${portNumber}s${slotNumber}f${rep_pf}${portName}rep"
			dbgPrint "Returning enp${portNumber}s${slotNumber}f${rep_pf}${portName}rep"
			return 0
		else
			echo -n "enp${portNumber}s${slotNumber}f${rep_pf}${portName}soc"
			dbgPrint "Returning enp${portNumber}s${slotNumber}f${rep_pf}${portName}soc"
			return 0
		fi
	elif [[ $name =~ $regexPFNAME3 ]]; then
		local domainNumber="${BASH_REMATCH[1]}"
		local portNumber="${BASH_REMATCH[2]}"
		local slotNumber="${BASH_REMATCH[3]}"
		local portName="${BASH_REMATCH[5]}"

		dbgPrint "PF name match #3: $domainNumber $portNumber $slotNumber ${BASH_REMATCH[4]} $portName"

		if [ "$rep_if" == "0" ]; then
			echo -n "enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}rep"
			dbgPrint "Returning enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}rep"
			return 0
		else
			echo -n "enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}soc"
			dbgPrint "Returning enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}soc"
			return 0
		fi
	else
		dbgPrint "Name of matching PF did not match a known format:"
		dbgPrint "$1"

		# A non-empty "name" is returned so the search for a matching PF stops.
		echo -n INVALID
		return 1
	fi

	dbgPrint "THIS SHOULD NOT BE REACHED"
}

# $1 - directory of the PF in sysfs
# $2 - representor pf number
# $3 - representor vf number
getVfRepName() {
	local pf="$1"
	local name="$(basename "$pf")"
	local rep_pf="$2"
	local rep_vf="$3"

	dbgPrint "Basename of $pf is $name"
	dbgPrint "pf $rep_pf; vf $rep_vf"

	if [[ $name =~ $regexPFNAME0 ]]; then
		dbgPrint "Matching PF did not get a proper name yet."

		return 2
	elif [[ $name =~ $regexPFNAME1 ]]; then
		local slotNumber="${BASH_REMATCH[1]}"
		local portName="${BASH_REMATCH[3]}"

		dbgPrint "PF name match #1: $slotNumber ${BASH_REMATCH[2]} $portName"

		# This represents the function on the *current* side
		echo -n "ens${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		dbgPrint "Returning ens${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		return 0
	elif [[ $name =~ $regexPFNAME2 ]]; then
		local portNumber="${BASH_REMATCH[1]}"
		local slotNumber="${BASH_REMATCH[2]}"
		local portName="${BASH_REMATCH[4]}"

		dbgPrint "PF name match #2: $portNumber $slotNumber ${BASH_REMATCH[3]} $portName"

		echo -n "enp${portNumber}s${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		dbgPrint "Returning enp${portNumber}s${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		return 0
	elif [[ $name =~ $regexPFNAME3 ]]; then
		local domainNumber="${BASH_REMATCH[1]}"
		local portNumber="${BASH_REMATCH[2]}"
		local slotNumber="${BASH_REMATCH[3]}"
		local portName="${BASH_REMATCH[5]}"

		dbgPrint "PF name match #3: $domainNumber $portNumber $slotNumber ${BASH_REMATCH[4]} $portName"

		echo -n "enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		dbgPrint "Returning enP${domainNumber}p${portNumber}s${slotNumber}f${rep_pf}${portName}v${rep_vf}rep"
		return 0
	else
		dbgPrint "Name of matching PF did not match a known format:"
		dbgPrint "$1"

		echo -n INVALID
		return 1
	fi

	dbgPrint "THIS SHOULD NOT BE REACHED"
}

# $1 - Interface's sysfs node
# $2 - phys_port_id to match
# returns 0 if this is a Xilinx ef100 PF, otherwise non-zero
checkNetworkInterface() {
	local nic="$1"
	local phys_switch_id="$2"

	dbgPrint "Looking at NIC $nic"

	phys_port_id="$(cat "$nic/phys_port_id" 2> /dev/null)"

	if [ "$?" != 0 ]; then
		dbgPrint "Failed to read phys_port_id"
		return 1
	fi

	if [ "$phys_switch_id" != "$phys_port_id" ]; then
		dbgPrint "$phys_switch_id != $phys_port_id"
		return 1
	fi

	device_vendor="$(cat "$nic/device/vendor" 2> /dev/null)"

	if [ "$?" != 0 ]; then
		dbgPrint "Failed to read PCI device vendor"
		return 1
	fi

	if [ "$device_vendor" != "0x10ee" ]; then
		dbgPrint "$device_vendor != 0x10ee"
		return 1
	fi

	device_class="$(cat "$nic/device/class" 2> /dev/null)"

	if [ "$?" != 0 ]; then
		dbgPrint "Failed to read PCI device class"
		return 1
	fi

	if [ "$device_class" != "0x020000" ]; then
		dbgPrint "$device_class != 0x020000"
		return 1
	fi

	dbgPrint "Found matching PF $nic"

	return 0
}

shouldLookForInterfacesAgain() {
	local CURRENT_TIME="$(date +%s)"
	# rep_name being non-empty means that a suitable PF was found for the representor.
	(( (CURRENT_TIME - INTERFACE_ENUMERATION_START) < INTERFACE_ENUMERATION_TIME_LIMIT )) && [ -z "$rep_name" ]
}

INTERFACE_ENUMERATION_START="$(date +%s)"
rep_name=""
ret=0

if [[ $phys_port_name =~ $regexPFREP ]]; then
	# If this is a PF representor, look for the matching PF's interface, and name this representor according to the PF.

	repPort="${BASH_REMATCH[1]}"
	repSide="${BASH_REMATCH[2]}"
	repPf="${BASH_REMATCH[3]}"

	dbgPrint "Found matches for PF representor!"
	dbgPrint "port: $repPort"
	dbgPrint "side: $repSide"
	dbgPrint "pf: $repPf"

	while shouldLookForInterfacesAgain; do
		for nic in /sys/class/net/*; do
			checkNetworkInterface "$nic" "$phys_switch_id" || continue

			rep_name="$(getPfRepName "$nic" "$repSide" "$repPf")"

			if [ "$?" == 0 ]; then
				new_name="$rep_name"
			fi

			# Whether a proper name was found or not, loop stops because a suitable PF was found.
			break
		done

		# No matching PF was found means that iteration will be attempted again,
		# but a short sleep is useful to prevent excessive loads on the system.
		[ -z "$rep_name" ] && sleep "$INTERFACE_ENUMERATION_SLEEP_DURATION"
	done
elif [[ $phys_port_name =~ $regexVFREP ]]; then
	# If this is a VF representor, look for the matching PF's interface. If found,
	# the representor will be named according to the VF as deduced from the PF's name.
	# The VF is not searched for, because it doesn't have a phys_port_id property that can be used.

	repPort="${BASH_REMATCH[1]}"
	repPf="${BASH_REMATCH[2]}"
	repVf="${BASH_REMATCH[3]}"

	dbgPrint "Found matches for VF representor!"
	dbgPrint "port: $repPort"
	dbgPrint "pf: $repPf"
	dbgPrint "vf: $repVf"

	while shouldLookForInterfacesAgain; do
		for nic in /sys/class/net/*; do
			checkNetworkInterface "$nic" "$phys_switch_id" || continue

			rep_name="$(getVfRepName "$nic" "$repPf" "$repVf")"

			if [ "$?" == 0 ]; then
				new_name="$rep_name"
			fi

			break
		done

		[ -z "$rep_name" ] && sleep "$INTERFACE_ENUMERATION_SLEEP_DURATION"
	done
elif [[ $phys_port_name =~ $regexSOCVFREP ]]; then
	# If this is a VF representor on the SoC, look for the matching PF's interface.
	# If found, the representor will be named according to the VF as deduced from the PF's name.
	# The VF is not searched for, because it doesn't have a phys_port_id property that can be used.

	repPort="${BASH_REMATCH[1]}"
	repSide="${BASH_REMATCH[2]}"
	repPf="${BASH_REMATCH[3]}"
	repVf="${BASH_REMATCH[4]}"

	dbgPrint "Found matches for VF representor!"
	dbgPrint "port: $repPort"
	dbgPrint "side: $repSide"
	dbgPrint "pf: $repPf"
	dbgPrint "vf: $repVf"

	if [ "$repSide" != "0" ]; then
		dbgPrint "Expected SoC VF representor if to be 0, got $repSide instead."
	fi

	while shouldLookForInterfacesAgain; do
		for nic in /sys/class/net/*; do
			checkNetworkInterface "$nic" "$phys_switch_id" || continue

			rep_name="$(getVfRepName "$nic" "$repPf" "$repVf")"

			if [ "$?" == 0 ]; then
				new_name="$rep_name"
			fi

			break
		done

		[ -z "$rep_name" ] && sleep "$INTERFACE_ENUMERATION_SLEEP_DURATION"
	done
else
	dbgPrint "Regexes don't match phys_port_name"

	echo -n "$orig_name"
	exit 1
fi

if [ -z "$rep_name" ]; then
	CURRENT_TIME="$(date +%s)"

	if (( (CURRENT_TIME - INTERFACE_ENUMERATION_START) >= INTERFACE_ENUMERATION_TIME_LIMIT )); then
		dbgPrint "Tried searching for interface for too long."
	fi

	echo -n "$orig_name"
	exit 2
fi

# Once a suitable name is produced for the representor, attempt to set it as altname first.

if [ "$new_name" != "$orig_name" ]; then
	# If the interface name is preserved, adding an altname is not attempted.
	dbgPrint "Trying to add altname $new_name"
	if [ -n "$LOG_TO_STDERR" ]; then
		ip link property add dev "$orig_name" altname "$new_name" 1>&2
		ret=$?
	elif [ -n "$LOGFILE" ]; then
		ip link property add dev "$orig_name" altname "$new_name" >> $LOGFILE 2>> $LOGFILE
		ret=$?
	else
		ip link property add dev "$orig_name" altname "$new_name" > /dev/null 2> /dev/null
		ret=$?
	fi
else
	ret=0
fi

if [ "$ret" == 0 ]; then
	# Altname was successfully set (or skipped entirely), therefore the name of the interface is preserved.
	echo -n "$orig_name"
else
	# Altname failed to set, therefore the interface is renamed.
	echo -n "$new_name"
fi

