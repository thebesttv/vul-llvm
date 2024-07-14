#!/bin/bash

# exit on error
set -e

cd build-release
ninja

rm -rf AppDir
mkdir AppDir
DESTDIR=AppDir ninja install || true

# remove unused binaries
cd AppDir/usr/bin/ && rm $(ls | \
    grep -v thebesttv | \
    grep -v '^clang$' | \
    grep -v '^clang-17$' | \
    grep -v '^clang++$' \
) && cd ../../..

# Do not strip libraries when running local (ie., when GA is not set).
# In ArchLinux, trying to strip libraries will cause error
if [ -z "$GITHUB_ACTIONS" ]; then
    export NO_STRIP=true
fi

linuxdeploy \
    --appdir AppDir --output appimage \
    -d ../graph-generation/tool.desktop \
    -i ../graph-generation/tool.png
