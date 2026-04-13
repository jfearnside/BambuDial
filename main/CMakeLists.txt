idf_component_register(
    SRCS "main.c" "printer_state.c" "bambu_mqtt.c" "bambu_cloud.c" "ui.c" "error_lookup.c"
    INCLUDE_DIRS "."
    EMBED_FILES "certs/bambu_ca_bundle.pem"
    EMBED_TXTFILES "error_lookup.tsv"
)
