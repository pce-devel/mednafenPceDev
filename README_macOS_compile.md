# Mednafen

## Compiling this Repository on macOS (tested on macOS Monterey)

### Dependencies

Mednafen is built upon a number of external libraries which are required in order to have a successful compile.

The below is assuming that you already have a working install of Homebrew/MacPorts, and a working set of git/autoconf/automake/gcc/make and the Xcode Command Line Tools installed,
if not the following (and other) commands may still be needed:

```
Get Xcode Command Line tools:
xcode-select --install
```

We have confirmed a successful Mednafen build on macOS Monterey and higher running on both Intel(x86_64) and Apple silicon M1(ARM64) architectures.

For a native Intel(x86_64) or Apple Silicon(ARM64) build we will use "Homebrew" to pull in the needed Mednafen dependencies:
```
brew install pkg-config gettext sdl2 libsndfile jack lzo zstd 
```

### Compilation Commands

Mednafen's build process makes use of 'configure' in order to set up environment-specific options in preparation for the compile.
 
The below instructions to build Mednafen will attempt to use all of your machine's threads when compiling as the compilation process can take up quite some time, we disable optimization for the build as Mednafen has proven quite unstable when attempting to have the clang compiler on macOS optimize the application. The compilation procedure is as follows:

```
./configure --enable-ss

make CFLAGS='-g -w' CXXFLAGS='-g -w' -j `sysctl -n hw.logicalcpu`
```

At this point, the Mednafen binary can be found in the mednafen/src/ folder; you may manually copy/move it
from there, or use the prescribed:
```
sudo make install
```

## Further Mednafen documentation:

Mednafen has quite a few options that can affect the experience, luckily the Mednafen documentation site has quite a few pages of resources that live here:
[https://mednafen.github.io/documentation/)
