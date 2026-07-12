#!/usr/bin/env python3
"""
generate_glados_chassis.py — baked GLaDOS chassis assets (v2 two-piece optic).

Redesign (, approved via the brainstorm-companion "shape v6"): the head is
a long, uniform-width white shell with LESS corner rounding, an upright pose, and a
full-length dark cavity + bellows whose aperture/optic is OFFSET LEFT of the shell
centre (more ceramic on the right). There are NO eyelids and NO head-tilt pose-swap;
expression is carried by the eye sliding VERTICALLY in the aperture (top -> centre,
never below centre) plus per-phase colour/glow.

Two baked pieces (this script) + one live optic (voice-overlay.yaml):

  glados_shell.png   FRONT faceplate — metallic shell-shade + ceramic shell + recessed
                     bezel, with the aperture opening CUT TRANSPARENT. Its opaque
                     ceramic occludes the eye outside the opening (no eyelids needed).
  glados_cavity.png  BACK — dark recessed cavity + full-length horizontal bellows,
                     offset left, sized a touch larger than the opening so it underlaps
                     the bezel (no transparent seam).
  glados_chassis_contract.json  eye-centre, eye-top, slide range, aperture rect, live
                     optic widget diameters and the status-LED offset.

Layer order on device (va_head, back -> front):
    glados_cavity  ->  LIVE eye (socket / iris rings / pupil / glow)  ->  glados_shell
The eye TRANSLATES only (no rotation/scale) so LVGL never builds a transform layer.

Geometry is native px in a W x H head box; voice-overlay.yaml scales/places to taste.
Deterministic Pillow (no network/browser). Requires Pillow>=10.
Run:  /tmp/glados-venv/bin/python generate_glados_chassis.py
"""
import os
import json
from PIL import Image, ImageDraw, ImageFilter, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 3                         # supersample for clean AA

# ── Head geometry (native px) ───────────────────────────────────────────────
W, H = 288, 680                # head box at ACTIVE display size: fills ~680 of the
                               # 720 screen rendered at IDENTITY (no runtime scale →
                               # no transform layer). Long, uniform width; ratio ~2.36.
CX = W // 2                    # shell centre x (144)
AP_OFF = 24                    # aperture shifted LEFT of the shell centre
ACX = CX - AP_OFF              # aperture centre x (120)
ACY = H // 2                   # aperture centre y (340)

# Layers: (w, h, radius) native. shell pieces are centred on (CX, ACY); the recessed
# bezel + cavity are centred on the offset (ACX, ACY). Less rounding than the old
# stadium (radii are ~20-38% of width, not a full semicircle).
SHADE_W,  SHADE_H,  SHADE_R  = 267, 659, 58   # metallic rim (HOR)
SHELL_W,  SHELL_H,  SHELL_R  = 249, 641, 50   # ceramic body (VER + inset shading)
BEZEL_W,  BEZEL_H,  BEZEL_R  = 152, 302, 55   # recessed frame (HOR)
CAV_W,    CAV_H,    CAV_R     = 126, 276, 47   # dark opening / cavity (VER + recess)
BELLOWS_INSET = 0.0            # 0 = bellows fill the slot; the slot height (CAV_H) was
                               # shortened to frame them (more white cowl) rather than
                               # insetting the slats inside a tall opening.

# Live optic (placed by voice-overlay.yaml; diameters in native px).
EYE_TOP_Y   = 253              # highest eye-centre the aperture allows (grounded cap is tighter)
SOCKET_D    = 110              # dark socket
IRIS_RIM_D  = 94               # dim iris ring
IRIS_D      = 73               # lit iris (per-phase colour + glow)
HOT_D       = 47               # hot inner
PUPIL_D     = 26               # pupil
LED_OFF     = (44, 29)         # status-LED offset from eye centre (down-right)


def hx(c):
    """'#rrggbb' or 0xRRGGBB -> (r,g,b)."""
    if isinstance(c, str):
        c = int(c.lstrip('#'), 16)
    return ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def grad_stops(stops, pos):
    if pos <= stops[0][0]:
        return stops[0][1]
    for i in range(1, len(stops)):
        if pos <= stops[i][0]:
            p0, c0 = stops[i - 1]
            p1, c1 = stops[i]
            t = 0 if p1 == p0 else (pos - p0) / (p1 - p0)
            return lerp(c0, c1, t)
    return stops[-1][1]


def gradient(w, h, stops, direction):
    img = Image.new('RGB', (w, h))
    px = img.load()
    if direction == 'VER':
        for y in range(h):
            col = grad_stops(stops, y / max(1, h - 1))
            for x in range(w):
                px[x, y] = col
    else:
        for x in range(w):
            col = grad_stops(stops, x / max(1, w - 1))
            for y in range(h):
                px[x, y] = col
    return img


def pill_mask(w, h, radius):
    r = min(radius, w // 2, h // 2)
    m = Image.new('L', (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    return m


def directional_inner_shadow(mask, color, alpha, edge, depth):
    w, h = mask.size
    ramp = Image.new('L', (w, h), 0)
    rp = ramp.load()
    d = max(1, depth)
    for y in range(h):
        a = max(0.0, 1.0 - y / d) if edge == 'top' else max(0.0, 1.0 - (h - 1 - y) / d)
        v = int(alpha * a)
        if v:
            for x in range(w):
                rp[x, y] = v
    ramp = ImageChops.multiply(ramp, mask)
    layer = Image.new('RGBA', (w, h), color + (0,))
    layer.putalpha(ramp)
    return layer


def perimeter_inner_shadow(mask, color, alpha, depth):
    w, h = mask.size
    inv = ImageChops.invert(mask)
    glow = inv.filter(ImageFilter.GaussianBlur(depth))
    ring = ImageChops.multiply(glow, mask)
    ring = ring.point(lambda v: int(v * alpha / 255))
    layer = Image.new('RGBA', (w, h), color + (0,))
    layer.putalpha(ring)
    return layer


def rgb_layer(w, h, stops, direction, radius, inner_shadows=None):
    grad = gradient(w, h, stops, direction).convert('RGBA')
    mask = pill_mask(w, h, radius)
    grad.putalpha(mask)
    if inner_shadows:
        for sh in inner_shadows:
            grad.alpha_composite(sh)
    return grad


def place(canvas, w, h, cx, cy, r, stops, direction, shadows=None):
    """Build a layer (native w/h/r) and alpha-composite it centred at native (cx, cy)."""
    layer = rgb_layer(w * SS, h * SS, stops, direction, r * SS, shadows)
    canvas.alpha_composite(layer, (round((cx - w / 2) * SS), round((cy - h / 2) * SS)))


def _opening_mask():
    """Full-canvas alpha mask of the aperture opening (the front cut / clip rect)."""
    m = Image.new('L', (W * SS, H * SS), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        [round((ACX - CAV_W / 2) * SS), round((ACY - CAV_H / 2) * SS),
         round((ACX + CAV_W / 2) * SS), round((ACY + CAV_H / 2) * SS)],
        radius=CAV_R * SS, fill=255)
    return m


def build_shell():
    """FRONT faceplate: shell-shade + ceramic + bezel, aperture opening cut transparent."""
    cv = Image.new('RGBA', (W * SS, H * SS), (0, 0, 0, 0))
    # 1) metallic shell shade (HOR 5-stop)
    place(cv, SHADE_W, SHADE_H, CX, ACY, SHADE_R,
          [(0.00, hx('#4a4d54')), (0.22, hx('#70747c')), (0.50, hx('#b6bac2')),
           (0.80, hx('#6a6d75')), (1.00, hx('#3a3d44'))], 'HOR')
    # 2) ceramic shell (VER 3-stop + top highlight + bottom shade)
    sm = pill_mask(SHELL_W * SS, SHELL_H * SS, SHELL_R * SS)
    shell_shadows = [
        directional_inner_shadow(sm, hx('#ffffff'), 0x55, 'top', 16 * SS),
        directional_inner_shadow(sm, hx('#000000'), 0x55, 'bottom', 40 * SS),
    ]
    place(cv, SHELL_W, SHELL_H, CX, ACY, SHELL_R,
          [(0.00, hx('#c4c8d0')), (0.45, hx('#b0b4bc')), (1.00, hx('#9a9ea6'))],
          'VER', shell_shadows)
    # 3) recessed bezel (HOR 4-stop) at the offset aperture centre
    place(cv, BEZEL_W, BEZEL_H, ACX, ACY, BEZEL_R,
          [(0.00, hx('#4a4d54')), (0.40, hx('#3a3c42')), (0.70, hx('#1a1c20')),
           (1.00, hx('#0a0a0c'))], 'HOR')
    # 4) cut the aperture opening transparent (back cavity + live eye show through)
    a = ImageChops.subtract(cv.split()[-1], _opening_mask())
    cv.putalpha(a)
    return cv.resize((W, H), Image.LANCZOS)


def build_cavity():
    """BACK: dark recessed cavity + full-length bellows (offset left). A touch larger
    than the opening so it underlaps the bezel — no transparent seam at the rim."""
    cv = Image.new('RGBA', (W * SS, H * SS), (0, 0, 0, 0))
    cw, ch, cr = CAV_W + 8, CAV_H + 8, CAV_R + 4
    cm = pill_mask(cw * SS, ch * SS, cr * SS)
    cav = rgb_layer(cw * SS, ch * SS,
                    [(0.0, hx('#181a1c')), (1.0, hx('#2a2f34'))], 'VER', cr * SS,
                    [perimeter_inner_shadow(cm, hx('#000000'), 0xCC, 16 * SS)])
    # bellows: horizontal slats over the CENTRAL span of the cavity — inset 20% from the
    # top and 20% from the bottom of the full slat field, leaving more
    # plain recess above/below the eye.
    bd = ImageDraw.Draw(cav)
    x0, x1 = int(12 * SS), int((cw - 12) * SS)
    b_top, b_bot = 14.0, ch - 12.0            # full slat field (native px)
    span = b_bot - b_top
    b_top += BELLOWS_INSET * span             # trim off the top
    b_bot -= BELLOWS_INSET * span             # trim off the bottom
    yy, i = b_top * SS, 0
    while yy < b_bot * SS:
        col = (46, 50, 58, 255) if i % 2 == 0 else (10, 11, 14, 255)
        bd.rounded_rectangle([x0, int(yy), x1, int(yy + 5 * SS)], radius=int(3 * SS), fill=col)
        yy += 13 * SS
        i += 1
    cv.alpha_composite(cav, (round((ACX - cw / 2) * SS), round((ACY - ch / 2) * SS)))
    return cv.resize((W, H), Image.LANCZOS)


def main():
    shell = build_shell()
    cavity = build_cavity()
    p_shell = os.path.join(HERE, 'glados_shell.png')
    p_cav = os.path.join(HERE, 'glados_cavity.png')
    shell.save(p_shell)
    cavity.save(p_cav)

    # Bellows band + grounded eye-slide (head coords). The eye should stay over the
    # slats: its highest centre puts the socket TOP at the bellows TOP, so it never
    # floats in the plain recess above the trimmed bellows.
    _ch = CAV_H + 8
    _span = (_ch - 12.0) - 14.0
    _cav_top = ACY - _ch / 2.0
    _bell_top = _cav_top + 14.0 + BELLOWS_INSET * _span
    _bell_bot = _cav_top + (_ch - 12.0) - BELLOWS_INSET * _span
    _eye_top_grounded = round(_bell_top + SOCKET_D / 2.0)

    contract = {
        'comment': 'Two-piece GLaDOS optic. Place glados_cavity then the '
                   'LIVE eye then glados_shell in va_head (same origin). The eye '
                   'TRANSLATES vertically between eye_centre (rest, lowest) and eye_top '
                   '(highest) — translate only, no transform layer. Aperture/optic is '
                   'offset left of the shell centre. No eyelids; no head-tilt poses.',
        'head_wh': [W, H],
        'shell_centre': [CX, ACY],
        'aperture': {'cx': ACX, 'cy': ACY, 'w': CAV_W, 'h': CAV_H, 'r': CAV_R},
        'eye_centre': [ACX, ACY],            # rest / lowest position
        'eye_top': [ACX, EYE_TOP_Y],         # highest the APERTURE allows
        'eye_slide_px': ACY - EYE_TOP_Y,
        'bellows': {'top': round(_bell_top), 'bottom': round(_bell_bot)},
        'eye_top_grounded': [ACX, _eye_top_grounded],      # highest that keeps the socket on the bellows
        'eye_slide_grounded_px': ACY - _eye_top_grounded,  # the cap the live eye-slide uses
        'eye_widget_diameters': {
            'socket': SOCKET_D, 'iris_rim': IRIS_RIM_D, 'iris': IRIS_D,
            'hot': HOT_D, 'pupil': PUPIL_D,
        },
        'led_offset': list(LED_OFF),
        'layer_order': ['glados_cavity (back)', 'LIVE eye', 'glados_shell (front)'],
    }
    with open(os.path.join(HERE, 'glados_chassis_contract.json'), 'w') as f:
        json.dump(contract, f, indent=2)

    print(f'wrote {p_shell}  {shell.size}  ({os.path.getsize(p_shell)} bytes)')
    print(f'wrote {p_cav}    {cavity.size}  ({os.path.getsize(p_cav)} bytes)')
    print(f'head={W}x{H}  aperture centre=({ACX},{ACY}) offset_left={AP_OFF}  '
          f'eye slide {EYE_TOP_Y}..{ACY} ({ACY - EYE_TOP_Y}px)')


if __name__ == '__main__':
    main()
