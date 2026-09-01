set(XMAS_BAKED_C "${CMAKE_BINARY_DIR}/gen/xmas_baked.c")

idf_component_register(SRCS "main.c" "net.c" "settings.c" "ui.c"
                            "http_api.c" "captive_dns.c"
                            "model_bundle.c" "model_store.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_partition esp_timer
                                nvs_flash esp_wifi esp_netif esp_event
                                esp_http_server esp_http_client lwip mbedtls
                                toridraw toridraw_rscache rscache qrcodegen
                       # The control pages, linked as .rodata and served
                       # straight out of flash -- see http_api.c. They are
                       # small enough that a filesystem partition would be
                       # more moving parts than it saves.
                       EMBED_TXTFILES "www/portal.html" "www/app.html")

# The baked model and animation, generated before the build; see
# tools/bake_model/bake_model.c. Added after the register for the same reason
# the toridraw tables are: ESP-IDF evaluates this file in script mode first,
# where set_source_files_properties does not exist.
set_source_files_properties("${XMAS_BAKED_C}" PROPERTIES GENERATED TRUE)
target_sources(${COMPONENT_LIB} PRIVATE "${XMAS_BAKED_C}")
