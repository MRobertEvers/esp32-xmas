# toridraw_rscache, from oldschool-clientc/3rd/toridraw_rscache.
#
# The adaptor that turns a decoded RSCache_Model into a ToriDraw_Model. It
# exists because the two structs are not the same shape -- 32-bit cache
# vertices against ToriDraw's 16-bit ones, unpacked face priorities against
# packed nibbles, and the 4x vertex scale that format 13 and up stores and the
# raster does not want. This component replaces the old `rsmodel` one, whose
# model_cache_stubs.c was this conversion written by hand.
#
# ToriDraw_RSCacheModelFromBlob is the entry a client this size wants: decode,
# steal the arrays that cross by pointer, free the shell. It never holds both
# representations of the geometry at once, which on a part with 512 KB is the
# difference between fitting and not.

if(NOT DEFINED CLIENTC_DIR)
    get_filename_component(CLIENTC_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../3rd/oldschool-clientc" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${CLIENTC_DIR}" CLIENTC_DIR)

set(ADAPTOR_DIR "${CLIENTC_DIR}/3rd/toridraw_rscache")

if(NOT EXISTS "${ADAPTOR_DIR}/toridraw_rscache_unity.c")
    message(FATAL_ERROR
        "toridraw_rscache not found at ${ADAPTOR_DIR}. "
        "Set -DCLIENTC_DIR=<path to oldschool-clientc>.")
endif()

idf_component_register(
    SRCS
        "${ADAPTOR_DIR}/toridraw_rscache_unity.c"
    INCLUDE_DIRS
        "${ADAPTOR_DIR}/include"
    # Both, and PUBLIC by being REQUIRES: this library's header names types
    # from each of them, so anything including it needs both include paths.
    REQUIRES
        toridraw
        rscache
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
)
