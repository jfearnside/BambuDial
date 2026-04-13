#pragma once

#include "printer_state.h"

/* Start MQTT connection to the selected printer */
void bambu_mqtt_start(printer_manager_t *mgr);

/* Switch to a different printer (disconnects current, connects new) */
void bambu_mqtt_switch(int printer_idx);

/* Re-send pushall to refresh current printer state */
void bambu_mqtt_request_pushall(void);

/* Stop MQTT connection */
void bambu_mqtt_stop(void);
