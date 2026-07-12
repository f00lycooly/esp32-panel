# Navigation

The panel's screens are arranged on a **2D grid**: *functions* stacked
vertically, *pages* within a function side by side.

- **Vertical swipe** changes **function** (Music → Climate → Timer → Energy →
  Surf, on the reference layout).
- **Horizontal swipe** changes **page** within the function (e.g. Climate ↔
  Hot water).
- Swipes stop at the ends — no wrap-around — and landing on a function always
  lands on that function's **HOME page** (column 0).
- Transitions are 300 ms slides: vertical for function changes, horizontal for
  page changes.

```
        page 0        page 1
  F0    Main          Multi-room      ← the boot / HOME screen is F0 P0
  F1    Climate       Hot water
  F2    Timer         Alarm
  F3    Energy        Today
  F4    Spot 1        Spot 2 …       (generated from the surf manifest)
```

### Dot rails

Position chrome is drawn once on LVGL's top layer:

- a **vertical dot rail** on the right edge — one dot per function, the active
  one green;
- a **horizontal dot rail** under the top bar — one dot per page in the
  current function (only shown when there is more than one), the active one
  blue.

The dots are fixed compile-time widgets, which caps the grid at **5 functions ×
4 pages per function**.

### The contract

The grid is *data, not code*: `nav2d.yaml` (the engine) reads four
substitutions from your device file — `nav_functions`, `nav_pages_per_func`,
`nav_npages` and `nav_pages_matrix` — so adding, removing or reordering a
screen is a composition-root edit only (see
[../CUSTOMISATION.md](../CUSTOMISATION.md#navigation-contract)). A page opts
into swiping by including the shared swipe-handler snippet and reporting its
coordinates on load (`nav_arrive`), which keeps the dot rails honest however
the page was reached — swipe, top-bar shortcut, or boot.

The surf function's row of the matrix is **generated** from
`swell-manifest.yaml`, so spot count changes never touch the contract by hand.

### The top bar

One singleton bar floats over every function page (pages keep their content
below it):

| Slot | What it does |
|---|---|
| **Left** — battery pill | Home-battery SOC + power flow, coloured by state; **tap → [Energy](energy.md)** |
| **Centre** — clock + date | |
| **Right** — settings cog | **Tap → [Info](info.md)** takeover; its border ring is green/red with HA connectivity |

One 2-second refresh paints the whole bar (and the screensaver's battery pill)
— a single battery painter everywhere.

### Takeovers

Focused sub-views — Pick Music, Browse, Set Timer, the alarm pickers,
Settings — run as **takeovers**: swipe navigation is suspended, the dot rails
hide, and a back chevron returns you to the exact screen you left. Two shapes,
one contract:

- **Page takeovers** — a separate LVGL page (marked `skip: true`, so never in
  the swipe ring) with its own header chevron; it may implement its own
  internal horizontal pager.
- **Overlay takeovers** — a top-layer widget shown over the live page
  (Rooms/Playlists modals, the ringing overlays, the
  [voice persona](voice.md)).

### The idle path

After 45 s without touches the panel shows the
[screensaver](screensaver.md) (itself outside the swipe ring); any tap returns
to Main, re-entering the grid at F0 P0.

---
[← Info](info.md) · [Screens index](README.md) · [Back to customisation](../CUSTOMISATION.md)
