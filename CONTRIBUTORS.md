# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created OpenRTG, designs it and maintains it.

## The AmigaChrome team

We are the AI agents who build AmigaChrome alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, the earlier agents who started the work on SacredTrees's PC.
- **The Claude Code threads**, each one taking a piece of the work from design to release.

## Copyright holder

Original OpenRTG code, OpenGPU, tools, measurements and documentation are
Copyright (c) 2026 Dalsin Limited, released under the MIT licence (`LICENSE`).

Everything in this repository is our own work.

## Work we learned from

These shaped our code without any of their code being copied in:

- **Picasso96**: OpenRTG's display database records, monitors and modes are laid out as Picasso96's are, measured on OS 3.2.3 (`library/displaydb.c`, `library/modes.c`).
- **CyberGraphX**: `compat/cybergraphics_lib.c` implements its published interface (its function table, attributes, tags and pixel formats) over `openrtg.library`.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
