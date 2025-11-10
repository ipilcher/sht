#! /bin/sh -x

#
# Fedora COPR build script
#
# NOTE:  This script lives in this repository for version control, but it isn't
#	 run from within the repository.  It needs to be copied into the COPR
#	 custom build definition.
#
#	 The build definition must specify git-core as a build dependency.
#

set -e
RESULT_DIR=$PWD
git clone https://github.com/ipilcher/sht

# Find the most recent version tag - vX.Y.Z.  (Most recent means the tag that
# points to the most recent commit, not necessarily the tag that was created
# most recently.
GIT_TAG=$(git -C sht tag --sort=committerdate \
	| grep -E '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' \
	| tail -n 1
)

LIB_VER=${GIT_TAG#v}
SO_VER=${LIB_VER%.*}

sed -e s/@@LIBVER@@/${LIB_VER}/ -e s/@@SOVER@@/${SO_VER}/ \
	sht/rpm/libsht.spec > libsht.spec

tar cvzf libsht-${LIB_VER}.tar.gz --exclude='sht/.*' sht
