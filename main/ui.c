#include "ui.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui";

static printer_manager_t *s_mgr;

/* UI elements */
static lv_obj_t *scr;
static lv_obj_t *lbl_name;
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_progress;
static lv_obj_t *arc_progress;
static lv_obj_t *lbl_nozzle;
static lv_obj_t *lbl_bed;
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_file;
static lv_obj_t *lbl_layer;
static lv_obj_t *ind_conn;           /* connection indicator dot */
static lv_obj_t *lbl_selector;       /* "1/3" printer selector indicator */

static lv_timer_t *update_timer;

static void update_timer_cb(lv_timer_t *timer)
{
    ui_update();
}

void ui_init(printer_manager_t *mgr)
{
    s_mgr = mgr;

    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111111), 0);

    /* --- Progress arc (outer ring) --- */
    arc_progress = lv_arc_create(scr);
    lv_obj_set_size(arc_progress, 230, 230);
    lv_obj_center(arc_progress);
    lv_arc_set_rotation(arc_progress, 270);
    lv_arc_set_range(arc_progress, 0, 100);
    lv_arc_set_value(arc_progress, 0);
    lv_arc_set_bg_angles(arc_progress, 0, 360);
    lv_obj_remove_style(arc_progress, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_progress, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x00CC66), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x333333), LV_PART_MAIN);

    /* --- Printer name (top) --- */
    lbl_name = lv_label_create(scr);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_name, "---");

    /* --- Connection indicator (small dot next to name) --- */
    ind_conn = lv_obj_create(scr);
    lv_obj_set_size(ind_conn, 8, 8);
    lv_obj_align_to(ind_conn, lbl_name, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    lv_obj_set_style_radius(ind_conn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ind_conn, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_border_width(ind_conn, 0, 0);

    /* --- Printer selector "1/2" --- */
    lbl_selector = lv_label_create(scr);
    lv_obj_align(lbl_selector, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(lbl_selector, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(lbl_selector, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_selector, "1/1");

    /* --- Status text --- */
    lbl_status = lv_label_create(scr);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_status, "---");

    /* --- Large progress number --- */
    lbl_progress = lv_label_create(scr);
    lv_obj_align(lbl_progress, LV_ALIGN_CENTER, 0, -5);
    lv_obj_set_style_text_color(lbl_progress, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_progress, &lv_font_montserrat_36, 0);
    lv_label_set_text(lbl_progress, "--");

    /* --- Nozzle temperature --- */
    lbl_nozzle = lv_label_create(scr);
    lv_obj_align(lbl_nozzle, LV_ALIGN_CENTER, -40, 30);
    lv_obj_set_style_text_color(lbl_nozzle, lv_color_hex(0xFF6633), 0);
    lv_obj_set_style_text_font(lbl_nozzle, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_nozzle, "N: --");

    /* --- Bed temperature --- */
    lbl_bed = lv_label_create(scr);
    lv_obj_align(lbl_bed, LV_ALIGN_CENTER, 40, 30);
    lv_obj_set_style_text_color(lbl_bed, lv_color_hex(0x3399FF), 0);
    lv_obj_set_style_text_font(lbl_bed, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_bed, "B: --");

    /* --- Remaining time --- */
    lbl_time = lv_label_create(scr);
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 65);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_time, "");

    /* --- Layer info --- */
    lbl_layer = lv_label_create(scr);
    lv_obj_align(lbl_layer, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_text_color(lbl_layer, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_layer, &lv_font_montserrat_10, 0);
    lv_label_set_text(lbl_layer, "");

    /* --- File name / error message (below temps, inside the arc) --- */
    lbl_file = lv_label_create(scr);
    lv_obj_set_width(lbl_file, 180);
    lv_obj_align(lbl_file, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_text_color(lbl_file, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(lbl_file, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(lbl_file, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_file, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_file, "");

    /* Create periodic update timer */
    update_timer = lv_timer_create(update_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI initialized");
}

void ui_update(void)
{
    if (!s_mgr) return;
    if (!printer_manager_lock(s_mgr, pdMS_TO_TICKS(50))) return;

    int idx = s_mgr->selected;
    printer_state_t *ps = &s_mgr->printers[idx];

    /* Printer name */
    lv_label_set_text(lbl_name, ps->name);

    /* Connection indicator */
    lv_obj_set_style_bg_color(ind_conn,
        ps->mqtt_connected ? lv_color_hex(0x00CC66) : lv_color_hex(0xFF3333), 0);
    lv_obj_align_to(ind_conn, lbl_name, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    /* Selector "1/2" */
    char sel_buf[16];
    snprintf(sel_buf, sizeof(sel_buf), "%d/%d", idx + 1, s_mgr->num_printers);
    lv_label_set_text(lbl_selector, sel_buf);

    /* Status text — show error message if there is one */
    const char *state_name = print_state_name(ps->state);
    bool has_error = (ps->print_error != 0 && ps->error_message[0] != '\0');

    if (has_error) {
        lv_label_set_text(lbl_status, "ERROR");
    } else {
        lv_label_set_text(lbl_status, state_name);
    }

    uint32_t color = has_error ? 0xFF3333 : print_state_color(ps->state);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(color), 0);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(color), LV_PART_INDICATOR);

    /* Progress */
    if (has_error) {
        lv_label_set_text(lbl_progress, "!");
        lv_arc_set_value(arc_progress, 0);
    } else if (ps->state == PRINT_STATE_PRINTING || ps->state == PRINT_STATE_PAUSED) {
        char prog_buf[8];
        snprintf(prog_buf, sizeof(prog_buf), "%d%%", ps->progress);
        lv_label_set_text(lbl_progress, prog_buf);
        lv_arc_set_value(arc_progress, ps->progress);
    } else {
        lv_label_set_text(lbl_progress, state_name);
        lv_arc_set_value(arc_progress, ps->state == PRINT_STATE_FINISHED ? 100 : 0);
    }

    /* Temperatures */
    char temp_buf[24];
    snprintf(temp_buf, sizeof(temp_buf), "N:%.0f/%.0f", ps->nozzle_temp, ps->nozzle_target);
    lv_label_set_text(lbl_nozzle, temp_buf);
    snprintf(temp_buf, sizeof(temp_buf), "B:%.0f/%.0f", ps->bed_temp, ps->bed_target);
    lv_label_set_text(lbl_bed, temp_buf);

    /* Remaining time */
    if (ps->state == PRINT_STATE_PRINTING && ps->remaining_min > 0) {
        int hrs = ps->remaining_min / 60;
        int mins = ps->remaining_min % 60;
        char time_buf[24];
        if (hrs > 0) {
            snprintf(time_buf, sizeof(time_buf), "%dh %dm left", hrs, mins);
        } else {
            snprintf(time_buf, sizeof(time_buf), "%dm left", mins);
        }
        lv_label_set_text(lbl_time, time_buf);
    } else {
        lv_label_set_text(lbl_time, "");
    }

    /* Layer info */
    if (ps->state == PRINT_STATE_PRINTING && ps->total_layers > 0) {
        char layer_buf[24];
        snprintf(layer_buf, sizeof(layer_buf), "Layer %d/%d", ps->layer_num, ps->total_layers);
        lv_label_set_text(lbl_layer, layer_buf);
    } else {
        lv_label_set_text(lbl_layer, "");
    }

    /* File name / error message */
    if (has_error) {
        lv_label_set_text(lbl_file, ps->error_message);
        lv_obj_set_style_text_color(lbl_file, lv_color_hex(0xFF6666), 0);
    } else if (ps->subtask_name[0] != '\0') {
        lv_label_set_text(lbl_file, ps->subtask_name);
        lv_obj_set_style_text_color(lbl_file, lv_color_hex(0x666666), 0);
    } else {
        lv_label_set_text(lbl_file, "");
    }

    printer_manager_unlock(s_mgr);
}

void ui_show_selected(void)
{
    ui_update();
}

lv_obj_t *ui_get_arc(void)
{
    return arc_progress;
}
