#!/usr/bin/env python3
"""Create OpenTensor AmigaOS 3.5+ GlowIcons.

The output is a complete icon set for the OpenTensor drawer:

  OpenTensor.info                  drawer icon (placed beside the drawer)
  OpenTensor/OpenTensor.info       application icon
  OpenTensor/Launch.info           full-production IconX launcher
  OpenTensor/Launch-ActII.info     Act II IconX launcher

The artwork is original and generated deterministically with Pillow. The
binary .info files are written by the AmigaChrome GlowIcon writer so they
retain classic four-colour fallbacks as well as normal and selected images.
"""
from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SIZE = 64
SCALE = 4
HI = SIZE * SCALE


def load_writer(path: Path):
    spec = importlib.util.spec_from_file_location("amigaicon", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load icon writer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rgba_canvas() -> Image.Image:
    return Image.new("RGBA", (HI, HI), (0, 0, 0, 0))


def rounded_panel(img: Image.Image, selected: bool, inset: int = 9) -> None:
    draw = ImageDraw.Draw(img, "RGBA")
    i = inset * SCALE
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow, "RGBA")
    glow_colour = (34, 184, 255, 115 if selected else 54)
    gd.rounded_rectangle(
        (i - 4 * SCALE, i - 4 * SCALE, HI - i + 4 * SCALE, HI - i + 4 * SCALE),
        radius=11 * SCALE,
        fill=glow_colour,
    )
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(5 * SCALE)))
    top = (13, 25, 45, 245) if not selected else (14, 39, 67, 250)
    bottom = (2, 6, 18, 248)
    draw.rounded_rectangle(
        (i, i, HI - i, HI - i),
        radius=9 * SCALE,
        fill=bottom,
        outline=(92, 150, 190, 235),
        width=SCALE,
    )
    draw.rounded_rectangle(
        (i + SCALE, i + SCALE, HI - i - SCALE, HI // 2),
        radius=8 * SCALE,
        fill=top,
    )


def starfield(img: Image.Image, selected: bool, seed: int = 1, count: int = 44) -> None:
    draw = ImageDraw.Draw(img, "RGBA")
    value = seed & 0xFFFFFFFF
    for n in range(count):
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        px = 7 + (value & 0xFFFF) % 50
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        py = 6 + (value & 0xFFFF) % 50
        radius = (1 if n % 7 else 2) * SCALE
        alpha = 130 + (value & 0x7F)
        colour = (170, 220, 255, min(255, alpha + (40 if selected else 0)))
        draw.ellipse(
            (px * SCALE - radius, py * SCALE - radius,
             px * SCALE + radius, py * SCALE + radius),
            fill=colour,
        )


def tensor_grid(img: Image.Image, selected: bool, horizon: float = 42.0) -> None:
    draw = ImageDraw.Draw(img, "RGBA")
    cyan = (55, 190, 235, 170 if selected else 118)
    warm = (190, 180, 155, 96)
    hy = int(horizon * SCALE)
    for x in range(-8, 73, 8):
        draw.line((HI // 2, hy, x * SCALE, 62 * SCALE),
                  fill=cyan, width=max(1, SCALE // 2))
    for row in range(8):
        t = row / 8.0
        y = hy + int((1.0 - (1.0 - t) ** 2) * (20 * SCALE))
        draw.line((5 * SCALE, y, 59 * SCALE, y),
                  fill=warm if row & 1 else cyan,
                  width=max(1, SCALE // 2))


def draw_boing_sphere(img: Image.Image, cx: float, cy: float, radius: float,
                      selected: bool, rotation: float = 0.0) -> None:
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow, "RGBA")
    sd.ellipse(
        ((cx - radius * 1.05) * SCALE, (cy + radius * 0.72) * SCALE,
         (cx + radius * 1.05) * SCALE, (cy + radius * 1.17) * SCALE),
        fill=(0, 0, 0, 150),
    )
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(2 * SCALE)))

    halo = Image.new("RGBA", img.size, (0, 0, 0, 0))
    hd = ImageDraw.Draw(halo, "RGBA")
    hd.ellipse(
        ((cx - radius - 3) * SCALE, (cy - radius - 3) * SCALE,
         (cx + radius + 3) * SCALE, (cy + radius + 3) * SCALE),
        fill=(42, 198, 255, 130 if selected else 62),
    )
    img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(4 * SCALE)))

    pixels = img.load()
    rcx, rcy, rr = cx * SCALE, cy * SCALE, radius * SCALE
    xmin = max(0, int(rcx - rr - 2))
    xmax = min(HI, int(rcx + rr + 3))
    ymin = max(0, int(rcy - rr - 2))
    ymax = min(HI, int(rcy + rr + 3))
    light = (-0.48, -0.62, 0.62)
    for y in range(ymin, ymax):
        ny = (y + 0.5 - rcy) / rr
        for x in range(xmin, xmax):
            nx = (x + 0.5 - rcx) / rr
            q = nx * nx + ny * ny
            if q > 1.04:
                continue
            if q > 1.0:
                alpha = int(255 * max(0.0, (1.04 - q) / 0.04))
                nz = 0.0
            else:
                alpha = 255
                nz = math.sqrt(max(0.0, 1.0 - q))
            lon = math.atan2(nx, nz) + rotation
            lat = math.asin(max(-1.0, min(1.0, -ny)))
            check = (
                int(math.floor((lon + math.pi) / (math.pi / 4.0)))
                + int(math.floor((lat + math.pi / 2.0) / (math.pi / 6.0)))
            ) & 1
            base = (224, 38, 31) if check else (239, 239, 229)
            diffuse = max(0.0, nx * light[0] + ny * light[1] + nz * light[2])
            rim = (1.0 - max(0.0, nz)) ** 2
            shade = 0.42 + 0.58 * diffuse + (0.08 if selected else 0.0)
            red = min(255, int(base[0] * shade + 30 * rim))
            green = min(255, int(base[1] * shade + 60 * rim))
            blue = min(255, int(base[2] * shade + 90 * rim))
            if (nx + 0.38) ** 2 + (ny + 0.42) ** 2 < 0.025:
                red = min(255, red + 70)
                green = min(255, green + 70)
                blue = min(255, blue + 70)
            pixels[x, y] = (red, green, blue, alpha)


def tensor_rings(img: Image.Image, cx: float, cy: float, selected: bool,
                 count: int = 3) -> None:
    draw = ImageDraw.Draw(img, "RGBA")
    for n in range(count):
        rx = (21 + n * 4) * SCALE
        ry = (7 + n * 2) * SCALE
        offset = (n - 1) * 1.5 * SCALE
        colour = (46, 202, 255, (230 if selected else 155) - n * 28)
        draw.ellipse(
            (cx * SCALE - rx, cy * SCALE - ry + offset,
             cx * SCALE + rx, cy * SCALE + ry + offset),
            outline=colour,
            width=max(2, SCALE),
        )


def program_art(selected: bool) -> Image.Image:
    img = rgba_canvas()
    rounded_panel(img, selected, 5)
    starfield(img, selected, seed=0x0A0B0C0D, count=36)
    tensor_grid(img, selected, horizon=39)
    tensor_rings(img, 32, 31, selected, 3)
    draw_boing_sphere(img, 32, 29, 16, selected, rotation=0.35)
    draw = ImageDraw.Draw(img, "RGBA")
    for n in range(7):
        x = (12 + n * 6) * SCALE
        height = (2 + n % 3) * SCALE
        draw.rectangle(
            (x, 52 * SCALE - height, x + SCALE, 52 * SCALE),
            fill=(255, 190, 70, 200 if selected else 145),
        )
    return img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def drawer_art(selected: bool) -> Image.Image:
    img = rgba_canvas()
    starfield(img, selected, seed=0x12345678, count=24)
    tensor_grid(img, selected, horizon=34)
    draw = ImageDraw.Draw(img, "RGBA")
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow, "RGBA")
    gd.polygon(
        [(7 * SCALE, 28 * SCALE), (23 * SCALE, 15 * SCALE),
         (56 * SCALE, 20 * SCALE), (48 * SCALE, 35 * SCALE)],
        fill=(35, 186, 255, 115 if selected else 50),
    )
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SCALE)))
    draw.polygon(
        [(8 * SCALE, 31 * SCALE), (22 * SCALE, 17 * SCALE),
         (56 * SCALE, 22 * SCALE), (48 * SCALE, 37 * SCALE)],
        fill=(23, 45, 69, 255),
        outline=(119, 175, 205, 255),
    )
    draw.polygon(
        [(8 * SCALE, 31 * SCALE), (48 * SCALE, 37 * SCALE),
         (55 * SCALE, 55 * SCALE), (13 * SCALE, 59 * SCALE)],
        fill=(45, 70, 92, 255),
        outline=(147, 173, 191, 255),
    )
    draw.polygon(
        [(13 * SCALE, 37 * SCALE), (46 * SCALE, 41 * SCALE),
         (50 * SCALE, 52 * SCALE), (17 * SCALE, 54 * SCALE)],
        fill=(3, 9, 22, 255),
    )
    draw.line((14 * SCALE, 57 * SCALE, 54 * SCALE, 53 * SCALE),
              fill=(210, 220, 225, 230), width=SCALE)
    draw.rounded_rectangle(
        (27 * SCALE, 54 * SCALE, 39 * SCALE, 57 * SCALE),
        radius=SCALE,
        fill=(25, 31, 40, 255),
        outline=(170, 185, 196, 255),
    )
    tensor_rings(img, 33, 31, selected, 2)
    draw_boing_sphere(img, 33, 29, 11, selected, rotation=0.7)
    return img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def launch_art(selected: bool) -> Image.Image:
    img = program_art(selected).resize((HI, HI), Image.Resampling.NEAREST)
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay, "RGBA")
    draw.ellipse(
        (18 * SCALE, 18 * SCALE, 46 * SCALE, 46 * SCALE),
        fill=(1, 6, 15, 185),
        outline=(84, 211, 255, 230),
        width=SCALE,
    )
    draw.polygon(
        [(28 * SCALE, 24 * SCALE), (28 * SCALE, 40 * SCALE),
         (41 * SCALE, 32 * SCALE)],
        fill=(248, 247, 235, 245),
    )
    if selected:
        overlay = overlay.filter(ImageFilter.GaussianBlur(int(0.35 * SCALE)))
    img.alpha_composite(overlay)
    return img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def act2_art(selected: bool) -> Image.Image:
    img = rgba_canvas()
    rounded_panel(img, selected, 5)
    starfield(img, selected, seed=0xA2A2A2A2, count=28)
    draw = ImageDraw.Draw(img, "RGBA")
    for n in range(8):
        pad = (7 + n * 3) * SCALE
        alpha = 210 - n * 18
        draw.rectangle(
            (pad, pad + 2 * SCALE, HI - pad, HI - pad - 2 * SCALE),
            outline=(42, 190, 245, alpha),
            width=SCALE,
        )
    for x in (24, 37):
        draw.rounded_rectangle(
            (x * SCALE, 17 * SCALE, (x + 5) * SCALE, 46 * SCALE),
            radius=SCALE,
            fill=(245, 188, 61, 245 if selected else 220),
            outline=(255, 235, 160, 255),
            width=SCALE,
        )
    draw_boing_sphere(img, 32, 49, 8, selected, rotation=1.1)
    return img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def save_glowicon(writer, path: Path, kind: int, normal: Image.Image,
                  selected: Image.Image, *, default_tool: str | None = None,
                  stack: int = 8192,
                  tool_types: list[str] | None = None) -> None:
    icon = writer.Icon(
        type=kind,
        width=SIZE,
        height=SIZE,
        default_tool=default_tool,
        tool_types=tool_types or [],
        stack=stack,
        normal=normal,
        selected=selected,
    )
    data = writer.glowicon(icon)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    parsed = writer.read(data)
    if parsed.get("type") != kind:
        raise RuntimeError(f"{path}: wrong icon type {parsed.get('type')} != {kind}")
    if tuple(parsed.get("face") or ()) != (SIZE, SIZE):
        raise RuntimeError(f"{path}: missing 64x64 GlowIcon face")
    if len(parsed.get("pictures") or []) < 2:
        raise RuntimeError(f"{path}: normal/selected GlowIcon images were not written")


def preview(path: Path, items: list[tuple[str, Image.Image, Image.Image]]) -> None:
    font = ImageFont.load_default()
    cell_width, cell_height = 150, 96
    canvas = Image.new(
        "RGBA", (cell_width * len(items), cell_height), (20, 23, 30, 255)
    )
    draw = ImageDraw.Draw(canvas)
    for index, (label, normal, selected) in enumerate(items):
        x = index * cell_width
        canvas.alpha_composite(normal, (x + 7, 8))
        canvas.alpha_composite(selected, (x + 77, 8))
        draw.text((x + 7, 76), label, font=font, fill=(235, 238, 242, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--writer", required=True, type=Path,
                        help="path to tools/icons/amigaicon.py")
    parser.add_argument("--out", required=True, type=Path,
                        help="package root containing the OpenTensor drawer")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    writer = load_writer(args.writer)
    drawer_normal, drawer_selected = drawer_art(False), drawer_art(True)
    app_normal, app_selected = program_art(False), program_art(True)
    launch_normal, launch_selected = launch_art(False), launch_art(True)
    act2_normal, act2_selected = act2_art(False), act2_art(True)

    save_glowicon(
        writer, args.out / "OpenTensor.info", writer.WBDRAWER,
        drawer_normal, drawer_selected, stack=8192,
    )
    save_glowicon(
        writer, args.out / "OpenTensor" / "OpenTensor.info", writer.WBTOOL,
        app_normal, app_selected, stack=1_000_000,
        tool_types=["MESA=VIRGL", "AUDIO=AHI", "DISPLAY=OPENRTG"],
    )
    save_glowicon(
        writer, args.out / "OpenTensor" / "Launch.info", writer.WBPROJECT,
        launch_normal, launch_selected, default_tool="C:IconX", stack=1_000_000,
    )
    save_glowicon(
        writer, args.out / "OpenTensor" / "Launch-ActII.info", writer.WBPROJECT,
        act2_normal, act2_selected, default_tool="C:IconX", stack=1_000_000,
    )

    if args.preview:
        preview(args.preview, [
            ("Drawer", drawer_normal, drawer_selected),
            ("OpenTensor", app_normal, app_selected),
            ("Full Demo", launch_normal, launch_selected),
            ("Act II", act2_normal, act2_selected),
        ])

    for path in (
        args.out / "OpenTensor.info",
        args.out / "OpenTensor" / "OpenTensor.info",
        args.out / "OpenTensor" / "Launch.info",
        args.out / "OpenTensor" / "Launch-ActII.info",
    ):
        print(f"{path}: {path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
