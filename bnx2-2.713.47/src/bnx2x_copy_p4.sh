#!/bin/sh

bnx2x_dir="../../../../../../servers/main/nx2/577xx/drivers/linux"
bnx2x_files="bnx2x.h bnx2x_reg.h bnx2x_fw_defs.h bnx2x_hsi.h bnx2x_mfw_req.h bnx2x_57710_int_offsets.h bnx2x_57711_int_offsets.h bnx2x_57712_int_offsets.h bnx2x_compat.h bnx2x_sp.h bnx2x_dcb.h bnx2x_link.h bnx2x_stats.h bnx2x_vfpf.h" 

cd ../../../../../../servers/main/nx2/577xx/drivers/linux
make -j
cd -

bnx2x_count=0

if [ -d "$bnx2x_dir" ]
then
	for i in $bnx2x_files
	do
		diff -E -Z -B -b $bnx2x_dir/$i $i > /dev/null
		if [ "$?" = "1" ]; then
			echo "MISMATCH Submit $bnx2x_dir/$i in depot/main/NetXtreme2/linux/bnx2/src !!"
			bnx2x_count=`expr $bnx2x_count + 1`
			p4 edit $i
			cp -f $bnx2x_dir/$i .
		 else
                        echo "Copying $i"
                        cp -f $bnx2x_dir/$i .
                fi
	done
else
	echo "Cannot find $bnx2x_dir so no bnx2x header files will be copied"
fi

if [ "$bnx2x_count" != "0" ]; then
	echo "Total $bnx2x_count headers need re-packaging!!"
fi

# Exit 0 in either case so we don't cause make to fail
exit 0
