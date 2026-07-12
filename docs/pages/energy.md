# Energy

![Energy Now page](../images/energy.jpg)

*With top-layer chrome:*

![Energy page with overlays](../images/energy-overlays.jpg)

The ENERGY function: two pages that make a solar + battery + EV + heat-pump
home legible at a glance. Everything is drawn with LVGL-honest primitives —
canvas-filled ribbons and label/MDI nodes, no SVG — and the decorative layers
are non-clickable so swipes pass straight through.

### Page 1 — Now (live Sankey)

Live power flows between the system nodes — **PV**, **Home**, **Grid**,
**Battery**, **EV charger** and **heat pump** — with ribbon widths and arrows
following the instantaneous readings. A foot strip adds context: solar
forecast remaining today and the EV's battery level. Grid import and export
are read from *dedicated* import/export sensors rather than one signed net
power sensor, which avoids sign-convention ambiguity across inverter brands.

The top bar's battery pill (visible on every page) is fed from the same
sensors — tapping it jumps here from anywhere.

### Page 2 — Today (daily Sankey)

![Energy Today page](../images/today.jpg)

Swipe left for the day's *totals*: a Sankey of energy moved along each
source→destination path since midnight, built from thirteen daily flow meters
(PV/Battery/Grid → Home/Battery/Grid/EV/Heat-pump). The `*_to_home` meters
count *all* load; the panel subtracts the EV and heat-pump splits to recover
the home-remainder per source.

*Reference rendering of the daily Sankey:*

![Daily Sankey reference](../images/sankey-today.png)

### Configuration (user-config.yaml)

| Substitution | Meaning | Example |
|---|---|---|
| `en_pv_power_entity` | Live PV generation (kW) | `sensor.pv_power` |
| `en_consumed_power_entity` | Live house consumption (kW) | `sensor.consumed_power` |
| `en_grid_import_entity` | Live grid import (kW, ≥ 0) | `sensor.grid_import_power` |
| `en_grid_export_entity` | Live grid export (kW, ≥ 0) | `sensor.grid_export_power` |
| `en_ev_power_entity` | EV charger power (kW) | `sensor.ev_charger_power` |
| `en_ev_status_entity` | EV charger status text | `sensor.ev_charger_status` |
| `en_hp_power_entity` | Heat-pump power (kW) | `sensor.heat_pump_power` |
| `en_solcast_remaining_entity` | Solar forecast remaining today (kWh) | `sensor.solar_forecast_remaining_today` |
| `en_tesla_soc_entity` | EV battery level (%) | `sensor.ev_battery_level` |
| `en_tesla_location_entity` | EV location tracker (home/away gates the EV node) | `device_tracker.ev_location` |
| `en_df_pv_to_home_entity` … | The thirteen daily flow meters (kWh): `pv/battery/grid` → `home/battery/grid/ev/hp` | `sensor.daily_flow_pv_to_home` |

The battery pill and battery node read the shared battery sensors declared in
`ui-shared.yaml` — bind `battery_power_entity` (kW, +charge / −discharge) and
`battery_soc_entity` (%) in the same config.

### Home Assistant entities

- Live node powers from your inverter/charger integrations (any brand — the
  substitutions decouple the page from specific integrations).
- The `sensor.daily_flow_*` meters from
  [`homeassistant/examples/energy_daily_flows.yaml`](../../homeassistant/examples/energy_daily_flows.yaml):
  template sensors allocate instantaneous flows (PV serves load first, then
  battery, then export; shortfall from battery, then grid), Riemann-sum
  integrations accumulate them, and daily utility meters reset at midnight.

---
[← Alarm](alarm.md) · Next: [Surf conditions →](swelligence.md) · [Back to customisation](../CUSTOMISATION.md)
