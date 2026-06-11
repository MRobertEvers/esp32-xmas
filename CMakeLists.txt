cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3)

if(NOT DEFINED RASTER_DIR)
    set(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../3d-raster" CACHE PATH "Path to 3d-raster repository")
endif()
set(MODEL_ID "7" CACHE STRING "dat1 model archive id to extract from cache")
set(CACHE_DIR "${RASTER_DIR}/cache" CACHE PATH "dat1 OSRS cache directory")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(st7789_lvgl)

set(MODEL_BIN "${CMAKE_BINARY_DIR}/model.bin")
set(HSL16_BIN "${CMAKE_SOURCE_DIR}/components/toridraw/data/hsl16_rgb_table.bin")
set(EXTRACT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract-model.ps1")
set(GEN_HSL16_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen-hsl16-table.ps1")

add_custom_command(
    OUTPUT "${MODEL_BIN}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${EXTRACT_SCRIPT}"
        -ModelId ${MODEL_ID}
        -CacheDir "${CACHE_DIR}"
        -OutFile "${MODEL_BIN}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Extracting dat1 model archive ${MODEL_ID} to model.bin"
    VERBATIM
)

add_custom_target(model_bin ALL DEPENDS "${MODEL_BIN}")

add_custom_command(
    OUTPUT "${HSL16_BIN}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${GEN_HSL16_SCRIPT}"
        -OutFile "${HSL16_BIN}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating precomputed HSL16 RGB lookup table"
    VERBATIM
)

add_custom_target(hsl16_table_bin ALL DEPENDS "${HSL16_BIN}")
add_dependencies(flash hsl16_table_bin)
add_dependencies(__idf_toridraw hsl16_table_bin)

partition_table_get_partition_info(model_size "--partition-name model" "size")
partition_table_get_partition_info(model_offset "--partition-name model" "offset")

if(model_size AND model_offset)
    esptool_py_flash_to_partition(flash "model" "${MODEL_BIN}")
    add_dependencies(flash model_bin)
endif()
