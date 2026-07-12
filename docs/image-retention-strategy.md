# Panel image-retention strategy

The panel keeps many decoded `online_image` buffers resident in PSRAM. This
documents what is retained, what may be lazily released, and the measurement gate
before the runtime change lands.

## Retained decoded buffers (RGB565 = 2 bytes/px)

| Buffer(s) | Where | Size each | Count | Total | Visible when |
| --- | --- | --- | --- | --- | --- |
| `np_bg_art` 480×480 | page-main now-playing | 450 KB | 1 | **450 KB** | always (home) |
| `pm_cover_img_0..7` 150×150 | Pick-Music grid | 44 KB | 8 | **352 KB** | Pick takeover only |
| `ar_cover_img_0..7` 120×120 | Browse-Artist grid | 28 KB | 8 | **225 KB** | Browse takeover only |
| `rec_cover_img_0..3` 104×104 | page-main recent tiles | 21 KB | 4 | **85 KB** | always (home) |
| `al_who_*_img` 120×120 | Alarm Who-picker | 28 KB | 3 | **84 KB** | Who-picker takeover only |

**Resident total ≈ 1.20 MB.** PSRAM has room, but the P4 + ESP32-C6 esp-hosted
link has a documented SDIO/heap-sensitivity history (see HARDWARE.md, lwIP/ESP-Hosted notes), so trimming
idle residency buys page-switch + download-burst headroom.

## Releasable vs pinned

- **Pinned** (visible on the home page, cannot release): `np_bg_art` (450 KB),
  `rec_cover_img_0..3` (85 KB).
- **Releasable** (only visible inside a takeover): Pick grid (352 KB), Browse grid
  (225 KB), Who-picker photos (84 KB) — **≈ 660 KB** reclaimable when not in those
  takeovers, ≈ 577 KB from Pick+Browse alone.

## Mechanism (available, verified in the ESPHome tree)

- `online_image.release` action / `OnlineImage::release()` frees the decoded
  buffer; the next `set_url` + `update()` re-allocates and re-downloads.
- The cover pump already caches each cover's URL in `cov_url[]` and fetches on a
  `cov_want[]` flag (page-media.yaml). So **re-fetch on open is just setting
  `cov_want[range] = true`** — no new URL plumbing.

## Planned change (gated — NOT yet implemented)

1. On **Pick** takeover exit → `online_image.release` `pm_cover_img_0..7`; on open →
   `cov_want[0..7] = true`.
2. On **Browse** takeover exit → release `ar_cover_img_0..7`; on open →
   `cov_want[8..15] = true`.
3. On **Who-picker** exit → release `al_who_*_img`; re-fetch on the next person
   `entity_picture` push or on open.

### Why it is gated, not landed here

- **Acceptance requires measured on-device improvement** — idle + page-switch
  PSRAM headroom before/after, read from the Perf overlay (see CUSTOMISATION.md, developer extras) /
  `sensor.perf_free_psram`, ideally with the object-count telemetry described in lvgl-object-budgets.md. That needs
  the physical panel.
- **UX trade-off needs sign-off**: released grids re-download progressively through
  the serialised pump (~750 ms/cover → a full 8-tile grid repaints over several
  seconds on each open). Acceptable only if the placeholder/fade reads well on the
  device.
- **Couples to planned pump rework**: a follow-up converts the pump from a polling interval to a
  queued trigger script; the re-fetch-on-open trigger should be built on the new
  shape rather than the soon-to-change interval, to avoid reworking it twice.

### Measurement procedure (for the on-device session)

1. Flash, enable the **Perf overlay** switch, settle on `main_page`; record
   `perf_free_psram` (idle baseline).
2. Open Pick, let the grid fill; record. Close; record (expect ≈ +352 KB after
   release lands). Repeat for Browse + Who-picker.
3. Confirm no blank-grid / decode-stall regressions across 10 open/close cycles and
   that idle PSRAM is stable (no leak from repeated release/re-fetch).
