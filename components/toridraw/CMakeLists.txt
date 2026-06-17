if(NOT DEFINED RASTER_DIR)
    get_filename_component(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../3d-raster" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${RASTER_DIR}" RASTER_DIR)

set(TORIDRAW_DIR "${RASTER_DIR}/src2/toridraw")
set(GAMECACHE_DIR "${RASTER_DIR}/src2/gamecache")
set(TORIDRAW_TABLES_C "${CMAKE_BINARY_DIR}/gen/toridraw_tables_data.c")

idf_component_register(
    SRCS
        "${TORIDRAW_TABLES_C}"
        "${TORIDRAW_DIR}/toridraw.c"
        "${TORIDRAW_DIR}/toridraw_gccontext.c"
        "${TORIDRAW_DIR}/toridraw_vec.c"
        "${TORIDRAW_DIR}/toridraw_map.c"
        "${TORIDRAW_DIR}/toridraw_animation.c"
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
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    TORIDRAW_PIXEL16
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TORIDRAW_TABLES_PRECOMPUTED
    TORIDRAW_DISABLE_SIMD_TABLES
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-parameter
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
