#!/bin/bash
# Package a Windows build of PianoBooster (built with MSYS2) into a portable zip.
#
# Run from an "MSYS2 UCRT64" shell, after building with:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_PACKAGE_NAME=Qt6
#   cmake --build build
#
# Usage: tools/package-windows.sh [build-dir]   (default: build)
# Result: <build-dir>/package/PianoBooster-<version>-win64.zip

set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$(cd "${1:-$SRC_DIR/build}" && pwd)"
BIN_DIR="$BUILD_DIR/build"
MSYS_BIN="${MINGW_PREFIX:-/ucrt64}/bin"

VERSION="$(grep PB_VERSION "$SRC_DIR/src/version.h" | cut -d '"' -f 2)"
NAME="PianoBooster-$VERSION-win64"
OUT_DIR="$BUILD_DIR/package/$NAME"
ZIP="$BUILD_DIR/package/$NAME.zip"

if [ ! -f "$BIN_DIR/pianobooster.exe" ]; then
    echo "error: $BIN_DIR/pianobooster.exe not found, build first" >&2
    exit 1
fi

echo "Packaging $NAME"
rm -rf "$OUT_DIR" "$ZIP"
mkdir -p "$OUT_DIR/fonts" "$OUT_DIR/translations"

# Program and the data files it looks for next to the executable
cp "$BIN_DIR/pianobooster.exe" "$OUT_DIR/"
cp "$BIN_DIR/BoosterMusicBooks.zip" "$OUT_DIR/"
cp "$BIN_DIR/fonts/"*.ttf "$OUT_DIR/fonts/"
cp "$BIN_DIR/translations/"*.qm "$BIN_DIR/translations/langs.json" "$OUT_DIR/translations/"
cp "$SRC_DIR/README.md" "$SRC_DIR/license.txt" "$SRC_DIR/gplv3.txt" "$SRC_DIR/Changelog.txt" "$OUT_DIR/"

# Qt libraries and plugins (platform, styles, image formats...)
"$MSYS_BIN/windeployqt6.exe" --release --no-compiler-runtime "$OUT_DIR/pianobooster.exe"

# Every other DLL coming from MSYS2 (FluidSynth, FTGL, GCC runtime...), for the
# executable and for the Qt plugins; repeat until no new dependency appears.
while true; do
    added=0
    while IFS= read -r dll; do
        base="$(basename "$dll")"
        if [ ! -f "$OUT_DIR/$base" ]; then
            cp "$dll" "$OUT_DIR/"
            added=1
        fi
    done < <(find "$OUT_DIR" -name '*.exe' -o -name '*.dll' | xargs ldd 2>/dev/null \
             | awk '{print $3}' | grep -i "^${MSYS_BIN}/" | sort -u)
    [ "$added" -eq 0 ] && break
done

# Create the zip with Windows' own PowerShell (no extra tools needed)
powershell.exe -NoProfile -Command \
    "Compress-Archive -Path '$(cygpath -w "$OUT_DIR")' -DestinationPath '$(cygpath -w "$ZIP")'"

echo "Done: $(cygpath -w "$ZIP")"
