#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include "lvgl_screenshot.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cstring>
#include <strings.h>

// ---------------------------------------------------------------------------
// LVGL v9 port (vendored from dcgrove/esphome-lvgl-screenshot, originally
// written for LVGL v8). ESPHome 2026.4.0 moved to LVGL 9.5; this file is the
// HomeAutomation-local fork that builds against it.
//
// Capture strategy — lv_snapshot (NOT lv_display_get_buf_active):
//   The original v8 code read the display's active render buffer directly.
//   That only works when LVGL is in FULL render mode (one screen-sized buffer
//   that always holds the whole frame). This panel runs LVGL in PARTIAL mode
//   (ESPHome default — `buffer_size: 100%` sizes the buffer but does NOT set
//   the render mode), so the active buffer only ever holds the last redrawn
//   dirty region, laid out at the dirty area's width. Reading it as a full
//   720-wide frame produced a half-width-ghosted clock over stale-PSRAM noise.
//
//   Instead we ask LVGL to render the active screen into a fresh buffer on
//   demand via lv_snapshot_take(lv_screen_active(), RGB888). This is render-
//   mode-independent and always yields the complete current frame. It requires
//   LV_USE_SNAPSHOT=1 — enabled via CONFIG_LV_USE_SNAPSHOT=y in the device's
//   sdkconfig_options (see modules/display-capture.yaml). Must be called from
//   the LVGL/main task — we do, from loop() via the capture semaphore.
//
//   RGB888 byte order: LVGL stores LV_COLOR_FORMAT_RGB888 as {blue,green,red}
//   in memory; stb's JPEG writer wants R,G,B. We swap channels during the
//   repack. If a capture ever comes out red/blue swapped, flip
//   LVGL_SS_SWAP_RB below.
//
// Overlay compositing (?overlays=1):
//   The base snapshot above only re-renders the active *screen*. LVGL 9 keeps
//   overlays (top bar, voice-persona eye, perf HUD, position dots, Rooms /
//   Playlists / takeover modals) on two separate display layers reachable via
//   lv_layer_top() / lv_layer_sys(). When ?overlays=1 is requested we also
//   snapshot each layer in LV_COLOR_FORMAT_ARGB8888 (per-pixel alpha; an empty
//   layer is fully transparent) and alpha-composite them onto rgb_buf_ in
//   LVGL's draw order: base -> top -> sys. Each layer buffer is freed
//   immediately after compositing to cap peak PSRAM use (one ~2 MB ARGB layer
//   live at a time on top of the persistent RGB/JPEG buffers).
//
//   ARGB8888 byte order: LVGL stores it little-endian 0xAARRGGBB, i.e. memory
//   {blue,green,red,alpha}. Alpha is always byte [3]; the R/B channels follow
//   the same LVGL_SS_SWAP_RB convention as RGB888.
// ---------------------------------------------------------------------------

// Set to 0 if on-device captures show red/blue swapped (see note above).
#define LVGL_SS_SWAP_RB 1

// LVGL 9's software renderer emits straight (non-premultiplied) alpha for
// ARGB8888 snapshots, so we use the straight-alpha over operator. If composited
// overlays show dark halos / fringing around anti-aliased edges, the buffer is
// premultiplied — set this to 1 to switch to the premultiplied over operator.
#define LVGL_SS_PREMULTIPLIED_OVERLAY 0

namespace esphome {
namespace lvgl_screenshot {

static const char *const TAG = "lvgl_screenshot";

LvglScreenshot *LvglScreenshot::instance_ = nullptr;

// Context passed to stb's JPEG write callback
struct JpegWriteCtx {
  uint8_t *buf;
  size_t capacity;
  size_t size;
  bool truncated;
};

// ---------------------------------------------------------------------------
// jpeg_write_cb_()  –  stb calls this repeatedly as it produces JPEG data
// ---------------------------------------------------------------------------
void LvglScreenshot::jpeg_write_cb_(void *ctx, void *data, int size) {
  auto *c = (JpegWriteCtx *) ctx;
  if (size <= 0 || !data)
    return;
  size_t avail = c->capacity - c->size;
  size_t copy = std::min((size_t) size, avail);
  if (copy < (size_t) size) {
    ESP_LOGW(TAG, "JPEG buffer full — truncating %d → %u bytes", size, (unsigned) copy);
    c->truncated = true;
  }
  memcpy(c->buf + c->size, data, copy);
  c->size += copy;
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void LvglScreenshot::setup() {
  instance_ = this;

  // Create semaphores for HTTP handler <-> main-loop synchronisation
  this->capture_requested_ = xSemaphoreCreateBinary();
  this->capture_done_ = xSemaphoreCreateBinary();

  if (!this->capture_requested_ || !this->capture_done_) {
    ESP_LOGE(TAG, "Failed to create semaphores");
    this->mark_failed();
    return;
  }

  // Determine display dimensions from the default LVGL display (v9 API)
  lv_display_t *disp = lv_display_get_default();
  if (!disp) {
    ESP_LOGE(TAG, "No LVGL display found - is lvgl: initialised before this component?");
    this->mark_failed();
    return;
  }

  uint32_t width = (uint32_t) lv_display_get_horizontal_resolution(disp);
  uint32_t height = (uint32_t) lv_display_get_vertical_resolution(disp);

  // RGB888 intermediate buffer: width * height * 3 bytes
  size_t rgb_size = width * height * 3u;
  this->rgb_buf_ = (uint8_t *) heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);
  this->rgb_capacity_ = rgb_size;
  if (!this->rgb_buf_) {
    ESP_LOGE(TAG, "Failed to allocate %u bytes for RGB buffer in PSRAM", (unsigned) rgb_size);
    this->mark_failed();
    return;
  }

  // JPEG output buffer: allocate 60% of the raw RGB size — more than enough for quality 80
  this->jpeg_capacity_ = rgb_size * 6 / 10;
  this->jpeg_buf_ = (uint8_t *) heap_caps_malloc(this->jpeg_capacity_, MALLOC_CAP_SPIRAM);
  if (!this->jpeg_buf_) {
    ESP_LOGE(TAG, "Failed to allocate %u bytes for JPEG buffer in PSRAM", (unsigned) this->jpeg_capacity_);
    this->mark_failed();
    return;
  }

  this->jpeg_size_ = 0;

  if (!this->start_server_()) {
    ESP_LOGE(TAG, "LVGL screenshot server NOT started");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "LVGL screenshot server started — http://<device-ip>:%u/screenshot", this->port_);
}

// ---------------------------------------------------------------------------
// start_server_()  –  spin up esp_http_server on the configured port
// ---------------------------------------------------------------------------
bool LvglScreenshot::start_server_() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = this->port_;
  cfg.stack_size = 8192;
  // Use a unique ctrl_port so it doesn't clash with any other httpd instance
  cfg.ctrl_port = (uint16_t) (this->port_ + 1u);

  if (httpd_start(&this->server_, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server on port %u", this->port_);
    this->server_ = nullptr;
    return false;
  }

  httpd_uri_t uri = {
      .uri = "/screenshot",
      .method = HTTP_GET,
      .handler = LvglScreenshot::handle_screenshot_,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(this->server_, &uri);
  return true;
}

// ---------------------------------------------------------------------------
// switch_to_page_()  –  show a page now (no animation), remembering its parent
// and prior index so CAP_SETTLING can restore the panel afterwards.
// ---------------------------------------------------------------------------
void LvglScreenshot::switch_to_page_(lvgl::LvPageType *page) {
  lvgl::LvglComponent *lv = page->get_parent();
  this->saved_parent_ = lv;
  this->saved_page_index_ = lv->get_current_page();
  lv->show_page(page->index, LV_SCR_LOAD_ANIM_NONE, 0);
  this->page_was_switched_ = true;
}

// ---------------------------------------------------------------------------
// loop()  –  called from the ESPHome main task; safe to touch LVGL here.
//
// Runs as a 2-state machine so that, when a ?page= switch is requested, we can
// hand control back to the main loop (and thus LVGL's own timer handler) while
// the new page settles, instead of blocking here with a delay.
// ---------------------------------------------------------------------------
void LvglScreenshot::loop() {
  switch (this->capture_state_) {
    case CAP_IDLE: {
      // Non-blocking check: did the HTTP handler signal a capture request?
      if (xSemaphoreTake(this->capture_requested_, 0) != pdTRUE)
        return;

      this->page_was_switched_ = false;
      this->shown_widgets_.clear();
      this->pending_after_.clear();
      this->pending_modal_ = nullptr;
      this->modal_prepped_ = false;
      uint32_t settle = 0;

      if (!this->requested_modal_.empty()) {
        // ?modal=<name> takes precedence over ?page=. Switch to the context page
        // now; the modal's before-hooks + show happen after the page-load settle
        // (CAP_SETTLING), because show_page defers the swap to the next LVGL tick
        // and some before-hooks (the idle voice cameo) depend on the new screen
        // actually being active.
        auto it = this->modals_.find(this->requested_modal_);
        if (it != this->modals_.end() && it->second.page != nullptr) {
          this->pending_modal_ = &it->second;
          this->switch_to_page_(it->second.page);
          this->requested_overlays_ = true;  // top_layer modals need compositing
          settle = this->settle_ms_;
        } else {
          ESP_LOGW(TAG, "Unknown screenshot modal '%s' — capturing current screen",
                   this->requested_modal_.c_str());
        }
      } else if (!this->requested_page_.empty()) {
        auto it = this->pages_.find(this->requested_page_);
        if (it != this->pages_.end() && it->second != nullptr) {
          this->switch_to_page_(it->second);
          settle = this->settle_ms_;  // give layout + on_load handlers time
        } else {
          ESP_LOGW(TAG, "Unknown screenshot page '%s' — capturing current screen",
                   this->requested_page_.c_str());
        }
      }

      this->settle_until_ = millis() + settle;
      this->capture_state_ = CAP_SETTLING;
      return;
    }

    case CAP_SETTLING: {
      // Keep yielding to the main loop until the settle window elapses.
      if ((int32_t) (millis() - this->settle_until_) < 0)
        return;

      // First settle done: the context page is now loaded. For a modal, run its
      // before-hooks and reveal it, then settle once more so it lays out/animates
      // before the shot.
      if (this->pending_modal_ != nullptr && !this->modal_prepped_) {
        ModalDef &m = *this->pending_modal_;
        for (auto *s : m.before)
          if (s != nullptr)
            s->execute();
        for (auto *w : m.widgets) {
          if (w != nullptr) {
            lv_obj_clear_flag(w, LV_OBJ_FLAG_HIDDEN);
            this->shown_widgets_.push_back(w);
          }
        }
        this->pending_after_ = m.after;
        this->modal_prepped_ = true;
        this->settle_until_ = millis() + this->settle_ms_;
        return;
      }

      this->do_capture_();

      // Re-hide any modal widgets we un-hid for the shot.
      for (auto *w : this->shown_widgets_)
        if (w != nullptr)
          lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
      this->shown_widgets_.clear();

      // Return the panel to wherever the user had it.
      if (this->page_was_switched_ && this->restore_page_ && this->saved_parent_ != nullptr)
        this->saved_parent_->show_page(this->saved_page_index_, LV_SCR_LOAD_ANIM_NONE, 0);

      // after-hooks run once we're back on the original page — e.g. resetting the
      // voice phase to idle so va_render hides the persona off the screensaver.
      for (auto *s : this->pending_after_)
        if (s != nullptr)
          s->execute();
      this->pending_after_.clear();

      this->pending_modal_ = nullptr;
      this->modal_prepped_ = false;
      this->requested_page_.clear();
      this->requested_modal_.clear();
      this->capture_state_ = CAP_IDLE;
      // loop() owns clearing in_progress_: if the HTTP handler timed out and
      // returned, the machine was still mid-capture — only now is it safe for a
      // new request to claim the slot (and overwrite the request strings/buffers).
      // Give BEFORE clearing so any stale token (timed-out waiter) is already
      // posted when the next handler claims the slot and drains capture_done_.
      xSemaphoreGive(this->capture_done_);
      this->in_progress_ = false;
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// do_capture_()  –  read LVGL's active RGB565 buffer, convert to RGB888, JPEG
// ---------------------------------------------------------------------------
void LvglScreenshot::do_capture_() {
  lv_obj_t *screen = lv_screen_active();
  if (!screen) {
    ESP_LOGE(TAG, "No active LVGL screen");
    this->jpeg_size_ = 0;
    return;
  }

  // Render the whole active screen into a fresh RGB888 draw buffer. This is
  // independent of the display's render mode (PARTIAL vs FULL), so it captures
  // the complete current frame even though LVGL only keeps a dirty-region
  // buffer live. The allocation goes to PSRAM via ESPHome's draw_buf handlers.
  lv_draw_buf_t *snap = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
  if (!snap || !snap->data) {
    ESP_LOGE(TAG, "lv_snapshot_take failed (is CONFIG_LV_USE_SNAPSHOT=y set?)");
    this->jpeg_size_ = 0;
    if (snap)
      lv_draw_buf_destroy(snap);
    return;
  }

  uint32_t width = snap->header.w;
  uint32_t height = snap->header.h;
  // Snapshot stride is in bytes and may be padded for alignment; honour it.
  uint32_t stride = snap->header.stride ? snap->header.stride : (width * 3u);

  // The snapshot should match the setup-time display size; guard against a
  // mismatch rather than writing out of bounds of rgb_buf_.
  if ((size_t) width * height * 3u > this->rgb_capacity_) {
    ESP_LOGE(TAG, "Snapshot %ux%u exceeds RGB buffer (%u bytes) — aborting capture",
             width, height, (unsigned) this->rgb_capacity_);
    this->jpeg_size_ = 0;
    lv_draw_buf_destroy(snap);
    return;
  }

  // ------------------------------------------------------------------
  // Repack RGB888 (LVGL B,G,R order, possibly row-padded) into the tightly
  // packed R,G,B buffer stb expects (no per-row padding, no stride param).
  // ------------------------------------------------------------------
  for (uint32_t y = 0; y < height; y++) {
    const uint8_t *src = snap->data + (size_t) y * stride;
    uint8_t *row = this->rgb_buf_ + (size_t) y * width * 3u;
    for (uint32_t x = 0; x < width; x++) {
      const uint8_t *px = src + x * 3u;
#if LVGL_SS_SWAP_RB
      row[x * 3 + 0] = px[2];  // R
      row[x * 3 + 1] = px[1];  // G
      row[x * 3 + 2] = px[0];  // B
#else
      row[x * 3 + 0] = px[0];
      row[x * 3 + 1] = px[1];
      row[x * 3 + 2] = px[2];
#endif
    }
  }

  // Done with the snapshot buffer — free before encoding to cap peak PSRAM use.
  lv_draw_buf_destroy(snap);

  // ------------------------------------------------------------------
  // Optional: composite the LVGL overlay layers over the base frame, in draw
  // order (base -> top -> sys). Each layer is snapshotted in ARGB8888 and
  // freed promptly so peak PSRAM stays bounded.
  // ------------------------------------------------------------------
  if (this->requested_overlays_) {
    this->composite_layer_(lv_layer_top(), width, height);
    this->composite_layer_(lv_layer_sys(), width, height);
  }

  // ------------------------------------------------------------------
  // Encode RGB888 → JPEG via stb_image_write (quality 80)
  // ------------------------------------------------------------------
  JpegWriteCtx ctx = {this->jpeg_buf_, this->jpeg_capacity_, 0, false};
  stbi_write_jpg_to_func(LvglScreenshot::jpeg_write_cb_, &ctx,
                         (int) width, (int) height, 3, this->rgb_buf_, 80);

  this->jpeg_size_ = ctx.size;
  this->jpeg_truncated_ = ctx.truncated;
  ESP_LOGD(TAG, "Captured %ux%u JPEG (%u bytes)", width, height, (unsigned) this->jpeg_size_);
}

// ---------------------------------------------------------------------------
// composite_layer_()  –  snapshot one display layer in ARGB8888 and alpha-blend
// it over rgb_buf_ (which already holds R,G,B). Called only from the LVGL task.
// ---------------------------------------------------------------------------
void LvglScreenshot::composite_layer_(lv_obj_t *layer, uint32_t width, uint32_t height) {
  if (!layer)
    return;

  // An empty layer is fully transparent — skip the ~2 MB allocation + render.
  if (lv_obj_get_child_count(layer) == 0)
    return;

  lv_draw_buf_t *snap = lv_snapshot_take(layer, LV_COLOR_FORMAT_ARGB8888);
  if (!snap || !snap->data) {
    ESP_LOGW(TAG, "Overlay snapshot failed for a layer — skipping");
    if (snap)
      lv_draw_buf_destroy(snap);
    return;
  }

  // Layer snapshots should match the screen size; guard against a mismatch
  // rather than reading out of bounds of rgb_buf_.
  if (snap->header.w != width || snap->header.h != height) {
    ESP_LOGW(TAG, "Overlay size %lux%lu != base %ux%u — skipping",
             (unsigned long) snap->header.w, (unsigned long) snap->header.h, width, height);
    lv_draw_buf_destroy(snap);
    return;
  }

  uint32_t stride = snap->header.stride ? snap->header.stride : (width * 4u);

  for (uint32_t y = 0; y < height; y++) {
    const uint8_t *src = snap->data + (size_t) y * stride;
    uint8_t *dst = this->rgb_buf_ + (size_t) y * width * 3u;
    for (uint32_t x = 0; x < width; x++) {
      const uint8_t *px = src + x * 4u;
      uint32_t a = px[3];  // alpha is the high byte of 0xAARRGGBB regardless of swap
      if (a == 0)
        continue;  // fully transparent overlay pixel — leave base untouched
#if LVGL_SS_SWAP_RB
      uint32_t sr = px[2], sg = px[1], sb = px[0];
#else
      uint32_t sr = px[0], sg = px[1], sb = px[2];
#endif
      uint8_t *d = dst + x * 3u;
      if (a == 255) {
        d[0] = (uint8_t) sr;
        d[1] = (uint8_t) sg;
        d[2] = (uint8_t) sb;
        continue;
      }
      uint32_t inv = 255u - a;
#if LVGL_SS_PREMULTIPLIED_OVERLAY
      // src channels already premultiplied by alpha: out = src + dst*(1-a)
      d[0] = (uint8_t) std::min(255u, sr + (d[0] * inv + 127u) / 255u);
      d[1] = (uint8_t) std::min(255u, sg + (d[1] * inv + 127u) / 255u);
      d[2] = (uint8_t) std::min(255u, sb + (d[2] * inv + 127u) / 255u);
#else
      // straight alpha: out = src*a + dst*(1-a)
      d[0] = (uint8_t) ((sr * a + d[0] * inv + 127u) / 255u);
      d[1] = (uint8_t) ((sg * a + d[1] * inv + 127u) / 255u);
      d[2] = (uint8_t) ((sb * a + d[2] * inv + 127u) / 255u);
#endif
    }
  }

  lv_draw_buf_destroy(snap);
}

// ---------------------------------------------------------------------------
// handle_screenshot_()  –  runs in esp_http_server's task, NOT the main loop
// ---------------------------------------------------------------------------
esp_err_t LvglScreenshot::handle_screenshot_(httpd_req_t *req) {
  LvglScreenshot *self = instance_;
  if (!self || !self->jpeg_buf_) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Component not ready");
    return ESP_FAIL;
  }

  // Only one capture at a time. Atomically claim the slot; it stays claimed
  // until loop() returns to CAP_IDLE (a timed-out request does NOT release it,
  // since the capture machine may still be mid-capture).
  if (self->in_progress_.exchange(true)) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Capture in progress, try again");
    return ESP_OK;
  }

  // Optional query params:
  //   ?page=<name>   — switch the panel to that page before capturing
  //   ?overlays=1    — also composite the top/sys overlay layers (default off)
  //   ?modal=<name>  — open a registered modal/overlay for the shot (implies overlays)
  self->requested_page_.clear();
  self->requested_modal_.clear();
  self->requested_overlays_ = false;
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen > 0 && qlen < 256) {
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
      char val[64];
      if (httpd_query_key_value(query, "page", val, sizeof(val)) == ESP_OK)
        self->requested_page_ = val;
      if (httpd_query_key_value(query, "modal", val, sizeof(val)) == ESP_OK)
        self->requested_modal_ = val;
      if (httpd_query_key_value(query, "overlays", val, sizeof(val)) == ESP_OK) {
        self->requested_overlays_ =
            (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
             strcasecmp(val, "on") == 0 || strcasecmp(val, "yes") == 0);
      }
    }
  }

  // Drain any stale completion token left by a previous timed-out request
  // (loop() gives capture_done_ even after the waiter has gone away).
  xSemaphoreTake(self->capture_done_, 0);

  // Ask the main loop to do the capture
  xSemaphoreGive(self->capture_requested_);

  // Wait for the main loop to finish: a normal capture is ~16 ms, but a ?page=
  // switch adds the settle window (and a ?modal= adds a second one), so budget
  // for both plus headroom.
  uint32_t wait_ms = 3000u + 2u * self->settle_ms_;
  if (xSemaphoreTake(self->capture_done_, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
    // Do NOT clear in_progress_: loop() may still be mid-capture and owns the
    // release when it returns to CAP_IDLE.
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture timed out");
    return ESP_FAIL;
  }

  if (self->jpeg_size_ == 0) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Framebuffer unavailable");
    return ESP_FAIL;
  }

  if (self->jpeg_truncated_) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JPEG buffer overflow — image truncated");
    return ESP_FAIL;
  }

  // Send headers
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"screenshot.jpg\"");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");

  // Stream the JPEG in 4 KB chunks
  const size_t CHUNK = 4096;
  size_t sent = 0;
  esp_err_t ret = ESP_OK;
  while (sent < self->jpeg_size_) {
    size_t chunk_len = std::min(CHUNK, self->jpeg_size_ - sent);
    ret = httpd_resp_send_chunk(req, (const char *) self->jpeg_buf_ + sent, (ssize_t) chunk_len);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to send chunk at offset %u", (unsigned) sent);
      break;
    }
    sent += chunk_len;
  }

  // Terminate chunked transfer. in_progress_ was already released by loop()
  // when the capture machine returned to CAP_IDLE; httpd's single server task
  // serialises requests, so nothing overwrites jpeg_buf_ while we stream it.
  httpd_resp_send_chunk(req, nullptr, 0);

  return ret;
}

}  // namespace lvgl_screenshot
}  // namespace esphome

#endif  // USE_ESP_IDF
