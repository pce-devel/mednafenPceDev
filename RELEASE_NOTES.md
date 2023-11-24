# Release Notes

## New release 1.2 2023/11/24

### New Features/Improvements:

#### PC-FX Improvements / BugFixes: 
 - Adds Azerty Keyboard support for Debugging functions
 - Improve PC-FX palette editor - In debug mode, change PC-FX palette editor to show 16-bit values, with a colour swatch in the right-hand side to demonstrate actual colour (based on YUV)
 - PSG Tweaks Update channel 1 frequency cache upon LFO frequency register writes (the way the channel 1 frequency and LFO frequency are combined is still inaccurate, however, causing frequency update timing granularity to be too high). Ported over MDFN 1.31.0 release

#### PC-Engine Improvements / Bugfixes:
- PSG Tweaks Update channel 1 frequency cache upon LFO frequency register writes (the way the channel 1 frequency and LFO frequency are combined is still inaccurate, however, causing frequency update timing granularity to be too high). Ported over MDFN 1.31.0 release
- Backup memory visibility was previously inconsistent between HuCard and CDROM, and locking behaviour was not always correct. Now, physical memory at $1EE000 should give the "processor's view" of the memory, depending on lock/unlock status, and "Backup Memory" screen should show the actual data (Note: HuCard startup state is still default 'unlocked' which is not correct). Also, $1807 register writes should be able to re-lock memory
- Remove Incorrect IRQ Acknowledgement scenario: TIMER interrupt is supposed to be acknowledged by write to $1403. Original code was also acknowledging it on reads from $1402, which was not supported by official documentation.
- Fix junk appearing on PCE Debug palette screen 
- Change Default gamesave name removing hash value for BRAM retention and compatbilty when doing PCE dev builds
- Fix improper CDROM BRAM locking in debugger
- PC Engine music player VU meter shows incorrect channels
- Fix VU Meter display
- Other minor changes to warnings and defaults
- Add CI builds for Windows / Linux
  
#### Other items:
 - This release also contains several Wonderswan module updates that improve emulation accuracy

## New release 1.1 2022/08/21

### New Features/Improvements:

#### PC-FX:
 - Step by frame, or scanline in debugger (issue 101, use Ctrl-S / Shift-S)

#### PC-Engine:
 - Partial screen-render as-it-happens when debugging (issue 65)
 - Step by frame, or scanline in debugger (issue 74, use Ctrl-S / Shift-S)
 - Expand frame/scanline step mode to graphics and memory viewers (issue 94)

### Bugfixes/Default Setting Changes:

#### PC-Engine
 - Fix timing of when Burst bit is latched (issues 8, 12)
 - Fix treatment of BG background blank bit (issue 84)
 - Approximate MWR VRAM access wait states (experimental, partial fix of issues 5 and 56,
pce.mwrtiming_approx in mednafen.cfg, defaults to original behaviour (off))
 - Change default codec paramters to use PNG (issue 88)
 - Fix treatment of CDROM "Read TOC" command $DE (issue 98) 
 - Adjust CDROM data transfer rate to 150KB/s (experimental, partial fix of issue 102,
pce.cdspeed (120KB/s=0 vs. 150KB/s=1) and pce.cdthrottle (to restabilize Sherlock Holmes) in
mednafen.cfg - both default to original behaviour (off))


## New release 1.0 2022/06/05

### New Features/Improvements:

#### PC-FX
 - Separate 'external backup memory' to its own file (pcfx.external_bram_file in mednafen.cfg)
 - Add 16-bit 'word' editing for video memory

#### PC Engine
 - Calculated CDROM Head Seek Time to match original console
 - Implement Memory Base 128 functionality (pce.memorybase128_enable and pce.memorybase128_file in mednafen.cfg)
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
 - Add option to halt execution on "Alt-D" to enter debug screen (debugger.haltondebug in mednafen.cfg)
 - Make debug overlay opacity persist between runs (debugger.opacity in mednafen.cfg)
 - Allow debug overlay to fractionally scale to fit window (debugger.fractionalscaling in mednafen.cfg)

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
