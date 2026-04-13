idf_component_register(
    SRCS "main.c" "printer_state.c" "bambu_mqtt.c" "bambu_cloud.c" "ui.c"
         "error_lookup.c" "config_store.c" "setup_portal.c"
         "fonts/mdi_30.c"
    INCLUDE_DIRS "." "fonts"
    EMBED_FILES "certs/bambu_ca_bundle.pem"
    EMBED_TXTFILES "error_lookup.tsv"
)
