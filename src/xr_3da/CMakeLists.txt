add_executable(xr_3da)

target_sources(xr_3da PRIVATE
    entry_point.cpp
    stdafx.h

    resource.h
    resource.rc

    splash_oxr.bmp
    stalker_cop.ico
    stalker_cs.ico
    stalker_soc.ico
)

set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY
    VS_STARTUP_PROJECT xr_3da
)

target_include_directories(xr_3da
    PRIVATE
    "${XRAY_SOURCE_DIR}/src"
)

target_link_libraries(xr_3da
    PRIVATE
    xrCore
    xrAPI
    xrEngine
    xrRender_GL
    xrGame
)

if (EMSCRIPTEN)
    # The media ports were enabled at compile time via add_compile_options;
    # the link step needs them too to pull in the port libraries (SDL2 with
    # its canvas/window glue, ogg/vorbis, libjpeg). OpenAL comes from the
    # emscripten sysroot (-lal, WebAudio based).
    target_link_options(xr_3da PRIVATE
        -sUSE_SDL=2
        -sUSE_VORBIS=1
        -sUSE_LIBJPEG=1
        -sALLOW_MEMORY_GROWTH=1
        -sMAX_WEBGL_VERSION=2
    )
endif()

set_target_properties(xr_3da PROPERTIES
    PREFIX ""
)

install(TARGETS xr_3da RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
