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
PIN=20c7465c0c6614d2a4cad1fae13129877940826f
REPO=${OGFX_CORE_REPO:-https://github.com/DalsinAI/openamigagfx.git}
SUMS='fb761c746a99406db9190f701e24eddfc0e65a1897ec9036e8c195bd6e2e5908  gfx_core.c
00f2b7d6ccf5664b6e1d37cdc3424585d3e549b103d9a64e241051dba9324926  gfx_core.h
ad7f05b3da9a27c8f90d53ff2a062316031619657ce0b4c22f0f2d416217f914  gfx_rp.c
de5cd2bad60385c1a9fdebd070c19394c4d2d5f7ea83a767888d4cae3854142c  gfx_rp.h'
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
