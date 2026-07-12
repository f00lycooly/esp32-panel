# Info / Settings

*(No screenshot — this page displays the panel's own network details (Wi-Fi
SSID, IP and MAC address, firmware version), so it is deliberately excluded
from the published capture set.)*

The device-information page from the vendored
esphome-modular-lvgl-buttons library (see [../../NOTICE.md](../../NOTICE.md)),
wrapped by `modules/page-info.yaml` so it participates in the optional
screenshot roster like every other page.

### What it shows

- Wi-Fi network name and signal
- IP address and MAC address
- ESPHome/firmware version and project metadata
  (`project_name` / `project_version` from your config)

### How you reach it

The info page is **not part of the swipe grid** — it is deliberately outside
the `nav_pages_matrix` contract and unreachable by swiping. The only way in is
the **settings cog** in the top bar (top-right on every page), which enters it
as a takeover: swipe navigation suspends and a back chevron returns you to the
page you came from. The cog's border doubles as the **HA connection ring** —
green when the Home Assistant API is connected, red when not — so the entry
point to the diagnostics page is itself a diagnostic.

### Configuration

Nothing page-specific. The identity values it displays come from the identity
group in `user-config.yaml` ([../CUSTOMISATION.md](../CUSTOMISATION.md#identity)).

---
[← Voice overlay](voice.md) · Next: [Navigation →](navigation.md) · [Back to customisation](../CUSTOMISATION.md)
