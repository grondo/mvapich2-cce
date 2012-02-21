#!/bin/sh
#
# $Id: fixup-debuginfo.sh 55 2006-08-17 15:07:48Z grondo $
#
# Fixup ELF debug DW_AT_comp_dir information for installed mvapich-debug RPM.
# (Borrowed heavily from RPMs find-debuginfo.sh, but we don't want to 
#  subsequently strip binaries)
#
#
prog=$(basename $0)
short_opts="d:p:s:b:n"
long_opts="builddir:,destdir:,path:,srcdir:,no-check"

echo "$0: called with $@"

builddir=`pwd`
srcdir=`pwd`/mvapich
debugpath="/usr/lib/mpi/dbg/mvapich-gen2" 
debugedit="/usr/lib/rpm/debugedit"
no_check=0

die () {
	echo -e "$prog: $@" >&2
	exit 1
}

set -- `/usr/bin/getopt -u -o $short_opts --long $long_opts -n $prog -- $@`
while true; do
    case "$1" in 
	  -b|--builddir) builddir="$2";  shift 2 ;;
	  -d|--destdir)  destdir="$2";   shift 2 ;;
	  -s|--srcdir)   srcdir="$2";    shift 2 ;;
	  -p|--path)     debugpath="$2"; shift 2 ;;
	  -n|--no-check) no_check=1;     shift ;;
	  --)            shift; break ;;
	  *)             die "Unknown option" ;;
	esac
done

SOURCEFILE=$builddir/files.debug.$$

[ -z "$destdir" ] && die "--destdir not set (and not running under rpmbuild?)"

echo -n > $SOURCEFILE

# Move debug information for all ELF binaries from real build path to
#  future install path. Collect all referenced sources into SOURCEFILE
#
#for f in `find "${builddir}" -type f \
for f in `find "${destdir}" -type f \
          \( -perm -0100 -or -perm -0010 -or -perm -0001 \) -exec file {} \; | \
          sed -n -e 's/^\(.*\):[ 	]*ELF.*, not stripped/\1/p'`
do
    p=`echo $f | sed 's,$destdir/$debugpath,,g'`
    echo "$p: moving srcdir from $srcdir to $debugpath/src"
    echo $debugedit -b "$srcdir" -d "$debugpath/src" -l "$SOURCEFILE" "$f"
    $debugedit -b "$srcdir" -d "$debugpath/src" -l "$SOURCEFILE" "$f"
done


#
# Copy referenced sources in SOURCEFILE (NUL terminated list of files) to 
#  install location.
#
umask 022
mkdir -p -m 0755 ${destdir}/${debugpath}/src

cat $SOURCEFILE | \
    ( cd $srcdir; LANG=C sort -z -u | \
	  cpio -Lpd0m ${destdir}/${debugpath}/src ) 2>/dev/null || :

rm $SOURCEFILE

#
# Make sure directories and files are world readable.
#
chmod -R go+rX ${destdir}/${debugpath}/src 

#
# Make sure some sources at least were installed.
#
if [ $no_check = 0 ]; then
    checksource=${destdir}/${debugpath}/src/src/include/mpi.h
    [ -f $checksource ] || die "Missing source: $checksource"
fi

exit 0
