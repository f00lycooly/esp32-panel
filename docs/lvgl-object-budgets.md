# LVGL object-count audit + per-function budgets

LVGL objects are created at compile time and live for the life of the firmware
(every page is built at boot, not on demand). On the ESP32-P4 with its documented
SDIO/heap sensitivity, unbounded object growth across new screens is a real
footprint risk, so this records a baseline and sets budgets for future work.

## Static audit (repeatable)

Run [`scripts/lvgl-object-audit.sh`](../scripts/lvgl-object-audit.sh) from the
repo root. It counts widget-creating YAML keys + `widgets/*.yaml` includes per
module — a compile-time proxy for the runtime tree.

### Baseline — 2026-06-25 (reference device)

| module | direct widgets | widget includes |
| --- | --- | --- |
| page-alarm | 220 | 0 |
| page-main | 73 | 5 |
| page-media | 68 | 45 |
| page-energy | 63 | 0 |
| page-timer | 59 | 3 |
| page-climate | 58 | 0 |
| page-screensaver | 27 | 0 |
| voice-overlay | 15 | 0 |
| nav2d | 10 | 0 |
| top-bar | 7 | 0 |
| perf-overlay | 2 | 0 |
| takeover / ui-foundation | ~3 | 0 |

`page-lights` (4+4) and `page-security` (49) are NOT included on this device
 — excluded from the budget. **Active direct total ≈ 607** plus the
~53 includes (each expands to ≥1 widget — page-media's 45 are the Pick/Browse/room
tiles + cover grid).

`page-alarm` is the heaviest by far (220) because it carries one swipe page **plus
five `skip:true` takeover pages** (edit / speaker / music / who / ringing overlay).
That is expected for the function; it is the first place to look if object pressure
shows up on-device.

## Exact runtime count (on-device)

The static count can't see include expansion or library-page internals. For an
exact per-screen count, drop this into a diagnostic lambda (e.g. behind the Perf
overlay switch, or a one-shot on `nav_arrive`) and read the log:

```cpp
// Recursive object count under a root. Call with lv_screen_active().
struct C { static int n(lv_obj_t* o){
  int t = 1; uint32_t k = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < k; i++) t += n(lv_obj_get_child(o, i));
  return t;
}};
ESP_LOGI("objcount", "active screen = %d objects", C::n(lv_screen_active()));
```

Record the per-page numbers here once captured on the panel; the static table is
the cheap proxy in the meantime.

## Budgets for new screens

- **Feature page (one swipe page):** aim ≤ **100 direct widgets**. The current
  feature pages sit at 58–73 — comfortable headroom.
- **Function with takeover sub-pages:** budget ≈ `100 + 60 × (takeover pages)`.
  page-alarm's 220 over 1 swipe + 5 takeovers (~37/page) is within this; treat it
  as the soft ceiling — a new function exceeding it needs review (split content,
  reuse shared widgets, or lazy-build).
- **Image buffers:** every `online_image` is a resident decoded buffer — budget
  them explicitly and lazy-release non-visible grids (see image-retention-strategy.md); they cost RAM,
  not just object slots.
- **Intervals:** prefer event/while-loop scheduling over permanent high-rate
  intervals ; a per-page redraw interval should gate on the page being
  loaded (the energy/now-flow pages already do via `*_active` globals).

## Verdict

There is a repeatable static audit (`scripts/lvgl-object-audit.sh`) and a
documented exact runtime-count method; the reference-device baseline is recorded above;
new screen work has explicit object / image-buffer / interval budgets.
