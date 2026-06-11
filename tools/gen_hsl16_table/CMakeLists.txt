cmake_minimum_required(VERSION 3.16)
project(gen_hsl16_table C)

if(NOT DEFINED RASTER_DIR)
    get_filename_component(RASTER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../3d-raster" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${RASTER_DIR}" RASTER_DIR)

add_executable(gen_hsl16_table
    main.c
    "${RASTER_DIR}/src/graphics/shared_tables.c"
)

target_include_directories(gen_hsl16_table PRIVATE "${RASTER_DIR}/src")

if(MSVC)
    target_compile_definitions(gen_hsl16_table PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gen_hsl16_table PRIVATE /W3 /utf-8)
else()
    target_compile_options(gen_hsl16_table PRIVATE -Wall -Wextra)
endif()

if(NOT MSVC)
    target_link_libraries(gen_hsl16_table PRIVATE m)
endif()
