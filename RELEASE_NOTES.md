# Release Notes

## New release 2022/06/05

### New Features/Improvements:

#### PC-FX
 - Separate 'external backup memory' to its own file (pcfx.external_bram_file in mednafen.cfg)
 - Add 16-bit 'word' editing for video memory

#### PC Engine
 - Calculated CDROM Head Seek Time to match original console
 - Implement Memory Base 128 functionality (pce.memorybase128_enable and pce.memorybase128_file)
 - Add 16-bit 'word' editing for video memory
 - Change Palette editor to G/R/B format with actual display of colors
 - Interpret data as if it is a SAT entry for certain type of memory
 - Add Sprite Bounding Box display toggle (CTRL + 0)
 - Toggle PSG channel output (ALT+1 thorugh ALT+6)
 - Add substantial debug logging for CDROM BIOS calls
 - Add debug logging for ADPCM accesses
 - Add 'seconds' counter to debug screen
 - Add 'frame' counter to debug screen

#### General
 - Significant improvements to debugger from jbrandwood's 'mednafen-happyeyes' modifications
 - Display memory near stack pointer for systems with zero-page and single-page stack pointers
 - Add option to halt execution on "Alt-D" to enter debug screen (debugger.haltondebug)
 - Make debug overlay opacity persist between runs (debugger.opacity)
 - Allow debug overlay to fractionally scale to fit window (debugger.fractionalscaling)

### Bugfixes/Default Setting Changes:

#### PC-FX
 - Make 'external backup memory' 128KB, same as FX-BMP (not 32KB)

#### PC-Engine
 - Increase visible Scanline defaults
 - Change Audio defaults to match real hardware
 - Enable Horizontal Overscan
 - Set defaults for interpolation to 'off', not 'bilinear interpolation'

### Documentation:
 - Add initial instructions for linux compile in top folder

## Starting Release: Mednafen 1.29.0

 - Reconsititued Mednafen sources with a version history for review
