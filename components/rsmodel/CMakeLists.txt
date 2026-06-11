if(NOT DEFINED RASTER_DIR)
    get_filename_component(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../3d-raster" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${RASTER_DIR}" RASTER_DIR)

set(RSCACHE_DIR "${RASTER_DIR}/src/osrs/rscache")

idf_component_register(
    SRCS
        "model_cache_stubs.c"
        "${RSCACHE_DIR}/tables/model.c"
    INCLUDE_DIRS
        "${RASTER_DIR}/src"
        "${RSCACHE_DIR}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable)
