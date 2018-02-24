#!/bin/bash

# This script is used to create disk images for different linux distros.
# Usage: ./make_package.sh {linux-distro} {version}

option=$1
ENIC_WHOLE_VERSION=$2
KNAME=$3
VERSION=$(echo $ENIC_WHOLE_VERSION | cut -d - -f 1)

function show_usage(){
	cat << STOP_ECHO
Usage: $0 <linux-distro> <version>

Valid values for <linux-distro> paramater are:

rhel6		RHEL 6.x
rhel7		RHEL 7.x
rhel8		RHEL 8.x
rhel9		RHEL 9.x
centos6		CentOS Linux 6.x
centos7		CentOS Linux 7.x
centos8		CentOS Linux 8.x
rocky8		Rocky Linux 8.x
rocky9		Rocky Linux 9.x
ol7		Oracle Linux 7.x
ol8		Oracle Linux 8.x
ol9		Oracle Linux 9.x
sles11		SLES 11.x
sles12		SLES 12.x
sles15		SLES 15.x
ubuntu		Ubuntu LTS 14.x, 16.x, and 18.x
xs70		Xen Server DDK 7.0
xs71		Xen Server DDK 7.1
xs72		Xen Server DDK 7.2
STOP_ECHO
	exit 1
}

# check required options
if [[ -z "${ENIC_WHOLE_VERSION}${option}" ]];
then
	show_usage
fi

function common_clean(){
	rm -fr ${PACKAGE_VER}
	mkdir -p ${PACKAGE_VER}
}

function rhel() {
	common_clean
	cp -r package-rhel/ddiskit-rhel/* ${PACKAGE_VER}
	mkdir -p ${PACKAGE_VER}/enic/rpm/SPECS
	mkdir -p ${PACKAGE_VER}/enic/rpm/SOURCES
	cp enic-$VERSION.tar.bz2 ${PACKAGE_VER}/enic/rpm/SOURCES
	cp package-rhel/enic.files ${PACKAGE_VER}/enic/rpm/SOURCES
	cp package-rhel/enic.conf ${PACKAGE_VER}/enic/rpm/SOURCES
	cp package-rhel/enic.spec ${PACKAGE_VER}/enic/rpm/SPECS/
	cp package-rhel/ddiskit/Makefile ${PACKAGE_VER}
	cp package-rhel/ddiskit/modules.dep ${PACKAGE_VER}/enic
	cd ${PACKAGE_VER}
	make KNAME=${KNAME} all
}

function sles() {
	common_clean

	cp enic-$VERSION.tar.bz2 ${PACKAGE_VER}/
	cd ${PACKAGE_VER}
	tar xf enic-$VERSION.tar.bz2
	mv enic-$VERSION cisco-enic-$VERSION
	tar cf cisco-enic-$VERSION.tar.bz2 cisco-enic-$VERSION
	cd ..
	rm -rf ${PACKAGE_VER}/cisco-enic-$VERSION
	rm -rf ${PACKAGE_VER}/enic-$VERSION.tar.bz2
	cp package-sles/cisco-enic.spec ${PACKAGE_VER}
	cp package-sles/cisco-enic.files ${PACKAGE_VER}
	cp package-sles/Makefile ${PACKAGE_VER}/
	cp package-sles/update.post ${PACKAGE_VER}/
	cd ${PACKAGE_VER}
	make all
}

function xenserver() {
	common_clean
	cp enic-$VERSION.tar.bz2 ${PACKAGE_VER}/cisco-enic-$VERSION.tar.bz2
	cp package-xs/enic.spec ${PACKAGE_VER}/enic.spec
	cp package-xs/Makefile ${PACKAGE_VER}/
	cd ${PACKAGE_VER}
	make
}

function debian() {
	common_clean
	cp enic-$VERSION.tar.bz2 ${PACKAGE_VER}/
	cd ${PACKAGE_VER}/
	tar xf enic-$VERSION.tar.bz2
	rm -f enic-$VERSION.tar.bz2
	mv enic-$VERSION/* ./
	rmdir enic-$VERSION
	cd ..
	cp -R package-debian/debian ${PACKAGE_VER}/
	cp package-debian/ChangeLog ${PACKAGE_VER}/
	cp package-debian/Makefile ${PACKAGE_VER}/Makefile.ubuntu
	sed -i "s/ENIC_WHOLE_VERSION/${ENIC_WHOLE_VERSION}/g" ${PACKAGE_VER}/Makefile.ubuntu
	cd ${PACKAGE_VER}
	make  -f Makefile.ubuntu all KNAME=${KNAME}
}

export PACKAGE_VER=enic-${VERSION}-${option}
case $option in
	rhel6 | rhel7 | rhel8 | rhel9 | centos6 | centos7 | centos8 | ol7 | ol8 | ol9 | rocky8 | rocky9)
		rhel
		;;
	sles11 | sles12 | sles15)
		sles
		;;
	ubuntu)
		debian
		;;
	xs70 | xs71 | xs72 | xs73 | xs74 | xs79 | xs80)
		xenserver
		;;
	*)
		show_usage
		;;
esac
