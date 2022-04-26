#!/bin/bash
VERSION=`head -n 1 mednafen/Documentation/modules.def`

CROSS_BASE="$HOME/mednafen-cross"
CROSS32_PATH="$CROSS_BASE/win32"
CROSS64_PATH="$CROSS_BASE/win64"
CROSS9X_PATH="$CROSS_BASE/win9x"
export PATH="$CROSS64_PATH/bin:$PATH"

cd build64 && \
cp src/mednafen.exe . && \
x86_64-w64-mingw32-strip mednafen.exe && \
x86_64-w64-mingw32-strip libgcc_s_seh-1.dll libcharset-1.dll libiconv-2.dll SDL2.dll && \
mkdir -p de/LC_MESSAGES ru/LC_MESSAGES && \
cp ../mednafen/po/de.gmo de/LC_MESSAGES/mednafen.mo && \
cp ../mednafen/po/ru.gmo ru/LC_MESSAGES/mednafen.mo && \
7za a -mtc- -mx9 -mfb=258 -mpass=15 ../mednafen-"$VERSION"-happyeyes-win64.zip mednafen.exe libgcc_s_seh-1.dll libcharset-1.dll libiconv-2.dll SDL2.dll de/ ru/ && \
cd ../mednafen && \
7za a -mtc- -mx9 -mfb=258 -mpass=15 ../mednafen-"$VERSION"-happyeyes-win64.zip COPYING ChangeLog Documentation/*.html Documentation/*.css Documentation/*.png Documentation/*.txt && \
cd ..

