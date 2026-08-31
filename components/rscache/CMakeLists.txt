# RSCache, from oldschool-clientc/3rd/rscache -- the model decoder only.
#
# NOT rscache_unity.c. That file is the whole cache library: the CS2 compiler
# and decompiler, sound synthesis, vorbis, map squares, every dat1 and dat2
# config codec. This client decodes one model blob out of a flash partition
# and has no filesystem to open a cache from.
#
# The set is the codecs this client decodes and the byte reader they share:
# a model, a framemap (the animation rig) and a frame. Everything else these
# files need is libc. If a future need pulls in another codec, add the file it
# needs; do not reach for the unity build, which would put a vorbis decoder and
# a CS2 compiler in a Christmas ornament.

if(NOT DEFINED CLIENTC_DIR)
    get_filename_component(CLIENTC_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../3rd/oldschool-clientc" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${CLIENTC_DIR}" CLIENTC_DIR)

set(RSCACHE_DIR "${CLIENTC_DIR}/3rd/rscache")

if(NOT EXISTS "${RSCACHE_DIR}/src/datatypes/model.c")
    message(FATAL_ERROR
        "rscache not found at ${RSCACHE_DIR}. "
        "Set -DCLIENTC_DIR=<path to oldschool-clientc>.")
endif()

idf_component_register(
    SRCS
        "${RSCACHE_DIR}/src/datatypes/model.c"
        "${RSCACHE_DIR}/src/datatypes/dat2_framemap.c"
        "${RSCACHE_DIR}/src/datatypes/dat2_frame.c"
        "${RSCACHE_DIR}/src/rsbuffer.c"
    INCLUDE_DIRS
        "${RSCACHE_DIR}/include"
        # rscache.h includes its own headers by bare name. That is RSCache's
        # packaging, not something this component chose.
        "${RSCACHE_DIR}/src"
)

# ESP-IDF compiles with -Werror=all. That is this project's policy and a good
# one, but it is not RSCache's: the library builds clean under its own makefile
# and its formatting is not ours to change from here. Demote the ones its style
# trips rather than editing a vendored source, which would have to be redone
# every time it is updated.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-parameter
    -Wno-error=misleading-indentation
    -Wno-error=implicit-fallthrough
    -Wno-error=char-subscripts
)
