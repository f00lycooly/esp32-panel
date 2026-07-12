# NOTICE

This repository is licensed under the MIT License (see `LICENSE`,
Copyright (c) 2026 f00lycooly). It bundles third-party code, fonts, and
artwork under their own licences, listed below. Where a bundled item is
under a different licence, that licence — not this repository's MIT
licence — governs that item.

## Bundled code

### esphome-modular-lvgl-buttons (vendored library)

The directory `esphome-modular-lvgl-buttons/` is a vendored copy of
[agillis/esphome-modular-lvgl-buttons](https://github.com/agillis/esphome-modular-lvgl-buttons)
by Andrew Gillis, MIT License (Copyright (c) 2024 Andrew Gillis). The
upstream LICENSE file is preserved at
`esphome-modular-lvgl-buttons/LICENSE`.

Local modification vs upstream:
`hardware/waveshare-esp32-p4-86-panel.yaml` was changed to restore
engineering-sample boot configuration for early ESP32-P4 silicon
(`engineering_sample: true`, pinned `espressif/esp_hosted`, default PSRAM
mode). All other files are unmodified from the vendored snapshot.

### lvgl_screenshot component

`components/lvgl_screenshot/` is derived from
[dcgrove/esphome-lvgl-screenshot](https://github.com/dcgrove/esphome-lvgl-screenshot)
by dcgrove (originally written for LVGL v8). This repository's version is
a substantial rework: an LVGL 9 port using `lv_snapshot`, page-switching
capture (`?page=`), and overlay/modal alpha-compositing. The upstream
CODEOWNERS credit (`@dcgrove`) is retained.

It embeds `stb_image_write.h` v1.16 from
[nothings/stb](https://github.com/nothings/stb) by Sean Barrett and
contributors, dual-licensed MIT / public domain (Unlicense) — the full
licence text is at the end of that file. Its JPEG writer is based on Jon
Olick's public-domain jo_jpeg.

### ESP32-P4 86-Panel hardware baseline

The hardware configuration in `modules/hardware/p4-86-panel.yaml`
(boot, PSRAM, and peripheral settings for the Waveshare
ESP32-P4-86-Panel-ETH-2RO) is derived from the known-working baseline in
[chrisdunnname/esphome-p4-86-panel-eth-2ro-lvgl](https://github.com/chrisdunnname/esphome-p4-86-panel-eth-2ro-lvgl)
by Christian Dunn, MIT License (Copyright (c) 2025 Christian Dunn). That
project in turn credits
[alaltitov/Waveshare-ESP32-P4-86-Panel-ETH-2RO](https://github.com/alaltitov/Waveshare-ESP32-P4-86-Panel-ETH-2RO)
and
[BigBobbas/ESP32-S3-Box3-Custom-ESPHome](https://github.com/BigBobbas/ESP32-S3-Box3-Custom-ESPHome).

## Fonts

Fonts are NOT covered by this repository's MIT licence; each is governed
by its own licence below.

- **Oswald** (`assets/fonts/Oswald-Bold.ttf`,
  `Oswald-SemiBold.ttf`) — Copyright 2016 The Oswald Project Authors
  (https://github.com/googlefonts/OswaldFont). Licensed under the
  SIL Open Font License 1.1 (https://scripts.sil.org/OFL). "Oswald" is a
  Reserved Font Name.
- **Material Design Icons webfont**
  (`assets/fonts/materialdesignicons-webfont.ttf`) — by the
  Pictogrammers icon collective / Austin Andrews and contributors
  (https://github.com/Templarian/MaterialDesign-Webfont). Licensed under
  the Apache License, Version 2.0.
- **Nunito**
  (`esphome-modular-lvgl-buttons/assets/fonts/Nunito-SemiBold.ttf`) —
  Copyright 2014 The Nunito Project Authors / Vernon Adams
  (https://github.com/googlefonts/nunito). SIL Open Font License 1.1.
- **Roboto**
  (`esphome-modular-lvgl-buttons/assets/fonts/Roboto-Regular.ttf`) —
  Copyright 2011 The Roboto Project Authors / Google
  (https://github.com/googlefonts/roboto-classic). Distributed by Google
  under the Apache License 2.0 (classic releases) and the SIL Open Font
  License 1.1 (current releases); either permits redistribution with this
  notice. "Roboto" is a trademark of Google LLC.
- **gluqlo.ttf**
  (`esphome-modular-lvgl-buttons/assets/fonts/gluqlo.ttf`) — a digits-only
  flip-clock font that ships with the
  [Gluqlo](https://github.com/alexanderk23/gluqlo) screensaver project
  (ISC-licensed repository) and arrived here inside the vendored
  esphome-modular-lvgl-buttons library (used by its flip-clock widget).
  The font file itself carries no embedded licence; its metadata says it
  was built in FontForge (2014) and names "Fliqlo", the flip-clock
  screensaver by Yuji Adachi whose digit style it imitates. We record this
  provenance transparently; if you are the rights holder and object to its
  inclusion, please open an issue and it will be removed.

> **Note on the upstream library's fonts.** The upstream
> esphome-modular-lvgl-buttons library bundled a commercial "Enphase
> Visuelt" font (Colophon Foundry / Monotype) used only by its Enphase
> solar-monitor example configurations. That font has been **removed** from
> this vendored copy and those examples now reference the OFL-licensed
> Roboto font instead — a deliberate divergence from upstream so that
> nothing in this repository carries an unlicensed commercial typeface.

## Artwork and trademarks

- **GLaDOS voice-persona artwork** (`assets/images/glados_shell.png`,
  `glados_cavity.png`, and the generator `generate_glados_chassis.py`):
  these images are procedurally generated in this repository from
  geometric primitives — no third-party artwork is copied or traced. They
  depict GLaDOS, a character from the Portal series. **GLaDOS, Portal, and
  Aperture Science are © Valve Corporation.** This is a non-commercial fan
  homage made under the spirit of Valve's fan-content guidelines; it is
  not affiliated with or endorsed by Valve. The DeviantArt fan artwork by
  browen2o that served as private visual reference during design is NOT
  included in this repository.
- Screenshots in `docs/images/` are captures of this project's own UI.
  Any album artwork visible in media-player captures is incidental and
  belongs to its respective rights holders.
- All other product names, logos, and brands are property of their
  respective owners.
