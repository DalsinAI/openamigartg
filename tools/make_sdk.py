#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""The OpenGPU developer kit: headers, link libraries, sdl2-config,
pkg-config and CMake files, examples, notices and documents for building
programs on OpenGPU (opengpu.library, SDL2.module and GL.module) with bebbo's
m68k-amigaos-gcc, on the PC or on the Amiga.

    tools/make_sdk.py --build BUILD --gl GL_OUT [--mesa MESA_SRC] [--version 0.6] OUT_DIR [--archives]

BUILD    library/build.sh's output (the SDL 2 libraries and headers, its
         sdl2-work/ with the pinned sources' notices, libminigl.a, libmgl.a)
GL_OUT   library/modules/gl/module/build.sh's output (libGL.a, include/gla)
MESA_SRC Mesa's source tree, for GL's and Khronos's headers; default: SRC
         in GL_WORK's gla-link.env (GL_WORK, default the GL module's build/)

It makes OUT_DIR/OpenGPU-SDK-VERSION/ and, with --archives, its .tar.gz
(for cross-compiling on the PC) and .lha (for the Amiga) beside it.
Everything in the kit is redistributable: each third-party header is
checked for its licence (MIT, Zlib or Khronos's) and the build stops if one
isn't; the notices go in Licences/. Nothing here is committed: the kit is
build output.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "sdk"
SDL_VERSION = "2.32.10"
SAT_VERSIONS = {"SDL2_image": "2.8.12", "SDL2_mixer": "2.8.2", "SDL2_ttf": "2.24.0", "SDL2_net": "2.4.0"}
# The CPU SDL2.module and the libraries are built for (library/modules/sdl2).
CPU = "-m68040 -m68881"

# The Team's headers in include/, by directory (all MIT, Dalsin Limited).
OWN_INCLUDE = ("opengpu", "openrtg", "proto", "inline", "clib", "libraries", "Warp3D", "mgl")
# GL's headers from Mesa: Mesa's gl.h (MIT) and Khronos's (MIT; the two
# platform headers Apache 2.0). GLX, WGL and GLES 1 (which GLA doesn't
# give) stay out, and so does GLES3/gl3ext.h, an empty header whose licence
# (SGI Free Software B 2.0) is named but not given.
MESA_HEADERS = ("GL/gl.h", "GL/glext.h", "GL/glcorearb.h",
                "GLES2/gl2.h", "GLES2/gl2ext.h", "GLES2/gl2platform.h",
                "GLES3/gl3.h", "GLES3/gl31.h", "GLES3/gl32.h", "GLES3/gl3platform.h",
                "KHR/khrplatform.h")
LIBS = ("libSDL2.a", "libSDL2_test.a", "libSDL2_image.a", "libSDL2_mixer.a", "libSDL2_ttf.a",
        "libSDL2_net.a", "libminigl.a", "libmgl.a")

APACHE_MARK = re.compile(r"SPDX-License-Identifier:\s*Apache-2\.0")
APACHE_TEXT = Path("/usr/share/common-licenses/Apache-2.0")
# SDL's, with no notice of its own (SDL's LICENSE.txt covers it).
SDL_NO_NOTICE = ("SDL_revision.h",)
MIT_MARK = re.compile(r"SPDX-License-Identifier:\s*MIT|Permission is hereby granted, free of charge")
ZLIB_MARK = re.compile(r"This software is provided 'as-is', without any express or implied")


def die(msg: str) -> None:
    sys.exit("make_sdk: " + msg)


def licence_of(path: Path) -> str:
    """MIT, Zlib or Dalsin (the Team's MIT) from a header's own notice; '' if unclear."""
    head = path.read_text(encoding="latin-1", errors="replace")[:6000]
    if "Dalsin Limited" in head and ("MIT" in head or "SPDX-License-Identifier: MIT" in head):
        return "MIT (Dalsin Limited)"
    if ZLIB_MARK.search(head):
        return "Zlib"
    if MIT_MARK.search(head):
        return "MIT"
    if APACHE_MARK.search(head):
        return "Apache-2.0"
    return ""


def copy(src: Path, dst: Path) -> None:
    if not src.is_file():
        die(f"missing {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def comment_head(path: Path) -> str:
    """A source file's first comment block."""
    text = path.read_text(encoding="latin-1", errors="replace")
    m = re.search(r"/\*.*?\*/", text, re.S)
    return m.group(0) if m else ""


def tail_from(path: Path, marker: str) -> str:
    text = path.read_text(encoding="latin-1", errors="replace")
    i = text.rfind(marker)
    return text[i:] if i >= 0 else ""


def mesa_src(args) -> Path:
    if args.mesa:
        return Path(args.mesa)
    work = Path(os.environ.get("GL_WORK", ROOT / "library" / "modules" / "gl" / "build"))
    env = work / "mesa-build-m68k-amigaos" / "gla-link.env"
    if env.is_file():
        m = re.search(r"^SRC='([^']+)'", env.read_text(), re.M)
        if m:
            return Path(m.group(1))
    die("give --mesa (Mesa's source tree) or GL_WORK with a gla-link.env")


SDL2_CONFIG = r'''#!/bin/sh
# sdl2-config for SDL 2 on OpenGPU (AmigaOS 3.2): the OpenGPU developer kit.
# Copyright (c) 2026 Dalsin Limited. MIT licence (Licences/OpenGPU.txt).
# Prints the flags m68k-amigaos-gcc needs for SDL 2:
#   m68k-amigaos-gcc game.c -o Game $(sdl2-config --cflags --libs)
# It finds the kit from where it is: the kit's own bin/, or a stove's
# prefix/bin/ with the kit in prefix/m68k-amigaos/. For GCC 16 (the
# os32-gcc16 stove) --cflags adds -fno-tree-loop-distribute-patterns.
self=$0
while [ -L "$self" ]; do
    link=$(readlink "$self")
    case $link in /*) self=$link ;; *) self=$(dirname "$self")/$link ;; esac
done
prefix=$(CDPATH= cd -- "$(dirname -- "$self")/.." && pwd)
if [ -d "$prefix/m68k-amigaos/include/SDL2" ]; then
    includedir=$prefix/m68k-amigaos/include
    libdir=$prefix/m68k-amigaos/lib
else
    includedir=$prefix/include
    libdir=$prefix/lib
fi
cc=$prefix/bin/m68k-amigaos-gcc
[ -x "$cc" ] || cc=${CC:-m68k-amigaos-gcc}
cpu=${SDL2_CPU:-"@CPU@"}
# libnix. A build that picks it with -mcrt=nix20 instead (ACKitchen's stoves
# do) says SDL2_RUNTIME=-mcrt=nix20: GCC must not get both.
runtime=${SDL2_RUNTIME:-"-noixemul"}
extra=
case $("$cc" -dumpversion 2>/dev/null) in
    1[0-9]*) extra=" -fno-tree-loop-distribute-patterns" ;;
esac
usage() {
    echo "Usage: sdl2-config [--prefix[=DIR]] [--exec-prefix[=DIR]] [--version] [--cflags] [--libs] [--static-libs]"
    exit $1
}
[ $# -eq 0 ] && usage 1 1>&2
out=
while [ $# -gt 0 ]; do
    case $1 in
    --prefix=*|--exec-prefix=*) ;;
    --prefix|--exec-prefix) out="$out $prefix" ;;
    --version) out="$out @VERSION@" ;;
    --cflags) out="$out -I$includedir/SDL2 $runtime $cpu$extra" ;;
    --libs|--static-libs) out="$out $runtime $cpu -L$libdir -lSDL2 -lGL -lm" ;;
    *) usage 1 1>&2 ;;
    esac
    shift
done
echo $out
'''

PC = {
    "sdl2": ("sdl2", "Simple DirectMedia Layer 2 on OpenGPU (AmigaOS 3.2, LIBS:OpenGPU/SDL2.module)",
             SDL_VERSION, "", "-L${libdir} -lSDL2 -lGL -lm", "-I${includedir}/SDL2"),
    "SDL2_image": ("SDL2_image", "Image loading for SDL 2", SAT_VERSIONS["SDL2_image"], "sdl2",
                   "-L${libdir} -lSDL2_image", "-I${includedir}/SDL2"),
    "SDL2_mixer": ("SDL2_mixer", "Sound and music mixing for SDL 2", SAT_VERSIONS["SDL2_mixer"], "sdl2",
                   "-L${libdir} -lSDL2_mixer", "-I${includedir}/SDL2"),
    "SDL2_ttf": ("SDL2_ttf", "TrueType fonts for SDL 2 (FreeType built in)", SAT_VERSIONS["SDL2_ttf"], "sdl2",
                 "-L${libdir} -lSDL2_ttf", "-I${includedir}/SDL2"),
    "SDL2_net": ("SDL2_net", "Networking for SDL 2 (bsdsocket.library)", SAT_VERSIONS["SDL2_net"], "sdl2",
                 "-L${libdir} -lSDL2_net -lsocket", "-I${includedir}/SDL2"),
    "gl": ("gl", "OpenGL and GLA on OpenGPU (AmigaOS 3.2, LIBS:OpenGPU/GL.module)", "26.2.4", "",
           "-L${libdir} -lGL -lm", "-I${includedir}"),
    "glesv2": ("glesv2", "OpenGL ES 2 and 3 on OpenGPU (LIBS:OpenGPU/GL.module)", "26.2.4", "",
               "-L${libdir} -lGL -lm", "-I${includedir}"),
}


def pc_text(name: str) -> str:
    n, desc, ver, req, libs, cflags = PC[name]
    return ("# OpenGPU developer kit. The kit's prefix is two levels up from this file:\n"
            "# the kit's own, or a stove's prefix/m68k-amigaos.\n"
            "prefix=${pcfiledir}/../..\nexec_prefix=${prefix}\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\n"
            f"Name: {n}\nDescription: {desc}\nVersion: {ver}\n"
            + (f"Requires: {req}\n" if req else "")
            # The CPU and runtime flags come once, from sdl2 (or gl) itself.
            + (f"Libs: {libs}\nCflags: {cflags}\n" if req else f"Libs: -noixemul {CPU} {libs}\nCflags: {cflags} -noixemul {CPU}\n"))


CMAKE_SDL2 = r'''# SDL 2 on OpenGPU (AmigaOS 3.2) for CMake: find_package(SDL2) gives
# SDL2::SDL2, SDL2::SDL2main (empty: SDL needs no main of its own here),
# SDL2::SDL2-static (the same), SDL2_INCLUDE_DIRS and SDL2_LIBRARIES.
# OpenGPU developer kit; the kit is three levels up (lib/cmake/SDL2).
get_filename_component(_ogpu_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SDL2_PREFIX "${_ogpu_prefix}")
set(SDL2_INCLUDE_DIR "${_ogpu_prefix}/include/SDL2")
set(SDL2_INCLUDE_DIRS "${_ogpu_prefix}/include;${_ogpu_prefix}/include/SDL2")
set(SDL2_LIBDIR "${_ogpu_prefix}/lib")
set(SDL2_LIBRARIES "${_ogpu_prefix}/lib/libSDL2.a;${_ogpu_prefix}/lib/libGL.a;-lm")
set(SDL2_VERSION "@VERSION@")
set(SDL2_FOUND TRUE)
if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 STATIC IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${_ogpu_prefix}/lib/libSDL2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        INTERFACE_COMPILE_OPTIONS "-noixemul;@CPU_LIST@"
        INTERFACE_LINK_OPTIONS "-noixemul;@CPU_LIST@"
        INTERFACE_LINK_LIBRARIES "${_ogpu_prefix}/lib/libGL.a;m")
    add_library(SDL2::SDL2-static INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2-static PROPERTIES INTERFACE_LINK_LIBRARIES SDL2::SDL2)
    add_library(SDL2::SDL2main INTERFACE IMPORTED)
    add_library(SDL2::SDL2test STATIC IMPORTED)
    set_target_properties(SDL2::SDL2test PROPERTIES
        IMPORTED_LOCATION "${_ogpu_prefix}/lib/libSDL2_test.a" INTERFACE_LINK_LIBRARIES SDL2::SDL2)
endif()
'''

CMAKE_SAT = r'''# @NAME@ for SDL 2 on OpenGPU (AmigaOS 3.2): find_package(@NAME@) gives
# @NAME@::@NAME@ and @NAME@::@NAME@-static. OpenGPU developer kit.
get_filename_component(_ogpu_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include(CMakeFindDependencyMacro)
find_dependency(SDL2 CONFIG PATHS "${_ogpu_prefix}/lib/cmake/SDL2" NO_DEFAULT_PATH)
set(@NAME@_VERSION "@VERSION@")
set(@NAME@_FOUND TRUE)
if(NOT TARGET @NAME@::@NAME@)
    add_library(@NAME@::@NAME@ STATIC IMPORTED)
    set_target_properties(@NAME@::@NAME@ PROPERTIES
        IMPORTED_LOCATION "${_ogpu_prefix}/lib/lib@NAME@.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_ogpu_prefix}/include/SDL2"
        INTERFACE_LINK_LIBRARIES "SDL2::SDL2@EXTRA@")
    add_library(@NAME@::@NAME@-static INTERFACE IMPORTED)
    set_target_properties(@NAME@::@NAME@-static PROPERTIES INTERFACE_LINK_LIBRARIES @NAME@::@NAME@)
endif()
'''


def make_kit(args) -> Path:
    build, gl = Path(args.build).resolve(), Path(args.gl).resolve()
    work = build / "sdl2-work"
    mesa = mesa_src(args)
    kit = Path(args.out).resolve() / f"OpenGPU-SDK-{args.version}"
    if kit.exists():
        die(f"{kit} is there already: choose another OUT_DIR, or move it away")
    inc, lib = kit / "include", kit / "lib"
    notices: list[tuple[str, str]] = []     # (what, licence) for Licences/README
    apache: list[str] = []                  # headers under Apache 2.0 (Khronos's platform headers)

    # 1. The Team's headers: opengpu.library, OpenRTG, Warp3D, MiniGL.
    for d in OWN_INCLUDE:
        for f in sorted((ROOT / "include" / d).rglob("*.h")):
            if licence_of(f) != "MIT (Dalsin Limited)":
                die(f"{f}: not the Team's MIT notice")
            copy(f, inc / f.relative_to(ROOT / "include"))
    # The libraries' SFD files, for other compilers' stubs and pragmas.
    for f in (ROOT / "library" / "opengpu" / "opengpu_lib.sfd", ROOT / "library" / "openrtg_lib.sfd",
              ROOT / "library" / "warp3d" / "warp3d_lib.sfd"):
        copy(f, kit / "sfd" / f.name)
    for f in sorted((gl / "include" / "gla").rglob("*.h")):     # GLA, OpenGPU's GL interface
        copy(f, inc / "gla" / f.relative_to(gl / "include" / "gla"))

    # 2. SDL 2's headers and the satellites', all Zlib. SDL_config.h is the
    #    Team's SDL_config_amigaos.h; the other platforms' configs stay out.
    sdlinc = build / "include" / "SDL2"
    for f in sorted(sdlinc.glob("*.h")):
        if f.name.startswith("SDL_config_") and f.name != "SDL_config_amigaos.h":
            continue
        if f.name == "SDL_config.h":
            continue
        lic = licence_of(f)
        if f.name in SDL_NO_NOTICE:
            lic = "Zlib"
        if lic not in ("Zlib", "MIT", "MIT (Dalsin Limited)", "Apache-2.0"):
            die(f"{f}: licence unclear")
        if lic == "Apache-2.0" or APACHE_MARK.search(f.read_text(encoding="latin-1")):
            apache.append("SDL2/" + f.name)
        copy(f, inc / "SDL2" / f.name)
    copy(sdlinc / "SDL_config_amigaos.h", inc / "SDL2" / "SDL_config.h")
    for need in ("SDL.h", "SDL_image.h", "SDL_mixer.h", "SDL_ttf.h", "SDL_net.h", "SDL_config.h"):
        if not (inc / "SDL2" / need).is_file():
            die(f"no {need} in {sdlinc}")

    # 3. GL's headers: Mesa's and Khronos's, MIT.
    for rel in MESA_HEADERS:
        f = mesa / "include" / rel
        lic = licence_of(f)
        if lic not in ("MIT", "Apache-2.0"):
            die(f"{f}: licence unclear")
        if lic == "Apache-2.0":
            apache.append(rel)
        copy(f, inc / rel)

    # 4. The libraries. One set serves both stoves: the GCC 16 builds link
    #    with GCC 6.5's libnix too (same libnix, a.out objects, FPU ABI).
    for name in LIBS:
        copy(build / name, lib / name)
    copy(gl / "libGL.a", lib / "libGL.a")
    # libSDL2main.a: SDL needs no main of its own on the Amiga; an empty one
    # for build systems that link -lSDL2main anyway.
    ar = shutil.which("m68k-amigaos-ar") or str(Path(args.stove) / "bin" / "m68k-amigaos-ar")
    subprocess.run([ar, "rcs", str(lib / "libSDL2main.a")], check=True)

    # 5. sdl2-config, pkg-config and CMake files.
    cfg = kit / "bin" / "sdl2-config"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text(SDL2_CONFIG.replace("@CPU@", CPU).replace("@VERSION@", SDL_VERSION))
    cfg.chmod(0o755)
    for name in PC:
        p = lib / "pkgconfig" / f"{name}.pc"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(pc_text(name))
    cm = lib / "cmake" / "SDL2"
    cm.mkdir(parents=True, exist_ok=True)
    body = CMAKE_SDL2.replace("@VERSION@", SDL_VERSION).replace("@CPU_LIST@", CPU.replace(" ", ";"))
    (cm / "SDL2Config.cmake").write_text(body)
    (cm / "sdl2-config.cmake").write_text(body)
    for name, ver in SAT_VERSIONS.items():
        d = lib / "cmake" / name
        d.mkdir(parents=True, exist_ok=True)
        extra = ";-lsocket" if name == "SDL2_net" else ""
        text = CMAKE_SAT.replace("@NAME@", name).replace("@VERSION@", ver).replace("@EXTRA@", extra)
        (d / f"{name}Config.cmake").write_text(text)
        (d / f"{name.lower()}-config.cmake").write_text(text)

    # 6. Examples, the readme and the reference.
    shutil.copytree(SDK / "examples", kit / "examples")
    copy(SDK / "README", kit / "README")
    copy(SDK / "AutoDocs-OpenGPU.md", kit / "AutoDocs-OpenGPU.md")

    # 7. Notices.
    L = kit / "Licences"
    L.mkdir()
    copy(ROOT / "LICENSE", L / "OpenGPU.txt")
    notices.append(("OpenGPU, OpenRTG, Warp3D, MiniGL and GLA headers; libGL.a, libminigl.a, libmgl.a; libSDL2.a's SDL2_gl.o; sdl2-config, the pkg-config and CMake files; the examples", "MIT, Dalsin Limited (OpenGPU.txt)"))
    sdl = work / f"SDL2-{SDL_VERSION}"
    copy(sdl / "LICENSE.txt", L / "SDL2.txt")
    notices.append((f"SDL {SDL_VERSION}: SDL2/ headers (SDL_config.h is the Team's SDL_config_amigaos.h, MIT), libSDL2.a (SDL's dynamic API, altered by the Team), libSDL2_test.a", "Zlib (SDL2.txt)"))
    copy(sdl / "LICENSE.libSDL2-amigaos3.txt", L / "libSDL2-amigaos3.txt")
    notices.append(("libSDL2-amigaos3 0.7.0: the Amiga back ends inside SDL2.module", "Zlib (libSDL2-amigaos3.txt)"))
    for name, ver in SAT_VERSIONS.items():
        copy(work / f"{name}-{ver}" / "LICENSE.txt", L / f"{name}.txt")
        notices.append((f"{name} {ver}: {name.replace('SDL2_', 'SDL_')}.h, lib{name}.a", f"Zlib ({name}.txt)"))
    img = work / f"SDL2_image-{SAT_VERSIONS['SDL2_image']}" / "src"
    bundled = ["Inside libSDL2_image.a, from SDL2_image's own source.\n",
               "== stb_image (public domain or MIT)\n", tail_from(img / "stb_image.h", "This software is available under 2 licenses"),
               "\n== nanosvg and nanosvgrast (Zlib; parts from Anti-Grain Geometry)\n", comment_head(img / "nanosvg.h"),
               "\n", comment_head(img / "nanosvgrast.h"),
               "\n== QOI (MIT)\n", re.search(r"-- LICENSE: The MIT License.*?\*/", (img / "qoi.h").read_text(encoding="latin-1"), re.S).group(0),
               "\n== miniz (public domain, the Unlicense)\n", tail_from(img / "miniz.h", "This is free and unencumbered software"),
               "\n== tiny_jpeg (public domain)\n", comment_head(img / "tiny_jpeg.h")]
    (L / "SDL2_image-bundled.txt").write_text("".join(bundled), encoding="latin-1")
    notices.append(("stb_image, nanosvg, QOI, miniz, tiny_jpeg inside libSDL2_image.a", "public domain, Zlib, MIT (SDL2_image-bundled.txt)"))
    mix = work / f"SDL2_mixer-{SAT_VERSIONS['SDL2_mixer']}" / "src" / "codecs"
    bundled = ["Inside libSDL2_mixer.a, from SDL2_mixer's own source.\n",
               "== stb_vorbis (public domain or MIT)\n", tail_from(mix / "stb_vorbis" / "stb_vorbis.h", "This software is available under 2 licenses"),
               "\n== minimp3 (CC0)\n", (mix / "minimp3" / "LICENSE").read_text(encoding="latin-1"),
               "\n== dr_flac (public domain or MIT-0)\n", (mix / "dr_libs" / "LICENSE").read_text(encoding="latin-1")]
    (L / "SDL2_mixer-bundled.txt").write_text("".join(bundled), encoding="latin-1")
    copy(mix / "timidity" / "COPYING", L / "Timidity.txt")
    (L / "Timidity-source.txt").write_text(
        "libSDL2_mixer.a contains Timidity (MIDI), from SDL2_mixer 2.8.2's src/codecs/timidity,\n"
        "unchanged by the Team, under the Artistic License in Timidity.txt. Its source is in\n"
        "SDL2_mixer 2.8.2: https://github.com/libsdl-org/SDL_mixer/releases/tag/release-2.8.2\n")
    notices.append(("stb_vorbis, minimp3, dr_flac, Timidity inside libSDL2_mixer.a",
                    "public domain, CC0, MIT-0, Artistic (SDL2_mixer-bundled.txt, Timidity.txt)"))
    ft = work / "freetype-2.14.3" / "docs"
    copy(ft / "FTL.TXT", L / "FreeType-FTL.txt")
    notices.append(("FreeType 2.14.3 inside libSDL2_ttf.a", "FreeType Licence (FreeType-FTL.txt)"))
    xmp = (work / "libxmp-4.7.3" / "README").read_text(encoding="latin-1")
    i = xmp.find("LICENSE")
    (L / "libxmp.txt").write_text(xmp[i:] if i >= 0 else xmp, encoding="latin-1")
    notices.append(("libxmp 4.7.3 inside libSDL2_mixer.a", "MIT (libxmp.txt)"))
    lic = mesa / "docs" / "license.rst"
    copy(lic, L / "Mesa.txt")
    notices.append(("Mesa 26.2.4: GL/gl.h", "MIT (Mesa.txt)"))
    (L / "Khronos.txt").write_text(
        "Khronos's headers. KHR/khrplatform.h carries the MIT licence in full:\n\n"
        + comment_head(mesa / "include" / "KHR" / "khrplatform.h") + "\n\n"
        "GL/glext.h, GL/glcorearb.h, GLES2/ and GLES3/ name it (SPDX MIT), each with:\n\n"
        + comment_head(mesa / "include" / "GL" / "glext.h") + "\n")
    notices.append(("Khronos: GL/glext.h, GL/glcorearb.h, GLES2/, GLES3/, KHR/", "MIT (Khronos.txt)"))
    if apache:
        if not APACHE_TEXT.is_file():
            die(f"no Apache 2.0 text at {APACHE_TEXT}")
        copy(APACHE_TEXT, L / "Apache-2.0.txt")
        notices.append(("Khronos's platform headers: " + ", ".join(apache), "Apache 2.0 (Apache-2.0.txt), Copyright The Khronos Group Inc."))
    with open(L / "README", "w") as fh:
        fh.write("What in the OpenGPU developer kit comes from where, and its licence.\n\n")
        for what, licence in notices:
            fh.write(f"- {what}\n    {licence}\n")

    # 8. A list of every file with its sha256.
    lines = []
    for f in sorted(p for p in kit.rglob("*") if p.is_file()):
        lines.append(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.relative_to(kit)}")
    (kit / "SHA256SUMS").write_text("\n".join(lines) + "\n")
    return kit


def archives(kit: Path) -> list[Path]:
    out = []
    tgz = kit.with_name(kit.name + ".tar.gz")
    for a in (tgz, kit.with_name(kit.name + ".lha")):
        if a.exists():
            die(f"{a} is there already")
    with tarfile.open(tgz, "w:gz") as t:
        t.add(kit, arcname=kit.name)
    out.append(tgz)
    lha = kit.with_name(kit.name + ".lha")
    # jlha makes archives; on some systems "lha" is lhasa, which only reads them
    tool = shutil.which("jlha") or shutil.which("lha")
    if tool:
        subprocess.run([tool, "-aq", lha.name, kit.name], cwd=kit.parent, check=True)
        out.append(lha)
    else:
        print("make_sdk: no lha on PATH; .lha not made")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--build", default=str(ROOT / "build"))
    ap.add_argument("--gl", default=str(ROOT / "build" / "gl"))
    ap.add_argument("--mesa")
    ap.add_argument("--version", default="0.6")
    ap.add_argument("--stove", default=os.environ.get("STOVE16", str(Path.home() / "AmigaChrome/stoves/os32-gcc16/prefix")),
                    help="a stove prefix, for m68k-amigaos-ar when it isn't on PATH")
    ap.add_argument("--archives", action="store_true", help="also make the .tar.gz and .lha")
    args = ap.parse_args()
    kit = make_kit(args)
    size = sum(f.stat().st_size for f in kit.rglob("*") if f.is_file())
    print(f"kit: {kit} ({sum(1 for f in kit.rglob('*') if f.is_file())} files, {size / 1e6:.1f} MB)")
    if args.archives:
        for a in archives(kit):
            print(f"archive: {a} ({a.stat().st_size / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
