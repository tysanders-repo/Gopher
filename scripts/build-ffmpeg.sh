#!/usr/bin/env bash
set -euo pipefail

# === paths & version ===
ROOT="$(cd "$(dirname "$0")/.."; pwd)"          # repo root
VENDOR="$ROOT/vendor/ffmpeg"
SRC="$ROOT/.third_party/ffmpeg-src"             # throwaway source/cache dir
VER="6.1.1"                                     # pick one and stick to it for reproducibility

mkdir -p "$VENDOR"/{arm64,x86_64,lib,include,lib/pkgconfig} "$SRC"
cd "$SRC"

# === fetch source once ===
TARBALL="ffmpeg-${VER}.tar.xz"
URL="https://ffmpeg.org/releases/${TARBALL}"
[ -f "$TARBALL" ] || curl -LO "$URL"
[ -d "ffmpeg-${VER}" ] || tar xf "$TARBALL"

COMMON_CFG=(
  --prefix=PREFIX_WILL_BE_OVERRIDDEN        # replaced per-arch below
  --target-os=darwin
  --enable-shared --disable-static
  --disable-debug --disable-doc
  --disable-programs                        # no ffmpeg/ffprobe binaries; faster, smaller
  --enable-avdevice --enable-avformat --enable-avcodec --enable-avutil
  --enable-swscale --enable-swresample
  --install-name-dir=@rpath                 # critical for clean @rpath linking
)

build_arch () {
  local ARCH="$1" ; local OUT="$2"
  local CFLAGS="-arch ${ARCH}"
  local LDFLAGS="-arch ${ARCH}"

  rm -rf "ffmpeg-${VER}/build-${ARCH}"
  mkdir -p "ffmpeg-${VER}/build-${ARCH}"
  cd "ffmpeg-${VER}/build-${ARCH}"

  PKG_CONFIG_PATH="" \
  ../configure \
    --arch="${ARCH}" \
    "${COMMON_CFG[@]/PREFIX_WILL_BE_OVERRIDDEN/$VENDOR/$ARCH}" \
    --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS"

  make -j"$(sysctl -n hw.ncpu)"
  make install
  cd - >/dev/null
}

# === per-arch builds ===
build_arch arm64   "$VENDOR/arm64"
build_arch x86_64  "$VENDOR/x86_64"

# === combine to UNIVERSAL dylibs with lipo (preserve versioned names) ===
mkdir -p "$VENDOR/lib"

merge_one() {
  local L="$1"   # e.g., avutil

  # Find the fully versioned 'real' dylib filenames in each arch
  local REAL_A
  REAL_A=$(cd "$VENDOR/arm64/lib"   && ls -1 "lib${L}."*.*.dylib | head -n1)
  local REAL_B
  REAL_B=$(cd "$VENDOR/x86_64/lib"  && ls -1 "lib${L}."*.*.dylib | head -n1)

  if [[ -z "$REAL_A" || -z "$REAL_B" ]]; then
    echo "ERROR: Could not find versioned lib for ${L}" >&2
    exit 1
  fi

  # Sanity: require the same soname (major) on both slices
  local MA_A MA_B
  MA_A=$(echo "$REAL_A" | sed -n 's/^lib[^.]*\.\([0-9][0-9]*\)\..*\.dylib$/\1/p')
  MA_B=$(echo "$REAL_B" | sed -n 's/^lib[^.]*\.\([0-9][0-9]*\)\..*\.dylib$/\1/p')
  if [[ "$MA_A" != "$MA_B" ]]; then
    echo "ERROR: ${L} major mismatch: arm64=$MA_A x86_64=$MA_B" >&2
    exit 1
  fi

  # Use the arm64 filename as the universal basename (both should match anyway)
  local UNIVERSAL_REAL="$VENDOR/lib/$REAL_A"
  echo "Merging $REAL_A  +  $REAL_B  ->  $(basename "$UNIVERSAL_REAL")"
  lipo -create \
    "$VENDOR/arm64/lib/$REAL_A" \
    "$VENDOR/x86_64/lib/$REAL_B" \
    -output "$UNIVERSAL_REAL"

  # Recreate symlink chain:
  # libX.<major>.dylib -> libX.<major>.<minor>.<patch>.dylib
  # libX.dylib         -> libX.<major>.dylib
  ( cd "$VENDOR/lib"
    ln -sf "$REAL_A"           "lib${L}.${MA_A}.dylib"
    ln -sf "lib${L}.${MA_A}.dylib" "lib${L}.dylib"
  )
}

for L in avcodec avformat avutil swscale swresample avdevice; do
  merge_one "$L"
done

echo "— verify fat binaries & @rpath install-names —"
for L in avcodec avformat avutil swscale swresample avdevice; do
  file "$VENDOR/lib/lib${L}.dylib" | sed 's/^/  /'
  otool -L "$VENDOR/lib/lib${L}.dylib" | sed 's/^/  /'
done

