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
    char error_message[128];    /* human-readable error from HMS lookup */
} printer_state_t;

typedef struct {
    printer_state_t printers[MAX_PRINTERS];
    int num_printers;
    int selected;               /* currently displayed printer index */
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
