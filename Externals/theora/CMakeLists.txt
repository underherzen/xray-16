# Vendored libtheora 1.1.1 (BSD-style license, see COPYING).
# Used when there is no system theora to link against: Emscripten/WebAssembly
# has no port for it. The engine includes <theora/theora.h> and uses the
# legacy theora_* API for cutscene video decoding.
# Built without HAVE_CONFIG_H and without the x86 asm directory (wasm has no
# use for it); encoder_disabled.c is excluded because the real encoder is built.
file(GLOB THEORA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c")
list(REMOVE_ITEM THEORA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/lib/encoder_disabled.c")

add_library(Theora STATIC ${THEORA_SOURCES})

target_include_directories(Theora
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# External code, not maintained by the project
if (XRAY_DISABLE_WARNINGS)
    target_compile_options(Theora PRIVATE ${XRAY_DISABLE_WARNINGS})
endif()
