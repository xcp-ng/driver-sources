                          Installation Notes
                            Version 1.0.2
                              04/22/2012
		  QLogic's Linux FCoE Offload Driver

                          QLogic Corporation
                         26650 Aliso Viejo Pkwy,
			 Aliso Viejo, CA 92656

             Copyright (c) 2007-2013 Broadcom Corporation
             Copyright (c) 2014 QLogic Corporation
                           All rights reserved


Table of Contents
=================

  Introduction
  Limitations
  Distros Supported
  Packaging and Installation
  Manually Reloading bnx2fc Driver
  BNX2FC Driver Dependencies
  BNX2FC Driver Parameters
  Driver Messages
  Creating FCoE Interface with Specific VLAN ID
  Collecting Driver Debug Stats
  Changing Switch DCBX Parameters  
  Unknown speed messages from fcoemon
  SLES 12 SP3 Boot From SAN Installation Parameter

Introduction
============

This file describes the bnx2fc Linux drivers for the QLogic's NetXtreme II
BCM57712/BCM57810 10G Mbps PCIe CNIC Network Controller chipset.  The bnx2fc
driver enables users to offload FCoE data path onto licensed 57712 devices.

Distros Supported
=================
	RedHat Enterprise Linux 6.1 and above
	RedHat Enterprise Linux 7.0 and above
	SLES 11 SP1 and above
	SLES 12

Limitations
===========

The current version of the driver has been tested on 2.6.x kernels starting
from 2.6.32 kernel which is included in RH 6.1 distribution. This driver may
not compile on older kernels. QLogic QA validation is limited to i386 and
x86_64 architectures, RHEL6.1 distribution.

Packaging & Installation
========================

Refer INSTALL.TXT for netxtreme2 package & installation instructions.

BNX2FC Driver Dependencies
==========================

The driver uses library functions in the scsi_transport_fc, bnx2, bnx2x, cnic.
It is required to load these library modules either as loadable module or as
kernel built-in component before attempting to load the driver or unresolved
symbol errors will appear. Using modprobe will resolve the dependencies
automatically.


Manually Reloading BNX2FC Driver
================================

To bnx2fc and dependent modules needed to be reloaded in a specific order for
the FCoE offload to be configured correctly when all the modules are reloaded.
Perform the following steps to reload bnx2fc and its dependent modules
correctly:

1. Delete all FCoE interfacesi (i.e. issue the following command for each
   interface):

  # fcoeadm -d <FCoE interface>

2. Remove the bnx2fc, cnic and if needed the bnx2x module in this order:

  # rmmod bnx2fc
  # rmmod cnic
  # rmmod bnx2x

3. Reload bnx2fc and dependent modules in the reverse order in which they were
   removed:

  # modprobe bnx2x
  # modprobe cnic
  # modprobe bnx2fc

4. Restart the fcoe system service to rediscover any devices:

  # systemcl restart fcoe

NOTE: Older distributions may use the 'service' command to restart system
      services such as fcoe.

BNX2FC Module Parameters
========================

optional parameters "debug_logging" can be supplied as a command line arguments
to the insmod or modprobe command for bnx2fc.

-------------------
bnx2fc_debug_level
-------------------
Description: "Bit mask to enable debug logging", enables/disables driver debug
	logging

Defaults: NONE

e.g.
	# modprobe bnx2fc debug_logging=0xff

IO level debugging	= 0x1
Session level debugging = 0x2
HBA level debugging	= 0x4
ELS debugging		= 0x8
Misc debugging		= 0x10
Max debugging		= 0xff



FCoE Operation
==============
QLogic FCoE offload through bnx2fc is full stateful hardware offload that
cooperates with all interfaces provided by the Linux ecosystem for FC/FCoE and
SCSI controllers.  As such, FCoE functionality, once enabled is largely
transparent. Devices discovered on the SAN will be registered and unregistered
automatically with the upper storage layers.

Despite the fact that the QLogic's FCoE offload is fully offloaded, it does
depend on the state of the network interfaces to operate. As such, the network
interface (e.g. eth0) associated with the FCoE offload initiator must be 'up'.
It is recommended that the network interfaces be configured to be brought up
automatically at boot time. This can be accomplished by configuring the
interface through YaST/YaST2 on SuSE Linux or through network device control on
Red Hat.

Furthermore, the QLogic FCoE offload solution creates VLAN interfaces to
support the VLANs that have been discovered for FCoE operation (e.g.
eth0.1001-fcoe).  Do not delete or disable these interfaces or FCoE operation
will be interupted.

MTU configuration of the native Ethernet interface is independent of the FCoE
protocols effective MTU. The networking MTU may be configured for any value,
and the FCoE offload will continue to use an appropriately size mini-jumbo MTU
for FCoE operation.

The interfaces on which bnx2fc driver has to operate need to be configured.
Here are the steps to configure:
1. cd /etc/fcoe
2. copy cfg-ethx to cfg-eth5 if FCoE has to be enabled on eth5. Repeat this for
all the interfaces where FCoE has to be enabled.
3. Edit all the cfg-eth files to set "no" for DCB_REQUIRED field

Refer to fcoeadm manpage for more information and fcoeadm operations to
create/destroy interfaces or to display lun/target information.

With QLogic's FCoE solution, LLDP packets are sent by the MCP firmware.
However, the lldp frames are also sent by lldpad daemon. To disable the
duplicate lldp packets, run the following command:

lldptool set-lldp -i <interface_name> adminStatus=disabled

Driver Messages
===============

The following are the most common sample messages that may be logged in the
file /var/log/messages. Use dmesg -n <level> to control the level at which the
messages should appear on the console. Mmost systems are set to level 6 by
default. To see all messages, set the level higher.

BNX2FC Driver Signon:
---------------------
QLogic NetXtreme II FCoE Driver bnx2fc v0.8.7 (Mar 25, 2011)

Driver comples handshake with FCoE Offload Enabled CNIC device:
--------------------------------------------------------------
bnx2fc [04:00.00]: FCOE_INIT passed

Driver fails handshake with FCoE Offload Enabled CNIC device:
------------------------------------------------------------
bnx2fc: init_failure due to invalid opcode
bnx2fc: init_failure due to context allocation failure
bnx2fc: init_failure due to NIC error
bnx2fc: init_failure due to completion status error
bnx2fc: init_failure due to HSI mismatch

No valid license to start FCoE:
-------------------------------
bnx2fc: FCoE function not enabled <ethX>
bnx2fC: FCoE not supported on <ethX>

Session failures due to exceeding maximum allowed FCoE offload connection limit
or memory limits:
-------------------------------------------------------------------------------
bnx2fc: Failed to allocate conn id for port_id <remote port id>
bnx2fc: exceeded max sessions..logoff this tgt
bnx2fc: Failed to allocate resources

Session offload failures:
------------------------
bnx2fc: bnx2fc_offload_session - Offload error
<rport> not FCP type. not offloading
<rport> not FCP_TARGET. not offloading

Session upload failures:
-----------------------
bnx2fc: ERROR!! destroy timed out
bnx2fc: Disable request timed out.  destroy not set to FW
bnx2fc: Disable failed with completion status <status>
bnx2fc: Destroy failed with completion status <status>

Unable to issue ABTS:
--------------------
bnx2fc: initiate_abts: tgt not offloaded
bnx2fc: initiate_abts: rport not ready
bnx2fc: initiate_abts: link is not ready
bnx2fc: abort failed, xid = <xid>

Unable to recover the IO using ABTS(due to ABTS timeout):
--------------------------------------------------------
bnx2fc: Relogin to the target

Unable to issue IO request due to session not ready:
---------------------------------------------------
bnx2fc: Unable to post io_req

Drop incorrect L2 receive frames:
--------------------------------
bnx2fc: FPMA mismatch... drop packet
bnx2fc: dropping frame with CRC error

HBA/lport allocation failures:
------------------------------
bnx2fc: Unable to allocate hba
bnx2fc: Unable to allocate scsi host

NPIV port creation:
-------------------
bnx2fc: Setting vport names, <WWNN>, <WWPN>


Creating FCoE Interface with Specific VLAN ID:
==============================================

There are some situations where we want to manually specify the network
VLAN ID that FCoE traffic will flow over such as when a switch does not
support FIP VLAN discovery.  To do this perform the following steps:

1. Bring up the network interface we wish to create a FCoE interface on top of:
	# ip link set ethX up

2. Add a VLAN interface on top of network interface:
	# ip link add link ethX ethX.Y type vlan id Y

3. Bring the VLAN interface up:
	# ip link set ethX.Y up

4. Copy the /etc/fcoe configuration file for the base network interface
   to a new file for the VLAN interface:
	# cp /etc/fcoe/cfg-ethx /etc/fcoe/cfg-ethX.Y

5. Edit the new FCoE configuration file (from step 4) and set AUTO_VLAN="no".

6. Restart the fcoe service.


Collecting Driver Debug Stats:
=============================
bnx2fc supports collection of debug statistics during normal operation. These
statistics can be then displayed via debugfs provided the kernel has been built
with the support. To display these stats the following steps should be
followed.

1. Mount the debugfs file system if it's not mounted
	mount -t debugfs none /sys/kernel/debug

2. Collect the stats
	cat /sys/kernel/debug/bnx2fc/info > /tmp/bnx2fc_debug.txt

Other stats exported by the driver include:

o Stats exported via /sys/class/fc_host/hostX/statistics nodes

fcp_control_requests - Number of non read/write requests issued.
fcp_input_megabytes - Number of read request megabytes received.
fcp_input_requests - Number of read requests issued.
fcp_output_megabytes - Number of write request megabytes sent.
fcp_output_requests - Number of write requests issued.
rx_frames - Number of FC frames received.
rx_words - Number of 4 byte words received.
tx_frames - Number of FC frames transmitted.
tx_words - Number of 4 byte words transmitted.

o Stats exported via /sys/class/scsi_host/hostX/stats

FLOGICount - Number of FLOGI ELS requests issued.
FDISCCount - Number of FDISC ELS requests issued.
ABTSCount - Number of ABTS ELS requests issued.
ResetTargetCount - Number of target reset task management requests issued.
ResetLUNCount - Number of LUN reset task management requests issued.
RECExpirations - Number of REC ELS requests timeouts that have occurred.
ULPTOVExpirations - Number of SCSI task/ABTS/ELS timeouts that have occurred.
SessionLevelRecoveryCount - Number of SRR ELS requests issued.
SequenceTimeouts - Number of sequence cleanup tasks issued.
EOFAFrames - Number of partial frames aborted by sender with EOF_a end of frame
             delimiter


Changing Switch DCBX Parameters
===============================

If the DCBX type on the switch port connected to the adapter is changed,
the FCoE interface needs to be reset.  This can be done with the following
command:

	fcoeadm -r <interface>

Unknown speed messages from fcoemon
===================================

On distributions:

- SLES 11 SP4 and below
- RHEL 6.7 and below

fcoemon -i may display the 'Speed' may be displayed as 'Unknown' if the
adapter is connected at 20Gb.  This is a known issue as older versions
of open-fcoe do not have the definition for the 20Gb connection speed and thus
will display the speed as 'Unknown' from fcoemon.

This is expected and will not cause an functional impact.

SLES 12 SP3 Boot From SAN Installation Parameter
================================================

When installing to a FCoE boot LUN exposed by bnx2fc, the name of the network
interface that underlies the primary FCoE interface can be renamed.  This
causes the configuration file for the FCoE interface to not be written in
/etc/fcoe correctly which may cause the system not to boot.

The following installation parameter needs to be passed at before the
operating system installation begins to prevent this condition:

net.ifnames=1

