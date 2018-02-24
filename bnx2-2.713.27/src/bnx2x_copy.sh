#!/bin/sh

bnx2x_dir="../../bnx2x/src"
bnx2i_dir="../../bnx2i/driver"
bnx2fc_dir="../../bnx2fc/driver"
bnx2x_files="bnx2x.h bnx2x_reg.h bnx2x_fw_defs.h bnx2x_hsi.h bnx2x_mfw_req.h bnx2x_57710_int_offsets.h bnx2x_57711_int_offsets.h bnx2x_compat.h bnx2x_sp.h" 
bnx2i_files="57xx_iscsi_constants.h 57xx_iscsi_hsi.h"
bnx2fc_files="bnx2fc_constants.h"

if [ -d "$bnx2x_dir" ]
then
	for i in $bnx2x_files
	do
		echo "Copying $i"
		cp -f $bnx2x_dir/$i .
	done
else
	echo "Cannot find $bnx2x_dir so no bnx2x header files will be copied"
fi

if [ -d "$bnx2i_dir" ]
then
	for i in $bnx2i_files
	do
		echo "Copying $i"
		cp -f $bnx2i_dir/$i .
	done
else
	echo "Cannot find $bnx2i_dir so no bnx2i header files will be copied"
fi

if [ -d "$bnx2fc_dir" ]
then
	for i in $bnx2fc_files
	do
		echo "Copying $i"
		cp -f $bnx2fc_dir/$i .
	done
else
	echo "Cannot find $bnx2fc_dir so no bnx2fc header files will be copied"
fi

# Exit 0 in either case so we don't cause make to fail
exit 0
