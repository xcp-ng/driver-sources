Introduction
============
libqedr is a userspace driver for Qlogic RoCE Adapters. It is a plug-in module
for libibverbs that allows programs to use Qlogic RoCE hardware directly from
userspace. See the libibverbs package for more.
information.


Using libqedr
===============
libqedr will be loaded and used automatically by programs linked with
libibverbs. The qedr kernel modules must be loaded for RoCE devices to be
detected and used.


Prerequisites
=============
In order to build libqedr on a server with INBOX OFED it is required to
install libibverbs for developers. For example:

> yum install libibverbs-devel
> cd /usr/lib64
> ln -s libibverbs.so libibverbs.so.1

Installation
============
In order to install libqedr simply type:

> make libqedr_install

This will invoke the configuration script, build and install the library. Note
that the configuration parameters may vary from one OS to the other and may be
version dependent too. The default configuration when building via 'make libqedr' is
given below and should be adequate for most distros and versions (e.g. for RHEL
& SLES):

> ./configure --prefix=/usr --libdir=${exec_prefix}/lib64 --sysconfdir=/etc

If Ubuntu is detected, another configuration is automatically used:

> ./configure --prefix=/usr --libdir=${exec_prefix}/lib --sysconfdir=/etc

Do note that if libibverbs was installed from sources rather than a binary then
a manual installation is the best choice to make sure that the same parameters
were entered to the both libibverbs and libqedr 'configure' scripts:

> cd <package>/<libqedr folder>
> ./configure <parameters>
> make install

In order to uninstall libqedr simply type:

> cd <package>
> make libqedr_uninstall


Loading
=======
In order to load libqedr the RDMA service must be started. For example:

> systemctl start rdma.service
OR
> /etc/init.d/rdma start

It is possible to start the RDMA service automatically each boot. For example:

> systemctl enable rdma


Supported Hardware
==================
libqedr supports all RoCE capable QLogic adapters.


Debugging
=========
The default output consists of only error messages. If the environment variable
QELR_DP_LEVEL is configured then more messages can be enabled:

* 0 - Verbose     - error messages will be printed along with debug information.
* 3 - Errors      - only error messages will be printed (the default).

In the verbose level many debug messages will be outputted. They can be
filtered using the the environment variable QELR_DP_MODULE by configuring the
following bitmap:

* 0x10000  - CQ messages
* 0x20000  - RQ messages
* 0x40000  - SQ messages
* 0x80000  - MR messages
* 0x100000 - Initialization messages
* 0x200000 - SRQ

The output is directed to stderr by default. It is possible to redirect it to a
file via the environment variable QELR_DEBUG_FILE.

For example, let's enable all QP related prints (i.e. SQ+RQ) and redriect them
to a file named output.txt:
> export QELR_DP_LEVEL=0
> export QELR_DP_MODULE=786432		# 786432=0xC0000=0x40000|0x80000
> export QELR_DEBUG_FILE=output.txt
