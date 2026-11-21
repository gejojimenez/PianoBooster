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

VERSION="$(for part in MAJOR MINOR PATCH; do
    awk -v key="PB_VERSION_$part" '$1 == "#define" && $2 == key {print $3}' "$SRC_DIR/src/version.h"
done | paste -sd. -)"
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

# Qt libraries and plugins (platform, styles, image formats, SQLite driver)
# - Not deployed: the plugins for touch input (generic), network information and TLS,
#   which would only pull in Qt6Network, not used by PianoBooster.
# - Only the SQLite database driver is used (by the song library); the others would
#   pull in the client libraries of other databases.
# - The Qt translations are not in MSYS2 by default (package qt6-translations), and the
#   DXC compiler is only for Direct3D 12, not used.
"$MSYS_BIN/windeployqt6.exe" --release --no-compiler-runtime --no-translations \
    --no-system-dxc-compiler --no-opengl-sw \
    --skip-plugin-types generic,networkinformation,tls \
    --exclude-plugins qsqlibase,qsqlmysql,qsqlodbc,qsqlpsql \
    "$OUT_DIR/pianobooster.exe"

# Qt modules PianoBooster no longer uses (Qt6Core5Compat since 1.3.4): windeployqt only
# deploys them if the executable needs them, which means that it is out of date.
for module in Qt6Core5Compat Qt6Xml Qt6Network; do
    if [ -f "$OUT_DIR/$module.dll" ]; then
        echo "error: $module.dll is needed by pianobooster.exe: rebuild it first (cmake --build build)" >&2
        exit 1
    fi
done

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
