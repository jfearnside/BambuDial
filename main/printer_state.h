#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config.h"

typedef enum {
    PRINT_STATE_UNKNOWN = 0,
    PRINT_STATE_IDLE,
    PRINT_STATE_PRINTING,
    PRINT_STATE_PAUSED,
    PRINT_STATE_FINISHED,
    PRINT_STATE_FAILED,
    PRINT_STATE_PREPARING,
} print_state_t;

/* Filament type — compact enum instead of storing strings */
typedef enum {
    FILAMENT_UNKNOWN = 0,
    FILAMENT_PLA,
    FILAMENT_PETG,
    FILAMENT_ABS,
    FILAMENT_TPU,
    FILAMENT_PA,
    FILAMENT_PC,
    FILAMENT_PVA,
    FILAMENT_ASA,
    FILAMENT_PLA_CF,
    FILAMENT_PA_CF,
    FILAMENT_PETG_CF,
    FILAMENT_PET_CF,
    FILAMENT_SILK,
    FILAMENT_OTHER,
} filament_type_t;

/* Per-tray data (6 bytes) */
typedef struct {
    uint32_t color;       /* RRGGBB */
    uint8_t  remain;      /* 0-100%, 255=unknown */
    uint8_t  type;        /* filament_type_t */
} ams_tray_t;

/* Per-AMS-unit data (25 bytes) */
typedef struct {
    ams_tray_t trays[4];
    uint8_t    humidity;  /* 0-100% raw */
} ams_unit_t;

typedef struct {
    /* Identity */
    char name[32];
    char ip[16];
    char serial[24];
    char access_code[16];

    /* Connection */
    bool mqtt_connected;

    /* Print state */
    print_state_t state;
    int progress;              /* 0-100 */
    int remaining_min;         /* minutes remaining */
    char subtask_name[64];     /* current print job name */
    int layer_num;
    int total_layers;

    /* Temperatures */
    float nozzle_temp;
    float nozzle_target;
    float bed_temp;
    float bed_target;

    /* Error */
    int print_error;
    char error_message[128];

    /* AMS */
    uint8_t    ams_count;      /* 0 = no AMS */
    uint8_t    tray_now;       /* active tray: 255=none, 254=external */
    ams_unit_t ams[4];         /* up to 4 AMS units */

    /* Print stage */
    int8_t     stg_cur;        /* current stage, -1=idle */

    /* UI state */
    bool       dismissed;      /* user dismissed DONE/FAILED — ignore FINISH from MQTT */
} printer_state_t;

typedef struct {
    printer_state_t printers[MAX_PRINTERS];
    int num_printers;
    int selected;
    SemaphoreHandle_t mutex;
} printer_manager_t;

/* Initialize the printer manager with configs from config.h */
void printer_manager_init(printer_manager_t *mgr);

/* Thread-safe: lock before reading/writing printer state */
bool printer_manager_lock(printer_manager_t *mgr, TickType_t timeout);
void printer_manager_unlock(printer_manager_t *mgr);

/* Cycle selection (called from encoder) */
void printer_manager_select_next(printer_manager_t *mgr);
void printer_manager_select_prev(printer_manager_t *mgr);

/* Parse gcode_state string into enum */
print_state_t parse_gcode_state(const char *state_str);

/* Get a color (LVGL format) for a given print state */
uint32_t print_state_color(print_state_t state);

/* Get human-readable state name */
const char *print_state_name(print_state_t state);

/* Filament type helpers */
filament_type_t parse_filament_type(const char *type_str);
const char *filament_type_name(filament_type_t type);

/* Print stage helper — returns NULL if nothing interesting to show */
const char *print_stage_name(int8_t stg_cur);
