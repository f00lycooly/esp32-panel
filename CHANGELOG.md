# Changelog

All notable changes to this project will be documented in this file. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-07-11

Initial public release.

This project was developed inside a private home-automation monorepo and has been
split out with a fresh history for publication. Everything site-specific (entity
ids, network addresses, room names) has been abstracted into
`user-config.yaml`, so the firmware builds unmodified for any home.

### Included in this release

- Composition-root device file for the Waveshare ESP32-P4-86-Panel
  (`waveshare-p4-86-panel.yaml`) with a per-panel `user-config.yaml` contract
- 2D swipe navigation (functions × pages) with position-dot chrome, a global
  top bar and a full-screen takeover framework
- Screens: now-playing home, multi-room audio, climate dial, hot-water dial,
  timer (aperture iris), alarms (HA-backed roster + edit/speaker/music/person
  pickers), energy (live Sankey + daily totals), surf-conditions pages
  (manifest-driven codegen), room card, robot vacuum, terminal clock/weather
  screensaver, and the device-info page
- On-device voice assistant (wake word, timers) with an animated persona overlay
- Optional diagnostics: HTTP screenshot endpoint (`:8080`), on-screen
  performance HUD
- Home Assistant example template packages for every entity contract the pages
  consume
