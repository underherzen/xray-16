# Vendored PUC-Rio Lua 5.1.5.
# Used when building without LuaJIT: modern distributions no longer ship
# Lua 5.1 packages, and cross toolchains (Emscripten/WebAssembly) have no
# system Lua at all. The engine and luabind are written against the Lua 5.1
# API which this implementation provides.
add_library(Lua51 STATIC)

target_sources(Lua51 PRIVATE
    lua/lapi.c
    lua/lauxlib.c
    lua/lbaselib.c
    lua/lcode.c
    lua/ldblib.c
    lua/ldebug.c
    lua/ldo.c
    lua/ldump.c
    lua/lfunc.c
    lua/lgc.c
    lua/linit.c
    lua/liolib.c
    lua/llex.c
    lua/lmathlib.c
    lua/lmem.c
    lua/loadlib.c
    lua/lobject.c
    lua/lopcodes.c
    lua/loslib.c
    lua/lparser.c
    lua/lstate.c
    lua/lstring.c
    lua/lstrlib.c
    lua/ltable.c
    lua/ltablib.c
    lua/ltm.c
    lua/lundump.c
    lua/lvm.c
    lua/lzio.c
    lua/print.c
)

target_include_directories(Lua51
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/lua"
)

set_target_properties(Lua51 PROPERTIES
    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

# External code, not maintained by the project
if (XRAY_DISABLE_WARNINGS)
    target_compile_options(Lua51 PRIVATE ${XRAY_DISABLE_WARNINGS})
endif()
