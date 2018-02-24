# We do not want debug packages
%define debug_package %{nil}
# We do not want rpmbuild to strip off the digital signature
%define __brp_strip %{nil}

%define kmod_name fnic
%define init_script_name fnic
%define _rpmfilename %%{ARCH}/%%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm

Name:		fnic
License:	GPLv2
Vendor:		Cisco
Version:        2.0.0.85
Release:        220.0.%{kmod_rel}
Summary:	Cisco VIC MQ FCoE and NVME HBA Driver Update Package

Group:		System/Kernel
URL:		https://www.cisco.com/
Source0:	%{name}-2.0.0.85.tar.bz2
Source1:	%{name}.files
Source2:	%{name}.conf
Source3:	fcc
Source4:	nvmef-connect.tar.bz2
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-2.0.0.85-220.0-XXXXXX)
BuildRequires:	%kernel_module_package_buildreqs

%kernel_module_package -f %{SOURCE1} default

%description
Cisco VIC MQ FCoE and NVME HBA Driver Update Package

%prep
%setup -n fnic-2.0.0.85

# Unpack the nvmef-connect tarball
%setup -D -T -b 4

%build
./configure 

# Use the fnic Makefile here: it will use the kernel Makefile to build
# the .ko, and then if configure selected it, the fnic Makefile will
# sign the .ko.
num_procs=`cat /proc/cpuinfo | grep processor | wc -l`
%{__make} -j $num_procs

# We started off in the fnic directory, so cd back out into the
# nvmef-connect directory and build it.
cd ../nvmef-connect
%{__make}

%install
export INSTALL_MOD_PATH=%{buildroot}
export INSTALL_MOD_DIR=extra/%{name}

# Use the kernel Makefile here because the fnic Makefile runs "depmod"
# (which we don't want/need to do here -- we're just copying the .ko
# to the right location in the build tree so that it can be packaged).
%{__make} -C /lib/modules/4.19.0+1/build M=$PWD modules_install

# Cleanup unnecessary kernel-generated module dependency files.
find $INSTALL_MOD_PATH/lib/modules -iname 'modules.*' -exec rm {} \;

# We started in the fnic tarball directory
# Cd back over to the nvmef-connect directory
cd ../nvmef-connect
install -D -m 500 nvmef-connect $RPM_BUILD_ROOT/usr/bin/nvmef-connect

# Install these directly from their source locations
install -m 644 -D %{SOURCE2} %{buildroot}/etc/depmod.d/%{name}.conf
install -D -m 500 %{SOURCE3} $RPM_BUILD_ROOT/usr/bin/fcc

%clean
rm -rf %{buildroot}

%changelog
* Fri Feb 01 2019 Jeff Squyres <jsquyres@cisco.com>
- Add support for digitally signing fnic.ko
- Converted to configure-based build system and standardized spec file

* Mon Oct 18 2010 Venkata Siva Vijayendra Bhamidipati <vbhamidi@cisco.com>
- spec file for fnic driver module
