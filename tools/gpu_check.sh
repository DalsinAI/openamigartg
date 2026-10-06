#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenGPU's Vulkan back end on this machine's GPU (a Pi 5's V3DV, daletop's
# RADV): the golden scenes and 3000 random streams against the C core, first
# in the back end's own memory, then in memory the test owns (as the
# runtime's board memory is), each way the GPU can reach it. Builds in
# /tmp, installs nothing; it says what is missing instead.
#   sh tools/gpu_check.sh
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/opengpu-check}
mkdir -p "$OUT"
[ -e /usr/include/vulkan/vulkan.h ] || { echo "missing: Vulkan headers (Debian/Pi OS package libvulkan-dev)"; exit 1; }
ldconfig -p 2>/dev/null | grep -q libvulkan.so.1 || { echo "missing: the Vulkan loader (libvulkan1)"; exit 1; }
cc -std=c99 -O2 -Wall -Wextra -o "$OUT/test_opengpu_vk" "$HERE/tests/test_opengpu_vk.c" "$HERE/host/vulkan/ogpu_vk.c" \
    "$HERE/library/opengpu/ogpu_core.c" "$HERE/library/opengpu/ogpu_build.c" -lvulkan || exit 1
rc=0
for mode in own auto import map; do
    if [ $mode = own ]; then host=0; mem=; elif [ $mode = auto ]; then host=1; mem=; else host=1; mem=$mode; fi
    echo "== video RAM: $mode"
    start=$(date +%s)
    OGPU_VK_HOST=$host OGPU_VK_HOSTMEM=$mem "$OUT/test_opengpu_vk" "$HERE/tests/golden/opengpu-g1.txt" 3000 "$HERE/tests/golden/opengpu-v11.txt"
    r=$?
    echo "   (exit $r, $(( $(date +%s) - start )) s)"
    # import or map alone may be missing on a device; own and auto must pass.
    if [ $r -ne 0 ] && { [ $mode = own ] || [ $mode = auto ]; }; then rc=1; fi
done
[ $rc -eq 0 ] && echo "gpu check: passed" || echo "gpu check: FAILED"
exit $rc
