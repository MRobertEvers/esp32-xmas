set(XMAS_BAKED_C "${CMAKE_BINARY_DIR}/gen/xmas_baked.c")

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_partition esp_timer toridraw toridraw_rscache rscache)

# The baked model and animation, generated before the build; see
# tools/bake_model/bake_model.c. Added after the register for the same reason
# the toridraw tables are: ESP-IDF evaluates this file in script mode first,
# where set_source_files_properties does not exist.
set_source_files_properties("${XMAS_BAKED_C}" PROPERTIES GENERATED TRUE)
target_sources(${COMPONENT_LIB} PRIVATE "${XMAS_BAKED_C}")
