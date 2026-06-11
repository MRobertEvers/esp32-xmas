cmake_minimum_required(VERSION 3.16)
project(extract_model C)

if(NOT DEFINED RASTER_DIR)
    get_filename_component(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../3d-raster" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${RASTER_DIR}" RASTER_DIR)

set(RSCACHE_DIR "${RASTER_DIR}/src/osrs/rscache")
set(RASTER_SRC "${RASTER_DIR}/src")

set(RSCACHE_SOURCES
    cache_stubs.c
    "${RSCACHE_DIR}/archive.c"
    "${RSCACHE_DIR}/archive_decompress.c"
    "${RSCACHE_DIR}/bitbuffer.c"
    "${RSCACHE_DIR}/cache_dat.c"
    "${RSCACHE_DIR}/compression.c"
    "${RSCACHE_DIR}/disk.c"
    "${RSCACHE_DIR}/filelist.c"
    "${RSCACHE_DIR}/rsbuf.c"
    "${RSCACHE_DIR}/xtea.c"
    "${RSCACHE_DIR}/tables/model.c"
    "${RSCACHE_DIR}/tables/string_utils.c"
    "${RSCACHE_DIR}/tables_dat/animframe.c"
    "${RSCACHE_DIR}/tables_dat/config_versionlist_mapsquare.c"
    "${RASTER_SRC}/3rd/bzip/bzip.c"
    "${RASTER_SRC}/3rd/miniz/miniz.c"
)

add_executable(extract_model main.c ${RSCACHE_SOURCES})

target_include_directories(extract_model PRIVATE
    "${RASTER_SRC}"
    "${RSCACHE_DIR}"
    "${RASTER_SRC}/3rd/miniz"
    "${RASTER_SRC}/3rd/bzip"
)

if(MSVC)
    target_compile_definitions(extract_model PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(extract_model PRIVATE /W3 /utf-8)
else()
    target_compile_options(extract_model PRIVATE -Wall -Wextra)
endif()
