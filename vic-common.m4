dnl
dnl Copyright 2008-2018 Cisco Systems, Inc.  All rights reserved.
dnl
dnl [Insert appropriate license here when releasing outside of Cisco]
dnl

dnl -------------------------------------------------------------------------
dnl Legacy macros that check for things by looking in the filesystem
dnl (e.g., checking for the existence of files, grepping in files, etc.).
dnl
dnl New code should not use these macros.
dnl -------------------------------------------------------------------------

dnl
dnl Macro to set var based on existence files
dnl
dnl VIC_LINUX_CHECK_FILE(file, var, comment)
dnl
dnl $1: file to check for
dnl $2: shell variable to set to 0 or 1, then DEFINE_UNQUOTED
dnl $3: comment for DEFINE_UNQUOTED
dnl
AC_DEFUN([VIC_LINUX_CHECK_FILE],
[
 AC_CHECK_FILE($1, $2=1, $2=0)
 AC_DEFINE_UNQUOTED($2, @S|@$2, $3)
])

dnl
dnl  Macro to grep for a word/pattern in a file specified by flag
dnl
dnl  VIC_GREP(file, word, yes_action, no_action, flag)
dnl   no_action is taken if file does not exist
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: execute if found
dnl $4: execute if not found
dnl $5: if "WORD", grep for "\b$2\b", otherwise grep for "$2"
dnl
AC_DEFUN([VIC_GREP],
[
 pat_found=""
 grep_input=""
 AC_MSG_CHECKING([for $2 in $1])
 if test -r "$1"; then
    case $5 in
        "PATTERN") grep_input="$2"
        ;;
        "WORD") grep_input="\b$2\b"
        ;;
        *) grep_input="$2"
        ;;
    esac
    if grep "$grep_input" $1 2>&1 > /dev/null; then
        pat_found=1
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
    fi
 else
    AC_MSG_RESULT([file not found])
 fi
 if test -n "$pat_found"; then
    true; $3
 else
    true; $4
 fi
])

dnl
dnl  Macro to grep for a pattern in a file between start and end
dnl  patterns, which should be on different lines.
dnl  start
dnl  	pattern
dnl  end
dnl
dnl  VIC_GREP_MULTILINE(file, search_pattern, start, end, yes_action, no_action)
dnl   no_action is taken if file does not exist
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: start pattern
dnl $4: end pattern
dnl $5: execute if found
dnl $6: execute if not found
dnl
AC_DEFUN([VIC_GREP_MULTILINE],
[
 pat_found=""
 AC_MSG_CHECKING([for $2 in $1 between $3 and $4])
 if test -r "$1"; then
    if sed -n -e '/$3/,/$4/p' $1 | grep "$2" 2>&1 > /dev/null; then
        pat_found=1
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
    fi
 else
    AC_MSG_RESULT([file not found])
 fi
 if test -n "$pat_found"; then
    true; $5
 else
    true; $6
 fi
])

dnl
dnl Macro to grep for a pattern in a file between start1/start2 and end2/end1:
dnl start1
dnl   start2
dnl     pattern
dnl   end2
dnl end1
dnl
dnl VIC_GREP_MULTILINE_NESTED(file, search_pattern,
dnl                                start1, end1, start2, end2,
dnl                                yes_action, no_action)
dnl   no_action is taken if file does not exist
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: start pattern 1
dnl $4: end pattern 1
dnl $5: start pattern 2
dnl $6: end pattern 2
dnl $7: execute if found
dnl $8: execute if not found
dnl
dnl
dnl
AC_DEFUN([VIC_GREP_MULTILINE_NESTED],
[
 pat_found=""
 AC_MSG_CHECKING([for $2 in $1 between '$3'/'$5' and '$6'/'$4'])
 if test -r "$1"; then
    if sed -n -e '/$3/,/$4/p' $1 | sed -n -e '/$5/,/$6/p' | grep "$2" 2>&1 > /dev/null; then
        pat_found=1
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
    fi
 else
    AC_MSG_RESULT([file not found])
 fi
 if test -n "$pat_found"; then
    true; $7
 else
    true; $8
 fi
])

dnl
dnl Macro to set var based on linux includes
dnl
dnl VIC_LINUX_DEF(file, search_pattern, var, comment, flag)
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: shell variable to set to 0 or 1, then SUBST and DEFINE_UNQUOTED
dnl $4: comment for DEFINE_UNQUOTED
dnl $5: $6 parameter for VIC_GREP (i.e., "WORD" or "PATTERN")
dnl
AC_DEFUN([VIC_LINUX_DEF],
[
 VIC_GREP($LINUX_HEADER_DIR/include/$1, $2, $3=1, $3=0, $5)
 AC_SUBST($3)
 AC_DEFINE_UNQUOTED($3, @S|@$3, $4)
])

dnl
dnl Macro to set var based on linux includes between start pattern and end pattern
dnl
dnl  VIC_LINUX_DEF_MULTILINE(file, search_pattern, var, start , end, comment)
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: macro_name for DEFINE_UNQUOTED
dnl $4: start
dnl $5: end
dnl $6: comment for DEFINE_UNQUOTED
dnl
AC_DEFUN([VIC_LINUX_DEF_MULTILINE],
[
 VIC_GREP_MULTILINE($LINUX_HEADER_DIR/include/$1, [$2], [$4], [$5], $3=1, $3=0)
 AC_SUBST($3)
 AC_DEFINE_UNQUOTED($3, @S|@$3, $6)
])

dnl
dnl Macro to grep and set var for a pattern in a file between start1/start2 and
dnl end2/end1:
dnl
dnl start1
dnl   start2
dnl     pattern
dnl   end2
dnl end1
dnl
dnl VIC_LINUX_DEF_MULTILINE_NESTED(file, search_pattern, var_name,
dnl                                   start1, end1, start2, end2)
dnl
dnl   no_action is taken,  var is set to 0 if file does not exist.
dnl
dnl $1: file to search
dnl $2: pattern to search for
dnl $3: shell variable to set/reset then SUBST and DEFINE_UNQUOTED
dnl $4: start pattern 1
dnl $5: end pattern 1
dnl $6: start pattern 2
dnl $7: end pattern 2
dnl
dnl
AC_DEFUN([VIC_LINUX_DEF_MULTILINE_NESTED],
[
 VIC_GREP_MULTILINE_NESTED($LINUX_HEADER_DIR/include/$1,
                                [$2], [$4], [$5], [$6], [$7], $3=1, $3=0)
 AC_SUBST($3)
 AC_DEFINE_UNQUOTED($3, @S|@$3, $8)
])

dnl -------------------------------------------------------------------------
dnl Macro to setup being able to run proper AC_* macros: we have to
dnl snarf a bunch of command line flags to use in CPPFLAGS so that we
dnl can compile Linux kernel source code.
dnl -------------------------------------------------------------------------

dnl
dnl Let's find the following kernel flags:
dnl
dnl LINUXINCLUDE
dnl NOSTDINC_FLAGS
dnl KBUILD_CPPFLAGS
dnl
AC_DEFUN([VIC_SETUP_KERNEL_SNARFING],[
	AC_MSG_CHECKING([for kernel compile flags (please wait)])

	# Find the Linux source and build trees
	dir=/lib/modules/$KNAME
	AS_IF([test -d $dir/source && test -d $dir/source/kernel],
	      [linux_src_tree=$dir/source],
	      [AS_IF([test -d $dir/build && test -d $dir/build/kernel],
		     [linux_src_tree=$dir/build],
		     [AC_MSG_WARN([Could not find Linux source tree])
		      AC_MSG_ERROR([Cannot continue])])
	      ])

	build_dir="$(readlink -f $dir/build)"
	source_dir="$(readlink -f $dir/source)"
	AS_IF([test "$build_dir" = "$source_dir" ],
	      [VIC_BUILDDIR_NEEDED=0], [VIC_BUILDDIR_NEEDED=1])

	# Make a dummy kernel module Makefile to
	# snarf the kernel build system $(c_flags)
	vic_startdir=`pwd`
	vic_snarfing_tmpdir=conftmp.$$
	rm -rf $vic_snarfing_tmpdir
	mkdir $vic_snarfing_tmpdir
	cd $vic_snarfing_tmpdir

	p=`pwd`
	outfile=$p/kernel-build-flags.txt

	# Note the obscure m4 quoting of the @ in the Makefile
	# (the [] will not appear in the emitted Makefile).
	cat >> Makefile <<EOF
# This is a bogus module makefile, just so that we can slurp out the
# computed c_flags and dump them into a file that we can read outside of
# the kernel "make" system.

obj-m := get-the-flags.o

outfile=$outfile

# We force this rule to run, because get-the-flags.c does not exist.
# Just to be social, this rule does actually create that file.
# But then it emits c_flags into the text outfile.
# \$(M) is set by the kernel build system to be the module source directory.
\$(M)/get-the-flags.c:
	touch \$[@]
	rm -f \$(outfile)
	echo VIC_CONFTEST_KERNEL_C_FLAGS=\"\$(c_flags)\" >> \$(outfile)
EOF

	# Run our dummy rule
	# &5 is config.log; save all the output there
	cmd="make M=$p -C $build_dir modules"
	$as_echo "$as_me:${as_lineno-$LINENO}: $cmd" >&5
	eval $cmd >&5 2>&5

	# Source the outfile that it generated, which will set the
	# VIC_CONFTEST_KERNEL_C_FLAGS env variable for us.
	if test ! -f $outfile; then
	    AC_MSG_WARN([Failed to get kernel c_flags])
	    VIC_SETUP_KERNEL_SNARFING_CLEANUP
	    AC_MSG_ERROR([See config.log for more details])
	fi
	. $outfile

	# Go through the c_flags and change all relative paths to be
	# absolute paths (because we're not going to compile in the
	# kernel build tree).
	VIC_KERNEL_CPPFLAGS=
	for token in $VIC_CONFTEST_KERNEL_C_FLAGS; do
		case $token in
		-I/*)
			# Absolute include dirs can be added directly.
			VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS $token"
			;;
		-I*)
			# If it wasn't -I/, then the path following -I
			# is a relative path.  Relative paths have to
			# be transmorgafied into absolute paths before
			# adding.  Just remove the prefix -I and the
			# optional "./", first, and then prefix it
			# with the relevant dir (source and/or build).
			rel_dir=`echo $token | sed -e 's/^-I//'`
			rel_dir=`echo $rel_dir | sed -e 's/^\.\///'`
			AS_IF([test -d "$source_dir/$rel_dir"],
			      [VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS -I$source_dir/$rel_dir"])
			AS_IF([test $VIC_BUILDDIR_NEEDED -eq 1 -a -d "$build_dir/$rel_dir"],
			      [VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS -I$build_dir/$rel_dir"])
			;;
		-W* | -c | -o | -g | -O* | -f* | -m* | -p* | -DCONFIG_* | -DKBUILD_* | -DDEBUG_* | -DMODULE)
			# Ignore all of these flags; we don't need
			# them for the purposes of
			# configure/compile-time testing.
			;;
		conftest* | get-the-flags*)
			# Ignore the dummy module arguments, too.
			;;
		include*|./include*|./arch*)
			# Relative include paths have to be
			# transmorgafied into absolute dirs before
			# adding.
			rel_file=`echo $token | sed -e 's/^.\///'`
			AS_IF([test -f "$build_dir/$rel_file"],
			      [VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS $build_dir/$rel_file"],
			      [VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS $source_dir/$rel_file"])
			;;
		*)
			# If we don't know what it is, add it, just to
			# be conservative.
			VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS $token"
			;;
		esac
	done

	# KBUILD_MODNAME is a kernel compile flag specific to module
	# targets.  We stripped it out, above.  Hardcoding a junk
	# value and adding it to kernel compile flags as it is needed
	# by some configure tests, specially those involving
	# ib_verbs.h.
	VIC_KERNEL_CPPFLAGS="$VIC_KERNEL_CPPFLAGS "'-DKBUILD_MODNAME="VIC_IS_AWESOME"'
	AC_MSG_RESULT([$VIC_KERNEL_CPPFLAGS])

	cd "$vic_startdir"

	# Check that compiling with these kernel CPPFLAGS works.
	#
	# Try to find a prerequisite kernel header file
	# (<linux/netdevice.h>.  If the compile doesn't work, then
	# either the compiler flags don't work or you have a busted
	# compiler.  Either way, it's not worth continuing.  Note that
	# we can't use VIC AC CHECK_HEADER directly, because it AC
	# REQUIREs this macro.  So call the back end macro to do the
	# heavy lifting.
	_VIC_WRAP_AC([AC_CHECK_HEADERS([linux/netdevice.h],
		[VIC_SNARFING_WORKED=1], [VIC_SNARFING_WORKED=0], [/**/])])

	AS_IF([test $VIC_SNARFING_WORKED -eq 0],
	    AC_MSG_WARN([A trivial program including <linux/netdevice.h> failed to compile])
	    AC_MSG_WARN([Things to check:])
	    AC_MSG_WARN([1. Do you have a compiler installed?])
	    AC_MSG_WARN([2. Is your compiler compatible with the kernel source?])
	    AC_MSG_WARN([3. Do you have the correct version of kernel-headers installed?])
	    AC_MSG_WARN([Check config.log for the specific error.])
	    VIC_SETUP_KERNEL_SNARFING_CLEANUP
	    AC_MSG_ERROR([Cannot continue.]))
])

AC_DEFUN([VIC_SETUP_KERNEL_SNARFING_CLEANUP],[
    AS_IF([test -n "$vic_startdir" -a -n "$vic_snarfing_tmpdir" -a -d "$vic_snarfing_tmpdir"],
	  [cd "$vic_startdir"
	   rm -rf "$vic_snarfing_tmpdir"
	   vic_startdir=
	   vic_snarfing_tmpdir=])
])

dnl -------------------------------------------------------------------------
dnl Simple wrappers around the AC_* macros.
dnl -------------------------------------------------------------------------

dnl
dnl Wrapper mechanism: make it easy to wrap existing Autoconf macros
dnl
dnl $1: the wrapper body
dnl
AC_DEFUN([VIC_WRAP_AC],[
    AC_REQUIRE([AC_PROG_CPP])
    AC_REQUIRE([VIC_SETUP_KERNEL_SNARFING])

    dnl Use indirection so that we can use the back-end here in
    dnl vic-common.m4 (without the REQUIREs).
    _VIC_WRAP_AC([$1])
])

dnl
dnl Identical to WRAP_AC, but without the REQUIREs.
dnl
dnl This allows us to use this back-end macro from with SETUP_KERNEL_SNARFING.
dnl
AC_DEFUN([_VIC_WRAP_AC],[
    CPPFLAGS_save=$CPPFLAGS
    CPPFLAGS=$VIC_KERNEL_CPPFLAGS

    $1

    CPPFLAGS=$CPPFLAGS_save
    unset CPPFLAGS_save
])

dnl
dnl Note that we do not wrap these AC macros because they assume too
dnl much userspace stuff.  For example, they include <stdio.h>, which
dnl tends to cause Bad Things to happen / conflict with kernel header
dnl files.  Unfortunately, there does not seem to be a way for us to tell
dnl these macros *not* to include these userspace-only header files, so
dnl our solution is just to not make them available here for kernel-level
dnl compliation tests.
dnl
dnl CHECK_FUNC
dnl CHECK_FUNCS
dnl CHECK_FUNCS_ONCE
dnl

dnl
dnl Simple wrapper around AC CHECK_HEADER for kernel include flies.
dnl
dnl $1 header to check
dnl $2 action if found
dnl $3 action if not found
dnl $4 additional includes
dnl
AC_DEFUN([VIC_AC_CHECK_HEADER],[
    dnl If we provide an empty $4, Autoconf will automatically use
    dnl AC_INCLUDES_DEFAULT, which includes things like <stdio.h> --
    dnl which is undesirable.  So make sure that we never provide an
    dnl empty 4th argument.
    AS_IF([test -z "$4"],
       [VIC_WRAP_AC([AC_CHECK_HEADERS([$1], [$2], [$3], [/* hello world */])])],
       [VIC_WRAP_AC([AC_CHECK_HEADERS([$1], [$2], [$3], [$4])])])
])


dnl
dnl Simple wrapper around VIC CHECK_HEADER: action if found/not found is
dnl to AC DEFINE a value to 1 (if found) or 0 (if not found)
dnl
dnl $1 header to check
dnl $2 macro to AC DEFINE to 1 or 0
dnl $3 addional includes
dnl
AC_DEFUN([VIC_AC_CHECK_HEADER_DEFINE],[
    VIC_AC_CHECK_HEADER([$1], [$2=1], [$2=0], [$3])
    AC_DEFINE_UNQUOTED([$2], [$$2], [Whether <$1> exists or not])
])


dnl
dnl Simple wrapper around AC CHECK_DECL for kernel symbols.
dnl
dnl $1 header to check
dnl $2 symbol
dnl $3 action if found
dnl $4 action if not found
dnl
AC_DEFUN([VIC_AC_CHECK_DECL],[
    VIC_WRAP_AC([AC_CHECK_DECL([$2], [$3], [$4], [#include "$1"])])
])


dnl
dnl Simple wrapper around VIC CHECK_DECL: action if found/not found is
dnl to AC DEFINE a value to 1 (if found) or 0 (if not found)
dnl
dnl $1 header to check
dnl $2 symbol
dnl $3 macro to AC DEFINE to 1 or 0
dnl
AC_DEFUN([VIC_AC_CHECK_DECL_DEFINE],[
    VIC_AC_CHECK_DECL([$1], [$2], [$3=1], [$3=0])
    AC_DEFINE_UNQUOTED([$3], [$$3], [Whether we have declaration $2 in <$2> or not])
])


dnl
dnl Variant of CHECK_DECL_DEFINE: Use when you need
dnl to include more than one header file
dnl
dnl $1 headers to include
dnl $2 symbol
dnl $3 macro to AC DEFINE to 1 or 0
dnl
AC_DEFUN([VIC_AC_CHECK_DECL_INCLUDES_DEFINE],[
    VIC_WRAP_AC([AC_CHECK_DECL([$2], [$3=1], [$3=0], [$1])])
    AC_DEFINE_UNQUOTED([$3], [$$3], [Whether we have declaration $2 in $1 or not])
])


dnl
dnl Simple wrapper around AC CHECK_MEMBER
dnl
dnl
dnl $1 header to check
dnl $2 aggregate.member
dnl $3 action if found
dnl $4 action if not found
dnl
AC_DEFUN([VIC_AC_CHECK_MEMBER],[
    VIC_WRAP_AC([AC_CHECK_MEMBER([$2], [$3], [$4], [#include "$1"])])
])


dnl
dnl Simple wrapper around VIC CHECK_MEMBER: action if found/not found is
dnl to AC DEFINE a value to 1 (if found) or 0 (if not found)
dnl
dnl $1 header to check
dnl $2 aggregate.member
dnl $3 macro to AC DEFINE to 1 or 0
dnl
AC_DEFUN([VIC_AC_CHECK_MEMBER_DEFINE],[
    VIC_AC_CHECK_MEMBER([$1], [$2], [$3=1], [$3=0])
    AC_DEFINE_UNQUOTED([$3], [$$3], [Whether we have member $2 in <$1> or not])
])


dnl
dnl Simple wrapper around AC CHECK_TYPE for kernel symbols.
dnl
dnl $1 type to check
dnl $2 action if found
dnl $3 action if not found
dnl $4 includes
dnl
AC_DEFUN([VIC_AC_CHECK_TYPE],[
    VIC_WRAP_AC([AC_CHECK_TYPE([$1], [$2], [$3], [$4])])
])

dnl
dnl Simple wrapper around VIC CHECK_TYPE: action if found/not found is
dnl to AC DEFINE a value to 1 (if found) or 0 (if not found).  Also
dnl set the corresponding shell variable to 1 or 0.
dnl
dnl $1 type to check
dnl $2 macro to AC DEFINE to 1 or 0
dnl $3 includes
dnl
AC_DEFUN([VIC_AC_CHECK_TYPE_DEFINE],[
    VIC_AC_CHECK_TYPE([$1], [$2=1], [$2=0], [$3])
    AC_DEFINE_UNQUOTED([$2], [$$2], [Whether we have $1 or not])
])


dnl
dnl Simple wrapper around AC COMPILE_IFELSE for kernel symbols.
dnl
dnl $1 input
dnl $2 action if true
dnl $3 action if false
dnl
AC_DEFUN([VIC_AC_COMPILE_IFELSE],[
    VIC_WRAP_AC([AC_COMPILE_IFELSE([$1], [$2], [$3])])
])

dnl -------------------------------------------------------------------------
dnl Do several things regarding gcc retpoline support:
dnl
dnl 1. Add --[en|dis]able-compile-without-retpoline CLI flag
dnl 2. Check to see if gcc supports the retpoline CLI flags
dnl 3. If gcc does not, and --disable-compile-without-retpoline was
dnl    specified, abort.
dnl 4. If gcc supports the retpoline CLI flags and they are not
dnl    already in $CFLAGS, add the missing flags to VIC_EXTRA_CFLAGS (and
dnl    AC DEFINE and AC SUBST it).
dnl
AC_DEFUN([VIC_CHECK_GCC_RETPOLINE],[
    AC_REQUIRE([AC_PROG_CPP])
    AC_REQUIRE([VIC_SETUP_KERNEL_SNARFING])

    AC_ARG_ENABLE([compile-without-retpoline],
                  [AS_HELP_STRING([--enable-compile-without-retpoline])])

    # See if gcc supports retpoline CLI options
    AC_MSG_CHECKING([if gcc supports retpoline CLI options])
    CFLAGS_save=$CFLAGS
    retpoline_cflags="-mindirect-branch=thunk-inline -mindirect-branch-register"
    CFLAGS="$CFLAGS $retpoline_cflags"
    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[
int i = 0;])],
                       [vic_gcc_retpoline=1
		        AC_MSG_RESULT([yes])],
                       [vic_gcc_retpoline=0
		        AC_MSG_RESULT([no])])
    CFLAGS=$CFLAGS_save
    AC_DEFINE_UNQUOTED([VIC_HAVE_RETPOLINE], [$vic_gcc_retpoline],
                       [Whether this driver was compiled with retpoline support or not])

    # If --disable-compile-without-retpoline was supported and gcc
    # does not support retpoline, abort.
    AS_IF([test $vic_gcc_retpoline -eq 0 && test "$enable_compile_without_retpoline" = "no"],
          [AC_MSG_WARN([gcc does not support retpoline (i.e.: $retpoline_flags)])
	   AC_MSG_WARN([and --disable-compile-without-retpoline was specified,])
	   AC_MSG_WARN([so we are aborting.])
	   AC_MSG_ERROR([Cannot continue])])

    # If gcc supports retpoline, see if each of them are already in
    # CFLAGS.  If not, add them to EXTRA_KCFLAGS.
    VIC_EXTRA_KCFLAGS=
    AS_IF([test $vic_gcc_retpoline -eq 1],
          [for flag in $retpoline_cflags; do
               AS_IF([test -z "`echo $CFLAGS_save | grep -- $flag`"],
                     [VIC_EXTRA_KCFLAGS="$flag $VIC_EXTRA_KCFLAGS"])
           done
          ])
    AC_SUBST(VIC_EXTRA_KCFLAGS)
    AC_DEFINE_UNQUOTED([VIC_EXTRA_KCFLAGS], ["$VIC_EXTRA_KCFLAGS"],
                       [Extra KCFLAGS from VIC_CHECK_GCC_RETPOLINE])
])

dnl -------------------------------------------------------------------------
dnl
dnl Helper macro to save environment variables before we source
dnl /etc/os-release (because we may already have env variables of the
dnl same names as the fields in /etc/os-release).
dnl
dnl Used by VIC_LINUX_DISTRO.
dnl
AC_DEFUN([_VIC_SAVE_OS_RELEASE_ENV],[
    for line in `cat /etc/os-release`; do
        vic_var=`echo $line | grep = | cut -d= -f1`
        if test -n "$vic_var"; then
           eval ${vic_var}_save=\${$vic_var}
        fi
    done
])

dnl
dnl Restore the env variables that were saved via VIC_SAVE_OS_RELEASE_ENV.
dnl
dnl Used by VIC_LINUX_DISTRO.
dnl
AC_DEFUN([_VIC_RESTORE_OS_RELEASE_ENV],[
    for line in `cat /etc/os-release`; do
        vic_var=`echo $line | grep = | cut -d= -f1`
        if test -n "$vic_var"; then
	   vic_save_name=${vic_var}_save
           eval "$vic_var=\${$vic_save_name}"
	   unset $vic_save_name
        fi
    done
])

dnl Macro to check which Linux distro we are using
dnl
dnl These shell variables are SUBST and/or DEFINE_UNQUOTED:
dnl
dnl *  LINUX_DISTRO
dnl *  LINUX_DISTRO_DISK
dnl *  RHEL_VER
dnl *  RHEL_MAJOR
dnl *  RHEL_MINOR
dnl *  RHEL_EXTRA
dnl *  SLES_VER
dnl *  SLES_PATCHLEVEL
dnl
AC_DEFUN([VIC_LINUX_DISTRO],[
    LINUX_DISTRO=
    SLES_VER=0
    SLES_PATCHLEVEL=0

    # CentOS 6 requires 3 here.
    # CentOS 7 and beyond will change this value, below.
    centos_field=3

    AC_ARG_WITH(xenserver-version,
        AC_HELP_STRING([--with-xenserver-version=<version>],
                      [XenServer version used in package name]))

    AC_MSG_CHECKING([Linux distribution])

    os_release_file=/etc/os-release
    if test -r $os_release_file; then
        _VIC_SAVE_OS_RELEASE_ENV
        . $os_release_file

        if test -n "`echo $NAME | grep 'Red Hat Enterprise Linux'`"; then
            # RHEL 7 and beyond (RHEL 6 does not have /etc/os-release)
            RHEL_VER=$VERSION_ID
            RHEL_MAJOR=`echo $RHEL_VER | cut -d. -f1`
            RHEL_MINOR=`echo $RHEL_VER | cut -d. -f2`
            RHEL_EXTRA=""

            RPM_BUILD_PKG=redhat-rpm-config

            LINUX_DISTRO=rhel
            LINUX_DISTRO_DISK="rhel$RHEL_MAJOR"
            LINUX_HEADER_DIR=/lib/modules/$KNAME/source
            DISTRO_VER=${LINUX_DISTRO}${RHEL_MAJOR}u${RHEL_MINOR}

        elif test -n "`echo $NAME | grep 'Rocky Linux'`"; then
            # Rocky linux is a clone of RHEL
            RHEL_VER=$VERSION_ID
            RHEL_MAJOR=`echo $RHEL_VER | cut -d. -f1`
            RHEL_MINOR=`echo $RHEL_VER | cut -d. -f2`
            RHEL_EXTRA=""

            RPM_BUILD_PKG=redhat-rpm-config

            LINUX_DISTRO=rocky
            LINUX_DISTRO_DISK="rocky$RHEL_MAJOR"
            LINUX_HEADER_DIR=/lib/modules/$KNAME/source
            DISTRO_VER=${LINUX_DISTRO}${RHEL_MAJOR}u${RHEL_MINOR}

        elif test -n "`echo $NAME | grep 'Oracle Linux'`"; then
            # Oracle Linux is a clone of RHEL
            # But we also have to care about UEK kernels
            RHEL_VER=$VERSION_ID
            RHEL_MAJOR=`echo $RHEL_VER | cut -d. -f1`
            RHEL_MINOR=`echo $RHEL_VER | cut -d. -f2`

            RPM_BUILD_PKG=redhat-rpm-config

            LINUX_DISTRO=ol
            LINUX_DISTRO_DISK="ol$RHEL_MAJOR"
            LINUX_HEADER_DIR=/lib/modules/$KNAME/source

            if test -n "`echo $KNAME | grep 'uek'`"; then
                # If this is UEK, then don't put the major/minor in the name
                # and put the uek kernel version there.
                RHEL_EXTRA="uek_`echo $KNAME | sed -E 's;(.*)\.el[[[:digit:]]]+uek.*;\1;' | tr - _`"
                DISTRO_VER=${LINUX_DISTRO}${RHEL_EXTRA}
            else
                RHEL_EXTRA="rhck"
                DISTRO_VER=${LINUX_DISTRO}${RHEL_MAJOR}u${RHEL_MINOR}${RHEL_EXTRA}
            fi



        elif test -n "`echo $NAME | grep CentOS`"; then
            # CentOS 7 and beyond (CentOS 6 does not have
            # /etc/os-release) only has the major version number in
            # /etc/os-release (!).  Note that, per above, this is
            # different than RHEL >=7.x, which *does* have major and
            # minor in /etc/os-release.  Thanks, CentOS!

            # So we might as well fall through to the old CentOS code
            # block, below (i.e., keep $LINUX_DISTRO empty), which
            # extracts the major and minor from /etc/redhat-release
            # (which is a sym link to /etc/centos-release).
            centos_field=4

        elif test -n "`echo $NAME | grep Xen`"; then
            XS_MAJOR=`echo $VERSION_ID | cut -d. -f1`
            XS_MINOR=`echo $VERSION_ID | cut -d. -f2`

            RPM_BUILD_PKG=redhat-rpm-config

            LINUX_DISTRO=XenServer
            LINUX_DISTRO_DISK="xs${XS_MAJOR}${XS_MINOR}"
            LINUX_HEADER_DIR=/usr/src/kernels/$KNAME-x86_64
            # We'll check $with_xenserver_version below

	elif test -n "`echo $NAME | grep Citrix`"; then
            XS_MAJOR=`echo $VERSION_ID | cut -d. -f1`
            XS_MINOR=`echo $VERSION_ID | cut -d. -f2`

            RPM_BUILD_PKG=redhat-rpm-config

            LINUX_DISTRO=CitrixHypervisor
            LINUX_DISTRO_DISK="xs${XS_MAJOR}${XS_MINOR}"
            LINUX_HEADER_DIR=/usr/src/kernels/$KNAME-x86_64

        elif test -n "`echo $NAME | grep SLES`"; then
            SLES_VER="`echo $VERSION_ID | cut -d. -f1`"
            if test -n "`echo $VERSION_ID | fgrep .`"; then
                SLES_PATCHLEVEL=`echo $VERSION_ID | cut -d. -f2`
            else
                SLES_PATCHLEVEL=0
            fi

            RPM_BUILD_PKG=rpm-build

            LINUX_DISTRO=SLES
            LINUX_DISTRO_DISK=sles$SLES_VER
            LINUX_HEADER_DIR=/usr/src/linux-`echo $KNAME |  sed "s/-default//"`
            DISTRO_VER=sles${SLES_VER}sp${SLES_PATCHLEVEL}

        elif test -n "`echo $NAME | grep Ubuntu`"; then
            DEB_BUILD_PKG=dpkg-dev

            LINUX_DISTRO=Ubuntu
            LINUX_DISTRO_DISK=ubuntu
            LINUX_HEADER_DIR=/usr/src/linux-headers-`echo $KNAME | sed "s/-generic//"`
            DISTRO_VER=ubuntu${DISTRO_REL}
        fi
        _VIC_RESTORE_OS_RELEASE_ENV
    fi

    # If we get here an $LINUX_DISTRO is empty, then it's RHEL 6
    # (because RHEL 6 does not have /etc/os-release).
    if test -z "$LINUX_DISTRO"; then
        file=/etc/redhat-release
        if test -r $file; then
            OUT=`cat $file`
            if test -n "`echo $OUT | grep 'Red Hat'`"; then
                RHEL_VER=`echo $OUT | cut -d\  -f7`
                RHEL_MAJOR=`echo $RHEL_VER | cut -d. -f1`
                RHEL_MINOR=`echo $RHEL_VER | cut -d. -f2`
                RHEL_EXTRA=""

                RPM_BUILD_PKG=redhat-rpm-config

                LINUX_DISTRO=rhel
                LINUX_DISTRO_DISK="rhel$RHEL_MAJOR"
                LINUX_HEADER_DIR=/lib/modules/$KNAME/source
                DISTRO_VER=${LINUX_DISTRO}${RHEL_MAJOR}u${RHEL_MINOR}

            elif test -n "`echo $OUT | grep CentOS`"; then
                RHEL_VER=`echo $OUT | cut -d\  -f$centos_field`
                RHEL_MAJOR=`echo $RHEL_VER | cut -d. -f1`
                RHEL_MINOR=`echo $RHEL_VER | cut -d. -f2`
                RHEL_EXTRA=""

                RPM_BUILD_PKG=redhat-rpm-config

                LINUX_DISTRO=centos
                LINUX_DISTRO_DISK="centos$RHEL_MAJOR"
                LINUX_HEADER_DIR=/usr/src/kernels/$KNAME
                DISTRO_VER=${LINUX_DISTRO}${RHEL_MAJOR}u${RHEL_MINOR}
            fi
        fi
    fi

    AC_MSG_RESULT([$LINUX_DISTRO / $LINUX_DISTRO_DISK])

    # If we didn't figure out the Linux distro, abort.
    if test -z "$LINUX_DISTRO"; then
        AC_MSG_WARN([Unsupported linux distribution])
        AC_MSG_ERROR([cannot continue])
    fi

    AC_MSG_CHECKING([for user-specific Xen Server version string])
    if test "$with_xenserver_version" = "yes" || test "$with_xenserver_version" = "no" || test -z "$with_xenserver_version"; then
        if test -n "`echo $LINUX_DISTRO_DISK | grep xs`"; then
            ENIC_XS_VERSION=${XS_MAJOR}.${XS_MINOR}
            msg=$ENIC_XS_VERSION
        else
            ENIC_XS_VERSION=
            msg="None"
        fi
    else
        ENIC_XS_VERSION=$with_xenserver_version
        msg=$ENIC_XS_VERSION
    fi
    AC_MSG_RESULT([$msg])
    AC_SUBST([ENIC_XS_VERSION])

    # There's still a small number of places we compare against the
    # SLES version number in #if statements.
    AC_DEFINE_UNQUOTED(SLES_VERSION, [$SLES_VER],
        [defines sles version if building for a SLES distro])
    AC_DEFINE_UNQUOTED(SLES_PATCHLEVEL, [$SLES_PATCHLEVEL],
        [defines sles patchlevel if building for a SLES distro])

    AC_SUBST(LINUX_DISTRO)
    AC_SUBST(LINUX_DISTRO_DISK)
    AC_SUBST(DISTRO_VER)
    AC_SUBST(RHEL_VER)
    AC_SUBST(RHEL_MAJOR)
    AC_SUBST(RHEL_MINOR)
    AC_SUBST(RHEL_EXTRA)
    AC_SUBST(SLES_VER)
    AC_SUBST(SLES_PATCHLEVEL)
])

dnl -------------------------------------------------------------------------
dnl
dnl Setup crypto signing for Linux kernel drivers
dnl
dnl Input:
dnl - None
dnl
dnl Output (all are AC SUBST'ed):
dnl $VIC_SWIMS_TICKET_FILE: abs path to SWIMS ticket file
dnl $VIC_SWIMS_KEY_TYPE: "DEV" or "RELEASE" or "REV"
dnl $VIC_SWIMS_SIGNING_SCRIPT: abs path to swims-ticket-sign-elf.py
dnl $VIC_SWIMS_CONFIGURE_CLI_OPTIONS: needed for sub-invocations of
dnl     configure (e.g., when packaging)
dnl
AC_DEFUN([VIC_SWIMS_SETUP],[
    # Register the CLI params
    __VIC_SWIMS_CLI_SETUP

    # See if the user intends to sign
    __VIC_SWIMS_CLI_SANITY([vic_swims_happy=1], [vic_swims_happy=0])

    # If all was good, do final setups
    AS_IF([test $vic_swims_happy -eq 1],
        [__VIC_SWIMS_CLI_PROCESS([vic_swims_happy=1], [vic_swims_happy=0])])
    AS_IF([test $vic_swims_happy -eq 1],
        [__VIC_SWIMS_FIND_PYTHON([vic_swims_happy=1], [vic_swims_happy=0])])
    AS_IF([test $vic_swims_happy -eq 1],
        [__VIC_SWIMS_SET_FINAL_VARS])

])

dnl
dnl Setup the CLI options for SWIMS (i.e., digitally signing .ko
dnl files) and sanity check their values.
dnl
dnl Input:
dnl None
dnl
dnl Output:
dnl None
dnl
AC_DEFUN([__VIC_SWIMS_CLI_SETUP],[
    AC_ARG_WITH([swims-ticket-file],
        [AS_HELP_STRING([--with-swims-ticket-file],
            [If provided, use this Cisco SWIMS ticket file to digitally sign the built driver .ko file])])

    AC_ARG_WITH([swims-key-type],
        [AS_HELP_STRING([--with-swims-key-type],
            [Specify the type of SWIMS key corresponding to the SWIMS ticket file (only relevant if --with-swims-ticket-file specified).  Can be DEV, RELEASE, or REV.  Digital signing will fail of the SWIMS ticket file does not match the key type specified by this option (default: DEV)])])

    AC_ARG_WITH([swims-signing-script],
        [AS_HELP_STRING([--with-swims-signing-script],
             [Path to the location of the signing script (since it is not in the source code tree)])])
])

dnl
dnl Sanity check: if signing, both --with-swims-ticket-file and
dnl --with-swims-signing-script must be supplied.  If only one of
dnl the two is specified, error
dnl
dnl Input:
dnl $1: Action if all CLI options are sane
dnl $2: Action if CLI options are blank
dnl
AC_DEFUN([__VIC_SWIMS_CLI_SANITY],[
    AC_MSG_CHECKING([if digitally signing the .ko driver])

    # Check to make sure both or neither of --with-swims-ticket-file
    # and --with-swims-signing-script were specified.
    vic_swims_cli_happy=1
    AS_IF([test -n "$with_swims_ticket_file" -a -z "$with_swims_signing_script"],
          [vic_swims_cli_happy=0])
    AS_IF([test -z "$with_swims_ticket_file" -a -n "$with_swims_signing_script"],
          [vic_swims_cli_happy=0])
    AS_IF([test $vic_swims_cli_happy -eq 0],
          [AC_MSG_RESULT([confusion])
	   AC_MSG_WARN([If --with-swims-ticket-file is specified, --with-swims-signing-script must also be specified (and vice versa)])
	   AC_MSG_ERROR([Cannot continue])])

    # If both CLI options were specified, sanity check them
    AS_IF([test -n "$with_swims_ticket_file" -a -n "$with_swims_signing_script"],
          [AC_MSG_RESULT([yes])
	   $1],
          [AC_MSG_RESULT([no])
	   $2])
])

dnl
dnl Sanity check all the SWIMS CLI values that were provided by the
dnl user.  This is intended to only be called if we have established
dnl that SWIMS signing is desired (i.e., they user has specified
dnl *some* value for --with-swims-ticket-file and
dnl --with-swims-signing-script).
dnl
dnl Input:
dnl $1: Action if user specified all the relevant CLI options and they
dnl     are good
dnl $2: Action if user did not specify any relevant CLI options
dnl
dnl Output:
dnl None
dnl
AC_DEFUN([__VIC_SWIMS_CLI_PROCESS],[
    # Check for the SWIMS ticket file
    AC_MSG_CHECKING([for SWIMS ticket file])
    file=`readlink -f $with_swims_ticket_file`
    AS_IF([test ! -r "$file"],
        [AC_MSG_RESULT([not found])
         AC_MSG_WARN([Cannot read $with_swims_ticket_file])
         AC_MSG_ERROR([Cannot continue])
         AC_MSG_RESULT([$file])
        ])
    VIC_SWIMS_TICKET_FILE=$file
    AC_MSG_RESULT([$VIC_SWIMS_TICKET_FILE])
    AC_SUBST(VIC_SWIMS_TICKET_FILE)

    # Check for the SWIMS key type
    AC_MSG_CHECKING([SWIMS key type])
    AS_IF([test -z "$with_swims_key_type"],
        [with_swims_key_type=DEV])
    AS_CASE([$with_swims_key_type],
        [DEV],     [VIC_SWIMS_KEY_TYPE=DEV],
        [RELEASE], [VIC_SWIMS_KEY_TYPE=RELEASE],
        [REV],     [VIC_SWIMS_KEY_TYPE=REV],
        [*],       [AC_MSG_WARN([--with-swims-key-type must be one of DEV, RELEASE, or REV])
                        AC_MSG_ERROR([Cannot continue])])
    AC_MSG_RESULT([$VIC_SWIMS_KEY_TYPE])
    AC_SUBST(VIC_SWIMS_KEY_TYPE)

    # If we're signing, then we need an absolute directory location
    # for the signing script (because it's not included in the
    # tarball, and we'll need it to make binary packages).  If we were
    # given a relative filename, canonicalize it.
    AC_MSG_CHECKING([for signing script])
    AS_IF([test ! -x "$with_swims_signing_script"],
        [AC_MSG_RESULT([Cannot read $with_swims_signing_script, or it is not executable])
         AC_MSG_ERROR([Cannot continue])
        ])

    # Turn the signing script filename into an absolute filename
    VIC_SWIMS_SIGNING_SCRIPT=`readlink -f $with_swims_signing_script`
    AC_MSG_RESULT([$VIC_SWIMS_SIGNING_SCRIPT])
    AC_SUBST(VIC_SWIMS_SIGNING_SCRIPT)
])

dnl
dnl Find Python for the SWIMS signing script
dnl
AC_DEFUN([__VIC_SWIMS_FIND_PYTHON],[
    AC_CHECK_PROGS(VIC_SWIMS_PYTHON, [python python3 python2])
    AS_IF([test -z "$VIC_SWIMS_PYTHON"],
        [AC_MSG_WARN([requested crypto signing, but cannot find Python])
         AC_MSG_ERROR([Cannot continue])
	])
    AC_SUBST(VIC_SWIMS_PYTHON)
])

dnl
dnl Setup final variables.  This is only called if we have decided
dnl that we're signing the drivers and all the CLI params have checked
dnl out ok.
dnl
AC_DEFUN([__VIC_SWIMS_SET_FINAL_VARS],[
    # Generate a string to pass in to package building
    VIC_SWIMS_CONFIGURE_CLI_OPTIONS="--with-swims-ticket-file=$VIC_SWIMS_TICKET_FILE"
    VIC_SWIMS_CONFIGURE_CLI_OPTIONS="$VIC_SWIMS_CONFIGURE_CLI_OPTIONS --with-swims-key-type=$VIC_SWIMS_KEY_TYPE"
    VIC_SWIMS_CONFIGURE_CLI_OPTIONS="$VIC_SWIMS_CONFIGURE_CLI_OPTIONS --with-swims-signing-script=$VIC_SWIMS_SIGNING_SCRIPT"
    AC_SUBST(VIC_SWIMS_CONFIGURE_CLI_OPTIONS)
])

dnl -------------------------------------------------------------------------
dnl Macro to check for everything we need in the common/kcompat.h file
dnl
AC_DEFUN([VIC_KCOMPAT_TESTS], [

	VIC_AC_CHECK_HEADER_DEFINE([net/flow_dissector.h],
		[VIC_HAVE_FLOW_DISSECTOR_H])

	VIC_AC_CHECK_HEADER_DEFINE([net/flow_keys.h], [VIC_HAVE_FLOW_KEYS_H],
					[#include <linux/types.h>])

	VIC_AC_CHECK_HEADER_DEFINE([linux/printk.h], [VIC_HAVE_PRINTK_H])

	VIC_AC_CHECK_HEADER_DEFINE([linux/ethtool.h], [VIC_HAVE_ETHTOOL_H])

	VIC_AC_CHECK_HEADER_DEFINE([linux/crash_dump.h], [VIC_HAVE_CRASH_DUMP_H])

	VIC_LINUX_DEF([linux/skbuff.h], [pkt_hash_types], [VIC_HAVE_PKT_HASH_TYPES],
			  [kernel src have pkt_hash_types], [WORD])

	VIC_LINUX_DEF(linux/skbuff.h, skb_get_hash_raw, VIC_HAVE_SKB_GET_HASH_RAW,
			  [Does skbuff.h define skb_get_hash_raw?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_set_hash, VIC_HAVE_SKB_SET_HASH,
			  [Does skbuff.h define skb_set_hash?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_transport_offset,
			 VIC_HAVE_SKB_TRANSPORT_OFFSET,
			 [Does skbuff.h define skb_transport_offset?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_checksum_start_offset,
			  VIC_HAVE_SKB_CHECKSUM_START_OFFSET,
			  [Does skbuff.h define skb_checksum_start_offset?], WORD)

	VIC_AC_CHECK_MEMBER_DEFINE([linux/skbuff.h], [struct sk_buff.csum_start],
			[VIC_HAVE_SKB_CSUM_START])

	VIC_LINUX_DEF(linux/skbuff.h, netdev_alloc_skb, VIC_HAVE_NETDEV_ALLOC_SKB,
			  [Does skbuff.h define netdev_alloc_skb?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_record_rx_queue, VIC_HAVE_SKB_RECORD_RX_QUEUE,
			  [Does skbuff.h define skb_record_rx_queue?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, netdev_alloc_skb_ip_align,
			  VIC_HAVE_NETDEV_ALLOC_SKB_IP_ALIGN,
			  [Does skbuff.h define netdev_alloc_skb_ip_align?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_frag_size, VIC_HAVE_SKB_FRAG_SIZE,
			  [Does skbuff.h define skb_frag_size?], WORD)

	VIC_LINUX_DEF(linux/skbuff.h, skb_frag_dma_map, VIC_HAVE_SKB_FRAG_DMA_MAP,
			  [Does skbuff.h define skb_frag_size?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netdev_name, VIC_HAVE_NETDEV_NAME,
			  [Does netdevice.h define netdev_name?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netif_set_real_num_rx_queues,
			 VIC_HAVE_NETIF_SET_REAL_NUM_RX_QUEUES,
			 [Does netdevice.h define netif_set_real_num_rx_queues?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netif_set_real_num_tx_queues,
			  VIC_HAVE_NETIF_SET_REAL_NUM_TX_QUEUES,
			  [Does netdevice.h define netif_set_real_num_tx_queues?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netdev_get_tx_queue,
			  VIC_HAVE_NETDEV_GET_TX_QUEUE,
			  [Does netdevice.h define netdev_get_tx_queue?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netif_tx_stop_queue,
			  VIC_HAVE_NETIF_TX_STOP_QUEUE,
			  [Does netdevice.h define netif_tx_stop_queue?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netif_tx_queue_stopped,
			  VIC_HAVE_NETIF_TX_QUEUE_STOPPED,
			  [Does netdevice.h define netif_tx_queue_stopped?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netdev_for_each_mc_addr,
			  VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR,
			  [Does netdevice.h define netdev_for_each_mc_addr?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netif_set_xps_queue,
			  VIC_HAVE_NETIF_SET_XPS_QUEUE,
			  [Does netdevice.h define netif_set_xps_queue?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, [[ndo_features_check]],
			 VIC_HAVE_FEATURES_CHECK,
			 [Does netdevice.h define ndo_features_check?], PATTERN)

	VIC_LINUX_DEF(linux/netdevice.h, ndo_add_vxlan_port,
			  VIC_HAVE_ADD_VXLAN_PORT,
			  [Does netdevice.h define ndo_add_vxlan_port?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netdev_extended, VIC_HAVE_NETDEV_EXTENDED,
			  [Does netdevice.h define VIC_HAVE_NETDEV_EXTENDED?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h,
			  [[static inline int skb_linearize(struct sk_buff *skb, gfp_t gfp]],
			  VIC_HAVE_SKB_LINEARIZE_GFP_ARG,
			  [Does skb_linearize have gfp_t arg?], PATTERN)

	VIC_LINUX_DEF(linux/netdevice.h, [[ bool flush_old);]],
			 VIC_HAVE_NAPI_GRO_FLUSH_HAS_FLUSH_OLD_ARG,
			 [Does napi_gro_flush has flush_old arg?], PATTERN)

	VIC_LINUX_DEF(linux/netdevice.h, napi_struct, VIC_HAVE_NAPI_STRUCT,
			  [Does netdevice.h define napi_struct?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, napi_schedule_irqoff, VIC_HAVE_NAPI_SCHEDULE_IRQOFF,
			  [Does netdevice.h define napi_schedule_irqoff?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, [[NETIF_F_HW_VLAN_CTAG_RX]],
			  VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV,
			  [Does netdevice.h define NETIF_F_HW_VLAN_CTAG_RX?], PATTERN)

	VIC_LINUX_DEF(linux/netdev_features.h, [[NETIF_F_HW_VLAN_CTAG_RX]],
			  VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV_FEATURES,
			  [Does netdev_features.h define NETIF_F_HW_VLAN_CTAG_RX?],
			  PATTERN)


	if test $VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV -eq 1 ||
	   test $VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX_NETDEV_FEATURES -eq 1; then
		RESULT=1
	else
		RESULT=0
	fi

	AC_DEFINE_UNQUOTED(VIC_HAVE_NETIF_F_HW_VLAN_CTAG_RX, [$RESULT],
			   [checks whether NETIF_F_HW_VLAN_CTAG_RX is defined or not])

	VIC_LINUX_DEF(linux/netdevice.h, [[NETIF_F_HW_VLAN_CTAG_TX]],
			  VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV,
			  [Does netdevice.h define NETIF_F_HW_VLAN_CTAG_TX?],
			  PATTERN)

	VIC_LINUX_DEF(linux/netdev_features.h, [[NETIF_F_HW_VLAN_CTAG_TX]],
			  VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV_FEATURES,
			  [Does netdev_features.h define NETIF_F_HW_VLAN_CTAG_TX?],
			  PATTERN)

	if test $VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV -eq 1 ||
	   test $VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX_NETDEV_FEATURES -eq 1; then
		RESULT=1
	else
		RESULT=0
	fi

	AC_DEFINE_UNQUOTED(VIC_HAVE_NETIF_F_HW_VLAN_CTAG_TX, [$RESULT],
			   [checks whether NETIF_F_HW_VLAN_CTAG_TX is defined or not])

	VIC_AC_CHECK_DECL([linux/ethtool.h], [ethtool_cmd_speed_set],
			[], [AC_MSG_ERROR([Your kernel is too old -- ethtool_cmd_speed_set() support is required])])

	VIC_AC_CHECK_DECL_DEFINE([linux/etherdevice.h], [alloc_etherdev_mqs],
			[VIC_HAVE_ALLOC_ETHERDEV_MQS])

	VIC_LINUX_DEF(linux/etherdevice.h, ether_addr_equal,
			  VIC_HAVE_ETHER_ADD_EQUAL,
			  [Does etherdevice.h define ether_addr_equal?], WORD)

	VIC_LINUX_DEF(linux/ip.h, ip_hdr, VIC_HAVE_IP_HDR,
			  [Does ip.h define ip_hdr?], WORD)

	VIC_LINUX_DEF(linux/tcp.h, tcp_hdr, VIC_HAVE_TCP_HDR,
			  [Does tcp.h define tcp hdr?], WORD)

	VIC_LINUX_DEF(linux/tcp.h, tcp_hdrlen, VIC_HAVE_TCP_HDRLEN,
			  [Does tcp.h define tcp hdrlen?], WORD)

	VIC_LINUX_DEF(linux/netdevice.h, netdev_err, VIC_HAVE_NETDEV_ERR,
			  [Does netdevice.h define netdev_err?], WORD)

	VIC_LINUX_DEF(linux/pci.h, DEFINE_PCI_DEVICE_TABLE,
			  VIC_HAVE_DEFINE_PCI_DEVICE_TABLE,
			  [Does pci.h define DEFINE_PCI_DEVICE_TABLE?], WORD)

	VIC_LINUX_DEF(linux/pci.h, pci_enable_device_mem,
			  VIC_HAVE_PCI_ENABLE_DEVICE_MEM,
			  [Does pci.h define pci_enable_device_mem?], WORD)

	VIC_LINUX_DEF(linux/pci.h, pci_enable_msix_range,
			 VIC_HAVE_PCI_ENABLE_MSIX_RANGE,
			 [Does pci.h define pci_enable_msix_range?], WORD)

	VIC_LINUX_DEF(linux/slab.h, kzalloc, VIC_HAVE_KZALLOC,
			  [Does slab.h define kzalloc?], WORD)

	VIC_LINUX_DEF(linux/if_vlan.h, skb_vlan_tag_get, VIC_HAVE_SKB_VLAN_TAG_GET,
			  [Does if_vlan.h define skb_vlan_tag_get?], WORD)

	VIC_LINUX_DEF(linux/if_vlan.h, skb_vlan_tag_present,
			 VIC_HAVE_SKB_VLAN_TAG_PRESENT,
			 [Does if_vlan.h define skb_vlan_tag_get?], WORD)

	VIC_LINUX_DEF_MULTILINE(linux/if_vlan.h, vlan_proto,
				    VIC_HAVE_VLAN_HWACCEL_PUT_TAG_VLAN_PROTO_ARG,
				    [__vlan_hwaccel_put_tag(], [)],
				    [Does __vlan_hwaccel_put_tag have a vlan_proto arg?])

	VIC_AC_CHECK_MEMBER_DEFINE([linux/skbuff.h], [struct sk_buff.csum_offset],
			[VIC_HAVE_CSUM_OFFSET])

	VIC_LINUX_DEF(linux/sched.h,  schedule_timeout_uninterruptible,
			  VIC_HAVE_SCHEDULE_TIMEOUT_UNINTERRUPTIBLE,
			  [Does sched.h define  schedule_timeout_uninterruptible?],
			  WORD)

	VIC_LINUX_DEF_MULTILINE(linux/pci-dma-compat.h, struct pci_dev,
				    VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV1,
				    [pci_dma_mapping_error(], [)],
				    [Does pci_dma_mapping_error have pdev arg?])
	AC_SUBST(VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV1)

	VIC_LINUX_DEF_MULTILINE(asm-generic/pci-dma-compat.h, struct pci_dev,
				    VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV2,
				    [pci_dma_mapping_error(], [)],
				    [Does pci_dma_mapping_error have pdev arg?])
	AC_SUBST(VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV2)

	if test $VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV1 -eq 1 ||
	   test $VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV2 -eq 1; then
		RESULT=1
	else
		RESULT=0
	fi

	AC_DEFINE_UNQUOTED(VIC_HAVE_PCI_DMA_MAPPING_ERROR_PDEV, [$RESULT],
			   [checks whether pci_dma_mapping_error have pdev arg])

	VIC_LINUX_DEF(linux/workqueue.h, INIT_WORK, VIC_HAVE_INIT_WORK,
			  [Does workqueue.h define INIT_WORK?], WORD)

	VIC_LINUX_DEF(linux/net.h, net_warn_ratelimited,
			 VIC_HAVE_NET_WARN_RATELIMITED,
			 [Does net.h define net_warn_ratelimited?], WORD)

	VIC_LINUX_DEF(linux/interrupt.h, irq_set_affinity_hint,
			 VIC_HAVE_IRQ_SET_AFFINITY_HINT,
			 [Does interrupt.h define irq_set_affinity_hint?], WORD)

	VIC_LINUX_DEF(linux/interrupt.h, [[typedef irqreturn_t]],
			  VIC_HAVE_IRQRETURN_T_TYPEDEF,
			  [Does interrupt.h define irqreturn_t typedef?], PATTERN)

	VIC_LINUX_DEF(linux/cpumask.h, cpumask_set_cpu, VIC_HAVE_CPUMASK_SET_CPU,
			  [Does cpumask.h define cpumask_set_cpu?], WORD)

	VIC_LINUX_DEF(linux/list.h,
			  [[list_for_each_entry_safe(tpos, pos, n, head, member)]],
			  VIC_HAVE_LIST_FOR_EACH_ENTRY_SAFE_POS_ARG,
			  [Does hlist_for_each_entry_safe have tpos arg?], PATTERN)

	VIC_LINUX_DEF(linux/list.h,
			  [[hlist_for_each_entry(tpos, pos, head, member)]],
			  VIC_HAVE_LIST_FOR_EACH_ENTRY_POS_ARG,
			  [Does hlist_for_each_entry have pos arg?], PATTERN)

	VIC_LINUX_DEF(linux/netdevice.h, [[netdev_for_each_mc_addr(ha]],
			  VIC_HAVE_NETDEV_FOR_EACH_MC_ADDR_HA_ARG,
			  [Does netdev_for_each_mc_addr have arg hardware address?],
			  PATTERN)

])

dnl -------------------------------------------------------------------------
dnl Macro to check if kfifo_put can execute
dnl
AC_DEFUN([VIC_NO_KFIFO_PUT_TYPECHECK],[

	AC_MSG_CHECKING([if kfifo_put() ptr type checking is not required])

	VIC_AC_COMPILE_IFELSE([AC_LANG_SOURCE(
	[
		#include <linux/kfifo.h>

		void test_kfifo_put() {
			DEFINE_KFIFO(fifo, int, 4);
			kfifo_put(&fifo, 1);
		}
	])],

	[AC_MSG_RESULT([yes])
	 value=1],

	[AC_MSG_RESULT([no])
	 value=0]

	) dnl end VIC_AC_COMPILE_IFELSE
]) dnl end AC_DEFUN

dnl -------------------------------------------------------------------------
dnl Common function to help drivers that call into the enic API find the API
dnl header and the module symvers file.
dnl
dnl Input:
dnl $1: label to print where we're looking
dnl $2: directory where to look
dnl $3: 1=look for Module.symvers, 0=don't look
dnl
dnl Output:
dnl - $ENIC_HDR_DIR: Path to directory containing enic_api.h
dnl - If $3 == 1:
dnl   - $ENIC_SYMVERS: Path to Module.symvers file
dnl
AC_DEFUN([VIC_FIND_ENIC_HDR_DIR],[
    ENIC_HDR_DIR=
    ENIC_SYMVERS=

    AC_MSG_NOTICE([looking for enic_api.h in $1])
    AC_CHECK_FILES([$2/enic_api.h],
                   [ENIC_HDR_DIR=$(readlink -f $2)])

    AS_IF([test $3 -eq 1 -a -n "$ENIC_HDR_DIR"],
          [AC_MSG_NOTICE([looking for Module.symvers in $1])
           AC_CHECK_FILES([$2/Module.symvers],
                          [ENIC_SYMVERS="$ENIC_HDR_DIR/Module.symvers"])])
])

dnl Check if the alloc_pd and alloc_cq needs struct ib_ucontext.
dnl RHEL < 8.2:
dnl struct ib_pd * (*alloc_pd)(struct ib_device *ibdev,
dnl 			       struct ib_ucontext *ibcontext,
dnl 			       struct ib_udata *udata)
dnl
dnl RHEL 8.2 uses:
dnl int (*alloc_pd)(struct ib_pd *pd, struct ib_udata *udata)
dnl This change was made in the kernel at ff23dfa134576
dnl
dnl Input:
dnl $1: Action if alloc_pd has (ib_ucontext*) param
dnl $2: Action if alloc_pd does not have (ib_ucontext*) param
AC_DEFUN([VIC_IF_ALLOC_PD_HAVE_UCONTEXT],[

    AC_MSG_CHECKING([if alloc_pd() requires 'struct ib_ucontext *' arg])

    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
	[
	    const struct ib_cq_init_attr *attr;
	    struct ib_device ibdev;
	    struct ib_udata *udata;
	    struct ib_pd *pd;
	    int ret;

	    ret = ibdev.ops.alloc_pd(pd, udata);
	])],
	[AC_MSG_RESULT([no])
	 $2],
	[AC_MSG_RESULT([yes])
	 $1]
    ) dnl end VIC_AC_COMPILE_IFELSE
]) dnl end AC_DEFUN

dnl Helper function for determining with RDMA prototype to use for the
dnl functions that are passed to the rdma upper layer.
dnl $1 return type
dnl $2 rdma function pointer name
dnl $3 arguments
dnl $4 name of define previously set 'have ops' check
dnl $5 action if true

AC_DEFUN([VIC_RDMA_TRY_PROTOTYPE], [
    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
    [
        $1 foo($3);
        struct ib_device dev;
#if $4
        dev.ops.$2 = foo;
#else
        dev.$2 = foo;
#endif
        return 0;
    ])],
    [$5])
]) dnl End of AC_DEFUN for VIC_RDMA_TRY_PROTOTYPE

dnl
dnl create_ah and destroy_ah have changed in the kernel over time.
dnl The information following the commit hash below is from the command
dnl `git describe --contains <hash>`, which tells you the first tag in the tree
dnl that contains that particular commit.
dnl
dnl Here's their history:
dnl
dnl CREATE_AH
dnl *** Version C1:
dnl The earliest version we support, which goes back to the beginning of
dnl history in the linux git repo.
dnl
dnl struct ib_ah *(*create_ah)(struct ib_pd *pd,
dnl                            struct ib_ah_attr *attr);
dnl
dnl *** Version C2:
dnl struct ib_ah *(*create_ah)(struct ib_pd *pd,
dnl                            struct ib_ah_attr *ah_attr,
dnl                            struct ib_udata *udata);
dnl -> kernel.org commit 477864c8fc / v4.10-rc1~20^2~17^2~20
dnl
dnl *** Version C3:
dnl struct ib_ah *(*create_ah)(struct ib_pd *pd,
dnl                            struct rdma_ah_attr *ah_attr,
dnl                            struct ib_udata *udata);
dnl -> kernel.org commit 90898850ec / v4.12-rc1~64^3~44
dnl
dnl Note: The following kernel commit doesn't change create_ah's
dnl       signature, but does change how our configure test has to call it
dnl       as the function pointer moved into an ops structure.  There are two
dnl       configure tests that check for the C3 variant.
dnl --> kernel.org commit 521ed0d92ab0d / v5.0-rc1~111^2~79
dnl
dnl *** Version C4:
dnl struct ib_ah *(*create_ah)(struct ib_pd *pd,
dnl                            struct rdma_ah_attr *ah_attr, u32 flags,
dnl                            struct ib_udata *udata);
dnl --> kernel.org commit b090c4e3a07c3 / v5.0-rc1~111^2~28
dnl
dnl *** Version C5:
dnl int (*create_ah)(struct ib_ah *ah, struct rdma_ah_attr *ah_attr,
dnl                         u32 flags, struct ib_udata *udata);
dnl --> kernel.org commit d345691471b42 / v5.2-rc1~61^2~93
dnl
dnl ** Version C6:
dnl int (*create_ah)(struct ib_ah *ah, struct rdma_ah_init_attr *attr,
dnl 		     struct ib_udata *udata);
dnl
dnl --> kernel.org commit fa5d010c5630b / v5.8-rc1~127^2~188
dnl
dnl DESTROY_AH
dnl
dnl *** Version D1:
dnl int (*destroy_ah)(struct ib_ah *ah);
dnl This is the original version of destroy_ah that we supported.
dnl
dnl Note:  See Note for this commit in the create_ah section, as it also
dnl        applies to destroy_ah.  There are two configure tests checking
dnl        for the D1 variant.
dnl --> kernel.org commit 521ed0d92ab0d / v5.0-rc1~111^2~79
dnl
dnl *** Version D2:
dnl int (*destroy_ah)(struct ib_ah *ah, u32 flags);
dnl --> kernel.org commit 2553ba217eea3 / v5.0-rc1~111^2~27
dnl
dnl *** Version D3:
dnl NOTE: This change is in the same merge 5.2 merge window as the next change.
dnl       There is no kernel.org released kernel that will ever have this
dnl       version, but a vendor kernel might.
dnl int (*destroy_ah)(struct ib_ah *ah, u32 flags, struct ib_udata *udata);
dnl --> kernel.org commit c4367a26357be / v5.2-rc1~61^2~109
dnl
dnl *** Version D4:
dnl void (*destroy_ah)(struct ib_ah *ah, u32 flags);
dnl --> kernel.org commit d345691471b42 / v5.2-rc1~61^2~93
dnl
dnl Note: As of kernel.org commit 9a9ebf8cd72b8 / v5.10-rc1 timeframe
dnl The prototype changed from return void to returning int, which makes
dnl the same as version D2.
dnl
dnl It is difficult to tell between Versions C2 and C3, and C4 and C5
dnl because they only differ in the type of the pointer of a parameter.
dnl Nominally, you can TRY_COMPILE with
dnl CFLAGS="-Werror=incompatible-pointer-types $CFLAGS" and figure it
dnl out that way, but we also have to support RHEL7/OL7 which has gcc
dnl 4.8.5 which does not support this flag (i.e., this warning cannot be made
dnl into an error until later versions of gcc).  OL7, especially, can be used
dnl with older rhel compatibility kernels or newer UEK kernels.  Consequently,
dnl if the compiler supports it we use the incompatible pointer option, but
dnl on older gcc versions we use -Werror, and hope we don't have spurious
dnl warnings on these tests.  In this way we hope to err on the side of
dnl annoying ourselves during configure time instead of accidentally allowing
dnl built drivers to crash due to misidentified variants.

dnl
dnl Figure out if gcc supports -Werror=incompatible-pointer-types
dnl $1 name of variable to set with what we find
dnl
AC_DEFUN([VIC_FIND_BEST_FLAG_FOR_ERROR_ON_INCOMPATIBLE_POINTERS], [
        AC_MSG_CHECKING([does gcc supports error on incompatible pointers?])
        CFLAGS_save=$CFLAGS
        CFLAGS="$CFLAGS -Werror=incompatible-pointer-types"
        VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [int i = 0;])],
                [$1="-Werror=incompatible-pointer-types"
                 AC_MSG_RESULT([yes])],
                [$1="-Werror"
                 AC_MSG_RESULT([no])])
        CFLAGS=$CFLAGS_save
])

dnl
dnl Figure out which create_ah() to use.
dnl
dnl $1 name of variable to use in the AC_DEFINE
dnl $2 cflags to cause errors on incompatible pointer types
dnl $3 the name of the define previously set to 'have opts' check
dnl

AC_DEFUN([VIC_RDMA_WHICH_CREATE_AH], [
    AC_MSG_CHECKING([which variant of create_ah() to use])
    VIC_CREATE_AH=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([struct ib_ah *], create_ah,
        [struct ib_pd *pd, struct ib_ah_attr *ah_attr], [$3],
        [VIC_CREATE_AH=1
         MSG="struct ib_ah * (struct ib_pd *pd, struct ib_ah_attr *ah_attr)"])
    AS_IF([test $VIC_CREATE_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE([struct ib_ah *], create_ah,
            [struct ib_pd *pd, struct ib_ah_attr *ah_attr, struct ib_udata *udata], [$3],
            [VIC_CREATE_AH=2
             MSG="struct ib_ah * (struct ib_pd *pd, struct ib_ah_attr *ah_attr, struct ib_udata *udata)"])])
    AS_IF([test $VIC_CREATE_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE([struct ib_ah *], create_ah,
            [struct ib_pd *pd, struct rdma_ah_attr *ah_attr, struct ib_udata *udata], [$3],
            [VIC_CREATE_AH=3
             MSG="struct ib_ah * (struct ib_pd *pd, struct rdma_ah_attr *ah_attr, struct ib_udata *udata)"])])
    AS_IF([test $VIC_CREATE_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE([struct ib_ah *], create_ah,
            [struct ib_pd *pd, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata], [$3],
            [VIC_CREATE_AH=4
             MSG="struct ib_ah * (struct ib_pd *pd, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata)"])])
    AS_IF([test $VIC_CREATE_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE([int], create_ah,
            [struct ib_ah *ah, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata], [$3],
            [VIC_CREATE_AH=5
             MSG="int (struct ib_ah *ah, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata)"])])
    AS_IF([test $VIC_CREATE_AH -eq 0],
         [VIC_RDMA_TRY_PROTOTYPE([int], create_ah,
            [struct ib_ah *ah, struct rdma_ah_init_attr *ah_attr, struct ib_udata *udata], [$3],
            [VIC_CREATE_AH=6
             MSG="int (struct ib_ah *ah, struct rdma_ah_init_attr *ah_attr, struct ib_udata *udata)"])])
    AS_IF([test $VIC_CREATE_AH -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine variant of create_ah().])
         AC_MSG_ERROR([Cannot continue])])
    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_CREATE_AH: $MSG])
    AC_DEFINE_UNQUOTED($1, [$VIC_CREATE_AH],
        [Which variant of create_ah() to use])
]) dnl end AC_DEFUN

dnl
dnl See above for the 4 different variants of destroy_ah.
dnl
dnl Figure out which one we use and set a macro accordingly.
dnl
dnl $1 Name of the variable to use in the AC_DEFINE
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 Name of define of previously set 'have ops' check
dnl

AC_DEFUN([VIC_RDMA_WHICH_DESTROY_AH], [
    AC_MSG_CHECKING([which variant of destroy_ah() to use])
    VIC_DESTROY_AH=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([int], destroy_ah, [struct ib_ah *ah], [$3],
        [VIC_DESTROY_AH=1
         MSG="int (struct ib_ah *ah)"])
    AS_IF([test $VIC_DESTROY_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], destroy_ah, [struct ib_ah *ah, u32 flags], [$3],
            [VIC_DESTROY_AH=2
             MSG="int (struct ib_ah *ah, u32 flags)"])])
    AS_IF([test $VIC_DESTROY_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], destroy_ah, [struct ib_ah *ah, u32 flags, struct ib_udata *udata], [$3],
            [VIC_DESTROY_AH=3
             MSG="int (struct ib_ah *ah, u32 flags, struct ib_udata *udata)"])])
    AS_IF([test $VIC_DESTROY_AH -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [void], destroy_ah, [struct ib_ah *ah, u32 flags], [$3],
            [VIC_DESTROY_AH=4
             MSG="void (struct ib_ah *ah, u32 flags)"])])
    AS_IF([test $VIC_DESTROY_AH -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which destroy_ah() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_DESTROY_AH: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_DESTROY_AH],
        [Which variant of destroy_ah() to use])
]) dnl end AC_DEFUN

dnl
dnl Figure out which alloc_mr to use.
dnl
dnl The alloc_mr now has two versions.
dnl Version 1
dnl     struct ib_mr *(*alloc_mr)(struct ib_pd *pd, enum ib_mr_type mr_type,
dnl                                 u32 max_num_sg);
dnl Version 2
dnl     struct ib_mr *(*alloc_mr)(struct ib_pd *pd, enum ib_mr_type mr_type,
dnl                                 u32 max_num_sg, struct ib_udata *udata);
dnl
dnl Version 2 came into existence in commit kernel.org commit c4367a26357be in
dnl kernel 5.2-rc1, but then it changed back in commit 42a3b153966c9 in kernel
dnl 5.9-rc1.

AC_DEFUN([VIC_RDMA_WHICH_ALLOC_MR], [
	AC_MSG_CHECKING([Which variant of alloc_mr() to use])
	VIC_ALLOC_MR=0
	CFLAGS_save=$CFLAGS
	CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([struct ib_mr *], alloc_mr,
            [struct ib_pd *pd, enum ib_mr_type mr_type, u32 max_num_sg], [$3],
            [VIC_ALLOC_MR=1
             MSG="struct ib_mr *(struct ib_pd *pd, enum ib_mr_type mr_type, u32 max_num_sg)"])

	AS_IF([test $VIC_ALLOC_MR -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE([struct ib_mr *], alloc_mr,
            [struct ib_pd *pd, enum ib_mr_type mrtype, u32 max_num_sg, struct ib_udata *udata], [$3],
            [VIC_ALLOC_MR=2
             MSG="struct ib_mr *(struct ib_pd *pd, enum ib_mr_type mr_type, u32 max_num_sg, struct ib_udata *udata)"])])

	AS_IF([test $VIC_ALLOC_MR -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine variant of alloc_mr().])
         AC_MSG_ERROR([Cannot continue])])
	CFLAGS=$CFLAGS_save
	AC_MSG_RESULT([type $VIC_ALLOC_MR: $MSG])
	AC_DEFINE_UNQUOTED($1, [$VIC_ALLOC_MR],
        [Which variant of alloc_mr() to use])
]) dnl end AC_DEFUN

dnl
dnl Figure out which dealloc_pd we are using
dnl
dnl Version 1 - original version
dnl     int (*dealloc_pd)(struct ib_pd *pd);
dnl Version 2 - v5.1-rc1
dnl     void (*dealloc_pd)(struct ib_pd *pd);
dnl Version 3 - v5.2-rc1
dnl     void (*dealloc_pd)(struct ib_pd *pd, struct ib_udata *udata);
dnl Version 4 - v5.10-rc1
dnl     int (*dealloc_pd)(struct ib_pd *pd, struct ib_udata *udata);
dnl
dnl $1 variable to set
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 name define of previously set 'have ops' check.
dnl
AC_DEFUN([VIC_RDMA_WHICH_DEALLOC_PD], [
    AC_MSG_CHECKING([which variant of dealloc_pd() to use])
    VIC_DEALLOC_PD=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([int], dealloc_pd, [struct ib_pd *pd], [$3],
        [VIC_DEALLOC_PD=1
         MSG="int (struct ib_pd *pd)"])
    AS_IF([test $VIC_DEALLOC_PD -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [void], dealloc_pd, [struct ib_pd *pd], [$3],
            [VIC_DEALLOC_PD=2
             MSG="void (struct ib_pd *pd)"])])
    AS_IF([test $VIC_DEALLOC_PD -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [void], dealloc_pd, [struct ib_pd *pd, struct ib_udata *ud], [$3],
            [VIC_DEALLOC_PD=3
             MSG="void (struct ib_pd *pd, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DEALLOC_PD -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], dealloc_pd, [struct ib_pd *pd, struct ib_udata *ud], [$3],
            [VIC_DEALLOC_PD=4
             MSG="int (struct ib_pd *pd, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DEALLOC_PD -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which dealloc_pd() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_DEALLOC_PD: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_DEALLOC_PD],
        [Which variant of dealloc_pd() to use])
])

dnl
dnl Figure out which destroy_cq we are using
dnl
dnl Version 1 - original version
dnl     int (*destroy_cq)(struct ib_cq *cq);
dnl
dnl Version 2 - v5.2-rc1
dnl     int (*destroy_cq)(struct ib_cq *cq, struct ib_udata *udata);
dnl
dnl Version 3 - v5.3-rc1
dnl     void (*destroy_cq)(struct ib_cq *cq, struct ib_udata *udata);
dnl
dnl Version 2 - 5.10-rc1 destroy_cq once again returns int
dnl
dnl $1 variable to set
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 name define of previously set 'have ops' check.
dnl
AC_DEFUN([VIC_RDMA_WHICH_DESTROY_CQ], [
    AC_MSG_CHECKING([which variant of destroy_cq() to use])
    VIC_DESTROY_CQ=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([int], destroy_cq, [struct ib_cq *cq], [$3],
        [VIC_DESTROY_CQ=1
         MSG="int (struct ib_cq *cq)"])
    AS_IF([test $VIC_DESTROY_CQ -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], destroy_cq, [struct ib_cq *cq, struct ib_udata *ud], [$3],
            [VIC_DESTROY_CQ=2
             MSG="void (struct ib_cq *cq, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DESTROY_CQ -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [void], destroy_cq, [struct ib_cq *cq, struct ib_udata *ud], [$3],
            [VIC_DESTROY_CQ=3
             MSG="int (struct ib_cq *cq, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DESTROY_CQ -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which destroy_cq() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_DESTROY_CQ: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_DESTROY_CQ],
        [Which variant of destroy_cq() to use])
])

dnl
dnl Figure out which create_qp  to use
dnl
dnl Version 1 - original version
dnl     struct ib_qp *(*create_qp)(struct ib_pd *pd,
dnl                                struct ib_qp_init_attr *qp_init_attr,
dnl                                struct ib_udata *udata);
dnl Version 2 - v5.15-rc1
dnl     int (*create_qp)(struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr,
dnl                      struct ib_udata *udata);
dnl
dnl $1 variable to set
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 name define of previously set 'have ops' check.
dnl

AC_DEFUN([VIC_RDMA_WHICH_CREATE_QP], [
    AC_MSG_CHECKING([which variant of create_qp() to use])
    VIC_CREATE_QP=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([struct ib_qp *], create_qp, [struct ib_pd *pd, struct ib_qp_init_attr *qp_init_attr, struct ib_udata *udata], [$3],
        [VIC_CREATE_QP=1
         MSG="struct ib_qp *(struct ib_pd *pd, struct ib_qp_init_attr *qp_init_attr, struct ib_udata *udata)"])
    AS_IF([test $VIC_CREATE_QP -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], create_qp, [struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr, struct ib_udata *ud], [$3],
            [VIC_CREATE_QP=2
             MSG="int (struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr, struct ib_udata *ud)"])])
    AS_IF([test $VIC_CREATE_QP -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which create_qp() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_CREATE_QP: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_CREATE_QP],
        [Which variant of create_qp() to use])
])

dnl
dnl Figure out which destroy_qp  to use
dnl
dnl Version 1 - original version
dnl     int (*destroy_qp)(struct ib_qp *qp);
dnl Version 2 - v5.2-rc1
dnl     int (*destroy_qp)(struct ib_qp *qp, struct ib_udata *udata);
dnl
dnl $1 variable to set
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 name define of previously set 'have ops' check.
dnl

AC_DEFUN([VIC_RDMA_WHICH_DESTROY_QP], [
    AC_MSG_CHECKING([which variant of destroy_qp() to use])
    VIC_DESTROY_QP=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([int], destroy_qp, [struct ib_qp *qp], [$3],
        [VIC_DESTROY_QP=1
         MSG="int (struct ib_qp *qp)"])
    AS_IF([test $VIC_DESTROY_QP -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], destroy_qp, [struct ib_qp *qp, struct ib_udata *ud], [$3],
            [VIC_DESTROY_QP=2
             MSG="int (struct ib_qp *qp, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DESTROY_QP -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which destroy_qp() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_DESTROY_QP: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_DESTROY_QP],
        [Which variant of destroy_qp() to use])
])

dnl
dnl Figure out which dereg_mr  to use
dnl
dnl Version 1 - original version
dnl     int (*dereg_mr)(struct ib_mr *mr);
dnl Version 2 - kernel.org commit c4367a26357be - v5.2-rc1
dnl     int (*dereg_mr)(struct ib_mr *mr, struct ib_udata *udata);
dnl
dnl $1 variable to set
dnl $2 flags to gcc error on incompatible pointer types
dnl $3 name define of previously set 'have ops' check.
dnl

AC_DEFUN([VIC_RDMA_WHICH_DEREG_MR], [
    AC_MSG_CHECKING([which variant of dereg_mr() to use])
    VIC_DEREG_MR=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_RDMA_TRY_PROTOTYPE([int], dereg_mr, [struct ib_mr *mr], [$3],
        [VIC_DEREG_MR=1
         MSG="int (struct ib_mr *mr)"])
    AS_IF([test $VIC_DEREG_MR -eq 0],
        [VIC_RDMA_TRY_PROTOTYPE(
            [int], dereg_mr, [struct ib_mr *mr, struct ib_udata *ud], [$3],
            [VIC_DEREG_MR=2
             MSG="int (struct ib_mr *mr, struct ib_udata *ud)"])])
    AS_IF([test $VIC_DEREG_MR -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which dereg_mr() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_DEREG_MR: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_DEREG_MR],
        [Which variant of dereg_mr() to use])
])

dnl
dnl Figure out size of ib_port_attr.active_speed
dnl
dnl $1 Name of te variable to use in the AC_DEFINE
dnl
dnl For a long time it was u8, but then in upstream 5.10 it was changed
dnl to u16.

AC_DEFUN([VIC_RDMA_IB_ACTIVE_SPEED_SIZE], [
    AC_MSG_CHECKING([size of ib_port_attr.active_speed])
    VIC_ACTIVE_SPEED_SIZE=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS -Werror=overflow"

    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
        [
                struct ib_port_attr attr;
                attr.active_speed = 10000;
        ])],
        [VIC_ACTIVE_SPEED_SIZE=16
         MSG="16-bit"])

    AS_IF([test $VIC_ACTIVE_SPEED_SIZE -eq 0],
        [VIC_ACTIVE_SPEED_SIZE=8
         MSG="8-bit"])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([$MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_ACTIVE_SPEED_SIZE],
        [Size of ib_port_attr.active_speed_size])
]) dnl end AC_DEFUN

dnl
dnl Figure out size of the type used for port number
dnl
dnl $1 Name of te variable to use in the AC_DEFINE
dnl
dnl For a long time it was u8, but then in upstream 5.13 it was changed
dnl to u32 in most of the core.  This was changed almost everywhere in
dnl the core code, except for user facing calls.  We check one location
dnl chaged, the ib_gid_attr structure's port number.

AC_DEFUN([VIC_RDMA_PORTNUM_SIZE], [
    AC_MSG_CHECKING([size of type used for port number])
    VIC_PORTNUM_SIZE=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS -Werror=overflow"

    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
        [
                struct ib_gid_attr attr;
                attr.port_num = 4000000000;
        ])],
        [VIC_PORTNUM_SIZE=32
         MSG="32-bit"])

    AS_IF([test $VIC_PORTNUM_SIZE -eq 0],
        [VIC_PORTNUM_SIZE=8
         MSG="8-bit"])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([$MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_PORTNUM_SIZE],
        [Size of type used for port number])
]) dnl end AC_DEFUN

dnl
dnl Figure out which version of ib_register_device to use
dnl
dnl There are now many versions of ib_register_device:
dnl Version 1 kernel.org commit 9a6edb60ec10d - 2.6.35-rc1  (first we support)
dnl     int ib_register_device(struct ib_device *device,
dnl         int (*port_callback)(struct ib_device *,
dnl                             u8, struct kobject *));
dnl
dnl Version 2 kernel.org commit e349f858d29f3 - 4.20-rc1
dnl     int ib_register_device(struct ib_device *device, const char *name,
dnl         int (*port_callback)(struct ib_device *,
dnl                             u8, struct kobject *));
dnl
dnl Version 3 kernel.org commit ea4baf7f116a1 - 5.1-rc1
dnl     int ib_register_device(struct ib_device *device, const char *name);
dnl
dnl Version 4 kernel.org commit e0477b34d9d11 - 5.10-rc1
dnl     int ib_register_device(struct ib_device *device, const char *name,
dnl                             struct device *dma_device);
dnl
dnl $1 Variable name to set with result
dnl $2 Flag to use to get incompatible pointer error

AC_DEFUN([VIC_RDMA_WHICH_IB_REGISTER], [
    AC_MSG_CHECKING([which variant of ib_register_device() to use])
    VIC_IB_REGISTER=0
    CFLAGS_save=$CFLAGS
    CFLAGS="$CFLAGS $2"

    VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
        [
            struct ib_device dev;
            int callback(struct ib_device *, u8, struct kobject *);

            ib_register_device(&dev, callback);
        ])],
        [VIC_IB_REGISTER=1
         MSG="int (struct ib_device *, init_function)"])

     AS_IF([test $VIC_IB_REGISTER -eq 0],
        [VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
            [
                struct ib_device dev;
                int callback(struct ib_device *, u8, struct kobject *);

                ib_register_device(&dev, "foo", callback);
            ])],
            [VIC_IB_REGISTER=2
             MSG="int (struct ib_device *, const char *name, init_function)"])
        ])

     AS_IF([test $VIC_IB_REGISTER -eq 0],
        [VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
            [
                struct ib_device dev;

                ib_register_device(&dev, "foo");
            ])],
            [VIC_IB_REGISTER=3
             MSG="int (struct ib_device *, const char *name)"])
        ])

     AS_IF([test $VIC_IB_REGISTER -eq 0],
        [VIC_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <rdma/ib_verbs.h>],
            [
                struct ib_device dev;
                struct device *dma_dev;

                ib_register_device(&dev, "foo", dma_dev);
             ])],
            [VIC_IB_REGISTER=4
             MSG="int (struct ib_device *dev, const char *name, struct device *dma_dev)"])
        ])

    AS_IF([test $VIC_IB_REGISTER -eq 0],
        [AC_MSG_RESULT([unknown])
         AC_MSG_WARN([Cannot determine which ib_register_device() variant to use])
         AC_MSG_ERROR([Cannot continue])])

    CFLAGS=$CFLAGS_save
    AC_MSG_RESULT([type $VIC_IB_REGISTER: $MSG])
    AC_DEFINE_UNQUOTED($1, $[VIC_IB_REGISTER],
        [Which variant of ib_register_device() to use])
]) dnl end AC_DEFUN
