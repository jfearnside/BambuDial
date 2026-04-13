#pragma once

#include "printer_state.h"
#include "config_store.h"

/*
 * Bambu Cloud Client
 *
 * Authenticates with Bambu Lab cloud, discovers bound printers,
 * and connects to cloud MQTT for status updates.
 */

/* Set cloud credentials from device config. Call before login. */
void bambu_cloud_set_config(const device_config_t *cfg);

/* Authenticate with Bambu Cloud. Returns true on success. */
bool bambu_cloud_login(void);

/* Fetch list of bound printers from cloud. */
bool bambu_cloud_fetch_printers(printer_manager_t *mgr);

/* Start cloud MQTT connections for all discovered printers. */
void bambu_cloud_mqtt_start(printer_manager_t *mgr);

/* Stop cloud MQTT connections. */
void bambu_cloud_mqtt_stop(void);
