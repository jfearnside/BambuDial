#pragma once

#include "printer_state.h"

/*
 * Bambu Cloud Client
 *
 * Authenticates with Bambu Lab cloud, discovers bound printers,
 * and connects to cloud MQTT for status updates.
 */

/* Authenticate with Bambu Cloud. Returns true on success.
 * Uses email/password from config.h, or direct token if set. */
bool bambu_cloud_login(void);

/* Fetch list of bound printers from cloud.
 * Populates the printer manager with discovered printers.
 * Must be called after successful login. */
bool bambu_cloud_fetch_printers(printer_manager_t *mgr);

/* Start cloud MQTT connections for all discovered printers. */
void bambu_cloud_mqtt_start(printer_manager_t *mgr);

/* Stop cloud MQTT connections. */
void bambu_cloud_mqtt_stop(void);

/* Get the access token (for debug/display). */
const char *bambu_cloud_get_token(void);
