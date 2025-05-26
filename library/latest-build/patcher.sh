#!/bin/zsh
# patch_latest.sh - Patch and build the latest Muffin version in muffin_versions/

set -e

PATCH_DIR="$(dirname "$0")"
VERSIONS_DIR="$PATCH_DIR/muffin_versions"

# Find the latest version directory (sorted by version number)
LATEST_VERSION=$(ls -1d $VERSIONS_DIR/*/ 2>/dev/null | sort -V | tail -n 1)

if [[ -z "$LATEST_VERSION" ]]; then
  echo "No Muffin versions found in $VERSIONS_DIR. Please add a version directory."
  exit 1
fi

cd "$LATEST_VERSION"
echo "Using Muffin source: $LATEST_VERSION"

# Clean previous build if exists
if [[ -d build ]]; then
  rm -rf build
fi

# Apply patch (assumes patch file is in $PATCH_DIR/patch.diff)
if [[ -f "$PATCH_DIR/patch.diff" ]]; then
  echo "Applying patch..."
  patch -p1 < "$PATCH_DIR/patch.diff"
else
  echo "No patch.diff found in $PATCH_DIR. Skipping patch step."
fi

# Build Muffin
meson setup build
ninja -C build

# Install (requires sudo)
echo "Installing patched Muffin (sudo required)..."
sudo ninja -C build install

gschema_dir=$(find build -type d -name 'gschemas.compiled' -prune -o -type d -name 'data' -print | head -n 1)
if [[ -n "$gschema_dir" ]]; then
  echo "Compiling GSettings schemas..."
  sudo glib-compile-schemas "$gschema_dir"
fi

echo "Patched Muffin $LATEST_VERSION installed. Please restart Cinnamon."
