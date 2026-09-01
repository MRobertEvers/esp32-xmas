# qrcodegen, from 3rd/qrcodegen.
#
# Two files copied verbatim from upstream, so this component is the whole of
# the build integration -- there is no configuration, no generated table and
# nothing to keep in step. See 3rd/qrcodegen/README.md for the pin.
#
# The library allocates nothing and holds no static state, so what it costs the
# image is text: 5.6 KB at -Os with NDEBUG, which IDF release builds define.
# The caller owns both buffers, and their size is set by the highest version it
# allows rather than by the data -- qrcodegen_BUFFER_LEN_FOR_VERSION.

get_filename_component(QRCODEGEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../3rd/qrcodegen" ABSOLUTE)

if(NOT EXISTS "${QRCODEGEN_DIR}/qrcodegen.c")
    message(FATAL_ERROR "qrcodegen not found at ${QRCODEGEN_DIR}.")
endif()

idf_component_register(
    SRCS "${QRCODEGEN_DIR}/qrcodegen.c"
    INCLUDE_DIRS "${QRCODEGEN_DIR}"
)
