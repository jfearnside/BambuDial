#pragma once

#include "printer_state.h"
#include "lvgl.h"

/* Create the LVGL UI. Must be called with LVGL lock held. */
void ui_init(printer_manager_t *mgr);

/* Refresh UI from current printer state. Called periodically by LVGL timer. */
void ui_update(void);

/* Force UI to show the currently selected printer (after encoder rotation). */
void ui_show_selected(void);

/* Get the progress arc widget (used as encoder focus target). */
lv_obj_t *ui_get_arc(void);

/* Manually cycle the info page (called from button press). */
void ui_cycle_info_page(void);
