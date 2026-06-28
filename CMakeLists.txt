# CMakeLists.txt — BeetleRecomp (static-recomp PC port of Beetle Adventure Racing)
#
# Adapted from Zelda64Recomp/Zelda64Recomp @ dev (CMakeLists.txt). Pinned deps in this repo:
#   lib/N64ModernRuntime @ main (recomp::start(Configuration) — see src/main/main.cpp)
#   lib/rt64 @ main, lib/RmlUi @ 6.1.x, lib/lunasvg @ 3.5.x, lib/N64Recomp @ mod-tool-release
#
# !!! UNTESTED — not yet configured/built (no local toolchain). Treat as a first draft.
#
# Prereqs before this will configure/build:
#   1) scripts/setup.*  -> `git submodule update --init --recursive` (populates the NESTED
#      submodules: N64ModernRuntime/N64Recomp, rt64/contrib, etc.) and builds N64Recomp.
#   2) ./N64Recomp BeetleRecomp.toml  -> populates RecompiledFuncs/*.c (this file globs them).
#   3) A C++20 toolchain: Windows = VS2022 + clang-cl + Ninja (RT64 -> D3D12);
#      Linux = clang + ninja + SDL2/GTK (RT64 -> Vulkan). See BUILDING.md.

cmake_minimum_required(VERSION 3.20)
project(BeetleRecomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# RmlUi menu UI is OFF by default for now (it needs Freetype — see BUILDING.md). The core
# port (recompiled code + runtime + RT64) builds without it. Enable with -DBEETLE_ENABLE_UI=ON.
option(BEETLE_ENABLE_UI "Build the RmlUi menu UI (requires Freetype)" OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()
if (WIN32)
    # Avoid the constexpr mutex ctor so we don't require a fresh vcredist (matches upstream).
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ---- RT64 first: defines the `rt64` target + build_*_shader() helpers ----
set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)
# RT64 vendors SDL2 2.26.3, whose SDL_endian.h manually defines _m_prefetch (an old Clang-11
# workaround). clang 17+ provides it as a builtin -> "definition of builtin function" error.
# Defining SDL's own guard macro makes it skip that definition and use the compiler builtin.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(rt64 PRIVATE __PRFCHWINTRIN_H)
endif()

# ---- UI libs (RmlUi + lunasvg). Gated OFF by default: RmlUi requires Freetype. On Windows,
# add the lib/freetype-windows-binaries submodule + set FREETYPE_LIBRARY/FREETYPE_INCLUDE_DIRS
# before this block, then configure with -DBEETLE_ENABLE_UI=ON. ----
if (BEETLE_ENABLE_UI)
    set(BUILD_SHARED_LIBS OFF)
    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/lunasvg)
    set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
    set(RMLUI_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RmlUi)
    target_compile_definitions(rmlui_core PRIVATE LUNASVG_BUILD_STATIC)
endif()

# ---- N64ModernRuntime: provides the `librecomp` and `ultramodern` targets ----
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

# Shared include set for the generated C and the host.
set(RECOMP_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    # Populated by recursive submodule init (N64ModernRuntime's nested N64Recomp):
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

# ---------------------------------------------------------------------------
# RecompiledFuncs — N64Recomp output. Run `./N64Recomp BeetleRecomp.toml` FIRST,
# or this target will have no sources and CMake will error.
# ---------------------------------------------------------------------------
add_library(RecompiledFuncs STATIC)
target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing                 # MANDATORY for recompiled C (it puns memory constantly)
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)
target_include_directories(RecompiledFuncs PRIVATE ${RECOMP_INCLUDES})
file(GLOB FUNC_C_SOURCES   CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# ---------------------------------------------------------------------------
# PatchesLib — recompiled custom patches. Disabled until patches exist
# (RecompiledPatches/ is produced by `make -C patches && ./N64Recomp patches.toml`).
# TODO(BAR): uncomment + add to the link list once you author patches.
# ---------------------------------------------------------------------------
# add_library(PatchesLib STATIC
#     ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
#     ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c)
# target_compile_options(PatchesLib PRIVATE -fno-strict-aliasing -Wno-unused-variable -Wno-implicit-function-declaration)
# target_include_directories(PatchesLib PRIVATE ${RECOMP_INCLUDES})

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(BeetleRecomp
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/os_unimpl_stubs.cpp   # no-op stubs for ignored OS funcs BAR calls
    # TODO(BAR): add as authored (mirroring Zelda's src/main + src/game):
    #   src/main/rt64_render_context.cpp   (RT64 RendererContext subclass)
    #   src/main/register_overlays.cpp / register_patches.cpp
    #   src/game/input.cpp / config.cpp
    #   rsp/aspMain.cpp                    (RSPRecomp output)
)
target_include_directories(BeetleRecomp PRIVATE
    ${RECOMP_INCLUDES}
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(BeetleRecomp PRIVATE -march=nehalem -fno-strict-aliasing -fms-extensions)
else()
    target_compile_options(BeetleRecomp PRIVATE -fno-strict-aliasing -fms-extensions)
endif()
if (MSVC)
    target_link_options(BeetleRecomp PRIVATE /OPT:NOICF)   # don't fold identical recompiled funcs
endif()

# ---- Windows: RT64 picks D3D12 by default; SDL2 via FetchContent ----
if (WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3")
    FetchContent_Declare(sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip)
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(BeetleRecomp PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(BeetleRecomp PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)
    add_compile_definitions(NOMINMAX)
    add_custom_command(TARGET BeetleRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:BeetleRecomp>)
    set_target_properties(BeetleRecomp PROPERTIES
        LINK_FLAGS_DEBUG          "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE        "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    target_link_libraries(BeetleRecomp PRIVATE SDL2 Winmm.lib)
    # TODO(BAR): app icon -> target_sources(BeetleRecomp PRIVATE icons/app.rc)
endif()

# ---- Linux: RT64 -> Vulkan ----
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")
    target_include_directories(BeetleRecomp PRIVATE ${SDL2_INCLUDE_DIRS})
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(BeetleRecomp PRIVATE
        "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads SDL2::SDL2)
endif()

# ---- Link the runtime + generated code ----
target_link_libraries(BeetleRecomp PRIVATE
    # PatchesLib            # enable once patches exist
    RecompiledFuncs
    librecomp
    ultramodern
    rt64
)
if (BEETLE_ENABLE_UI)
    target_link_libraries(BeetleRecomp PRIVATE RmlUi::Core RmlUi::Debugger lunasvg)
endif()

set_property(TARGET BeetleRecomp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
