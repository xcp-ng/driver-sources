%define name bnx2
%define version

%define debug_package %{nil}

Summary: QLogic bnx2 Gigabit ethernet driver
Name: %{name}
Version: %{version}
Release: 1
Vendor: QLogic Corporation
License: GPL
Group: System Environment/Base
Source: bnx2-%{version}.tar.bz2
BuildRoot: /var/tmp/%{name}-buildroot

%description
This package contains the QLogic bnx2 Gigabit ethernet driver.

%prep
%setup -T -b 0

%build
value=%{?KVER}
if [ -z "$value" ];then
	value=$BUILD_KERNEL
	if [ -z "$value" ];then
		KVER=$(uname -r)
	else
		KVER=$value
	fi
else
	KVER=$value
fi
make KVER=$KVER

%install
value=%{?KVER}
if [ -z "$value" ];then
	value=$BUILD_KERNEL
	if [ -z "$value" ];then
		KVER=$(uname -r)
	else
		KVER=$value
	fi
else
	KVER=$value
fi

BCM_CNIC=0
BCM_KVER=`echo $KVER | cut -c1-3 | sed 's/2\.[56]/2\.6/'`
if [ "$BCM_KVER" = "2.6" ];then
	BCM_CNIC=`echo $KVER | cut -c5- | cut -d. -f1 | cut -d- -f1 | awk '{ if ($1 > 15) print "1"; else print "0"}'`
	BCM_DRV=bnx2.ko
else
	BCM_DRV=bnx2.o
fi

echo "%defattr(-,root,root)" > $RPM_BUILD_DIR/file.list.%{name}
echo "/lib/modules/$KVER/kernel/drivers/net/$BCM_DRV" >> $RPM_BUILD_DIR/file.list.%{name}
if [ "$BCM_CNIC" = "1" ];then
echo "/lib/modules/$KVER/kernel/drivers/net/cnic.ko" >> $RPM_BUILD_DIR/file.list.%{name}
echo "/usr/src/bnx2/bnx2.h"    >> $RPM_BUILD_DIR/file.list.%{name}
echo "/usr/src/bnx2/cnic_if.h" >> $RPM_BUILD_DIR/file.list.%{name}
echo "/usr/src/bnx2/cnic_drv.h" >> $RPM_BUILD_DIR/file.list.%{name}
fi
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KVER/kernel/drivers/net

echo "/usr/share/man/man4/bnx2.4.*" >> $RPM_BUILD_DIR/file.list.%{name}
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man4

make install PREFIX=$RPM_BUILD_ROOT KVER=$KVER

%post
depmod -a > /dev/null 2> /dev/null
exit 0

%postun
depmod -a > /dev/null 2> /dev/null
exit 0

%clean
rm -rf $RPM_BUILD_ROOT $RPM_BUILD_DIR/file.list.%{name}

%files -f ../file.list.%{name}
%doc LICENSE README.TXT RELEASE.TXT

%changelog
