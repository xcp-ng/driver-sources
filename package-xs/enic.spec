%define uname  %{kernel_version}
%define module_dir updates
%define _rpmfilename %%{ARCH}/%%{NAME}-%{xs_release}-%{kernel_version}-modules-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm

Summary:	Cisco VIC Ethernet NIC Driver Update Package
Name:		cisco-enic
License:	GPLv2
Vendor:		Cisco
Version:        4.5.0.7
Release:        939.23
Epoch:        1

Group:		System/Kernel
URL:		https://www.cisco.com/
Source:		%{name}-%{version}.tar.bz2
BuildRoot:	%{_tmppath}/%{name}-%{version}-buildroot
Provides:	%{name}-modules = %{kernel_version}
Requires:	kernel-uname-r = %{kernel_version}

%description
Cisco VIC Ethernet NIC Driver Update Package

%prep
%setup -q -n enic-%{version}

%build
./configure 

# Use the enic Makefile here: it will use the kernel Makefile to build
# the .ko, and then if configure selected it, the enic Makefile will
# sign the .ko.
num_procs=`cat /proc/cpuinfo | grep processor | wc -l`
%{__make} -j $num_procs

%install
rm -rf $RPM_BUILD_ROOT

export INSTALL_MOD_PATH=%{buildroot}
export INSTALL_MOD_DIR=%{module_dir}

# Use the kernel Makefile here because the enic Makefile runs "depmod"
# (which we don't want/need to do here -- we're just copying the .ko
# to the right location in the build tree so that it can be packaged).
%{__make} -C /lib/modules/4.19.0+1/build M=$PWD modules_install

%clean
rm -rf %{buildroot}

%post
/sbin/depmod %{kernel_version}
%{regenerate_initrd_post}

%postun
/sbin/depmod %{kernel_version}
%{regenerate_initrd_postun}

%posttrans
%{regenerate_initrd_posttrans}

%files
%defattr(-,root,root,-)
/lib/modules/%{uname}/%{module_dir}/*.ko
%exclude /lib/modules/%{uname}/modules.*
%doc README

%changelog
* Fri Oct 12 2018 Jeff Squyres <jsquyres@cisco.com>
- Add support for digitally signing enic.ko
