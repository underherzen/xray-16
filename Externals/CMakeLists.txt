if (XRAY_USE_LUAJIT)
    add_subdirectory(LuaJIT-proj)
else()
    # Modern distributions no longer package Lua 5.1 and cross toolchains
    # (Emscripten/WebAssembly) have no system Lua at all:
    # build the vendored PUC-Rio Lua 5.1.5 instead of find_package(Lua51).
    add_subdirectory(lua51)
endif()

add_subdirectory(luabind)
add_subdirectory(xrLuaFix)
add_subdirectory(OPCODE)
add_subdirectory(ode)

if (EMSCRIPTEN) # no system lzo2 and no Emscripten port: build the vendored one
    add_subdirectory(lzo)
    add_library(LZO::LZO ALIAS LZO)
endif()

# Silence all warnings for external libraries
add_compile_options(${XRAY_DISABLE_WARNINGS})

add_subdirectory(GameSpy)
add_subdirectory(imgui-proj)

if (NOT TARGET xrLuabind)
    message(FATAL_ERROR
        "You probably have downloaded OpenXRay sources as ZIP archive, you can't do that. Use git to clone the repository.\n"
        "Read the build instructions: https://github.com/OpenXRay/xray-16/wiki"
    )
endif()
