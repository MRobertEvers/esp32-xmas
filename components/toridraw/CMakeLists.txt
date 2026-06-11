if(NOT DEFINED RASTER_DIR)
    get_filename_component(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../3d-raster" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${RASTER_DIR}" RASTER_DIR)

set(TORIDRAW_DIR "${RASTER_DIR}/src2/toridraw")
set(GAMECACHE_DIR "${RASTER_DIR}/src2/gamecache")

idf_component_register(
    SRCS
        "esp_hsl16_load.c"
        "${TORIDRAW_DIR}/toridraw.c"
        "${TORIDRAW_DIR}/toridraw_model.c"
        "${TORIDRAW_DIR}/toridraw_math.c"
        "${TORIDRAW_DIR}/toridraw_hsl16.c"
        "${TORIDRAW_DIR}/toridraw_lighting.c"
        "${TORIDRAW_DIR}/toridraw_light_model.c"
        "${RASTER_DIR}/src/graphics/shared_tables.c"
        "${GAMECACHE_DIR}/toridraw_cachemodel.c"
    INCLUDE_DIRS
        "${RASTER_DIR}/src"
        "${RASTER_DIR}/src2"
        "${TORIDRAW_DIR}"
        "${GAMECACHE_DIR}"
    REQUIRES
        rsmodel
    LDFRAGMENTS
        linker.lf
    EMBED_FILES
        "data/hsl16_rgb_table.bin"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-parameter
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=init_hsl16_to_rgb_table")
