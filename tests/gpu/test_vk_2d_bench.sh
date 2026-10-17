#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# OpenGPU's Vulkan back end against its C core on the 2D that SDL 2's
# renderer and OpenRTG's Workbench send (tests/gpu/vk_2d_bench.c): the time
# each batch takes on each, what the GPU drew and what fell to the core, and
# a check that both leave the same bytes. On this machine's GPU (daletop's
# RX 460 through Cradle's repo_test, a Pi 5's V3DV); builds in /tmp and
# installs nothing. Says what is missing instead of failing when there is
# no Vulkan here.
#   sh tests/gpu/test_vk_2d_bench.sh        (BENCH_SCALE=0.1 for a quick look)
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-/tmp/opengpu-vk-2d-bench}
mkdir -p "$OUT"
INC=${VULKAN_INCLUDE:-/usr/include}
if [ ! -e "$INC/vulkan/vulkan.h" ]; then
    echo "missing: Vulkan headers (package libvulkan-dev, or VULKAN_INCLUDE=<Vulkan-Headers>/include)"
    exit 1
fi
LIB=$(ldconfig -p 2>/dev/null | awk '/libvulkan\.so\.1 /{print $NF; exit}')
if [ -z "$LIB" ]; then
    echo "missing: the Vulkan loader (libvulkan1)"
    exit 1
fi
uname -srm
cc -std=c99 -O2 -Wall -Wextra -I"$INC" -o "$OUT/vk_2d_bench" "$HERE/tests/gpu/vk_2d_bench.c" \
    "$HERE/host/vulkan/ogpu_vk.c" "$HERE/library/opengpu/ogpu_core.c" "$HERE/library/opengpu/ogpu_build.c" "$LIB" || exit 1
"$OUT/vk_2d_bench" "${BENCH_SCALE:-1}"
