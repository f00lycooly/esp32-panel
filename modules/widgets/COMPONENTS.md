# Shared components library

Reusable LVGL widget includes for the panel UI. Each is a small
`!include` fragment, namespaced by `${uid}`, that a page drops into its widget
tree. Built LVGL-honest (LVGL 9.6: label/bar/arc/obj/image/roller/dropdown/
slider — no SVG/CSS/blur).

## Catalogue

| Widget | File | Kind | Used by |
|--------|------|------|---------|
| Circular icon button | `icon_button.yaml` | property-map | everywhere |
| Status pill | `status_pill.yaml` | property-map | top bar, screensaver |
| Stat cell / inline | `stat_cell.yaml`, `stat_cell_inline.yaml` | property-map | bottom bars |
| Chip | `chip.yaml` | property-map | filter/category/picker chips |
| State glyph | `state_glyph.yaml` | property-map | any state-driven glyph |
| Stepper `[−] v [+]` | `stepper.yaml` | property-map + scripts | climate setpoint, timer/alarm |
| Tumbler (roller) | `tumbler.yaml` | property-map | Set Timer / alarm time pickers |
| Mode dropdown | `mode_dropdown.yaml` | property-map | climate mode, source |
| Person photo | `person_photo.yaml` | property-map + online_image | alarm "Who", presence |
| Category tile | `category_tile.yaml` | property-map | Browse / Pick-Music grids |
| Cover tile | `cover_tile.yaml` | property-map + online_image | cover grids |
| Room row | `room_row.yaml` | property-map + scripts | Multi-Room list |
| Now-playing dock | `now_playing_dock.yaml` | property-map + online_image + scripts | Now Playing, mini-dock |
| Playlist / Sonos tile | `tile_playlist.yaml`, `tile_media_device.yaml` | full-widget | media modals |
| Toggle / media-device tile | `tile_toggle.yaml`, `tile_media_device.yaml` | full-widget | lights, rooms |

## Conventions

- **Property-map includes** emit a widget's *body* (starting at `id: ${uid}`),
  no `button:`/`obj:` wrapper. The caller merges them under the widget type it
  wants and adds the action:
  ```yaml
  - button:
      <<: !include {file: widgets/chip.yaml, vars: {uid: c1, text: "Rock", ...}}
      on_click: [{script.execute: pick_rock}]
  ```
- **Full-widget includes** (the `tile_*` set) emit the whole `button:` with the
  action baked to a fixed script; used with a plain `- !include`.
- **`${uid}` vs `$uid`** — ESPHome substitution + LVGL: inside a **flow mapping**
  `{...}`, the braces of `${uid}` break YAML, so use `$uid` there (only safe when
  followed by a non-identifier char, e.g. `id: lbl_$uid,`). For an id with a
  suffix (`ic_${uid}_play`) use **block style** — `$uid_play` would bind to a
  variable named `uid_play`. The composites here are block-style for that reason.
- **Abstract-script contract** — nested controls can't take per-instance actions
  through an include, so they call conventionally-named scripts the page defines:
  - `stepper` → `${uid}_dec`, `${uid}_inc`
  - `room_row` → `${uid}_vol` (read `lv_slider_get_value(id(vol_${uid}))`), `${uid}_toggle`
  - `now_playing_dock` → `${uid}_prev`, `${uid}_play`, `${uid}_next`
- **State/colour is owned by the page** — widgets set neutral initial colours;
  a page refresh lambda recolours via the documented ids.
- **`online_image`** — image-backed widgets (`person_photo`, `cover_tile`,
  `now_playing_dock`) take an `online_image` id as `src`; the page declares the
  `online_image` (entity_picture / cover url, runtime-refreshed).
- **LVGL v9 obj-clickable trap** — children are `clickable: false` (see bd memory
  `lvgl-v9-obj-clickable-default`).

## Validation status

The structure of every file is YAML-validated. Because ESPHome only compiles
`!include`s that are **referenced**, the new widgets are **not yet ESPHome-
compiled** — they are validated against the toolchain when their first consumer
page wires them up ( media,  climate,  timer/alarm). Treat the
ESPHome-specific props on `tumbler` (roller), `mode_dropdown` (dropdown) and
`room_row` (slider `indicator`/`knob`) as provisional until that first use; the
font `glyphs:` lists on the consuming page must include every `$mdi_*` referenced
here (`$mdi_skip_previous/next`, `$mdi_account`, `$mdi_link_variant`, etc.).
