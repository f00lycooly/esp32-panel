# Takeover framework

A **takeover** is a focused sub-view that **suspends the 2D swipe-nav** (nav2d,
) and is left via a **back chevron**. Used by Pick Music, Browse (drill-down),
Set Timer, the alarm sub-pickers (Speaker/Music/Who), and Settings.

Implemented in `modules/takeover.yaml`. Builds on `nav2d.yaml` (`nav_suspended`,
`nav_chrome`, `nav_arrive`).

## Two shapes, one contract

| Shape | What it is | Why nav_suspended matters |
|-------|------------|---------------------------|
| **Page** | Its own `lvgl` page that does **not** `<<: !include nav2d-swipe.yaml`. | The page isn't wired to `nav_go`, so function swipes can't fire while it's active. The flag is belt-and-braces. |
| **Overlay** | A `top_layer` widget shown over the live page (e.g. Rooms / Playlists modals). | The page underneath is still the active screen — its swipe handlers stay live, so **`nav_suspended` is what stops them navigating behind the overlay**. |

## Contract

```yaml
# OPEN — call takeover_enter, then show your page/overlay:
on_click:
  - script.execute: takeover_enter
  - lvgl.page.show: my_takeover_page      # PAGE
  # - lvgl.widget.show: my_overlay        # OVERLAY

# EXIT — the back chevron calls takeover_exit:
on_click:
  - script.execute: takeover_exit
```

- `takeover_enter` — records the screen to return to (`nav_return_screen`), raises
  `nav_suspended`, hides the nav position-dots (`nav_chrome 0`), and shows the
  **framework floating back chevron** (`takeover_back`, top-left).
- `takeover_exit` — hides the floating chevron, lowers `nav_suspended`, and slides
  back to the recorded screen. That screen's `on_load nav_arrive(f,p)` restores
  the coords + nav dots automatically.

## Back chevron: floating vs. own-header

- The framework provides a **floating** chevron on `top_layer`, shown by
  `takeover_enter`. Use it for **overlays** and for pages you can't edit (e.g. the
  stock library `info_screen` — this is how Settings works today).
- A **page** takeover that draws the standard sub-page top bar should host its own
  chevron in the header **LEFT** slot (`widgets/icon_button.yaml`,
  `glyph: $mdi_chevron_left`, `on_click: [{script.execute: takeover_exit}]`) and
  hide the floating one right after entering: `- lvgl.widget.hide: takeover_back`.
  See `widgets/PAGE-SHELL.md` for the header (back · title · action) layout.

## Internal pagination (page dots)

Single-level return is by design — drill-down (Browse) is **internal** to one
takeover page. A paginating takeover wires its **own** `on_swipe_left/right` to an
internal pager script and draws its **own** page dots (the nav2d horizontal dots
are reserved for function-pages). It must **not** call `nav_go`.

## Current users

- **Settings** — the main-page cog runs `takeover_enter` + `lvgl.page.show:
  info_screen`; the floating chevron returns to main. (The stock `info_screen`
  still carries the library's own swipe handlers; its full conversion to a
  native Settings takeover page lands in ****.)
