# Review: reverting to standard Home Assistant entities

Goal: remove as much custom Home Assistant YAML (`homeassistant/examples/`)
and custom/generated panel-side sensors as possible, so a new install binds
the panel to entities HA integrations already provide.

## Key enabler

Most of the HA packages exist to work around two ESPHome limits that no
longer apply on this project's floor (`min_version: 2026.5.0`):

1. **Action responses.** Since ESPHome 2025.10, `homeassistant.action`
   supports `capture_response: true`, `response_template` and `on_success`
   (with a `response` JSON object). The panel can therefore call
   `weather.get_forecasts`, `recorder.get_statistics` and
   `spotifyplus.get_*` itself. The vendored library already does this in
   `esphome-modular-lvgl-buttons/ui/weather/forecast.yaml:79-115` and
   `ui/solar/update_solar_today.yaml:29-90`.
   This requires "Allow the device to perform Home Assistant actions" on the
   ESPHome integration entry, which the panel already needs.
2. **Fixed entity ids.** The room roster is already a compile-time list
   (`media_room_0..6_slug`), so the panel can bind each room's
   `media_player` directly. A "mirror" sensor is not needed.

## Inventory and verdict

| HA package (lines) | What it provides | Standard replacement | Verdict |
|---|---|---|---|
| `energy_normalised.yaml` (91) | 8 kW renames, sign flips and a grid import/export split | Bind directly to the inverter entities. Use ESPHome `filters: multiply:` with new `en_power_scale`, `en_batt_sign`, `en_grid_sign` substitutions. Use one signed `en_grid_power_entity`, since the "Now" lambda only computes `net = imp − exp` (`page-energy.yaml:224-233`) and the top bar never reads import/export. | **Remove** |
| `energy_daily_flows.yaml` (400) | 13 templates, 13 Riemann integrations and 13 utility meters feeding the "Today" Sankey | `recorder.get_statistics` (`period: day`, `types: [change]`) on the Energy-dashboard kWh counters the user already has: PV, import, export, battery charge/discharge, and optionally EV/HP. Split panel-side the way HA's energy-distribution card does. | **Remove** (approximate split, see below) |
| `weather_forecast.yaml` (39) | `sensor.panel_weather_forecast` today/tomorrow attributes | Panel calls `weather.get_forecasts` with a `response_template` that emits the `cond\|hi\|lo` string `apply_forecast` (`page-screensaver.yaml:94-130`) already parses. Bind to any `weather.*`. | **Remove** |
| `media_mirror.yaml` (82, one per panel) + `input_text.<slug>_active_player` | "What's playing on my selected player" | Per-room `homeassistant` sensors on `media_player.${media_room_N_slug}`: state, `media_title`, `media_artist`, `entity_picture`, `volume_level`, `group_members`. Selection stays in the existing restored global `active_media_player` (`page-media.yaml:449`). | **Remove** |
| `sonos_topology.yaml` (106), polled every 5 s | rooms/g0..g3/g_count blobs | Derived panel-side from the same per-room bindings: coordinator = `group_members[0] == self`. Event-driven instead of a 5 s poll. | **Remove** |
| `input_boolean.sonos_follow_me` | Follow-me flag (no behaviour in repo) | ESPHome `switch: platform: template` with `restore_mode`, exposed as `switch.<panel>_follow_me` | **Remove** |
| `spotify_panel.yaml` (239), needs SpotifyPlus | Playlists, artists, recents and devices feeds | Panel calls `spotifyplus.get_playlist_favorites` / `get_users_top_artists` / `get_player_recent_tracks` on demand with `capture_response`. `sensor.spotify_panel_devices` is not consumed at all. | **Remove** (the integration remains a dependency) |
| `alarm_panel.yaml` (93) + external Node-RED backend + `esphome.panel_alarm_edit` event | Alarm list, ringing and snooze | **Decision needed.** See below. | — |
| Timer `intent_script` (documented in `docs/pages/timer.md`) | Routes voice timers into `timer.panel_timer` | Native ESPHome `voice_assistant` `on_timer_started/updated/finished` already exists (`assist.yaml:523`). The HA `timer` helper itself is a standard UI helper. | Keep the helper; consider dropping the intent bridge |

All other bindings are already standard: climate attributes, vacuum
(Roborock), security, lights, people, Solcast, Tesla and Easee.

### Panel-side custom code worth trimming

- **Swelligence** is about 2,200 lines: manifest, a 399-line generator,
  1,106 generated lines, and a 609-line spot body. It holds 75 of the
  panel's HA subscriptions and depends on a third-party integration. It is
  already optional, but disabling it means hand-editing the nav contract.
  Either delete it or move it to an opt-in example.
- **Climate is tied to myPyllant (Vaillant).** It uses
  `mypyllant.set_manual_mode_setpoint` and `mypyllant.set_quick_veto`
  (`page-climate.yaml:385,396,624`), hard-coded DHW operation-mode strings
  (`:541-550`), and treats `temperature == 0` as auto. The standard
  replacements are `climate.set_temperature` and `climate.set_preset_mode`,
  with DHW modes as substitutions and quick-veto optional.
- **Vendored `esphome-modular-lvgl-buttons/` (2.4 MB):** only about 12 files
  are used (ota, sensors_base, theme, popup_system, loading/info pages,
  fonts). The rest (example_code, hardware, most of `ui/`) can be pruned.
- **`components/lvgl_screenshot`** is the only custom C++. It is
  diagnostic-only and already off in production. Keep it; no change needed.
- **Minor:** the "Forced Reboot" button (`assist.yaml:548`) duplicates the
  library Restart button.

### Bugs found along the way

- The shuffle and repeat buttons always send `shuffle: true` /
  `repeat: all` (`page-main.yaml:639,693`). They never toggle off.
- The timer page relies on a `last_transition` attribute
  (`page-timer.yaml:114`), which is not a documented `timer` attribute.
  `finishes_at` or the `active → idle` transition is the documented route.

## Trade-offs to accept

- **Energy "Today" split becomes approximate.** Daily totals lose the
  per-instant simultaneity, so EV and heat-pump shares are allocated from the
  day's source mix. The totals per source and sink stay exact.
- **Media:** the active-player selection is no longer an HA entity, so HA
  automations can't repoint a panel. Only the 7 configured rooms can be
  shown, and new speakers need a recompile. Subscriptions grow to about 50,
  which the P4 handles easily, but there is no 5 s Jinja poll.
- **Spotify:** feeds are fetched on demand when Browse or Pick opens, so the
  first open shows a short load.

## Alarm options

1. **On-device alarms** (ESPHome `time` + `globals` with `restore_value`,
   playing through the existing `media_player.play_media` path). Needs zero
   HA YAML and no Node-RED. Alarms are per panel, with no cross-panel
   ringing overlay.
2. **Standard HA helpers per slot** (`input_datetime` + `input_boolean` (+
   `input_select` for the speaker)), created in the UI, plus one stock
   automation or blueprint. Alarms are shared across panels and visible in
   HA.

## Suggested phases

1. **Quick wins.** Follow-me switch; delete `spotify_panel_devices`; fix
   shuffle/repeat toggles; drop the duplicate reboot button.
2. **Energy.** Direct bindings with scale/sign filters; Today Sankey via
   `recorder.get_statistics`. Removes `energy_normalised.yaml` and
   `energy_daily_flows.yaml`.
3. **Weather.** `weather.get_forecasts` in the screensaver. Removes
   `weather_forecast.yaml`.
4. **Media.** Per-room bindings plus panel-side topology. Removes
   `media_mirror.yaml`, `sonos_topology.yaml`, and the
   `media_mirror_entity` / `active_player_helper` substitutions.
5. **Spotify.** Response-based feeds. Removes `spotify_panel.yaml`.
6. **Alarm.** Per the chosen option. Removes `alarm_panel.yaml` and the
   Node-RED contract.
7. **Optional.** Make climate generic, prune the vendored library, and
   decide on Swelligence.

Every phase needs `make validate` plus an on-device check. The
response-based phases (2, 3, 5) need HA ≥ 2025.10 and the "allow actions"
permission.
