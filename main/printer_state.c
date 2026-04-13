#include "printer_state.h"
#include <string.h>

static const printer_state_t s_printer_configs[] = PRINTER_CONFIGS;

void printer_manager_init(printer_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->num_printers = NUM_PRINTERS;
    mgr->selected = 0;
    mgr->mutex = xSemaphoreCreateMutex();

    for (int i = 0; i < NUM_PRINTERS && i < MAX_PRINTERS; i++) {
        strncpy(mgr->printers[i].name, s_printer_configs[i].name, sizeof(mgr->printers[i].name) - 1);
        strncpy(mgr->printers[i].ip, s_printer_configs[i].ip, sizeof(mgr->printers[i].ip) - 1);
        strncpy(mgr->printers[i].serial, s_printer_configs[i].serial, sizeof(mgr->printers[i].serial) - 1);
        strncpy(mgr->printers[i].access_code, s_printer_configs[i].access_code, sizeof(mgr->printers[i].access_code) - 1);
        mgr->printers[i].state = PRINT_STATE_UNKNOWN;
    }
}

bool printer_manager_lock(printer_manager_t *mgr, TickType_t timeout)
{
    return xSemaphoreTake(mgr->mutex, timeout) == pdTRUE;
}

void printer_manager_unlock(printer_manager_t *mgr)
{
    xSemaphoreGive(mgr->mutex);
}

void printer_manager_select_next(printer_manager_t *mgr)
{
    if (printer_manager_lock(mgr, pdMS_TO_TICKS(100))) {
        mgr->selected = (mgr->selected + 1) % mgr->num_printers;
        printer_manager_unlock(mgr);
    }
}

void printer_manager_select_prev(printer_manager_t *mgr)
{
    if (printer_manager_lock(mgr, pdMS_TO_TICKS(100))) {
        mgr->selected = (mgr->selected - 1 + mgr->num_printers) % mgr->num_printers;
        printer_manager_unlock(mgr);
    }
}

print_state_t parse_gcode_state(const char *state_str)
{
    if (!state_str) return PRINT_STATE_UNKNOWN;
    if (strcmp(state_str, "IDLE") == 0)     return PRINT_STATE_IDLE;
    if (strcmp(state_str, "RUNNING") == 0)  return PRINT_STATE_PRINTING;
    if (strcmp(state_str, "PRINTING") == 0) return PRINT_STATE_PRINTING;
    if (strcmp(state_str, "PAUSE") == 0)    return PRINT_STATE_PAUSED;
    if (strcmp(state_str, "FINISH") == 0)   return PRINT_STATE_FINISHED;
    if (strcmp(state_str, "FAILED") == 0)   return PRINT_STATE_FAILED;
    if (strcmp(state_str, "PREPARE") == 0)  return PRINT_STATE_PREPARING;
    if (strcmp(state_str, "SLICING") == 0)  return PRINT_STATE_PREPARING;
    return PRINT_STATE_UNKNOWN;
}

uint32_t print_state_color(print_state_t state)
{
    switch (state) {
        case PRINT_STATE_PRINTING:   return 0x00CC66;  /* green */
        case PRINT_STATE_IDLE:       return 0x3399FF;  /* blue */
        case PRINT_STATE_PAUSED:     return 0xFFAA00;  /* amber */
        case PRINT_STATE_FINISHED:   return 0x00CC66;  /* green */
        case PRINT_STATE_FAILED:     return 0xFF3333;  /* red */
        case PRINT_STATE_PREPARING:  return 0x9966FF;  /* purple */
        default:                     return 0x888888;  /* gray */
    }
}

const char *print_state_name(print_state_t state)
{
    switch (state) {
        case PRINT_STATE_PRINTING:   return "PRINTING";
        case PRINT_STATE_IDLE:       return "IDLE";
        case PRINT_STATE_PAUSED:     return "PAUSED";
        case PRINT_STATE_FINISHED:   return "DONE";
        case PRINT_STATE_FAILED:     return "FAILED";
        case PRINT_STATE_PREPARING:  return "PREPARING";
        default:                     return "---";
    }
}
