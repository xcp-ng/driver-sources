#!/bin/bash

# This script is used to create disk images for different linux distros.
# Usage: ./make_package.sh {linux-distro} {version}

option=$1
# FNIC version is A.B.C.D-E.F
FNIC_VERSION=$2
KNAME=$3
# Get just the A.B.C.D
VERSION=$(echo $FNIC_VERSION | cut -d - -f 1)

function show_usage(){
	cat << STOP_ECHO
Usage: $0 <linux-distro> <version>

Valid values for <linux-distro> paramater are:

rhel7		RHEL 7.x
rhel8		RHEL 8.x
rhel9		RHEL 9.x
centos7		CentOS Linux 7.x
centos8		CentOS Linux 8.x
sles12		SLES 12.x
sles15		SLES 15.x
xs8		Xen Server 8.x
oel7		Oracle Enterprise Linux 7
oel8            Oracle Enterprise Linux 8
oel9            Oracle Enterprise Linux 9
STOP_ECHO
	exit 1
}

# check required options
if [[ -z "${PACKAGE_VERSION}${option}" ]];
then
	show_usage
fi

function common_clean(){
	rm -fr ${PACKAGE_TMP}
	mkdir -p ${PACKAGE_TMP}
	PACKAGE_TMP=`readlink -f ${PACKAGE_TMP}`
}

function setup_fcc {
	dest=$1

	# fcc is a single, self-contained shell script
	mkdir -p "$dest"
	cp ${TOOLS}/fcc/fcc "${dest}"
}

function setup_nvmef_connect {
	dest=$1

	# nvmef-connect is a simple package
	mkdir -p "$dest"
	pushd "${TOOLS}"
	tar jcf "${dest}/nvmef-connect.tar.bz2" \
	    nvmef-connect/Makefile \
	    nvmef-connect/nvmef-connect.c
	popd
}

function rhel() {
	common_clean

	cp -r package-rhel/ddiskit-rhel/* ${PACKAGE_TMP}
	mkdir -p ${PACKAGE_TMP}/fnic/rpm/SPECS
	mkdir -p ${PACKAGE_TMP}/fnic/rpm/SOURCES

	setup_fcc ${PACKAGE_TMP}/fnic/rpm/SOURCES
	setup_nvmef_connect ${PACKAGE_TMP}/fnic/rpm/SOURCES

	cp fnic-$VERSION.tar.bz2 ${PACKAGE_TMP}/fnic/rpm/SOURCES
	cp package-rhel/fnic.files ${PACKAGE_TMP}/fnic/rpm/SOURCES
	cp package-rhel/fnic.conf ${PACKAGE_TMP}/fnic/rpm/SOURCES
	cp package-rhel/fnic.spec ${PACKAGE_TMP}/fnic/rpm/SPECS/
	cp package-rhel/ddiskit/Makefile ${PACKAGE_TMP}
	cp package-rhel/ddiskit/modules.dep ${PACKAGE_TMP}/fnic
	cd ${PACKAGE_TMP}
	make KNAME=${KNAME} all
}

function oel() {
        common_clean

        cp -r package-oel/ddiskit-oel/* ${PACKAGE_TMP}
        mkdir -p ${PACKAGE_TMP}/fnic/rpm/SPECS
        mkdir -p ${PACKAGE_TMP}/fnic/rpm/SOURCES

        setup_fcc ${PACKAGE_TMP}/fnic/rpm/SOURCES
        setup_nvmef_connect ${PACKAGE_TMP}/fnic/rpm/SOURCES

        cp fnic-$VERSION.tar.bz2 ${PACKAGE_TMP}/fnic/rpm/SOURCES
        cp package-oel/fnic.files ${PACKAGE_TMP}/fnic/rpm/SOURCES
        cp package-oel/fnic.conf ${PACKAGE_TMP}/fnic/rpm/SOURCES
        cp package-oel/fnic.spec ${PACKAGE_TMP}/fnic/rpm/SPECS/
        cp package-oel/ddiskit/Makefile ${PACKAGE_TMP}
        cp package-oel/ddiskit/modules.dep ${PACKAGE_TMP}/fnic
        cd ${PACKAGE_TMP}
        make KNAME=${KNAME} all
}

function sles() {
	common_clean

	rm -rf ${PACKAGE_TMP}/fnic-$VERSION
	rm -rf ${PACKAGE_TMP}/fnic-$VERSION.tar.bz2
	cp fnic-$VERSION.tar.bz2 ${PACKAGE_TMP}

	cd ${PACKAGE_TMP}
	tar xf fnic-$VERSION.tar.bz2
	mv fnic-$VERSION cisco-fnic-$VERSION
	tar cf cisco-fnic-$VERSION.tar.bz2 cisco-fnic-$VERSION
	rm -rf cisco-fnic-$VERSION
	cd ..

	setup_fcc ${PACKAGE_TMP}
	setup_nvmef_connect ${PACKAGE_TMP}

	cp package-sles/cisco-fnic.spec ${PACKAGE_TMP}
	cp package-sles/cisco-fnic.files ${PACKAGE_TMP}
	cp package-sles/Makefile ${PACKAGE_TMP}
	cp package-sles/update.post ${PACKAGE_TMP}
	cd ${PACKAGE_TMP}

	make all
}

function xenserver() {
        common_clean

	export FNIC_RPM_ROOT="$PACKAGE_TMP/fnic/rpm"
	mkdir -p ${FNIC_RPM_ROOT}/SOURCES
        cp fnic-$VERSION.tar.bz2 ${FNIC_RPM_ROOT}/SOURCES/cisco-fnic-$VERSION.tar.bz2
        cp package-xs/cisco-fnic.spec ${PACKAGE_TMP}
        cp package-xs/Makefile ${PACKAGE_TMP}

	setup_fcc ${FNIC_RPM_ROOT}/SOURCES
	setup_nvmef_connect ${FNIC_RPM_ROOT}/SOURCES

        cd ${PACKAGE_TMP}
        make
}

export TOP_SRC=`readlink -f ../../../../../..`
export TOOLS="${TOP_SRC}/sa/src/ofc/tools/"

# We'll get the absolute name of this directory later
export PACKAGE_TMP=fnic-${VERSION}-${option}

case $option in
	rhel7 | rhel8 | rhel9 | centos7 | centos8)
		rhel
		;;
	sles12 | sles15)
		sles
		;;
	xs80)
		xenserver
		;;
	oel7 | oel8 | oel9)
	        oel
		;;
	*)
		show_usage
		exit 1
		;;
esac
