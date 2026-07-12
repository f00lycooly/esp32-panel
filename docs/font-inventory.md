# Font / glyph inventory

Inventory of every font raster in the active reference build, the consolidation
decision, and justification for the large-size outliers. Raster estimate =
`glyphs × size² × 4bpp ÷ 8` bytes (upper bound; ESPHome packs/compresses, and
4bpp is the declared depth for all of these).

## Shared text + icon fonts (ui-shared.yaml)

| id | size | glyphs | est. |
| --- | --- | --- | --- |
| oswald_14/18/20/24/36/42/48/72 | 14–72 | GF_Latin_Core each | text — shared by every page |
| mdi_card_28 | 28 | ~38 | ~14 KB |
| mdi_icon_40 | 40 | ~19 | ~15 KB |

These are the cross-page shared fonts and are referenced widely.

## Page-local MDI fonts

| page | id | size | glyphs | est. |
| --- | --- | --- | --- | --- |
| main | mdi_main_28 | 28 | ~16 | ~6 KB |
| main | mdi_np_44 | 44 | ~8 | ~8 KB |
| main | mdi_main_54 | 54 | ~3 | ~4 KB |
| main | mdi_main_80 | 80 | ~1 | ~3 KB |
| media | mdi_media_28 | 28 | ~8 | ~3 KB |
| media | mdi_media_40 | 40 | ~3 | ~2 KB |
| alarm | mdi_alarm_28 | 28 | ~25 | ~10 KB |
| alarm | mdi_alarm_40 | 40 | ~6 | ~5 KB |
| climate | mdi_clim_22 | 22 | ~2 | <1 KB |
| climate | mdi_clim_28 | 28 | ~9 | ~4 KB |
| climate | mdi_clim_56 | 56 | ~3 | ~5 KB |
| energy | mdi_energy_28 | 28 | ~4 | ~2 KB |
| energy | mdi_energy_40 | 40 | ~8 | ~6 KB |
| energy | mdi_bar_30 | 30 | ~7 | ~3 KB |
| timer | mdi_timer_28 | 28 | ~4 | ~2 KB |
| timer | mdi_timer_56 | 56 | ~3 | ~5 KB |
| timer | **mdi_timer_plus_lg** | **150** | 1 | **~11 KB** |
| screensaver | mdi_batt_44 | 44 | ~5 | ~5 KB |
| screensaver | **mdi_weather_96** | **96** | ~13 | **~60 KB** |
| screensaver | **oswald_clock** | **216** | ~11 | **~256 KB** |

## Decision: keep page-local MDI fonts (consolidation rejected)

There are seven `mdi@28` and four `mdi@40` fonts, so consolidating into the shared
`mdi_card_28` / `mdi_icon_40` (carrying the union of glyphs) would dedupe the
glyphs that overlap across pages (chevrons, check, close, etc.).

**It is intentionally NOT done**, for reasons that outweigh the saving:

1. **It works against this epic.** The whole point of the modularisation work
   is page independence — a page owns its own glyph set and can be included or
   dropped from a composition without editing a shared font. Folding all `@28`
   glyphs into one shared font re-couples every page to one glyph list: adding a
   glyph for one page rebuilds the font for all, and every page links the whole
   union even for glyphs it never draws.
2. **The saving is small and flash is not the constraint.** All page-local `@28`
   + `@40` fonts together are ~60 KB of raster; the cross-page glyph overlap that
   consolidation would actually reclaim is a fraction of that. The ESP32-P4 has
   32 MB flash — font raster is nowhere near the binding resource.
3. **bpp is already 4** (not 8) on every MDI font, so the rasters are already at
   the lower-cost depth.

If flash ever becomes tight, the safe first move is to drop unused glyphs from the
larger page-local lists (alarm's ~25 and the shared card_28's ~38), not to merge
fonts.

## Large-raster outliers — justified

- **oswald_clock @216 (~256 KB)** — the screensaver's full-screen clock; ~11
  glyphs (digits + colon + a few). It IS the screensaver. It lives only on
  `screensaver_page` (idle-only, `skip:true`, never in the swipe ring), so it
  costs flash but no runtime/RAM cost while the UI is in use. Reducing the size
  would change the headline design. **Kept.**
- **mdi_weather_96 (~60 KB)** — 13 weather-condition glyphs for the screensaver
  weather line. Same idle-only page; one glyph shown at a time. **Kept.**
- **mdi_timer_plus_lg @150 (~11 KB, 1 glyph)** — the timer "add" button plus.
  Could be redrawn as two LVGL rectangles to drop the raster, but the saving is
  ~11 KB and the change carries visual-tuning risk; not worth it at current flash
  headroom. **Kept**, flagged as the cheapest future reduction if needed.

## Verdict

Inventory documented; repeated `@28`/`@40` MDI sizes are **intentionally kept
page-local** with the rationale above (module independence > marginal flash);
large rasters are measured and justified as intentional, idle-only screensaver
design. No code change — consolidation would regress the modularization goal.
