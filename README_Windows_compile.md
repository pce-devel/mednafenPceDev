# Mednafen

## Compiling this Repository for Windows... on Linux

### Dependencies

Mednafen is built upon a number of external libraries which are required in order to compile.

Apparently, one of the dependencies broke the MSYS2 build sometime in the past couple of years,
so Windows builds are cross-compiled from Linux now.

Assuming that you already have already built for linux, you will need to do the following:
```
sudo apt-get install build-essential pkg-config libmpfr-dev libgmp-dev libmpc-dev gawk p7zip-full
```

You will also need to do the following:
 1. Create a directory "mednafen-cross-sources", within your home directory
 2. Obtain the following packages and place them in that folder:
   - binutils-2.28.1.tar.bz2 (for example, from here: https://ftp.gnu.org/gnu/binutils/ )
   - mingw-w64-v5.0.4.tar.bz2 (for example, from here: https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/ )
   - gcc-4.9.4.tar.bz2 (for example, from here: https://ftp.gnu.org/gnu/gcc/gcc-4.9.4/ )
   - libiconv-1.16.tar.gz (for example, from here: https://ftp.gnu.org/gnu/libiconv/ )
   - flac-1.3.3.tar.xz (for example, from here: https://ftp.osuosl.org/pub/xiph/releases/flac/ )
   - zlib-1.2.11.tar.gz (for example, from here: https://zlib.net/fossils/ )
   - SDL2-2.0.9.tar.gz (for example, from here: https://www.libsdl.org/release/ )
 3. Copy all of the mednafen/mswin/*.patch files into that folder (the one named 'mednafen-cross-sources'). For example, from within the mednafen/mswin directory, ```cp *.patch ~/mednafen-cross-sources/```

### Build Toolchain Commands

Change directory into the mednafen/mswin directory, and:
 - Build the gcc cross compiler
 - Build the toolchain for cross-compile
 - Build mednafen for cross-compile

Assuming you are in the repository's top-level directory, you should execte the following:
```
cd mednafen/mswin
./build-linux-gcc-4.9.4.sh
./build-toolchain.sh
```

### Compile Commands

Now that the toolchain is created, the build of the mednafen source comes next.
If you have already built on linux in-place, you will need to clean up the residual configuration and objects.
Change directories to the 'mednafen' directory, then run:

```
make distclean
```
In order to build the source, the starting directory is the directory above the 'mednafen' folder,
so change to that (should be the root folder of the git repository). As part of the build, the
build script will create a 'build64' folder there with output files.

From within the (repository's root) folder, you would execute:

```
./mednafen/mswin/build-mednafen.sh
```

At this point, the mednafen binary can be found in the (base)/build64/src/ folder.

If you want to build a full zip file of the packaging, you will need to run this from the
repository's root folder (one level above the mednafen folder):
```
./mednafen/mswin/package-mednafen.sh
```

At this point, the mednafen-$(VERSION).zip file is in the (repo root) folder.


