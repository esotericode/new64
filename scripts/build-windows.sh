#!/bin/sh
# new64 -- cross-compile Windows x64 binaries from Linux and package them.
#
# Prerequisite (Debian/Ubuntu):   apt install mingw-w64
# There is no second prerequisite: the engine has no external dependencies, so
# there is no zlib or SDL to find. That is also why the resulting .exe files
# import nothing but Windows' own DLLs.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-win"
OUT="$ROOT/dist/new64-windows-x64"

cmake -S "$ROOT" -B "$BUILD" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

mkdir -p "$OUT/assets"
cp "$BUILD/new64_play.exe" "$BUILD/new64_headless.exe" "$BUILD/new64_tests.exe" "$OUT/"
cp "$ROOT/assets/mario_anims.txt" "$OUT/assets/"

# Fail loudly if anything crept in beyond the Windows system DLLs -- that would
# mean the zip needs extra files alongside it, which defeats the point.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    EXTRA=$(x86_64-w64-mingw32-objdump -p "$OUT"/*.exe \
            | grep -i 'DLL Name' | sort -u \
            | grep -viE 'kernel32|user32|gdi32|winmm|msvcrt|advapi32|shell32' || true)
    if [ -n "$EXTRA" ]; then
        echo "error: unexpected DLL dependency:" >&2
        echo "$EXTRA" >&2
        exit 1
    fi
    echo "verified: imports are Windows system DLLs only"
fi

( cd "$ROOT/dist" && rm -f new64-windows-x64.zip && zip -rq new64-windows-x64.zip new64-windows-x64 )
echo "wrote dist/new64-windows-x64.zip"
