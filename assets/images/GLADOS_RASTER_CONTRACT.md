# GLaDOS raster-split contract

Plan for splitting the panel GLaDOS overlay (`modules/voice-overlay.yaml`)
into a **baked static chassis image** + **live LVGL optic**, to cut the per-frame redraw cost
that pegs core-1 while the face animates. Cross-checked by the `glados-g80-contract` design
workflow (cost-model / coordinate / asset strands).

## Measured baseline (the thing to beat)

On device, persona = GLaDOS, **idle screensaver cameo animating** (continuous behaviour pool):

| metric | value |
|---|---|
| fps | 8.8 |
| core-0 | 23 % |
| core-1 | **85.5 %** |
| loop_time | **117 ms** |
| free_heap | 204 KB |
| free_psram | 22.4 MB |

The cameo runs the full organic head-rotation behaviour pool 24/7; every leg re-rasterises the
transformed 13-widget `va_head` layer. That is the bottleneck.

## Why chassis→image alone is NOT enough (the load-bearing finding)

During head **motion**, three things cost CPU each frame; `chassis→image` only removes #3:

1. **Transform-blit of the whole `va_head` layer (#1).** Any non-identity transform makes LVGL
   render `va_head`+descendants into an intermediate buffer (~240×432 active / ~120×216 cameo)
   then **software**-resample/rotate/blit it every frame the transform int changes. Same footprint
   with or without the image → unchanged by the swap.
2. **Live-eye `shadow_width` blooms (#2).** iris (30–56) + spread, aperture (10), pupil (7), led —
   LVGL box-shadow is an expensive per-pixel falloff, re-rendered into the layer every motion frame.
3. **The 11 static chassis gradient rounded-rects (#3).** Re-drawn into the layer every motion
   frame today. **This is the only cost `chassis→image` removes** (→ one image blit).

So `chassis→image` is "noticeably better, ship first" — not "clean FPS" on its own.

## Lever set (ship-now vs escalation)

**Ship now (spq + initial pm1.14), in one firmware change:**

- **L1 chassis→image** — replace the 6 static chassis widgets with ONE image in `va_head`. Removes #3.
- **L2 shadow-gating while moving** — zero/shrink iris+aperture+pupil `shadow_width` while the head
  is moving, restore the phase value when the lerp parks. Attacks #2 in exactly the worst window;
  the bloom "tightens" only mid-whip → reads as motion blur, barely noticeable.
- **L3 eps-snap the lerp** — snap `cur→target` once within ε so the quantised transform ints stop
  flipping the instant motion visually ends (kills phantom-motion relayers; also required so the
  before/after measurement isn't polluted by tail oscillation).
- **L4 idle cameo → eye-only** — drop the continuous head-rotation behaviour pool in the cameo;
  keep a static lit head + occasional cheap blink/dart (this is what pm1.14 specifies anyway, and
  it removes the bulk of the measured 85 % core-1, which is *caused* by the 24/7 head rotation).

**Escalate only if on-device measurement (during motion) still shows core-1 pegged:**

- **L5 pose-swap images** (g80 ladder rung b) — N pre-rendered tilt/scale chassis frames, swap per
  behaviour instead of per-frame transform. Eliminates #1 entirely; head motion becomes stepped.
  Requires per-pose eye-centre in the coordinate contract (recorded below).
- **L6 identity-placement cameo** (pm1.14) — render the cameo at native ~120×216 and place it by
  absolute x/y (NOT transform-translate +246/-44) so the idle head runs at **identity transform** →
  no layer at all → true dirty-area redraw of just the eye. Biggest possible win for the 24/7 idle
  case; needs a separate pre-scaled cameo subtree (pre-scaled eye geometry).
- **L7 PPA fork** (pm1.13) — GPU-accelerate the blit (#1). ~40 % only, tearing risk, ESPHome
  component-fork maintenance debt. Last resort; prefer pose-swap (data) over the fork.

**Movement priority:** the **active** face keeps full smooth tweened head motion (user priority —
eye-only/static is last-resort *there*). The **idle cameo** is intentionally eye-only (pm1.14),
because a 24/7 element should not thrash and that is the measured bottleneck.

## Layer contract

```
va_overlay (full-screen backdrop; opaque active / transparent idle)
 └ va_head (200×360 transform container; pivot x=100 y=24 — UNCHANGED)
    ├ va_chassis_img   ← NEW: baked static chassis (replaces 6 widgets below)   [image, first child]
    │     (was: va_neck, va_shell_shade, va_shell, va_bezel, va_cavity + 6 bellows)
    ├ va_eyeball { aperture, socket, iris3, iris, iris1, pupilgrp{pupil,catch} } [LIVE — unchanged]
    ├ va_lid_top / va_lid_bot                                                    [LIVE — unchanged]
    └ va_led                                                                     [LIVE — unchanged]
```

No front mask: the recessed socket has no occluder above the eye except the live lids (g80 #3).

## Asset spec

- **chassis_back**: `esphome/assets/images/glados_chassis_back.png`, **200×360** (== `va_head`
  native, so every kept eye coord stays 1:1 — no recomputation). RGBA PNG on disk.
- **chassis_back_cameo** (L6, optional): `glados_chassis_back_cameo.png`, **120×216** (0.60×).
- **ESPHome `image:`** entry: `type: RGB565`, `byte_order: little_endian`,
  `transparency: alpha_channel` (alpha is mandatory — the ceramic capsule does not fill the box and
  the idle backdrop is transparent; chroma-key would halo the AA rounded edge). ~211 KB / ~76 KB in
  flash, ~4 % of the 6.4 MB firmware; does not touch the 22 MB free PSRAM. **Do not 2×/pre-scale the
  active asset** — LVGL's bilinear transform already covers the 1.20× upscale on low-frequency art.
- **Regenerator**: `esphome/assets/images/generate_glados_chassis.py` (deterministic Pillow; no
  browser/network). Geometry lifted verbatim from the live LVGL coords in `voice-overlay.yaml`;
  gradients + ceramic inset-shading (shell top-highlight `#ffffff55`/bottom-shade `#00000055`,
  cavity inner shadow `#000`, bezel drop-shadow) from the approved mockup `glados-motion.html`.

## Coordinate contract (va_head local px; TOP_MID, x=0 = centred at x=100)

| layer | w×h | y | radius | role |
|---|---|---|---|---|
| neck | 54×18 | 0 | 9 | baked |
| shell_shade | 168×300 | 14 | 84 | baked |
| shell | 158×294 | 17 | 79 | baked |
| bezel | 96×244 | 56 | 48 | baked |
| cavity | 80×232 | 62 | 40 | baked (+6 bellows) |
| **eye centre** | — | **185** (eyeball y75 + r110) | — | **live** |
| aperture footprint | 96 dia | 185 | — | live (slightly overflows cavity, as today) |
| lid_top / lid_bot | 100×60 | 80 / 230 | — | live |
| led | 9×9 | x26 y250 | — | live |

- **Active placement**: `va_head` centred, transform scale 1.20 → effective 240×432; eye centre maps
  to scale-1.20 about pivot (100,24).
- **Cameo placement (current / L1–L4)**: transform scale 0.60 + translate +246/-44 → ~120×216
  top-right, in line with the 216 px screensaver clock.
- **Cameo placement (L6 escalation)**: pre-scaled 120×216 asset at absolute top-right x/y, identity
  transform; eye widgets pre-scaled (iris ~31, socket ~54, etc.), eye centre at 185×0.60 = 111 from
  the cameo subtree top.
- **Per-pose eye-centre (L5 escalation)**: each pose image records its own eye-centre so the live
  optic tracks the swapped head; the neutral frame is the table above.

## Measurement protocol (so a 't falsely marked "solved")

A settled transformed head already costs ~0 (gated writes). Therefore capture perf **during motion**,
isolated:
- **Active**: during the responding 9-pose talk thrash.
- **Cameo**: during an idle move-leg (or, post-L4, during a blink/dart) — NOT between moves.
Read `the panel's perf_{fps,cpu_core_0,cpu_core_1,loop_time} sensors` before/after.
Target: idle core-1 back toward ~5 % (pm1.14), active fps materially up from 8.8.
