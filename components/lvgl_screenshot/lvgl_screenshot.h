#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esphome/components/script/script.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace lvgl {
class LvPageType;     // fwd-decl; full def pulled into the .cpp only
class LvglComponent;  // fwd-decl; used only as a pointer (page's parent)
}  // namespace lvgl

namespace lvgl_screenshot {

class LvglScreenshot : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_settle_ms(uint32_t ms) { this->settle_ms_ = ms; }
  void set_restore_page(bool restore) { this->restore_page_ = restore; }
  void register_page(const std::string &name, lvgl::LvPageType *page) { this->pages_[name] = page; }

  // Modal registration (?modal=<name>). Built incrementally from codegen so we
  // never pass a std::vector across the Python/C++ boundary.
  void register_modal(const std::string &name, lvgl::LvPageType *page) { this->modals_[name].page = page; }
  void modal_add_show(const std::string &name, lv_obj_t *widget) { this->modals_[name].widgets.push_back(widget); }
  void modal_add_before(const std::string &name, script::Script<> *s) { this->modals_[name].before.push_back(s); }
  void modal_add_after(const std::string &name, script::Script<> *s) { this->modals_[name].after.push_back(s); }

 protected:
  // Capture proceeds as a small state machine driven from loop() so we can let
  // LVGL keep running (lv_timer_handler) between switching pages and capturing.
  enum CaptureState { CAP_IDLE, CAP_SETTLING };

  uint16_t port_{8080};
  httpd_handle_t server_{nullptr};

  // Named-page navigation (?page=<name>)
  std::map<std::string, lvgl::LvPageType *> pages_;
  uint32_t settle_ms_{300};
  bool restore_page_{true};
  std::string requested_page_;  // set by HTTP handler, consumed by loop()
  bool requested_overlays_{false};  // ?overlays=1 — composite top/sys layers

  // Modal capture (?modal=<name>): show an otherwise-hidden overlay for the shot.
  struct ModalDef {
    lvgl::LvPageType *page{nullptr};        // context page shown behind the modal
    std::vector<lv_obj_t *> widgets;        // widgets to un-hide (scrim, then modal)
    std::vector<script::Script<> *> before; // run after page switch, before capture
    std::vector<script::Script<> *> after;  // run after capture, before page restore
  };
  std::map<std::string, ModalDef> modals_;
  std::string requested_modal_;                 // set by HTTP handler, consumed by loop()
  ModalDef *pending_modal_{nullptr};            // resolved modal awaiting prep/capture
  bool modal_prepped_{false};                   // before-hooks+show done (after page-load settle)
  std::vector<lv_obj_t *> shown_widgets_;       // widgets to re-hide on restore
  std::vector<script::Script<> *> pending_after_;  // after-hooks to run on restore

  CaptureState capture_state_{CAP_IDLE};
  uint32_t settle_until_{0};
  size_t saved_page_index_{0};
  lvgl::LvglComponent *saved_parent_{nullptr};  // parent of the switched page (for restore)
  bool page_was_switched_{false};

  // Semaphore pair for synchronising HTTP handler <-> main loop
  SemaphoreHandle_t capture_requested_{nullptr};
  SemaphoreHandle_t capture_done_{nullptr};

  // Intermediate RGB888 buffer (stb input) — allocated in PSRAM
  uint8_t *rgb_buf_{nullptr};
  size_t rgb_capacity_{0};

  // JPEG output buffer — allocated in PSRAM
  uint8_t *jpeg_buf_{nullptr};
  size_t jpeg_capacity_{0};
  size_t jpeg_size_{0};
  bool jpeg_truncated_{false};  // last encode overflowed jpeg_buf_ (serve 500, not a torn image)

  // True while a capture is in flight (guards against concurrent requests).
  // Claimed by the HTTP handler (exchange), cleared by loop() when the capture
  // machine returns to CAP_IDLE — so a timed-out request can't hand the slot to
  // a new request while loop() is still mid-capture.
  std::atomic<bool> in_progress_{false};

  bool start_server_();
  void do_capture_();
  // Switch the panel to `page`, remembering its parent + prior index for restore.
  void switch_to_page_(lvgl::LvPageType *page);
  // Snapshot one display layer (top/sys) in ARGB8888 and alpha-composite it
  // over the already-populated rgb_buf_ (R,G,B). No-op for an empty layer.
  void composite_layer_(lv_obj_t *layer, uint32_t width, uint32_t height);

  static esp_err_t handle_screenshot_(httpd_req_t *req);
  static void jpeg_write_cb_(void *ctx, void *data, int size);

  static LvglScreenshot *instance_;
};

}  // namespace lvgl_screenshot
}  // namespace esphome

#endif  // USE_ESP_IDF
