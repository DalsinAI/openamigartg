#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenGfx's drawing core (DalsinAI/openamigagfx library/core) at a pinned
# commit, checked by sha256, into OUT/ogfx-core: the one copy of the core
# lives in openamigagfx, and opengpu.library builds it from there.
#   tools/fetch_ogfx_core.sh OUT_DIR
# OGFX_CORE_DIR=<an openamigagfx checkout> uses that instead of fetching (its
# files must still match the pin).
set -eu
OUT=${1:?usage: fetch_ogfx_core.sh OUT_DIR}
PIN=f8f646af13e5808e48f1f6547ec961a8be8979a7
REPO=${OGFX_CORE_REPO:-https://github.com/DalsinAI/openamigagfx.git}
SUMS='52fa2b89fdd526c0fa1f3b2cf07cda4d96259d12508fc989bb43b085a40a185d  gfx_core.c
ce60403283f6278ca5394cbbd13ac17322e4a4488dadad8d384f3cf5478863d5  gfx_core.h
3cee0f707102fe5456c3753ebe32bbcc955ab7c16094e1494b270f672d41337d  gfx_rp.c
efd12538072db19cea5142ff9b2c34f42020669735b18fc0439c4a1bea8f651b  gfx_rp.h'
DST="$OUT/ogfx-core"
mkdir -p "$DST"
if (cd "$DST" && echo "$SUMS" | sha256sum -c --quiet) >/dev/null 2>&1; then exit 0; fi
if [ -n "${OGFX_CORE_DIR:-}" ]; then
    SRC="$OGFX_CORE_DIR/library/core"
else
    TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
    git -C "$TMP" init -q
    git -C "$TMP" fetch -q --depth 1 "$REPO" "$PIN"
    git -C "$TMP" checkout -q FETCH_HEAD
    SRC="$TMP/library/core"
fi
for f in gfx_core.c gfx_core.h gfx_rp.c gfx_rp.h; do cp "$SRC/$f" "$DST/$f"; done
(cd "$DST" && echo "$SUMS" | sha256sum -c --quiet) || { echo "fetch_ogfx_core.sh: $DST does not match openamigagfx $PIN" >&2; exit 1; }
echo "OpenGfx core: openamigagfx $PIN"
