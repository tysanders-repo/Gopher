#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd -P)"
VENDOR="$ROOT/vendor/sdl2"
SRC="$ROOT/.third_party/sdl2-src"

mkdir -p "$VENDOR"/{lib,include} "$SRC"
cd "$SRC"

# 1) Get **SDL2** (not SDL3)
if [ ! -d SDL ]; then
  git clone --depth=1 -b SDL2 https://github.com/libsdl-org/SDL.git SDL
fi

# 2) (Optional) show available targets for sanity
xcodebuild -list -project SDL/Xcode/SDL/SDL.xcodeproj || true

# 3) Build the macOS **framework** as a universal (arm64 + x86_64)
#    NOTE: use -target, not -scheme
xcodebuild -project SDL/Xcode/SDL/SDL.xcodeproj \
  -target Framework \
  -configuration Release \
  -arch arm64 -arch x86_64 \
  CONFIGURATION_BUILD_DIR="$VENDOR/lib" \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  build

# 4) Copy headers (flat include/ alongside the framework)
rsync -a --delete SDL/include/ "$VENDOR/include/"

# 5) Verify slices
echo "— verify SDL2.framework slice —"
file "$VENDOR/lib/SDL2.framework/SDL2"
