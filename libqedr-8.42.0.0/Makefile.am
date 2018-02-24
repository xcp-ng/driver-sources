
lib_LTLIBRARIES = src/libqedr.la

AM_CFLAGS = -Wall -D_GNU_SOURCE

if HAVE_LD_VERSION_SCRIPT
    qedr_version_script = -Wl,--version-script=$(srcdir)/src/qelr.map
else
    qedr_version_script =
endif

src_libqedr_la_SOURCES = src/qelr_main.c src/qelr_verbs.c src/qelr_chain.c
src_libqedr_la_LDFLAGS = -avoid-version -module -release @IBV_DEVICE_LIBRARY_EXTENSION@\
    $(qedr_version_script)

qedrconfdir = $(sysconfdir)/libibverbs.d
qedrconf_DATA = qedr.driver

EXTRA_DIST = src/qelr.map src/qelr_main.h src/qelr_abi.h src/qed_chain.h src/qelr_hsi.h\
             libqedr.spec.in qedr.driver

dist-hook: libqedr.spec
	cp libqedr.spec $(distdir)
