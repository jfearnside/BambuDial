#pragma once

#include "printer_state.h"

/* Start MQTT connection to the selected printer */
void bambu_mqtt_start(printer_manager_t *mgr);

/* Switch to a different printer (disconnects current, connects new) */
void bambu_mqtt_switch(int printer_idx);

/* Re-send pushall to refresh current printer state */
void bambu_mqtt_request_pushall(void);

/* Check if data from current printer is stale (no updates for timeout_s seconds) */
bool bambu_mqtt_is_data_stale(int timeout_s);

/* Force reconnect to the current printer (use when data is stale) */
void bambu_mqtt_force_reconnect(void);

/* Stop MQTT connection */
void bambu_mqtt_stop(void);
