#!/usr/bin/env python3
"""
generate_timer_iris.py — baked 8-blade aperture iris for the timer page
. The iris is STATIC; the live countdown/up is a standard LVGL arc that
fills the petals around (no rotation — cheap). Two baked plates + a runtime-coloured
arc between them:

  timer_iris_base.png  dim 8-blade iris (unlit petals); transparent gaps + centre.
  timer_iris_seps.png  dark blade separators/rim (sits ON TOP of the arc) so the lit
                        petals stay crisply divided. Transparent over petal bodies.
  timer_iris_petal_0.png .. timer_iris_petal_7.png
                        8 pre-rotated petal plates (.2). Each is the full
                        480x480 canvas with ONLY that one blade filled OPAQUE WHITE
                        (so TIMER-core can image_recolor it to the mode colour at
                        runtime), the rest fully transparent. Petal 0's leading edge
                        is at the vertical-top seam; 1..7 go clockwise. NO runtime
                        transform (ESP32-P4 PPA cannot rotate at draw time) — the
                        rotation is BAKED into each PNG. TIMER-core stacks
                        base -> petal_0..petal_k (recoloured) -> seps -> time label
                        to "light one petal at a time clockwise" (discrete 8-step).
  timer_iris_contract.json  geometry: centre, opening radius, petal band in/out radius,
                        + the Portal-matched mode colours (down=orange, up=blue),
                        + the petal scheme (names, recolor intent, seam, direction).

Layer order on the page (back->front):
  timer_iris_base  ->  petal_0..petal_k (recoloured)  ->  timer_iris_seps  ->  time label
  (legacy: a single LVGL arc may be used instead of the discrete petals).
Inner opening is +20% vs the design mock so the time never collides with the blades.
Deterministic Pillow.  Run: /tmp/timer-venv/bin/python generate_timer_iris.py
"""
import os, json, math
from PIL import Image, ImageDraw, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
SZ, SS = 480, 3
C = SZ * SS / 2
R_OUT = 232 * SS                 # petal outer radius
R_IN  = 124 * SS                 # petal inner radius == opening (+20% vs the 104 mock)
N, GAP, SPIRAL = 8, 3.2, 15
DIM   = (58, 64, 74)
SEP   = (8, 10, 13)
PETAL = (255, 255, 255)          # neutral white -> image_recolor at runtime
DOWN_ORANGE = "#ff7a1e"          # Portal orange (count DOWN)
UP_BLUE     = "#2ba8ff"          # Portal blue   (count UP)


def pt(r, deg):
    return (C + r * math.cos(math.radians(deg)), C + r * math.sin(math.radians(deg)))


def blade_polys():
    polys = []
    for k in range(N):
        a0 = -90 + k * (360 / N) + GAP
        a1 = -90 + (k + 1) * (360 / N) - GAP
        outer = [pt(R_OUT, a0 + (a1 - a0) * t / 6) for t in range(7)]
        inner = [pt(R_IN, (a1 - SPIRAL) + ((a0 - SPIRAL) - (a1 - SPIRAL)) * t / 6) for t in range(7)]
        polys.append(outer + inner)
    return polys


def build():
    polys = blade_polys()
    # base: dim blades
    base = Image.new("RGBA", (SZ * SS, SZ * SS), (0, 0, 0, 0))
    bd = ImageDraw.Draw(base)
    for p in polys:
        bd.polygon(p, fill=DIM + (255,))
    # seps: a dark annulus with the blades cut out -> only gaps + rim remain
    ann = Image.new("L", (SZ * SS, SZ * SS), 0)
    ImageDraw.Draw(ann).ellipse([C - R_OUT, C - R_OUT, C + R_OUT, C + R_OUT], fill=255)
    ImageDraw.Draw(ann).ellipse([C - R_IN, C - R_IN, C + R_IN, C + R_IN], fill=0)
    blades_mask = Image.new("L", (SZ * SS, SZ * SS), 0)
    for p in polys:
        ImageDraw.Draw(blades_mask).polygon(p, fill=255)
    sep_alpha = ImageChops.subtract(ann, blades_mask)
    seps = Image.new("RGBA", (SZ * SS, SZ * SS), SEP + (0,))
    seps.putalpha(sep_alpha)
    base = base.resize((SZ, SZ), Image.LANCZOS)
    seps = seps.resize((SZ, SZ), Image.LANCZOS)
    base.save(os.path.join(HERE, "timer_iris_base.png"))
    seps.save(os.path.join(HERE, "timer_iris_seps.png"))

    # petals: one plate per blade, ONLY that blade filled opaque white on a
    # transparent canvas (so TIMER-core image_recolors it to the mode colour).
    # Same polys[] as base/seps -> pixel-aligned with both. Supersample +
    # LANCZOS-downscale identically so antialiased edges match the plates.
    petal_files = []
    for k, p in enumerate(polys):
        plate = Image.new("RGBA", (SZ * SS, SZ * SS), (0, 0, 0, 0))
        ImageDraw.Draw(plate).polygon(p, fill=PETAL + (255,))
        plate = plate.resize((SZ, SZ), Image.LANCZOS)
        fname = f"timer_iris_petal_{k}.png"
        plate.save(os.path.join(HERE, fname))
        petal_files.append(fname)

    contract = {
        "comment": "Static 8-blade iris. Place base, then either an LVGL "
                   "arc (mode colour, value=%) OR the discrete petal plates "
                   "(timer_iris_petal_0..7, image_recolored to the mode colour), then "
                   "seps, then the time label in the centre. down=orange (remaining), "
                   "up=blue (elapsed).",
        "size": SZ,
        "centre": [SZ // 2, SZ // 2],
        "opening_radius": R_IN // SS,
        "petal_outer_radius": R_OUT // SS,
        "petal_band_width": (R_OUT - R_IN) // SS,
        "blades": N,
        "colors": {"down": DOWN_ORANGE, "up": UP_BLUE, "dim": "#3a4049",
                   "finished": "#ff453a"},
        "petals": {
            "comment": "8 pre-rotated, baked petal plates (.2). Lit one at a "
                       "time clockwise for a discrete 8-step aperture fill. Each PNG is "
                       "the full 480x480 canvas with ONLY its blade opaque WHITE and the "
                       "rest transparent — neutral white so TIMER-core can image_recolor "
                       "it to the mode colour at runtime. NO runtime transform "
                       "(ESP32-P4 PPA limits): rotation is BAKED into each PNG.",
            "files": petal_files,
            "count": N,
            "recolor": "white -> mode colour via LVGL image_recolor / img_recolor_opa",
            "seam": "vertical top (12 o'clock)",
            "petal_0": "leading edge at the vertical-top seam",
            "direction": "clockwise (petal 0 -> 7)",
            "layer_order": "base -> petal_0..petal_k (recoloured) -> seps -> time label",
            "aligned_with": ["timer_iris_base.png", "timer_iris_seps.png"],
            "esphome_image_ids": [f"timer_petal_{k}" for k in range(N)],
            "esphome_image_type": "RGB565, transparency: alpha_channel",
        },
    }
    with open(os.path.join(HERE, "timer_iris_contract.json"), "w") as f:
        json.dump(contract, f, indent=2)
    print(f"wrote timer_iris_base.png + timer_iris_seps.png + "
          f"{len(petal_files)} petal plates ({SZ}x{SZ}); "
          f"opening_r={R_IN//SS} petal_band={R_IN//SS}..{R_OUT//SS}")


if __name__ == "__main__":
    build()
