#pragma once

#include "config_store.h"

/* Start the setup portal (WiFi AP + HTTP server + DNS captive portal).
 * Call this when no WiFi config exists or connection fails.
 * The portal serves a web form for configuration.
 * When the user saves config, it reboots automatically. */
void setup_portal_start(void);

/* Stop the setup portal (called before switching to station mode) */
void setup_portal_stop(void);
