# Mednafen

## Compiling this Repository on Linux

### Dependencies

Mednafen is built upon a number of external libraries which are required in order to compile.

Assuming that you already have a working in stall of gcc/make and other basic devloepre tools,
the following command(s) may still be needed:

```
sudo apt-get install build-essential pkg-config libasound2-dev libcdio-dev libsdl1.2-dev libsdl-net1.2-dev libsndfile1-dev zlib1g-dev  
```

### Compile Commands

Mednafen's build process makes use of 'configure' in order to set up environment-specific options in preparation
for the compile; the procedure is as follows:

```
./configure

make
```

At this point, the mednafen binary can be found in the mednafen/src/ folder; you may manually copy/move it
from there, or use the prescribed:
```
sudo make install
```

## Common Problems:

It is possible that the sound device may not be properly identified by mednafen; if this is the case, the original
mednafen documentation has some hints here:
[https://mednafen.github.io/documentation/#Section_troubleshooting_nosoundlinux](https://mednafen.github.io/documentation/#Section_troubleshooting_nosoundlinux)

