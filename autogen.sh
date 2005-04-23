# Usage:
#     compare_versions MIN_VERSION ACTUAL_VERSION
# returns true if ACTUAL_VERSION >= MIN_VERSION
compare_versions() {
    local min_version actual_version status save_IFS cur min
    min_version=$1
    actual_version=$2
    status=0
    IFS="${IFS=         }"; save_IFS="$IFS"; IFS="."
    set $actual_version
    for min in $min_version; do
        cur=`echo $1 | sed 's/[^0-9].*$//'`; shift # remove letter suffixes
        if [ -z "$min" ]; then break; fi
        if [ -z "$cur" ]; then status=1; break; fi
        if [ $cur -gt $min ]; then break; fi
        if [ $cur -lt $min ]; then status=1; break; fi
    done
    IFS="$save_IFS"
    return $status
}

# Usage:
#     version_check PACKAGE VARIABLE CHECKPROGS MIN_VERSION SOURCE
# checks to see if the package is available
version_check() {
    local package variable checkprogs min_version source status checkprog actual_version
    package=$1
    variable=$2
    checkprogs=$3
    min_version=$4
    source=$5
    status=1

    checkprog=`eval echo "\\$$variable"`
    if [ -n "$checkprog" ]; then
	echo "using $checkprog for $package"
	return 0
    fi

    echo "checking for $package >= $min_version..."
    for checkprog in $checkprogs; do
	echo -n "  testing $checkprog... "
	if $checkprog --version < /dev/null > /dev/null 2>&1; then
	    actual_version=`$checkprog --version | head -n 1 | \
                               sed 's/^.*[ 	]\([0-9.]*[a-z]*\).*$/\1/'`
	    if compare_versions $min_version $actual_version; then
		echo "found."
		# set variable
		eval "$variable=$checkprog"
		status=0
		break
	    else
		echo "too old (found version $actual_version)"
	    fi
	else
	    echo "not found."
	fi
    done
    if [ "$status" != 0 ]; then
	echo "***Error***: You must have $package >= $min_version installed"
	echo "  to build $PKG_NAME.  Download the appropriate package for"
	echo "  from your distribution or get the source tarball at"
        echo "    $source"
    fi
    return $status
}

# Required: autoconf >= 2.53, automake >= 1.6, libtool >= 1.5
version_check libtool LIBTOOLIZE \
  'libtoolize' 1.5 \
  "http://ftp.gnu.org/pub/gnu/libtool/" || exit 1
version_check autoconf AUTOCONF \
  'autoconf autoconf-2.53' 2.53 \
  "http://ftp.gnu.org/pub/gnu/autoconf/" || exit 1
AUTOHEADER=`echo $AUTOCONF | sed s/autoconf/autoheader/`
version_check automake AUTOMAKE \
  'automake automake-1.7 automake-1.6' 1.6 \
  "http://ftp.gnu.org/pub/gnu/automake/" || exit 1
ACLOCAL=`echo $AUTOMAKE | sed s/automake/aclocal/`

# Run
${ACLOCAL} -Im4 || exit 1
${AUTOHEADER} || exit 1
${LIBTOOLIZE} --force --copy || exit 1
${AUTOCONF} || exit 1
${AUTOMAKE} --add-missing --gnu || exit 1
test -n "$NOCONFIGURE" || \
  ./configure --enable-maintainer-mode --enable-compile-warnings $@
