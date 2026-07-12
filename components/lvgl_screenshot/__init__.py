"""LVGL Screenshot Component - serves a JPEG snapshot of the active LVGL screen via HTTP.

Optionally accepts a `pages:` map so the endpoint can switch the panel to a named
page before capturing: GET /screenshot?page=<name>. Without ?page it captures
whatever is currently shown.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_NAME, CONF_PAGES, CONF_PORT

CODEOWNERS = ["@dcgrove"]
DEPENDENCIES = ["lvgl", "script"]

from esphome.components.lvgl.types import lv_obj_t, lv_page_t
from esphome.components.script import Script

lvgl_screenshot_ns = cg.esphome_ns.namespace("lvgl_screenshot")
LvglScreenshot = lvgl_screenshot_ns.class_("LvglScreenshot", cg.Component)

CONF_SETTLE_TIME = "settle_time"
CONF_RESTORE_PAGE = "restore_page"
CONF_PAGE_ID = "page"
CONF_MODALS = "modals"
CONF_SHOW = "show"
CONF_BEFORE = "before"
CONF_AFTER = "after"

PAGE_SCHEMA = cv.Schema(
    {
        # Query name used in ?page=<name>; the LVGL page id it maps to.
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_PAGE_ID): cv.use_id(lv_page_t),
    }
)

# A "modal" is an overlay captured via ?modal=<name>: switch to its context page,
# run the `before` scripts (populate / set voice phase), un-hide the `show`
# widgets, capture (overlays composited), then re-hide and run the `after`
# scripts (e.g. reset voice phase). `show` may be empty for state-driven overlays
# whose visibility a `before` script drives (the voice persona).
MODAL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_PAGE_ID): cv.use_id(lv_page_t),
        cv.Optional(CONF_SHOW): cv.ensure_list(cv.use_id(lv_obj_t)),
        cv.Optional(CONF_BEFORE): cv.ensure_list(cv.use_id(Script)),
        cv.Optional(CONF_AFTER): cv.ensure_list(cv.use_id(Script)),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglScreenshot),
        cv.Optional(CONF_PORT, default=8080): cv.port,
        # How long to let LVGL run after switching pages before capturing, so
        # layout settles and on_load handlers get a chance to populate widgets.
        cv.Optional(
            CONF_SETTLE_TIME, default="300ms"
        ): cv.positive_time_period_milliseconds,
        # Return to the previously-shown page after capturing a ?page= request,
        # so a remote screenshot does not hijack the physical panel.
        cv.Optional(CONF_RESTORE_PAGE, default=True): cv.boolean,
        cv.Optional(CONF_PAGES): cv.ensure_list(PAGE_SCHEMA),
        cv.Optional(CONF_MODALS): cv.ensure_list(MODAL_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_settle_ms(config[CONF_SETTLE_TIME].total_milliseconds))
    cg.add(var.set_restore_page(config[CONF_RESTORE_PAGE]))
    for page in config.get(CONF_PAGES, []):
        page_var = await cg.get_variable(page[CONF_PAGE_ID])
        cg.add(var.register_page(page[CONF_NAME], page_var))
    # Modals registered incrementally (one call per widget/script) so we never
    # pass a std::vector through codegen.
    for modal in config.get(CONF_MODALS, []):
        name = modal[CONF_NAME]
        page_var = await cg.get_variable(modal[CONF_PAGE_ID])
        cg.add(var.register_modal(name, page_var))
        for widget_id in modal.get(CONF_SHOW, []):
            cg.add(var.modal_add_show(name, await cg.get_variable(widget_id)))
        for script_id in modal.get(CONF_BEFORE, []):
            cg.add(var.modal_add_before(name, await cg.get_variable(script_id)))
        for script_id in modal.get(CONF_AFTER, []):
            cg.add(var.modal_add_after(name, await cg.get_variable(script_id)))
