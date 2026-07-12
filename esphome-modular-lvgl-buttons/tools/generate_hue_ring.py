#!/usr/bin/env python3
"""
Generate a color-ring SVG for use as an LVGL color picker background.

Two modes:
  --mode hue     360° HSV hue rainbow (default)
  --mode kelvin  Blackbody color temperature 1000–8000K (Tanner Helland approx)

The ring is drawn as N filled wedge `<path>` elements rather than radial
stroked lines, with each wedge extended by a small angular overlap. This
eliminates the sub-pixel anti-aliasing gaps that show up as thin black
spokes after rasterization.

Center and outside of the ring are transparent. ESPHome's image: component
rasterizes this SVG to PNG at compile time via resvg_py, using the
`resize:` dimensions from the YAML.

Usage:
    python tools/generate_hue_ring.py \\
        --mode hue \\
        --out esphome-modular-lvgl-buttons/assets/images/hue_ring.svg

    python tools/generate_hue_ring.py \\
        --mode kelvin --min 1000 --max 8000 \\
        --out esphome-modular-lvgl-buttons/assets/images/kelvin_ring.svg
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path


# ----------------------------- color functions -----------------------------

def hsl_to_hex(h_deg: float) -> str:
    """HSL(h, 100%, 50%) → #RRGGBB."""
    h = (h_deg % 360) / 60.0
    c = 1.0
    x = c * (1 - abs((h % 2) - 1))
    if   0 <= h < 1: r, g, b = c, x, 0
    elif 1 <= h < 2: r, g, b = x, c, 0
    elif 2 <= h < 3: r, g, b = 0, c, x
    elif 3 <= h < 4: r, g, b = 0, x, c
    elif 4 <= h < 5: r, g, b = x, 0, c
    else:            r, g, b = c, 0, x
    return "#{:02X}{:02X}{:02X}".format(int(r * 255), int(g * 255), int(b * 255))


def _clamp8(x: float) -> int:
    return 0 if x < 0 else (255 if x > 255 else int(x))


def kelvin_to_hex(kelvin: float) -> str:
    """
    Blackbody color temperature → #RRGGBB using Tanner Helland's
    well-known approximation. Reasonable for ~1000–40000K.
    Reference: https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
    """
    t = kelvin / 100.0
    # Red
    if t <= 66:
        r = 255.0
    else:
        r = 329.698727446 * ((t - 60) ** -0.1332047592)
    # Green
    if t <= 66:
        g = 99.4708025861 * math.log(t) - 161.1195681661
    else:
        g = 288.1221695283 * ((t - 60) ** -0.0755148492)
    # Blue
    if t >= 66:
        b = 255.0
    elif t <= 19:
        b = 0.0
    else:
        b = 138.5177312231 * math.log(t - 10) - 305.0447927307
    return "#{:02X}{:02X}{:02X}".format(_clamp8(r), _clamp8(g), _clamp8(b))


# ------------------------------- geometry -------------------------------

def _wedge_path(cx: float, cy: float,
                r_in: float, r_out: float,
                a_start: float, a_end: float) -> str:
    """SVG path 'd' string for a single annular wedge."""
    x_in_s  = cx + r_in  * math.cos(a_start); y_in_s  = cy + r_in  * math.sin(a_start)
    x_out_s = cx + r_out * math.cos(a_start); y_out_s = cy + r_out * math.sin(a_start)
    x_out_e = cx + r_out * math.cos(a_end);   y_out_e = cy + r_out * math.sin(a_end)
    x_in_e  = cx + r_in  * math.cos(a_end);   y_in_e  = cy + r_in  * math.sin(a_end)
    # Both arcs are short (one segment) so large-arc-flag = 0.
    # Outer arc sweeps clockwise (sweep-flag 1), inner sweeps back (sweep-flag 0).
    return (
        f"M {x_in_s:.3f},{y_in_s:.3f} "
        f"L {x_out_s:.3f},{y_out_s:.3f} "
        f"A {r_out:.3f},{r_out:.3f} 0 0 1 {x_out_e:.3f},{y_out_e:.3f} "
        f"L {x_in_e:.3f},{y_in_e:.3f} "
        f"A {r_in:.3f},{r_in:.3f} 0 0 0 {x_in_s:.3f},{y_in_s:.3f} Z"
    )


def build_svg(viewbox: int, ring_thickness: int, segments: int,
              color_for_segment) -> str:
    """
    viewbox:        SVG viewBox edge length (square). Actual render size is
                    set by ESPHome's `resize:`; viewBox is just a coordinate
                    system, so a large round number gives good precision.
    ring_thickness: thickness of the ring in viewBox units.
    segments:       number of wedges (e.g. 360 = 1° each).
    color_for_segment(frac, angle_deg) -> "#RRGGBB"
    """
    cx = cy = viewbox / 2
    r_out = viewbox / 2
    r_in = r_out - ring_thickness

    seg_angle = 2 * math.pi / segments
    # Extend every wedge by 25% of its angular width on each side. The
    # overlap is fully covered by neighbouring wedges, but guarantees no
    # sub-pixel AA gaps after rasterization.
    overlap = seg_angle * 0.25

    paths = []
    for i in range(segments):
        # 0° at top of the ring (12 o'clock), increasing clockwise.
        a_mid_deg = (i + 0.5) * (360 / segments)
        a_start = (i * seg_angle)       - math.pi / 2 - overlap
        a_end   = ((i + 1) * seg_angle) - math.pi / 2 + overlap
        color = color_for_segment(i / segments, a_mid_deg)
        paths.append(
            f'    <path d="{_wedge_path(cx, cy, r_in, r_out, a_start, a_end)}" '
            f'fill="{color}"/>'
        )

    body = "\n".join(paths)
    return (
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'viewBox="0 0 {viewbox} {viewbox}" '
        f'width="{viewbox}" height="{viewbox}">\n'
        f'  <g shape-rendering="geometricPrecision" stroke="none">\n'
        f'{body}\n'
        f'  </g>\n'
        f'</svg>\n'
    )


# --------------------------------- CLI ---------------------------------

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True, type=Path, help="Output SVG path")
    p.add_argument("--mode", choices=("hue", "kelvin"), default="hue",
                   help="Color scheme around the ring")
    p.add_argument("--min", type=float, default=1000.0,
                   help="(kelvin mode) min temperature in K, default 1000")
    p.add_argument("--max", type=float, default=8000.0,
                   help="(kelvin mode) max temperature in K, default 8000")
    p.add_argument("--viewbox", type=int, default=1000,
                   help="SVG viewBox edge length (default 1000)")
    p.add_argument("--thickness", type=int, default=120,
                   help="Ring thickness in viewBox units (default 120 = 12%%)")
    p.add_argument("--segments", type=int, default=360,
                   help="Number of color segments (default 360 = 1° each)")
    args = p.parse_args()

    if args.mode == "hue":
        def color_fn(_frac, a_deg):
            return hsl_to_hex(a_deg)
    else:
        k_min, k_max = args.min, args.max
        def color_fn(frac, _a_deg):
            return kelvin_to_hex(k_min + frac * (k_max - k_min))

    svg = build_svg(args.viewbox, args.thickness, args.segments, color_fn)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(svg)
    extra = (f", kelvin {int(args.min)}-{int(args.max)}K"
             if args.mode == "kelvin" else "")
    print(f"Wrote {args.out} ({len(svg):,} bytes, "
          f"mode={args.mode}{extra}, "
          f"{args.segments} segments, "
          f"thickness {args.thickness}/{args.viewbox} "
          f"= {100*args.thickness/args.viewbox:.1f}% of radius)")


if __name__ == "__main__":
    main()
