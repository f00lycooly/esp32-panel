# Cline Rules — esphome-modular-lvgl-buttons

This is an ESPHome YAML library for building LVGL touchscreen UIs on ESP32 devices. Not Python or JavaScript — pure ESPHome YAML with inline C++ lambdas.

Read ARCHITECTURE.md for the full architecture reference.

## Key Conventions

- **Always use named colors, never hex values.** Use library colors from `common/color.yaml` (e.g., `ep_orange`, `misty_blue`, `very_dark_gray`, `gray50`-`gray900`) or ESPHome built-in CSS names (e.g., `coral`, `dodgerblue`, `teal`). Full list in `common/color.yaml`. If you need a new color, define it as a substitution and reference with `$name` — never inline hex.
- Every button needs a unique `uid` — generates widget IDs: `button_${uid}`, `icon_${uid}`, `label_${uid}`
- Buttons extend pages via `!extend ${page_id | default("main_page")}`
- Variable defaults: `${var | default(value)}`
- Color substitution vars: `$button_on_color`, `$button_off_color`, `$icon_on_color`, `$icon_off_color`, `$label_on_color`, `$label_off_color`
- Icons: `$mdi_*` substitutions from `common/mdi_glyph_substitutions.yaml`
- Fonts: `$icon_font` for icons, `$text_font` for labels
- Grid: `grid_cell_row_pos`/`grid_cell_column_pos` (0-based), optional `grid_cell_row_span`/`grid_cell_column_span`
- HA state: `binary_sensor` (on/off) or `text_sensor` (multi-state) from `platform: homeassistant`

## New Button Pattern

1. File in `buttons/` with comment header listing `vars:`
2. `lvgl: pages:` → `!extend` → `container` → `button` → labels
3. `binary_sensor`/`text_sensor` for HA state tracking
4. `lvgl.widget.update` for color changes on state
5. Reference: `buttons/switch_button.yaml` (simple), `buttons/cover_button.yaml` (complex)

## Testing

```bash
esphome config example_code/SDL-lvgl-display_modular_480px.yaml  # single config
bash testing/test_configs.sh  # all configs
```

## LVGL Gotchas

Read **LVGL_REFERENCE.md** for critical LVGL v8 behavior — especially image tiling, layout override rules, and the obj wrapper pattern for images.

## Key Files

- `LVGL_REFERENCE.md` — LVGL v8 gotchas and patterns (image tiling fix, layouts, sizing)
- `ARCHITECTURE.md` — Full architecture documentation
- `buttons/switch_button.yaml` — Canonical button template
- `common/theme_style.yaml` — LVGL theme system
- `common/mdi_glyph_substitutions.yaml` — Icon mappings (~6000)
- `common/color.yaml` — Named colors (~50)
- `common/fonts.yaml` — Nunito 12-72
- `hardware/` — Device configs (27+ devices)
