#include "printer_state.h"
#include <string.h>
#include <strings.h>

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
        mgr->printers[i].tray_now = 255;
        mgr->printers[i].stg_cur = -1;
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
    if (strcmp(state_str, "PAUSED") == 0)   return PRINT_STATE_PAUSED;
    if (strcmp(state_str, "FINISH") == 0)   return PRINT_STATE_FINISHED;
    if (strcmp(state_str, "FAILED") == 0)   return PRINT_STATE_FAILED;
    if (strcmp(state_str, "PREPARE") == 0)  return PRINT_STATE_PREPARING;
    if (strcmp(state_str, "SLICING") == 0)  return PRINT_STATE_PREPARING;
    return PRINT_STATE_UNKNOWN;
}

uint32_t print_state_color(print_state_t state)
{
    /* PrintSphere color scheme */
    switch (state) {
        case PRINT_STATE_PRINTING:   return 0x00FF00;  /* bright green */
        case PRINT_STATE_IDLE:       return 0x666666;  /* gray */
        case PRINT_STATE_PAUSED:     return 0xFFA500;  /* orange */
        case PRINT_STATE_FINISHED:   return 0x00FFFF;  /* cyan */
        case PRINT_STATE_FAILED:     return 0xFF3333;  /* red */
        case PRINT_STATE_PREPARING:  return 0xF0A64B;  /* warm orange */
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

filament_type_t parse_filament_type(const char *type_str)
{
    if (!type_str || !type_str[0]) return FILAMENT_UNKNOWN;
    if (strcasecmp(type_str, "PLA") == 0)      return FILAMENT_PLA;
    if (strcasecmp(type_str, "PETG") == 0)     return FILAMENT_PETG;
    if (strcasecmp(type_str, "ABS") == 0)      return FILAMENT_ABS;
    if (strcasecmp(type_str, "TPU") == 0)      return FILAMENT_TPU;
    if (strcasecmp(type_str, "PA") == 0)       return FILAMENT_PA;
    if (strcasecmp(type_str, "PC") == 0)       return FILAMENT_PC;
    if (strcasecmp(type_str, "PVA") == 0)      return FILAMENT_PVA;
    if (strcasecmp(type_str, "ASA") == 0)      return FILAMENT_ASA;
    if (strncasecmp(type_str, "PLA-CF", 6) == 0)  return FILAMENT_PLA_CF;
    if (strncasecmp(type_str, "PA-CF", 5) == 0)   return FILAMENT_PA_CF;
    if (strncasecmp(type_str, "PETG-CF", 7) == 0) return FILAMENT_PETG_CF;
    if (strncasecmp(type_str, "PET-CF", 6) == 0)  return FILAMENT_PET_CF;
    if (strncasecmp(type_str, "PLA-S", 5) == 0)   return FILAMENT_SILK;
    if (strncasecmp(type_str, "Silk", 4) == 0)     return FILAMENT_SILK;
    return FILAMENT_OTHER;
}

const char *filament_type_name(filament_type_t type)
{
    switch (type) {
        case FILAMENT_PLA:      return "PLA";
        case FILAMENT_PETG:     return "PETG";
        case FILAMENT_ABS:      return "ABS";
        case FILAMENT_TPU:      return "TPU";
        case FILAMENT_PA:       return "PA";
        case FILAMENT_PC:       return "PC";
        case FILAMENT_PVA:      return "PVA";
        case FILAMENT_ASA:      return "ASA";
        case FILAMENT_PLA_CF:   return "PLA-CF";
        case FILAMENT_PA_CF:    return "PA-CF";
        case FILAMENT_PETG_CF:  return "PETG-CF";
        case FILAMENT_PET_CF:   return "PET-CF";
        case FILAMENT_SILK:     return "Silk";
        case FILAMENT_OTHER:    return "Other";
        default:                return "?";
    }
}

const char *print_stage_name(int8_t stg)
{
    switch (stg) {
        case 1:  return "Leveling";
        case 2:  return "Heating Bed";
        case 3:  return "Sweeping";
        case 4:  return "Changing Filament";
        case 5:  return "Paused (M400)";
        case 6:  return "Filament Runout";
        case 7:  return "Heating Nozzle";
        case 8:  return "Calibrating";
        case 13: return "Homing";
        case 22: return "Unloading Filament";
        case 24: return "Loading Filament";
        case 77: return "AMS Preparing";
        default: return NULL;  /* 0=printing, -1/255=idle — nothing extra to show */
    }
}
