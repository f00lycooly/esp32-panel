# Page-shell skeleton

The structural contract every panel page is built on. Two bars that
toggle **independently**, framing a content region:

```
┌─────────────────────────────────────────────┐
│  TOP BAR · 72px · 3 zones                     │  ← top_bar
│  [ Left 150 ] [   Centre 388   ] [ Right 150 ]│
├───────────────────────────────────────────────┤  ← shell_divider
│                                               │
│        CONTENT REGION  ·  720 × 576           │
│        (y 72 → 648, both bars on)             │
│                                               │
├───────────────────────────────────────────────┤  ← shell_divider
│  BOTTOM BAR · 72px · 4 equal zones (172 each) │  ← bottom_bar
│  [ z1 ] [ z2 ] [ z3 ] [ z4 ]                  │
└─────────────────────────────────────────────┘
```

| Mode | top bar | bottom bar | content rect (y, h) |
|------|:------:|:----------:|---------------------|
| Standard | ✔ | ✔ | 72, 576 |
| Top-only | ✔ | ✘ | 72, 648 |
| Bottom-only | ✘ | ✔ | 0, 648 |
| **Full screen** | ✘ | ✘ | 0, 720 |

## Why this is a *pattern*, not a single `!include`

ESPHome `!include` substitutes **values**, not widget structure, and has no
`{% if %}`. A page's Left zone might hold a back button on one page and a
battery pill on another — different structures — so one include can't serve all.
The skeleton is therefore delivered as:

- **Constants** (`ui-shared.yaml` substitutions): `$shell_bar_h`, `$shell_edge`,
  `$shell_side_w`, `$shell_centre_w`, `$shell_zone_h`, `$shell_bz_w`,
  `$shell_content_y`, `$shell_content_h`. Single source of truth for geometry.
- **Chrome styles** (`ui-shared.yaml` `style_definitions`): `shell_bar`,
  `shell_divider`, `shell_zone`.
- **Leaf widgets** for zone *content* — fixed-structure, real includes:
  `widgets/icon_button.yaml`, `status_pill.yaml`,
  `stat_cell.yaml`.

A page assembles its bars **inline** from these. Bars are flex-row containers;
zones are flex children of fixed width, so SPACE_BETWEEN seats them
left/centre/right (or 4-up) with zero per-zone `x` maths. A zone holds **one**
slot widget, centred with the child's `align: CENTER`.

## Top bar — copy-paste (3 zones)

```yaml
# ── TOP BAR ──────────────────────────────────────────────────────────
- obj:
    id: <page>_topbar
    styles: shell_bar
    align: TOP_MID
    y: 0
    width: 100%
    height: $shell_bar_h
    scrollable: false
    clickable: false
    layout: {type: flex, flex_flow: ROW, flex_align_main: SPACE_BETWEEN, flex_align_cross: CENTER, pad_column: 0}
    widgets:
      - obj:   # LEFT  (back chevron · battery pill · or leave empty)
          styles: shell_zone
          width: $shell_side_w
          height: $shell_zone_h
          scrollable: false
          widgets:
            - button: {id: <page>_back, align: CENTER, width: 56, height: 56, radius: 28, bg_color: very_dark_gray,
                       widgets: [{label: {align: CENTER, text: $mdi_chevron_left, text_font: mdi_card_28, text_color: white}}],
                       on_click: [{lvgl.page.show: main_page}]}
      - obj:   # CENTRE  (page title · or clock+date)
          styles: shell_zone
          width: $shell_centre_w
          height: $shell_zone_h
          scrollable: false
          widgets:
            - label: {align: CENTER, text: "Climate", styles: header_title}
      - obj:   # RIGHT  (cog · action pill · mode chip · or empty)
          styles: shell_zone
          width: $shell_side_w
          height: $shell_zone_h
          scrollable: false
          widgets:
            - label: {id: lbl_clim_mode, align: CENTER, text: "—", text_font: oswald_20, text_color: gray500}
# divider rule under the top bar
- obj: {styles: shell_divider, align: TOP_MID, y: $shell_bar_h, width: 100%, height: 1}
```

## Bottom bar — copy-paste (4 equal zones)

```yaml
# ── BOTTOM BAR ───────────────────────────────────────────────────────
- obj:
    id: <page>_botbar
    styles: shell_bar
    align: BOTTOM_MID
    y: 0
    width: 100%
    height: $shell_bar_h
    scrollable: false
    clickable: false
    layout: {type: flex, flex_flow: ROW, flex_align_main: SPACE_BETWEEN, flex_align_cross: CENTER, pad_column: 0}
    widgets:
      - obj: {styles: shell_zone, width: $shell_bz_w, height: $shell_zone_h, scrollable: false,
              widgets: [<stat cell — see stat_cell.yaml>]}
      - obj: {styles: shell_zone, width: $shell_bz_w, height: $shell_zone_h, scrollable: false, widgets: [...]}
      - obj: {styles: shell_zone, width: $shell_bz_w, height: $shell_zone_h, scrollable: false, widgets: [...]}
      - obj: {styles: shell_zone, width: $shell_bz_w, height: $shell_zone_h, scrollable: false, widgets: [...]}
# divider rule above the bottom bar
- obj: {styles: shell_divider, align: BOTTOM_MID, y: -$shell_bar_h, width: 100%, height: 1}
```

## Content region

Place page content within the rectangle the enabled bars leave free. With both
bars on: `align: TOP_MID, y: $shell_content_y` (= 72) downward, within
`$shell_content_h` (= 576) of height. Full-screen pages (no bars, e.g. the
energy Sankey, screensaver, GLaDOS) own the whole 720×720 and may float their
own affordances (a back button) on top.

## Rules

- **Never hardcode** 72 / 150 / 388 / 172 / 16 — reference the `$shell_*`
  constants so a future re-tune propagates everywhere.
- One slot widget per zone, centred via the child's `align: CENTER`.
- Keep state-driven **colour** in the page's driving lambdas; the shell owns
  structure and neutral chrome only.
- A bar you don't want? Omit it and extend the content region per the table.
