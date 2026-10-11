# Vendored LZO 2.10 (GPLv2 with linking exception, see COPYING).
# Used when there is no system lzo2 to link against: Emscripten/WebAssembly
# has no port for it. The engine includes <lzo/lzo1x.h> and uses the
# lzo1x_* compression family for network packets and level data.
file(GLOB LZO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(LZO STATIC ${LZO_SOURCES})

target_include_directories(LZO
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# External code, not maintained by the project
if (XRAY_DISABLE_WARNINGS)
    target_compile_options(LZO PRIVATE ${XRAY_DISABLE_WARNINGS})
endif()
