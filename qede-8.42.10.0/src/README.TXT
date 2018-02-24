
		README

	QLogic Everest Driver for Ethernet protocol
        Cavium, Inc.
        Copyright (c) 2010-2017 Cavium, Inc.


Table of Contents
=================

  Introduction
  Link Speed
  Supported Kernels / Distros
  Limitations  
  Module Parameters
  Ethtool support
  Advanced Features
  Compilation-based options

Introduction
============

This file describes the QEDE (Qlogic Everest Driver for Ethernet) networking
driver for QL4xxx Series converged network interface cards. The Networking
Driver is designed to work in conjunction with the QED core module.


Link Speed
==========
The devices driven by this driver support the following link speed:

1x100G
2x50G
2x40G
4x25G
2x25G
4x10G
2x10G

Supported Kernels / Distros
===========================
RHEL6.6, RHEL 6.7, RHEL6.8, RHEL6.9, RHEL7.0, RHEL7.1, RHEL 7.2, RHEL7.4, RHEL7.5
SLES11 SP3, SLES11 SP4, SLES12, SLES12 SP1, SLES12 SP2, SLES12 SP3
Ubuntu 14.04 LTs, Ubuntu 16.04 LTs
CentOS 7.0, CentOS 7.1, CentOS 7.2
Citrix 7.0

SR-IOV
Hypervisor - RHEL7.1, RHEL7.2, RHEL7.4, RHEL7.5, SLES12, SLES12 SP1, SLES12 SP2,
             SLES12 SP3, Ubuntu 16.04 LTs, CentOS7.2
VMs - All supported distros [with the exception of Citrix 7.0].

Limitations
===========

 - Big endian support is not available.
 - qede was tested Everest 4 Big Bear revision B0. Revision A0 is not supported.
 - Most officially supported distros lack infrastructure support for displaying
   autoneg related capabilities for high speeds such as 25G/50G/100G.
   `ethtool --show-priv-flags <interface>' can be used to learn via private
   flag whether device supports 25g link speed.
 - Memory constraints - Memory for Rx rings far outweights other memory
   allocated by driver. The memory consumption for Rx is roughly:
       <Number of Rx queues> x <Ring size> x 4Kb
   As by default there would be 8 queues 1Kb entries in a ring, each interface
   will consume 32Mb of memory for Rx.
   To reduce this, user can reduce either number of queues or ring sizes using
   ethtool commands:
        `ethtool --set-channels <interface> combined <number>' controls number of queues.
        `ethtool --set-ring <interface> rx <number>' controls ring size.
 - SmartLinQ as a propritery link configuration isn't supported by standard
   linux tools. `--show-priv-flags' can indicate the current status of
   the feature. Once user explicitly changes link configuration
   [e.g., via `ethtool -s <interface> speed <forced speed>'] the feature would
   get disabled with user being unable to restart it baring the removal &
   re-probe of the qede.ko module.
   In addition, only PFs have knowledge of SmartLinQ support.

 - On older distros which doesn't support the higher speeds such as
   25G/50G/100G, "ethtool -s <interface> autoneg on advertise <speed>"
   configurations might get ignored.

 - XDP: It is not supported over jumbo MTUs.

 - Ethtool N-tuple/tc or arfs is not supported with 100G and
   multi function mode adapter. Only supported over single function mode.

Module Parameters
=================
the following module parameters exist for the qede driver:

debug - controls driver verbosity level. Similar to ethtool -s <dev> msglvl
	QED_MSG_SPQ = 0x10000,
	QED_MSG_STATS = 0x20000,
	QED_MSG_DCB = 0x40000,
	QED_MSG_IOV = 0x80000,
	QED_MSG_SP = 0x100000,
	QED_MSG_STORAGE = 0x200000,
	QED_MSG_OOO = 0x200000,
	QED_MSG_CXT = 0x800000,
	QED_MSG_LL2 = 0x1000000,
	QED_MSG_ILT = 0x2000000,
	QED_MSG_RDMA = 0x4000000,
	QED_MSG_DEBUG = 0x8000000,

int_mode - controls interrupt mode other than MSI-X.
gro_disable - disables the HW GRO feature, allowing the driver to utilize the
	      regular GRO of the linux network stack.
err_flags_override - a bitmap for disabling or forcing the actions taken in case
                     of a HW error:
		     bit #31 - an enable bit for this bitmask.
		     bit #0  - prevent HW attentions from being reasserted.
		     bit #1  - collect debug data.
		     bit #2  - trigger a recovery process.
		     bit #3  - call WARN to get a call trace of the flow that
                               led to the error.

rdma_lag_support - RDMA Bonding support enable - preview mode. Enable by
		   default.

Ethtool Support
==============
The following ethtool operations are supported:

ethtool -i 
Driver information. This includes versions of the running driver, fastpath
firmware and management firmware.

ethtool -a/-A
set/get pause options.

ethtool -c|-C
set/get coalesce options.
Maximum allowed coalescing value is 511 usec.
Coalescing might be rounded due to HW limitation for higher coalesce value.

ethtool -d
Register dump. This is a debug feature, dumping most of the device registers.

ethtool -g/-G
set/get ring params. Control transmit/receive ring sizes.
Notice that lowering this value sufficiently may cause 'no_buff_discards"
to appear in the device logs, as the Rx rings aren't sufficiently large
to hold current "window" of incoming packets. This is dependent upon various
parameters, e.g., PP/s, existance of FW aggregations, Rx coalescing, etc..

ethtool -l/-L
set/get amount of queues.
By default driver is going to use only `combined' queues [ones where same
interrupt line serves both Rx data and Tx completions], but this can
be used to configure tx/rx only queues, as long as there at least one
receiving queue and one transmitting queues.
For 100g devices, those numbers must be even.

ethtool -k/-K
set/get stateless offload settings. These include:
transmission checksum
receive checksum
receive side scaling
transmit side scaling
generic/large send offload
generic receive offload

ethtool -p
Show visible port identification (e.g. blinking).

ethtool -r
Restart N-WAY negotiation.

ethtool -S
Dump device statistics.

ethtool --show-priv-flags 
Query private flags. Currently contains:
  - information on whether PCI function is coupled [i.e., 100G] or not.
  - information on whether device is capable of SmartLinQ as well on whether
    its currently enabled or not.
  - information on whether 25g is supported.

ethtool -T
Show time stamping capabilities.

ethtool -n/-N, ethtool -u|-U
Show/Configure Rx network flow classification options.
Note that for UDP over IPv4/IPv6 either 2-tuple and 4-tuple hash is
supported, while for TCP only 4-tuple hash is supported.

ethtool -x/-X
Show/Set Rx flow hash indirection and/or hash key

ethtool -f
Write a firmware image to flash or other non-volatile memory on the device.
The driver API expects <filename> which contains the flash image and is resided
in the folder /lib/firmware/. The format of flash-file need to adhere to the
format defined by the management firmware i.e., header followed by data which
captures the meta data of the image.
While upgrading the flash image, user need to follow the steps defined the secure-
nvram-update of MFW. For example, for upgrading a nvram partition, user need to
invoke two flash commands, NVM_PUT_FILE_BEGIN command followed by NVM_PUT_FILE_DATA
command. NVM_PUT_FILE_BEGIN instructs the management firmware about start of nvm
flashing. NVM_PUT_FILE_DATA command flashes the actual image.

ethtool -n/-N
Following commands can be used for RX flow classifications for physical function
and virtual functions. Due to limited support for flow profiles for BB/AH
devices, only one type of flow steering supported at a time. Flow attributes
with masks are not supported.

For examples - 

ethtool -N p5p1 flow-type tcp4 src-ip 192.168.40.100 dst-ip 192.168.40.200
src-port 6660 dst-port 5550 action 7
[Steer flows based on TCP/UDP 4 tuples to PF receive queue 7]

ethtool -N p5p1 flow-type udp4 dst-port 12000 action 1
[Steer flows based on just TCP/UDP destination port to PF receive queue 1]

ethtool -N p5p1 flow-type tcp4 src-ip 192.168.40.100 dst-ip 192.168.40.200
src-port 6660 dst-port 5540 action 0x100000000
[Steer flows based on TCP/UDP 4 tuples to VF 0, please note that higher 32 bits
in "action" field represents VF no.]

ethtool -N p5p1 flow-type tcp4 src-ip 192.168.40.100 dst-ip 192.168.40.200
src-port 6660 dst-port 5550 user-def 0xffffffff00000000
[Steer flows based on TCP/UDP 4 tuples to VF 0 using user-def field, note that
lower 32 bits are used for VF no.]

ethtool -N p5p1 flow-type tcp4 src-ip 192.168.40.100 dst-ip 192.168.40.200
src-port 6660 dst-port 5550 action -1
[Drop the flows with mentioned TCP/UDP 4 tuples]

ethtool -N p5p1 flow-type udp4 dst-port 12000 action -1
[Drop the flows based on TCP/UDP destination port]

ethtool -N p5p1 flow-type tcp4 src-ip 192.168.40.100 action -1
[Drop the flows based on src/dst ip]

Tunnel specific flow clasification -
----------------------------------

ethtool -N p5p1 flow-type udp4 dst-port 4789 user-def 0xffffffff00000000
[Steer flows for vxlan tunnels based on outer UDP destination port, bearing
that hw is able to detect tunnels for such port. IOW, mentioned UDP port is
configured on hardware]

ethtool -N p5p1 flow-type udp4 dst-port 0x0 user-def 0xffffffff00000000
[Steer flows for GRE tunnels to the VF, udp port 0x0 is reserved for GRE]

ethtool -w/-W
The interfaces are used to dump the following info,
  - NVM Config id attributes.
  - GRC/Register dump of the required memory regions.

NVM config id dump:
------------------
Use ethtool -W to set the parameters for nvm_cfgid_read()
  - Command id = 1
  - NVM cfg id
  - Option id (i.e., pf-id)
Example: Read nvm mac-address of pf 3.
  ethtool -W ens1f0 1 (NVM_CFG_CMD)
  ethtool -W ens1f0 1 (NVM_MAC)
  ethtool -W ens1f0 3 (PF_ID)
  ethtool -w ens1f0 data file

GRC dump:
---------
Use ethtool -W to set GRC dump config flags.
  - Command id = 2
  - One or more Grc dump config flags
Example: GrcDump with brb/btb packets included.
  ethtool -W ens1f0 2 (GRC_CONFIG_CMD)
  ethtool -W ens1f0 20 (set brb flag)
  ethtool -W ens1f0 21 (set btb flag)
  ethtool -w ens1f0 data grc_brb_btb.bin

Advanced Features
====================================
For more details please see the QED core module README file.

- Network Partitioning (NPAR)
The device supports enumerating on the pci as multiple pci functions.
Configuration parameters to port level features are blocked from
ethtool, as it is a function level interface. e.g. link speed cannot be
changed by ethtool under NPAR as the same port and link are servicing multiple
networking interfaces.

Statistics collected by ethtool -S contain per port fields and per function
fields.

- Single Root Input Output Virtualization (SR-IOV)
The qede driver can drive Virtual Function devices as well as regular physical
devices (no need for a separate driver).

By default all virtual functions are initialized with zero MAC address, unless
there are provisioned addresses in the NVRAM. In case of zero address, user
should configure virtual functions with some valid MAC addresses prior to make
the VF devices interfaces up or load. Loading or bringing VF devices interfaces
up with zero MAC address would fail.

To start a VF with pre-assigned random MAC address, qed module should be loaded
with module parameter "vf_mac_origin" as 1, thus all VF devices will be started
with a random MAC unless there are provisioned addresses in the NVRAM, or as
'2', for a random MAC w/o a dependency on the NVRAM configuration.

VF mac address can be configured either from hypervisor or in VM using regular
network interface APIs on the VF devices itself. VF devices mac can also be
configured using it's parent PF on hypervisor using iproute2 utility.

Once PF sets a mac to a VF (also called as forced MAC), normally the VF is
unable to modify this mac. There are two ways in which VF can modify its MAC
after PF has configured a MAC for VF.
   1) Load qed module with module parameter "allow_vf_mac_change_mode=1",
      then PF will allow all VFs to change their MAC addresses despite PF
      has assigned a forced MAC.
   2) Use iproute2 utility to enable trust mode for particular VF of a PF.
      #ip link set dev <PF> vf <VF ID> trust on
      then PF will allow respective VF to change its MAC address despite
      PF has assigned a forced MAC

 - PF multiqueue (Receive/Transmit Side Scaling) is supported. Do notice that
maximum limitation of number of combined queues is based on capabilities of
the interface, and available resources due to the device configuration
[NVRAM allocation of resources]; maximum possible would be '64' in case RoCE
is supported and 128 otherwise [assuming sufficient resources].
VF multiqueue is also supported. The amount of queues available for all of the
VFs of a given PF is equal to the maximum amount of VFs.
If fewer than the maximum amount will be enabled, they will employ the unused
queues, and support multiqueue. This can translate to a significant performance
boost, depending on the scenario.

VF link speed report is based on information provided by the PF and may not 
represent the current line speed.

VFs support the same stateless offloads as PFs.

- Tunneling offloads
Tunneling refers to transmission of packets (usually L2 packets) that are
encapsulated within an outer-header. The outer-header can be above
L2/L2+L3/L2+L3+L4 etc. The idea is that there is a separation between the
physical network, that sees the outer-header, and the logical network that is
configured based on the inner headers.

Along with supporting tunneled traffic device may support offloading some work
of host like Tx checksum, Rx checksum, TSO etc for encapsulated packet.

qede supports VXLAN, L2GRE, IPGRE, L2GENEVE, and IPGENEVE tunnels and it is
enabled by default. qede supports all above protocols simultaneously.

qede supports Tx and Rx checksum offload and TSO for encapsulated packets.
VLAN HW acceleration is not supported for encapsulated packet.
RSS is supported for encapsulated traffic.

Only one UDP destination port is supported. Driver programs first vxlan
destination port provided to it by kernel. If driver programmed vxlan
destination port gets deleted driver programs next available vxlan
destination port provided by kernel.

Before using supported tunneling protocols please make sure kernel is
built with all related CONFIG_XXX enabled for required tunneling protocol.
For example, To use GENEVE tunneling kernel has to be built with
CONFIG_GENEVE enabled. Please also make sure respective tunneling protocol
modules are loaded prior to create the tunnel. For example to create geneve
tunnel module "geneve" has to be loaded, likewise for VXLAN "vxlan" module
has to be loaded.

Tools
Creating VXLAN,GRE tunnels and programming of VXLAN destination port is
supported through ip tools and ovs-vsctl (open vSwitch tools).
- ip tool command for creating vxlan device

  ip link add <vxlan_dev_name> type vxlan id <VNI> dev <ethernet_dev>
		group <multicast> dstport <vxlan dst port> ttl 255

 
  Here group is a multicast group to which vxlan device subscribes.

- ip tool command for creating geneve device

  ip link add dev <geneve_dev_name> type geneve remote <IP> vni <ID>
  
- open vSwitch command for creating vxlan device
  ovs-vsctl add-port <OVS bridge> <vxlan dev name> -- set interface <vxlan dev name>
  type=vxlan options:remote_ip=<remote interface ip addr>  options:key=<VNI>
  options:dst_port= <vxlan dst port>
  
  Note: Open vSwitch may support only the framing format for packets on the
  wire. There may be no support for the multicast aspects of VXLAN.
  To get around the lack of multicast support, it is possible to
  pre-provision MAC to IP address mappings either manually or from a
  controller.

  Same commands can be used to create GRE tunnel by replacing vxlan by GRE
  and some options may not be valid for GRE.

Distro support
  - RHEL7, SLES12. [For vxlan/gre tunnnel]
  - RHEL7.2 [For geneve tunnel]

Kernel documentation
https://www.kernel.org/doc/Documentation/networking/vxlan.txt

- L2 Forwarding offload for MACVLAN devices
Macvlan device is a virtual interface belonging to same L2 domain as of
physical interface with different and uniqe MAC address and different
IP address (not to confuse with VLAN). It is exposed directly to outer world
though underlying physical interface. Macvlan forwarding offload feature allows
physical interface to create dedicated fastpath queues for macvlan device and
bypass the kernel's switching path to transmit and receive packets directly
into NIC.

Maximum 64 macvlan devices can be offloaded. Number of macvlan offload instances
can be limited by available hardware resources such as fast path queues, etc.
Only 1 Rx and 1 Tx queues are supported per macvlan device.
Macvlan offload feature can not coexist with SRIOV, XDP and 100G mode.

 How to enable L2 forwarding offload?
 #ethtool -K eth1 l2-fwd-offload on

 How to create macvlan device?
 #ip link add mvlan1 link eth1 numtxqueues 1 numtxqueues 1 type macvlan mode bridge
 This command creates a macvlan device in bridge mode, named mvlan1 on top of base
 device eth1 with Tx and Rx queues = 1.

 How to delete macvlan device?
 #ip link del dev mvlan1

Compilation based options
=========================
Some debug facilities need to be explicitly enabled during *compilation*.
Such features are mostly fastpath-oriented, so compiling them in might affect
performance. Still, when debugging performance scenarios they might be useful.

ENABLE_TIME_DEBUG=1
	When set, driver would start counting time passing while running
NAPI as well as the work performed by it [to allow results such as CQEs/usec].
This results can be viewed via a dedicated debugfs node:
/sys/kernel/debug/qede/<bdf>/napi_time. Writing to that same node would zero
e current results.

ENABLE_FULL_TX_DEBUG=1
	Ethtool allows driver to set rx_status, tx_done and tx_queued verbosities. Problem is, the mere if-conditions for determining whether those should be printed is harming fastpath performance. Setting this compilation flag to '1' would enable additional fastpath verbose [that would still require enablement via ethtool/debug module parameter] that can be gathered.
