# We do not want debug packages
%define debug_package %{nil}
# We do not want rpmbuild to strip off the digital signature
%define __brp_strip %{nil}

%define kmod_name enic
%define init_script_name enic
%define _rpmfilename %%{ARCH}/%%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm

Name:		enic
License:	GPLv2
Vendor:		Cisco
Version:        4.5.0.7
Release:        939.23.%{kmod_rel}
Summary:	Cisco VIC Ethernet NIC Driver Update Package

Group:		System/Kernel
URL:		https://www.cisco.com/
Source0:	%{name}-4.5.0.7.tar.bz2
Source1:	%{name}.files
Source2:	%{name}.conf
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-4.5.0.7-939.23-XXXXXX)
BuildRequires:	%kernel_module_package_buildreqs

%kernel_module_package -f %{SOURCE1} default

%description
Cisco VIC Ethernet NIC Driver Update Package

%prep
%setup -n enic-4.5.0.7

%build
./configure 

# Use the enic Makefile here: it will use the kernel Makefile to build
# the .ko, and then if configure selected it, the enic Makefile will
# sign the .ko.
num_procs=`cat /proc/cpuinfo | grep processor | wc -l`
%{__make} -j $num_procs

%install
export INSTALL_MOD_PATH=%{buildroot}
export INSTALL_MOD_DIR=extra/%{name}

# Use the kernel Makefile here because the enic Makefile runs "depmod"
# (which we don't want/need to do here -- we're just copying the .ko
# to the right location in the build tree so that it can be packaged).
%{__make} -C /lib/modules/4.19.0+1/build M=$PWD modules_install

# Cleanup unnecessary kernel-generated module dependency files.
find $INSTALL_MOD_PATH/lib/modules -iname 'modules.*' -exec rm {} \;

install -m 644 -D %{SOURCE2} %{buildroot}/etc/depmod.d/%{name}.conf

%clean
rm -rf %{buildroot}

%changelog
* Fri Oct 12 2018 Jeff Squyres <jsquyres@cisco.com>
- Add support for digitally signing enic.ko

* Wed Jan 24 2018 Parvi Kaustubhi <pkaustub@cisco.com>
- Add support for using the new versioning scheme
- Add support for building with autoconf

* Wed Mar 3 2010 Jon Masters <jcm@redhat.com>
- Updated some of the documentation.

* Sun Aug 9 2009 Jiri Olsa <jolsa@redhat.com>
- Initial tmpl package.
