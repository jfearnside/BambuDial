#include "ui.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ui";

/* MDI icon font for nozzle/bed symbols */
LV_FONT_DECLARE(mdi_30);

/* MDI icon codepoints (UTF-8 encoded) */
static const char *ICON_NOZZLE    = "\xF3\xB0\xB9\x9B";  /* printer-3d-nozzle */
static const char *ICON_BED_HEAT  = "\xF3\xB1\xA1\x9B";  /* bed with heat arrows */
static const char *ICON_BED_IDLE  = "\xF3\xB0\x90\xB8";  /* radiator (neutral) */

static printer_manager_t *s_mgr;

/* UI elements */
static lv_obj_t *scr;
static lv_obj_t *lbl_name;
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_progress;
static lv_obj_t *arc_progress;
static lv_obj_t *lbl_nozzle_icon;
static lv_obj_t *lbl_nozzle;
static lv_obj_t *lbl_bed_icon;
static lv_obj_t *lbl_bed;
static lv_obj_t *lbl_info1;       /* bottom info line 1 (y=42) */
static lv_obj_t *lbl_info2;       /* bottom info line 2 (y=58) */
static lv_obj_t *lbl_info3;       /* bottom info line 3 (y=74) */
static lv_obj_t *dot_filament;    /* colored dot for active filament */
static lv_obj_t *ind_conn;
static lv_obj_t *lbl_selector;

static lv_timer_t *update_timer;

/* Info page rotation: 0=time, 1=ETA, 2=AMS/stage */
static int s_info_page = 0;
static int s_tick = 0;
#define INFO_PAGE_DURATION 5  /* seconds per page */

static void update_timer_cb(lv_timer_t *timer)
{
    s_tick++;
    if (s_tick % INFO_PAGE_DURATION == 0) {
        /* Determine max pages based on AMS availability */
        int max_pages = 2;  /* 0=time, 1=ETA */
        if (s_mgr && printer_manager_lock(s_mgr, pdMS_TO_TICKS(20))) {
            if (s_mgr->printers[s_mgr->selected].ams_count > 0)
                max_pages = 3;  /* add page 2: AMS/stage */
            printer_manager_unlock(s_mgr);
        }
        s_info_page = (s_info_page + 1) % max_pages;
    }
    ui_update();
}

void ui_init(printer_manager_t *mgr)
{
    s_mgr = mgr;

    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* --- Progress arc (outer ring) --- */
    arc_progress = lv_arc_create(scr);
    lv_obj_set_size(arc_progress, 234, 234);
    lv_obj_center(arc_progress);
    lv_arc_set_rotation(arc_progress, 270);
    lv_arc_set_range(arc_progress, 0, 100);
    lv_arc_set_value(arc_progress, 0);
    lv_arc_set_bg_angles(arc_progress, 0, 360);
    lv_obj_remove_style(arc_progress, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_progress, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_progress, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x101010), LV_PART_MAIN);

    /* --- Printer name (top) --- */
    lbl_name = lv_label_create(scr);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_name, "---");

    /* --- Connection indicator dot --- */
    ind_conn = lv_obj_create(scr);
    lv_obj_set_size(ind_conn, 8, 8);
    lv_obj_align_to(ind_conn, lbl_name, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    lv_obj_set_style_radius(ind_conn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ind_conn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(ind_conn, 0, 0);

    /* --- Printer selector "1/2" --- */
    lbl_selector = lv_label_create(scr);
    lv_obj_align(lbl_selector, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_text_color(lbl_selector, lv_color_hex(0x404040), 0);
    lv_obj_set_style_text_font(lbl_selector, &lv_font_montserrat_10, 0);
    lv_label_set_text(lbl_selector, "1/1");

    /* --- Status text --- */
    lbl_status = lv_label_create(scr);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, -55);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_status, "---");

    /* --- Large progress number --- */
    lbl_progress = lv_label_create(scr);
    lv_obj_align(lbl_progress, LV_ALIGN_CENTER, 0, -32);
    lv_obj_set_style_text_color(lbl_progress, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_progress, &lv_font_montserrat_36, 0);
    lv_label_set_text(lbl_progress, "--");

    /* --- Nozzle icon + temperature --- */
    lbl_nozzle_icon = lv_label_create(scr);
    lv_obj_align(lbl_nozzle_icon, LV_ALIGN_CENTER, -68, 10);
    lv_obj_set_style_text_font(lbl_nozzle_icon, &mdi_30, 0);
    lv_obj_set_style_text_color(lbl_nozzle_icon, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_nozzle_icon, ICON_NOZZLE);

    lbl_nozzle = lv_label_create(scr);
    lv_obj_align(lbl_nozzle, LV_ALIGN_CENTER, -30, 14);
    lv_obj_set_style_text_color(lbl_nozzle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_nozzle, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_nozzle, "--");

    /* --- Bed icon + temperature --- */
    lbl_bed_icon = lv_label_create(scr);
    lv_obj_align(lbl_bed_icon, LV_ALIGN_CENTER, 30, 10);
    lv_obj_set_style_text_font(lbl_bed_icon, &mdi_30, 0);
    lv_obj_set_style_text_color(lbl_bed_icon, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_bed_icon, ICON_BED_IDLE);

    lbl_bed = lv_label_create(scr);
    lv_obj_align(lbl_bed, LV_ALIGN_CENTER, 72, 14);
    lv_obj_set_style_text_color(lbl_bed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_bed, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_bed, "--");

    /* --- Bottom info area (3 rotating lines) --- */
    lbl_info1 = lv_label_create(scr);
    lv_obj_set_width(lbl_info1, 190);
    lv_obj_align(lbl_info1, LV_ALIGN_CENTER, 0, 42);
    lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(lbl_info1, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(lbl_info1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_info1, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_info1, "");

    lbl_info2 = lv_label_create(scr);
    lv_obj_align(lbl_info2, LV_ALIGN_CENTER, 0, 58);
    lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_text_font(lbl_info2, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_info2, "");

    lbl_info3 = lv_label_create(scr);
    lv_obj_align(lbl_info3, LV_ALIGN_CENTER, 0, 74);
    lv_obj_set_style_text_color(lbl_info3, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(lbl_info3, &lv_font_montserrat_10, 0);
    lv_label_set_text(lbl_info3, "");

    /* --- Filament color dot (shown on AMS info page) --- */
    dot_filament = lv_obj_create(scr);
    lv_obj_set_size(dot_filament, 10, 10);
    lv_obj_set_style_radius(dot_filament, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot_filament, 0, 0);
    lv_obj_set_style_bg_color(dot_filament, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);

    /* Create periodic update timer — 1 second */
    update_timer = lv_timer_create(update_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI initialized");
}

/* --- Info page rendering helpers --- */

static void show_page_time(printer_state_t *ps)
{
    /* Line 1: file name */
    if (ps->subtask_name[0] != '\0') {
        lv_label_set_text(lbl_info1, ps->subtask_name);
        lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0x94A3B8), 0);
    } else {
        lv_label_set_text(lbl_info1, "");
    }

    /* Line 2: time remaining */
    if (ps->state == PRINT_STATE_PRINTING && ps->remaining_min > 0) {
        int hrs = ps->remaining_min / 60;
        int mins = ps->remaining_min % 60;
        char buf[32];
        if (hrs > 0)
            snprintf(buf, sizeof(buf), "%dh %dm left", hrs, mins);
        else
            snprintf(buf, sizeof(buf), "%dm left", mins);
        lv_label_set_text(lbl_info2, buf);
        lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0x87CEEB), 0);
    } else {
        lv_label_set_text(lbl_info2, "");
    }

    /* Line 3: layer */
    if (ps->state == PRINT_STATE_PRINTING && ps->total_layers > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Layer %d/%d", ps->layer_num, ps->total_layers);
        lv_label_set_text(lbl_info3, buf);
    } else {
        lv_label_set_text(lbl_info3, "");
    }

    lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
}

static void show_page_eta(printer_state_t *ps)
{
    /* Line 1: file name */
    if (ps->subtask_name[0] != '\0') {
        lv_label_set_text(lbl_info1, ps->subtask_name);
        lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0x94A3B8), 0);
    } else {
        lv_label_set_text(lbl_info1, "");
    }

    /* Line 2: ETA */
    if (ps->state == PRINT_STATE_PRINTING && ps->remaining_min > 0) {
        time_t now;
        time(&now);
        now += (time_t)ps->remaining_min * 60;
        struct tm *eta = localtime(&now);
        char buf[32];
        if (eta)
            snprintf(buf, sizeof(buf), "ETA %02d:%02d", eta->tm_hour, eta->tm_min);
        else
            snprintf(buf, sizeof(buf), "ETA --:--");
        lv_label_set_text(lbl_info2, buf);
        lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0x87CEEB), 0);
    } else {
        lv_label_set_text(lbl_info2, "");
    }

    /* Line 3: layer */
    if (ps->state == PRINT_STATE_PRINTING && ps->total_layers > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Layer %d/%d", ps->layer_num, ps->total_layers);
        lv_label_set_text(lbl_info3, buf);
    } else {
        lv_label_set_text(lbl_info3, "");
    }

    lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
}

static void show_page_ams(printer_state_t *ps)
{
    /* Line 1: active filament with colored dot */
    if (ps->tray_now < 254 && ps->ams_count > 0) {
        int unit_idx = ps->tray_now / 4;
        int tray_idx = ps->tray_now % 4;
        if (unit_idx < ps->ams_count) {
            ams_tray_t *tray = &ps->ams[unit_idx].trays[tray_idx];
            const char *type_name = filament_type_name((filament_type_t)tray->type);
            char buf[32];
            if (tray->remain <= 100)
                snprintf(buf, sizeof(buf), "   %s %d%%", type_name, tray->remain);
            else
                snprintf(buf, sizeof(buf), "   %s", type_name);
            lv_label_set_text(lbl_info1, buf);
            lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0xFFFFFF), 0);

            /* Show colored dot */
            lv_obj_set_style_bg_color(dot_filament, lv_color_hex(tray->color), 0);
            lv_obj_clear_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(dot_filament, LV_ALIGN_CENTER, -45, 42);
        } else {
            lv_label_set_text(lbl_info1, "");
            lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (ps->tray_now == 254) {
        lv_label_set_text(lbl_info1, "External Spool");
        lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lbl_info1, "No filament");
        lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0x666666), 0);
        lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
    }

    /* Line 2: humidity per AMS unit */
    if (ps->ams_count > 0) {
        char buf[40];
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "Humidity:");
        for (int u = 0; u < ps->ams_count && u < 4; u++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %d%%", ps->ams[u].humidity);
        }
        lv_label_set_text(lbl_info2, buf);
        /* Color based on worst humidity */
        uint8_t worst = 0;
        for (int u = 0; u < ps->ams_count; u++) {
            if (ps->ams[u].humidity > worst) worst = ps->ams[u].humidity;
        }
        if (worst > 60)
            lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0xFF6666), 0);  /* red — high */
        else if (worst > 40)
            lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0xFFD54F), 0);  /* yellow — moderate */
        else
            lv_obj_set_style_text_color(lbl_info2, lv_color_hex(0x00CC66), 0);  /* green — low */
    } else {
        lv_label_set_text(lbl_info2, "");
    }

    /* Line 3: print stage detail */
    const char *stage = print_stage_name(ps->stg_cur);
    if (stage) {
        lv_label_set_text(lbl_info3, stage);
        lv_obj_set_style_text_color(lbl_info3, lv_color_hex(0xF0A64B), 0);
    } else {
        lv_label_set_text(lbl_info3, "");
    }
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
        ps->mqtt_connected ? lv_color_hex(0x00CC66) : lv_color_hex(0x666666), 0);
    lv_obj_align_to(ind_conn, lbl_name, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    /* Selector "1/2" */
    char sel_buf[16];
    snprintf(sel_buf, sizeof(sel_buf), "%d/%d", idx + 1, s_mgr->num_printers);
    lv_label_set_text(lbl_selector, sel_buf);

    /* Status text */
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

    /* Temperatures + icon colors */
    char temp_buf[40];

    /* Nozzle — show both if dual extruder (H2D) */
    bool has_nozzle2 = (ps->nozzle2_temp > 0 || ps->nozzle2_target > 0);
    if (has_nozzle2) {
        snprintf(temp_buf, sizeof(temp_buf), "%.0f|%.0f", ps->nozzle_temp, ps->nozzle2_temp);
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "%.0f/%.0f", ps->nozzle_temp, ps->nozzle_target);
    }
    lv_label_set_text(lbl_nozzle, temp_buf);

    /* Nozzle icon color — use hottest nozzle for color decision */
    float noz_temp_max = has_nozzle2 ? (ps->nozzle_temp > ps->nozzle2_temp ? ps->nozzle_temp : ps->nozzle2_temp) : ps->nozzle_temp;
    float noz_tgt_max = has_nozzle2 ? (ps->nozzle_target > ps->nozzle2_target ? ps->nozzle_target : ps->nozzle2_target) : ps->nozzle_target;
    if (noz_tgt_max > 0 && noz_temp_max < noz_tgt_max - 2)
        lv_obj_set_style_text_color(lbl_nozzle_icon, lv_color_hex(0xFFA500), 0);
    else if (noz_temp_max > noz_tgt_max + 5 && noz_tgt_max > 0)
        lv_obj_set_style_text_color(lbl_nozzle_icon, lv_color_hex(0x3399FF), 0);
    else
        lv_obj_set_style_text_color(lbl_nozzle_icon, lv_color_hex(0xFFFFFF), 0);

    snprintf(temp_buf, sizeof(temp_buf), "%.0f/%.0f", ps->bed_temp, ps->bed_target);
    lv_label_set_text(lbl_bed, temp_buf);
    if (ps->bed_target > 0 && ps->bed_temp < ps->bed_target - 2) {
        lv_label_set_text(lbl_bed_icon, ICON_BED_HEAT);   /* heat arrows when heating */
        lv_obj_set_style_text_color(lbl_bed_icon, lv_color_hex(0xFFA500), 0);
    } else if (ps->bed_temp > ps->bed_target + 5 && ps->bed_target > 0) {
        lv_label_set_text(lbl_bed_icon, ICON_BED_IDLE);   /* neutral when cooling */
        lv_obj_set_style_text_color(lbl_bed_icon, lv_color_hex(0x3399FF), 0);
    } else {
        lv_label_set_text(lbl_bed_icon, ICON_BED_IDLE);   /* neutral at temp or off */
        lv_obj_set_style_text_color(lbl_bed_icon, lv_color_hex(0xFFFFFF), 0);
    }

    /* Bottom info area — rotating pages */
    if (has_error) {
        /* Error overrides all info pages */
        lv_label_set_text(lbl_info1, ps->error_message);
        lv_obj_set_style_text_color(lbl_info1, lv_color_hex(0xFF6666), 0);
        lv_label_set_text(lbl_info2, "");
        lv_label_set_text(lbl_info3, "");
        lv_obj_add_flag(dot_filament, LV_OBJ_FLAG_HIDDEN);
    } else {
        switch (s_info_page) {
        case 0:  show_page_time(ps); break;
        case 1:  show_page_eta(ps);  break;
        case 2:  show_page_ams(ps);  break;
        default: show_page_time(ps); break;
        }
    }

    printer_manager_unlock(s_mgr);
}

void ui_show_selected(void)
{
    s_info_page = 0;  /* reset to first page on printer switch */
    s_tick = 0;
    ui_update();
}

lv_obj_t *ui_get_arc(void)
{
    return arc_progress;
}

void ui_cycle_info_page(void)
{
    int max_pages = 2;
    if (s_mgr && printer_manager_lock(s_mgr, pdMS_TO_TICKS(20))) {
        if (s_mgr->printers[s_mgr->selected].ams_count > 0)
            max_pages = 3;
        printer_manager_unlock(s_mgr);
    }
    s_info_page = (s_info_page + 1) % max_pages;
    s_tick = 0;  /* reset timer so page shows for full duration */
}

void ui_show_setup_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -55);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, "BambuDial Setup");

    /* Instructions */
    lv_obj_t *l1 = lv_label_create(scr);
    lv_obj_align(l1, LV_ALIGN_CENTER, 0, -25);
    lv_obj_set_style_text_color(l1, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_12, 0);
    lv_label_set_text(l1, "Connect to WiFi:");

    /* AP name */
    lv_obj_t *ap = lv_label_create(scr);
    lv_obj_align(ap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(ap, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ap, &lv_font_montserrat_14, 0);
    lv_label_set_text(ap, "BambuDial-Setup");

    /* Second instruction */
    lv_obj_t *l2 = lv_label_create(scr);
    lv_obj_align(l2, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_12, 0);
    lv_label_set_text(l2, "Then open browser");

    /* URL */
    lv_obj_t *url = lv_label_create(scr);
    lv_obj_align(url, LV_ALIGN_CENTER, 0, 55);
    lv_obj_set_style_text_color(url, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_10, 0);
    lv_label_set_text(url, "http://192.168.4.1");
}
