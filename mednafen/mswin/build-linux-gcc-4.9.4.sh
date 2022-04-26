#!/bin/bash

#
# apt-get install build-essential pkg-config libmpfr-dev libgmp-dev libmpc-dev gawk
#
CROSS_SOURCES="$HOME/mednafen-cross-sources"

PKGNAME_GCC="gcc-4.9.4"

cd $CROSS_SOURCES && \
tar -jxf $PKGNAME_GCC.tar.bz2 && \
patch -p0 < "$CROSS_SOURCES/$PKGNAME_GCC-linux-ucontext.patch" && \
cd $PKGNAME_GCC && \
./configure --prefix=$HOME/$PKGNAME_GCC --enable-languages=c,c++ --disable-multilib --disable-libsanitizer --disable-libcilkrts && \
make -j$(nproc) && \
make install && \
cd .. && \
rm --one-file-system -rf $PKGNAME_GCC && \

#
#
#
echo "Done."
