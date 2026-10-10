# Hardware

## The Waveshare ESP32-P4-86-Panel

This project targets the **Waveshare ESP32-P4-86-Panel**: a wall-mountable
smart panel in the 86-box form factor (fits a standard 86 mm wall switch
back-box) built around Espressif's ESP32-P4.

| | |
|---|---|
| SoC | ESP32-P4 — dual-core RISC-V, no native radio |
| Radio | Onboard **ESP32-C6** co-processor via ESP-Hosted (Wi-Fi 6 + BLE) |
| Display | 3.95" 720×720 IPS, MIPI-DSI, capacitive touch (GT911) |
| Memory | 32 MB flash, PSRAM |
| Audio | Microphone + speaker (I²S codec, amplifier switch exposed to HA) |
| Power | Backlight PWM-dimmable; touch wakes the display |

The 720×720 square display and the P4's compute headroom are what make a full
LVGL 9 UI with full-bleed album art and animated overlays practical.

## What `modules/hardware/p4-86-panel.yaml` configures

All electrical and low-level framework configuration is isolated in one
package so application modules never touch it. The electrical, framework,
PSRAM, ESP-Hosted, display, touch and audio settings are taken from
**Christian Dunn's (chrisdunnname) known-working baseline**,
`esphome-p4-86-panel-eth-2ro-lvgl` — full credit in
[../NOTICE.md](../NOTICE.md). On top of that baseline this module adds:

- **`min_version: 2026.5.0`** — the ESPHome floor for current P4 + LVGL 9
  support.
- **Board bring-up** — `esp32-p4-evboard` with `engineering_sample: true`
  (the panel ships engineering-sample P4 silicon; without this flag ESP-IDF
  assumes production silicon and the FreeRTOS timer-task stack allocation
  asserts at boot), 32 MB flash, code execution from PSRAM.
- **CPU clock 400 MHz** — ~11% more compute for the software rasteriser and
  overlay animation maths, stacked on an `-O2` build.
- **ESP-Hosted radio link** — the P4 talks to the onboard C6 over SDIO
  (`esp_hosted` managed component, pinned version), with enlarged RX/TX queues
  and `use_psram: true` to relieve queue exhaustion on this split P4/C6 board.
  The host library is pinned to **2.12.12**, and the matching C6 firmware image
  is embedded in the panel firmware (`firmware/esp-hosted/`), so the
  `ESP-Hosted Firmware` update entity can bring the C6 to the same version over
  SDIO without needing WiFi. A host/C6 version mismatch can wedge the WiFi
  link ~30 s after boot, so the panel checks ~15 s after boot and, if the C6
  differs from the embedded image, flashes it automatically (at most 3
  attempts across reboots; after that it logs an error and the entity's
  **Install** button or a USB flash is the fallback). Bump the pin, the image
  and its `sha256` together.
- **lwIP TCP tuning** — the panel↔C6 RPC round-trip (~65 ms) makes default TCP
  windows the throughput bottleneck (~88 KB/s), not the PHY. The module raises
  `TCP_WND`/`SND_BUF`/mailbox sizes to keep more bytes in flight — this is what
  makes full-bleed album-art downloads snappy. If heap pressure ever shows,
  dial `TCP_WND` back to 32768.
- **Display** — `mipi_dsi` platform, PWM-dimmable backlight exposed as a light.
- **Touch** — GT911 over I²C; `on_touch` turns the backlight on, so a tap wakes
  the panel before LVGL handles the press (the framebuffer stays live under a
  dark backlight).
- **Audio** — I²S codec plus an amplifier switch (visible in HA), consumed by
  the voice stack in `modules/assist.yaml` (microphone, wake word, media
  player for on-device sounds).
- **FreeRTOS run-time stats** — `${freertos_runtime_stats}` (default `"n"`)
  gates the Kconfig options that feed the [perf overlay's](CUSTOMISATION.md#developer-extras)
  per-core CPU%; production panels leave it off to avoid per-context-switch
  accounting cost.

## Package variables

| Var | Default | Meaning |
|---|---|---|
| `rotation` | `"0"` | Display rotation in degrees, for panels mounted sideways/upside-down. Passed as a package var from the device file: |

```yaml
packages:
  hardware:
    file: modules/hardware/p4-86-panel.yaml
    vars:
      rotation: "180"
```

## BLE proxy (optional package)

The P4 has no radio of its own — Bluetooth runs through the hosted C6. The
optional `modules/ble-proxy.yaml` package enables `esp32_ble_tracker` (active
scanning) plus `bluetooth_proxy`, so the panel doubles as a Home Assistant
**BLE proxy / presence tracker** for the room it is mounted in. Inclusion is
the switch: a panel that wants it adds

```yaml
ble_proxy: !include modules/ble-proxy.yaml
```

and a panel that does not simply omits the line — nothing BLE is compiled in.

## Microphone and voice

The onboard microphone and speaker drive the on-device voice assistant
(`modules/assist.yaml`): micro-wake-word detection runs locally, speech is
streamed to Home Assistant's Assist pipeline, and replies/sounds play through
the panel speaker or a room speaker. The animated persona overlay that
visualises assistant state is documented in [pages/voice.md](pages/voice.md).

## Practical notes

- **First flash must be over USB-C**; after that OTA is the normal path
  ([INSTALLATION.md](INSTALLATION.md)).
- Keep hardware changes isolated to this file and verify them on-device before
  touching anything else — the application packages assume this contract's ids.
- The engineering-sample silicon note above means boot/run stability is worth
  watching after toolchain upgrades; if instability appears, dropping the CPU
  clock back to 360 MHz is the first lever.
