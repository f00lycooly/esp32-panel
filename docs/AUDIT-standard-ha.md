# Audit: custom HA YAML and sensors → standard Home Assistant

**Date:** 2026-10-10
**Scope:** every Home Assistant binding in `modules/` and every package in
`homeassistant/examples/`. The audit also covers entities the panel exports
to HA and the custom code on the panel side.
**Goal:** remove as much custom HA YAML and as many custom sensors as
possible, so the panel binds to entities that HA integrations already
provide.
**Status:** audit only. No functional changes have been made yet.

---

## 1. Executive summary

| | Count today | After the plan |
|---|---|---|
| Custom HA packages in `homeassistant/examples/` | 7 (≈1,050 lines) | 0–1 (alarm, depending on the option chosen) |
| HA template sensors / helpers the user must create | ≈60 (13 templates + 13 integrations + 13 utility meters for energy flows alone) | ≈0 |
| External automation backends required (Node-RED) | 1 (alarms) | 0 |
| Polled Jinja templates in HA | 1 (`sonos_topology`, every 5 s) | 0 |

**Key enabler.** ESPHome **2025.10** added action responses to
`homeassistant.action`: `capture_response: true`, `response_template`, and
`on_success` with a `response` JSON object. This project already requires
`min_version: 2026.5.0` (`modules/hardware/p4-86-panel.yaml:23`).

Most of the HA packages exist only because the panel used to be unable to
read service-response data. With action responses, the panel can call
`weather.get_forecasts`, `recorder.get_statistics` and `spotifyplus.get_*`
itself. The vendored library already uses this pattern in
`esphome-modular-lvgl-buttons/ui/weather/forecast.yaml:79-115`,
`ui/solar/update_solar_today.yaml:29-90` and
`ui/solar/sensors_assets_solar_realtime_graph.yaml:24-37`.

**Prerequisites for the response-based items:**
- HA ≥ 2025.10.
- "Allow the device to perform Home Assistant actions" enabled on the
  ESPHome integration entry. The panel already needs this setting.

---

## 2. HA package inventory and verdicts

### 2.1 `energy_normalised.yaml` (91 lines). Verdict: **remove**

**What it does:** 8 template sensors (`pv_power`, `house_power`,
`grid_import_power`, `grid_export_power`, `battery_power`, `battery_soc`,
`ev_charger_power`, `heat_pump_power`). Each one is a rename, a unit scale,
a sign flip or a split.

**Consumers:**
- `page-energy.yaml:63-166` (pv_kw, home_kw, grid_imp_kw, grid_exp_kw, ev_kw, hp_kw).
- `ui-shared.yaml:48-58` (batt_kw, batt_soc).

**Replacement:**
- Bind directly to the inverter or integration entities.
- Normalise on the panel with ESPHome sensor `filters: - multiply:`, using
  new substitutions:
  - `en_power_scale` (`1` for kW, `0.001` for W)
  - `en_batt_sign` (±1)
  - `en_grid_sign` (±1)
- Collapse import/export into **one signed** `en_grid_power_entity`. This is
  safe for two reasons:
  - The "Now" lambda only computes `net_grid = gimp − gexp`
    (`page-energy.yaml:224-233`).
  - The top bar never reads import/export. It derives grid as
    `load + batt − pv` (`top-bar.yaml:62-76`).
- Where a ≥0 split is still needed, use `fmaxf(±net, 0)`.
- `house_power` could become optional, using the same identity the top bar
  already uses.

### 2.2 `energy_daily_flows.yaml` (400 lines). Verdict: **remove (approximate split)**

**What it does:**
1. 13 instantaneous flow templates (PV/battery/grid → home/battery/grid/EV/HP).
2. 13 Riemann `integration` sensors.
3. 13 daily `utility_meter`s, i.e. `sensor.daily_flow_*`.

**Consumers:**
- `page-energy.yaml:102-155` (13 `df_*` sensors).
- The Sankey draws from them at `page-energy.yaml:392-470`. "Home-rest" is
  computed on the panel as `*_to_home − to_ev − to_hp`.

**Replacement:**
- Call `recorder.get_statistics` (`period: day`, `types: [change]`, start
  of today) on the kWh counters the user's Energy dashboard already uses:
  PV, grid import, grid export, battery charge, battery discharge, plus
  optional EV and HP.
- Copy the library pattern from `ui/solar/update_solar_today.yaml`.
- Refresh on API connect plus every few minutes.
- Allocate on the panel, the same way HA's energy-distribution card does:
  - `pv_home = pv − export − charge`. If negative, the shortfall becomes
    grid→battery, then battery→grid.
  - `battery→home = discharge − battery→grid`
  - `grid→home = import − grid→battery`
  - `load = pv + import + discharge − export − charge`
  - EV and HP each take a share of every source in proportion to the day's
    home source mix.

**Trade-off:** per-source and per-sink daily totals stay exact. The
source→EV/HP attribution becomes an approximation, because the current
instant-by-instant split is lost. This replaces 13 entities and 39 HA
helpers with about 5–7 substitutions.

### 2.3 `weather_forecast.yaml` (39 lines). Verdict: **remove**

**What it does:** a trigger-template sensor `sensor.panel_weather_forecast`
that wraps `weather.get_forecasts` into `today` / `tomorrow` attributes,
formatted as `cond|hi|lo`.

**Consumer:** `page-screensaver.yaml:146-157` (`ss_today_src`,
`ss_tom_src`) → the `apply_forecast` script (`:94-130`).

**Replacement:**
- The screensaver calls `weather.get_forecasts` with a `response_template`
  that emits the same `cond|hi|lo` strings.
- Trigger it on API connect plus every 30 minutes.
- Change the substitution to `weather_entity`, which can be any `weather.*`
  entity (e.g. met.no).

### 2.4 `media_mirror.yaml` (82 lines, one copy per panel) + `input_text.<slug>_active_player`. Verdict: **remove**

**What it does:** mirrors whichever player the panel has selected into one
stable sensor. This was needed because `homeassistant` sensors bind to fixed
entity ids.

**Consumers:**

| id | attribute | file:line |
|---|---|---|
| media_volume | volume | page-media.yaml:663 |
| media_state | state (`tv` derived) | page-media.yaml:676 |
| media_title / media_artist | title / artist | page-media.yaml:679, 687 |
| media_device_name | device_name | page-media.yaml:683 |
| media_target_sync | target | page-media.yaml:714 |
| np_art_url | entity_picture | page-main.yaml:233 |
| active_player_sync (input_text) | state | page-media.yaml:693 |

- `input_text.set_value` writes are at page-media.yaml:199, :292 and :1696,
  and page-main.yaml:1112.
- The mirror's `shuffle` and `repeat` attributes are not read by anything.

**Replacement:**
- Rooms are already compile-time slots (`media_room_0..6_slug`).
- Bind `media_player.${media_room_N_slug}` per room to: state,
  `media_title`, `media_artist`, `entity_picture`, `volume_level` and
  `group_members`. Add `source` and `media_content_id` only if TV
  detection is kept.
- The selection already lives in the `active_media_player` global with
  `restore_value` (`page-media.yaml:449`). `np_refresh` reads the cached
  values for the selected index.
- The device name comes from `media_room_N_caption`.
- The coordinator is `group_members[0]` of the selected room.

**Trade-offs:**
- The selection is no longer an HA entity, so HA automations can't repoint
  a panel.
- Only the 7 configured rooms can be shown.
- Subscriptions grow to about 42–56. That is fine on the P4.

### 2.5 `sonos_topology.yaml` (106 lines, polled every 5 s). Verdict: **remove**

**Consumers:**
- `topo_rooms` (page-media.yaml:729) → Rooms modal, `room_state`/`room_vol`.
- `topo_g0..g3` and `topo_gcount` (page-media.yaml:770-800) → `mr_refresh`
  (page-main.yaml:1013), `adopt_playing_group` (page-media.yaml:258) and the
  Group reconcile (page-media.yaml:1600).
- `group_label` and `room_count` are not consumed.

**Replacement:**
- Derive everything on the panel from the per-room bindings in §2.4. A room
  is a coordinator when `group_members[0] == self`.
- Rebuild the existing blob format in C++ so downstream scripts barely
  change.
- `group_members` arrives as a Python-list string, so it needs a small
  parser.

**Gains and trade-offs:**
- Updates become event-driven instead of a 5 s poll.
- About 100 lines of duplicated Jinja are removed.
- Trade-off: there is no dynamic roster via `integration_entities('sonos')`,
  so new speakers need a recompile.

### 2.6 `input_boolean.sonos_follow_me`. Verdict: **remove**

**Consumers:** toggled at page-main.yaml:922, read at :928. The repo
contains no behaviour for it; any follow-me automation is external.

**Replacement:** ESPHome `switch: platform: template` with `restore_mode`.
It is exposed as `switch.<panel>_follow_me`, and HA automations can still
read it.

### 2.7 `spotify_panel.yaml` (239 lines, requires SpotifyPlus). Verdict: **remove**

**Consumers:**

| Feed | Attributes | Used at |
|---|---|---|
| Playlists | `names_8` / `ids_8` / `cover_0..7` | page-media.yaml:805, 846, 867, 917-962; **also page-alarm.yaml:244** |
| Artists | `names_8` / `ids_8` / `cover_0..7` | page-media.yaml:969-1052 |
| Recent | `cover_0..3` / `ids_4` | page-main.yaml:261-298 |
| Devices | — | **not consumed: dead YAML** |

**Replacement:**
- When Browse/Pick opens, the panel calls
  `spotifyplus.get_playlist_favorites` (`limit_total: 8`),
  `get_users_top_artists` and `get_player_recent_tracks` with
  `capture_response`.
- Use a `response_template` to trim the payload on the HA side.
- Feed the results into the existing `cov_url` / `cov_want` queue.
- Move the recents dedup-by-cover to C++.
- This also unblocks the Artist → Albums → Tracks drill-down TODO
  (page-media.yaml:1463).

**Trade-offs:**
- A short load on first open.
- Errors must be handled on the panel.
- SpotifyPlus stays a dependency.

**Fully standard fallback:** Sonos favourites via the `media_player`
`source_list` attribute plus `media_player.select_source`. This drops
cover art, artists and recents.

### 2.8 `alarm_panel.yaml` (93 lines) + external Node-RED backend. Verdict: **decision required**

**Reads:**
- `sensor.scheduled_alarms` attributes a0..a5 and `next`
  (page-alarm.yaml:195-203). Each attribute is a pipe-delimited row:
  `id|HH:MM|label|daysDigits|en|speaker|music|person|vol`.
- `input_text.alarm_ringing` / `_name` / `_speaker` (page-alarm.yaml:261-290).
- Person `entity_picture` (page-alarm.yaml:206-231).

**Writes:**
- The custom event `esphome.panel_alarm_edit` (toggle :459, add/update
  :608-629, delete :639).
- On dismiss/snooze, `input_boolean` and `input_text` set operations
  (page-alarm.yaml:776-794). The order matters, because the backend reads
  the armed flag when `ringing` clears.

**The backend is external:** storage, firing, playback and snooze re-fire
all live outside this repo.

| Option | HA YAML | Loses |
|---|---|---|
| **A. On-device alarms**: ESPHome `time` + `globals` (`restore_value`), played through the existing `media_player.play_media` path (as `assist.yaml:146-158` does), rung with the `timer_ringing` logic (`assist.yaml:604-645`) | **None**, and no Node-RED | Alarms are per panel; no cross-panel ringing overlay; no editing from HA; "person" is reduced to a label |
| **B. Standard HA helpers per slot**: `input_datetime` (time only) + `input_boolean` (+ `input_select` for the speaker), created in the UI, plus one stock automation or blueprint | One automation | Day masks are clumsy (extra booleans or a select) |
| C. Local Calendar (`calendar.*`) | Needs a template or response to list events | Speaker/music packed into the description |
| D. Assist timers | — | Not suitable: countdowns only, not recurring |

**Recommendation:** choose A for minimal YAML, or B if you want
cross-panel, HA-visible alarms.

---

## 3. Bindings that are already standard (no action needed)

| Area | Bindings | Notes |
|---|---|---|
| Climate | `climate.*` state, current_temperature, temperature, hvac_action, preset_mode; plain sensors for DHW tank/setpoint, outdoor, humidity, COP and pressure (page-climate.yaml:153-360) | Vendor coupling, see §4.2 |
| Timer | `timer.*` state, `remaining`, `duration` (page-timer.yaml:94-121) | Standard UI helper. See §5 for `last_transition` |
| Vacuum | 5 text sensors, 4 sensors, 5 binary sensors, routine buttons (page-vacuum.yaml:699-757) | Roborock-specific sentinels `"none"` / `"ok"` (:676, :680) |
| Security | `alarm_control_panel.*` (page-security.yaml:28) | Standard |
| Lights | `light.*` (page-lights.yaml:19-45) | Standard |
| Energy extras | Solcast remaining, Tesla SoC, Tesla location, EV status | Standard integrations. EV status strings are Easee-specific |
| People | `person.*` `entity_picture` | Standard |

There are no panel-side template sensors that duplicate HA data. Derived
values such as Home-rest, net grid and self-sufficiency are calculated
inside the draw lambdas. `platform: template` appears only in `assist.yaml`
and `perf-overlay.yaml`.

---

## 4. Panel-side custom code

### 4.1 Swelligence (≈2,200 lines). Verdict: **decision required**

| Piece | Lines |
|---|---|
| `swell-manifest.yaml` | 35 |
| `scripts/gen_swell_panel.py` | 399 |
| `modules/generated/*` (6 files) | 1,106 |
| `modules/swell-spot-body.yaml` | 609 |
| `modules/page-swelligence.yaml` | 56 |

- It makes **75 HA subscriptions** (51 text sensors and 24 sensors) for 3
  spots, the largest single group in the firmware.
- It depends on the third-party Swelligence integration.
- The `make generate` target exists only for this.

**To remove it:**
1. Drop the `swell_nav` and `page_swelligence` packages.
2. Set `nav_functions: "4"` and `nav_npages: "{2, 2, 2, 2}"`, and remove
   `${swell_matrix_row}` (`waveshare-p4-86-panel.yaml:65-73`).
3. Delete the manifest, the generator, `modules/generated/`,
   `swell-spot-body.yaml` and the Makefile target.
4. Remove the swell capture references and the user-config section.

Nothing else depends on it. Alternatively, move it to an opt-in example
device file.

### 4.2 Climate coupled to myPyllant (Vaillant). Verdict: **generalise**

**Vendor-specific code:**
- `mypyllant.set_manual_mode_setpoint` (page-climate.yaml:385).
- `mypyllant.set_quick_veto` (:396, :624).
- The `quick_veto_end_date_time` attribute (:338) and
  `clim_qv_duration_entity`.
- Hard-coded `boost` preset and heat_cool/auto/off mapping (:296-299).
- Hard-coded DHW operation modes "Time Controlled" / "Manual" /
  "Cylinder Boost" / "Off" (:357-363, :541-550).
- `temperature == 0.0` is treated as auto mode (:181).

**Replacement:**
- Use `climate.set_temperature` and `climate.set_preset_mode`.
- Make the DHW mode strings substitutions.
- Make quick-veto optional behind a substitution.

### 4.3 Vendored `esphome-modular-lvgl-buttons/` (2.4 MB, 191 files). Verdict: **prune**

**Used** (`ui-foundation.yaml:122-131`, `page-info.yaml:16`):
- `common/ota.yaml`, `common/sensors_base.yaml`, `common/theme/*` (4 files).
- `ui/popup/popup_system.yaml` (+ 2 svgs).
- `pages/loading.yaml` (+ 2 pngs) and `pages/info.yaml`.
- `common/swipe_navigation.yaml`, also used by the optional page-lights and
  page-security.
- The Nunito fonts.

That is about 12 files. **Unused:** `example_code/`, `hardware/`, most of
`ui/`, `testing/`, `tools/`, `AI_tools/` and the docs, roughly 90%.

Keep the weather and solar response examples as references until §2.2 and
§2.3 are done.

### 4.4 `components/lvgl_screenshot` (≈2,500 lines of C++). Verdict: **keep**

- It is the only custom component.
- It is diagnostic-only and already disabled in production.
- Stock `display_capture` doesn't support mipi_dsi
  (`display-capture.yaml:26-30`).

### 4.5 Entities exported to HA by the panel

| Entity | Location | Verdict |
|---|---|---|
| text "Voice target speaker", switches "Output audio externally", "Mute", "Play wake sound", "Mute Responses", select "Wake word engine location" | assist.yaml:533-683 | Keep. Same pattern as the stock HA Voice PE firmware |
| button "Forced Reboot" | assist.yaml:548 | **Remove**. Duplicates the library Restart button (`sensors_base.yaml:3`) |
| Library diagnostics (WiFi, CPU temp, status, version, IP/MAC/SSID, …) | sensors_base.yaml | Keep. Standard ESPHome platforms |
| Perf switch and 7 debug sensors | perf-overlay.yaml:54-106 | Keep. Diagnostic and off by default |
| Backlight, speaker enable, ESP-Hosted update | p4-86-panel.yaml:138, 176, 232 | Standard |

---

## 5. Defects found during the audit

1. **The shuffle and repeat buttons never turn off.** They always send
   `shuffle: true` / `repeat: all` (page-main.yaml:639, 693). They should
   toggle based on the player's current `shuffle` / `repeat` attributes.
2. **The timer finish signal relies on an undocumented attribute.**
   `last_transition` (page-timer.yaml:114-135) is not among the documented
   `timer` attributes (duration, editable, remaining, finishes_at, restore).
   Verify it on the live HA version. Otherwise switch to `finishes_at` or
   the `active → idle` transition.
3. **Voice timers depend on a custom HA `intent_script` bridge**
   (`docs/pages/timer.md`). The native ESPHome `voice_assistant`
   `on_timer_*` callbacks are already wired (`assist.yaml:523`), so consider
   whether the HA bridge is still needed.

---

## 6. Phased plan

| Phase | Work | Removes | Needs |
|---|---|---|---|
| 1. Quick wins | Follow-me as an ESPHome switch; drop `spotify_panel_devices`; fix shuffle/repeat; drop the Forced Reboot button | `input_boolean.sonos_follow_me`, dead YAML | — |
| 2. Energy | Direct bindings with scale/sign filters; Today Sankey via `recorder.get_statistics` | `energy_normalised.yaml`, `energy_daily_flows.yaml` | HA ≥ 2025.10 |
| 3. Weather | `weather.get_forecasts` in the screensaver | `weather_forecast.yaml` | HA ≥ 2025.10 |
| 4. Media | Per-room bindings and panel-side topology | `media_mirror.yaml`, `sonos_topology.yaml`, `media_mirror_entity` / `active_player_helper` substitutions | — |
| 5. Spotify | Response-based feeds (media pages + alarm picker) | `spotify_panel.yaml` | HA ≥ 2025.10 |
| 6. Alarm | Option A or B (§2.8) | `alarm_panel.yaml`, the `esphome.panel_alarm_edit` contract, Node-RED | Decision |
| 7. Optional | Generic climate; prune the vendored library; decide on Swelligence | ≈2.2k lines (swell), ≈2 MB (library) | Decision |

**Validation for every phase:**
- Run `make validate`.
- Check on the device.
- Update `user-config.yaml.example`, `docs/INSTALLATION.md` (§4, package
  table) and the affected `docs/pages/*.md`.

## 7. Open decisions

1. **Alarms:** option A (on-device) or option B (HA helpers + one automation)?
2. **Swelligence:** keep, delete, or move to an opt-in example?
3. **Climate:** generalise away from myPyllant?
4. **Spotify:** response-based feeds (keeps SpotifyPlus), or the fully
   standard Sonos-favourites fallback?
