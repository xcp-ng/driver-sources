# mpi3mr makefile
EXTRA_CFLAGS := -DVALIDATION_SUPPORT_CODE
obj-m += mpi3mr.o
mpi3mr-y +=  mpi3mr_os.o     \
		mpi3mr_fw.o \
		mpi3mr_app.o \
		mpi3mr_debugfs.o \
		mpi3mr_transport.o 


