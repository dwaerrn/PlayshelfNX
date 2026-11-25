#!/bin/sh
# Applies the two Switch-side Borealis patches the Library app needs. Safe to run more than once.
# Run from the project folder (the one that contains extern/borealis).
set -e
SV=extern/borealis/library/lib/platforms/switch/switch_video.cpp
DK=extern/borealis/library/include/borealis/extern/nanovg/dk_renderer.hpp

# 1) GPU image memory in 64 MiB blocks instead of 4 MiB (one poster no longer costs a whole block)
sed -i "s/#define IMAGES_POOL_SIZE 4 \* 1024 \* 1024/#define IMAGES_POOL_SIZE 64 * 1024 * 1024/" "$SV"
# 2) 4 MiB instead of 128 KiB for the per-frame draw command buffer (safety margin)
sed -i "s/DynamicCmdSize = 0x20000;/DynamicCmdSize = 0x400000;/" "$DK"

echo "Current values:"
grep -n "define IMAGES_POOL_SIZE" "$SV"
grep -n "DynamicCmdSize =" "$DK"
