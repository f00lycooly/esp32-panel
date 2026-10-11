# Installation

This guide takes you from an unopened panel to a flashed device that appears in
Home Assistant. For what each screen does, see the [screen documentation](pages/README.md);
for adapting the firmware to your home, see [CUSTOMISATION.md](CUSTOMISATION.md).

## What you need

- **Waveshare ESP32-P4-86-Panel** — ESP32-P4 with 32 MB flash, 720×720 IPS
  touchscreen, onboard ESP32-C6 radio, microphone and speaker. See
  [HARDWARE.md](HARDWARE.md).
- **USB-C data cable** for the first flash (subsequent updates are OTA).
- **ESPHome 2026.5.0 or newer** — the hardware module pins
  `esphome: min_version: 2026.5.0` (it relies on current ESP32-P4 + LVGL 9
  support). Install it locally with `pip install esphome` (a venv is fine; point the
  Makefile at it with `ESPHOME=/path/to/esphome`). Build on a workstation — see
  [Where to build](#where-to-build). The ESPHome container or Home Assistant
  add-on is fine for `make validate`, not for compiling.
- **Home Assistant** with the entities your panel will display — plus the
  template packages from `homeassistant/examples/` (below).
- 2.4/5 GHz Wi-Fi. The panel fetches album art and images over plain HTTP from
  your HA instance, so the panel and HA must be routable to each other.

## 1. Clone the repository

```bash
git clone https://git.bagofholding.co.uk/foolycooly/esp32-panel.git
cd esp32-panel
```

(A mirror is available at `https://github.com/f00lycooly/esp32-panel`.)

## 2. Create your secrets

```bash
cp secrets.yaml.example secrets.yaml
```

`secrets.yaml` must define at least:

| Secret | Meaning |
|---|---|
| `wifi_ssid` | Your Wi-Fi network name |
| `wifi_password` | Your Wi-Fi password |
| `esphome_api_key` | 32-byte base64 API encryption key — generate one with `esphome wizard` or `openssl rand -base64 32` |

`secrets.yaml` is gitignored — never commit it.

## 3. Create your panel configuration

```bash
cp user-config.yaml.example user-config.yaml
```

`user-config.yaml` holds everything site-specific: the panel's identity
(`name`, `friendly_name`), your HA base URL, and the Home Assistant entity ids
each screen binds to. Every value ships with a sensible generic default that
matches the example HA packages below, so a good strategy is: install the HA
examples first, flash with defaults, then tailor. The full group-by-group
walkthrough is in [CUSTOMISATION.md](CUSTOMISATION.md).

## 4. Install the Home Assistant packages

Several screens consume *derived* sensors that plain integrations do not
provide. Ready-made template packages live in
[`homeassistant/examples/`](../homeassistant/examples/); copy the ones you need
into your HA `packages:` include (or merge them into `configuration.yaml`) and
restart HA.

| Package | Provides | Feeds |
|---|---|---|
| `spotify_panel.yaml` | `sensor.spotify_panel_recent` / `_playlists` / `_artists` — cover-art feeds for the pickers | [Main](pages/main.md) recent row, Pick Music / Browse, [Alarm](pages/alarm.md) music picker |
| `alarm_panel.yaml` | The alarm page's helper contract: the `input_text.alarm_ringing` / `input_boolean.alarm_snooze_armed` ringing plumbing (the `sensor.scheduled_alarms` roster with rows `a0`–`a5` + `next`, and the backend that stores/fires alarms, is external — Node-RED, AppDaemon or HA automations; see the file header) | [Alarm](pages/alarm.md) |

The [Screensaver](pages/screensaver.md) forecast needs no package either — set
`weather_entity` to any `weather.*` entity (HA 2025.10+; the panel calls
`weather.get_forecasts` itself).

The [Energy](pages/energy.md) pages need no package either: they bind your
inverter/charger entities directly and read today's totals from the recorder
(HA ≥ 2025.10; see the Energy section of `user-config.yaml.example`).

Media needs no package: the panel binds each roster room's
`media_player.<slug>` directly (title, artist, artwork, volume, group members),
keeps the selected room on the device and derives the speaker-group topology
from `group_members` itself. Only the 7 roster rooms are seen, so a new speaker
needs a roster edit and a re-flash.

The [Timer](pages/timer.md) page needs no package — create a plain HA **timer
helper** (Settings → Devices & services → Helpers → Timer) and point
`timer_entity` at it. To make voice-set timers land in the same helper, add an
`intent_script` bridge in your HA `configuration.yaml` (see the note at the top
of `modules/page-timer.yaml`).

Screens degrade gracefully when a backend is missing (e.g. the alarm list shows
empty rows until `sensor.scheduled_alarms` exists) — you can install packages
incrementally.

## 5. First flash (USB)

Connect the panel via USB-C, then:

```bash
esphome run waveshare-p4-86-panel.yaml
```

ESPHome compiles the firmware (the first build takes ~20-25 minutes — it is a
full ESP-IDF build for the P4 with 2500+ objects; incremental builds take ~5) and offers the serial port for upload. The panel
reboots into a boot splash, joins your Wi-Fi and starts the UI. If HA is not
reachable, the splash auto-dismisses after 12 seconds anyway and the UI runs
offline; the settings cog's status ring shows red until the HA connection is up.

## 6. Subsequent updates (OTA)

Once on the network, flash over the air:

```bash
make flash DEVICE_IP=<panel-ip-or-mdns-name>    # compile + OTA upload
make upload DEVICE_IP=<panel-ip-or-mdns-name>   # upload the last build only
make logs DEVICE_IP=<panel-ip-or-mdns-name>     # stream logs
```

(Equivalent to `esphome run waveshare-p4-86-panel.yaml --device <ip>`.)

OTA reuses the existing API key, so Home Assistant keeps trusting the device
across updates.

### Where to build

Compile on a workstation with a local ESPHome, then OTA-upload from there.
Do not compile inside a shared server's ESPHome container: a cold ESP-IDF build
is heavy on RAM and CPU and can starve the host and the services running on it.
`make validate CONTAINER=<name>` (cheap, no C++ build) works in a container;
`make compile` refuses there unless you set `ALLOW_CONTAINER_COMPILE=1`.
`make validate` does not compile C++ lambdas, so run a full local
`make compile` before relying on a change.

## 7. Add the device to Home Assistant

HA usually discovers the panel automatically (Settings → Devices & Services →
ESPHome → *Configure*). If not, add the ESPHome integration manually with the
panel's IP and port 6053, and paste the `esphome_api_key` when prompted. Once
added, the panel exposes its diagnostic switches (perf overlay, capture
controls if enabled) and the voice-assistant entities.

## Troubleshooting

- **Panel broadcasts its own Wi-Fi network** — if the configured network cannot
  be joined, the fallback AP starts. Its SSID is derived from the panel `name`
  and the password is the `wifi_fallback_pwd` substitution in your
  `user-config.yaml`. Connect to it and the captive portal lets you fix the
  Wi-Fi credentials without reflashing.
- **"Invalid encryption key" when adding to HA** — the key in HA does not match
  `esphome_api_key` in `secrets.yaml`. Remove the device from HA and re-add it
  with the current key. Note the API server allows extra connection slots
  precisely because a key-mismatch retry storm can otherwise exhaust them.
- **Stuck on the boot splash** — it clears after 12 s even without HA; if it
  does not, check the serial log (`esphome logs waveshare-p4-86-panel.yaml`)
  for Wi-Fi or PSRAM errors.
- **Screens show blank values** — the corresponding HA entity is missing or
  renamed. Cross-check the entity tables on each [screen page](pages/README.md)
  against your `user-config.yaml` and the example packages from step 4.
- **Album art / images missing** — the panel fetches images over plain HTTP
  from `ha_base_url` (and, optionally, an image proxy). Confirm the URL is
  reachable *from the panel's VLAN* and is `http://` (the panel does not do
  TLS for image fetches).

Next: [CUSTOMISATION.md](CUSTOMISATION.md) to make the panel yours.
