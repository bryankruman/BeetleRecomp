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

# Tier-2 settings/launcher/cheats menu (src/ui, RmlUi over RT64). ON by default: the launcher +
# settings + BAR cheats menu ship in the standard build. It depends on the lib/freetype-windows-binaries
# submodule (vendored); turn the menu OFF with -DBEETLE_ENABLE_UI=OFF to build the bare core port
# (recompiled code + runtime + RT64) without RmlUi/Freetype.
option(BEETLE_ENABLE_UI "Build the RmlUi settings/launcher/cheats menu (requires the Freetype submodule)" ON)

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

# ---- UI libs (RmlUi + lunasvg + Freetype). The Tier-2 settings/launcher menu (src/ui) is built
# when -DBEETLE_ENABLE_UI=ON. RmlUi needs Freetype: on Windows we point it at the prebuilt
# lib/freetype-windows-binaries submodule (add it once with:
#   git submodule add https://github.com/ubawurinna/freetype-windows-binaries.git lib/freetype-windows-binaries
# ); on Linux/macOS the system Freetype is found. ----
if (BEETLE_ENABLE_UI)
    # Satisfy RmlUi's find_package("Freetype") + Freetype::Freetype requirement. On Windows the
    # standard FindFreetype can't locate the prebuilt binaries' spaced path, so create the imported
    # target ourselves before RmlUi's dependency scan runs.
    if (WIN32 AND NOT TARGET Freetype::Freetype)
        set(BAR_FT_DIR "${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries")
        if (NOT EXISTS "${BAR_FT_DIR}/include/ft2build.h")
            message(FATAL_ERROR
                "BEETLE_ENABLE_UI=ON but lib/freetype-windows-binaries is missing. Add it with:\n"
                "  git submodule add https://github.com/ubawurinna/freetype-windows-binaries.git lib/freetype-windows-binaries")
        endif()
        # The prebuilt-binaries repo's static-lib dir is VS-version-stamped (vs2015-2022/win64, vs2026/x64,
        # ...) and changes over time — glob for the x64 static lib instead of hardcoding the version path.
        file(GLOB_RECURSE BAR_FT_LIBS
            "${BAR_FT_DIR}/release static/*/x64/freetype.lib"
            "${BAR_FT_DIR}/release static/*/win64/freetype.lib")
        if (NOT BAR_FT_LIBS)
            message(FATAL_ERROR "freetype.lib not found under '${BAR_FT_DIR}/release static/*/x64' — check the submodule layout")
        endif()
        list(GET BAR_FT_LIBS 0 BAR_FT_LIB)
        message(STATUS "BeetleRecomp: using Freetype ${BAR_FT_LIB}")
        add_library(Freetype::Freetype STATIC IMPORTED)
        set_target_properties(Freetype::Freetype PROPERTIES
            IMPORTED_LOCATION "${BAR_FT_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${BAR_FT_DIR}/include")
    endif()

    set(BUILD_SHARED_LIBS OFF)
    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/lunasvg)
    set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
    set(RMLUI_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)        # no RmlUi samples/backend — we render via our RT64 bridge
    set(RMLUI_BACKEND "auto" CACHE STRING "" FORCE)   # unused when samples are off, but must be a valid value
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
    ${CMAKE_SOURCE_DIR}/src/main/os_unimpl_stubs.cpp     # no-op stubs for ignored OS funcs BAR calls
    ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp   # wires recomp_overlays.inl into librecomp
    ${CMAKE_SOURCE_DIR}/src/main/overlay_bridge.cpp      # registers uv modules as overlays on load
    ${CMAKE_SOURCE_DIR}/src/main/hw_stubs.cpp            # native stubs for raw RCP-register libultra funcs
    ${CMAKE_SOURCE_DIR}/src/main/rt64_render_context.cpp # RT64-backed RendererContext subclass
    ${CMAKE_SOURCE_DIR}/src/main/bar_config.cpp          # feature/settings flags (fast-boot, etc.)
    ${CMAKE_SOURCE_DIR}/src/main/bar_cheats.cpp          # BAR-specific cheats (host-side RDRAM pokes)
    ${CMAKE_SOURCE_DIR}/src/main/bar_preempt.cpp         # cooperative audio-thread preemption (no-underrun)
    ${CMAKE_SOURCE_DIR}/src/main/bar_crash.cpp           # symbolized crash backtrace (debug aid)
    ${CMAKE_SOURCE_DIR}/src/game/config.cpp              # host-owned settings persistence + high-FPS enable
    ${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp                  # RSPRecomp output: BAR audio ucode (M_AUDTASK)
    # TODO(BAR): add as authored (mirroring Zelda's src/main + src/game):
    #   src/main/register_patches.cpp
    #   src/game/input.cpp
)
target_include_directories(BeetleRecomp PRIVATE
    ${RECOMP_INCLUDES}
    ${CMAKE_SOURCE_DIR}/src                         # "game/config.hpp", etc.
    ${CMAKE_SOURCE_DIR}/RecompiledFuncs            # recomp_overlays.inl + funcs.h
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    # rt64_render_context.cpp includes RT64's internal headers, which pull in vendored contrib
    # headers (hlslpp, plume, ...). Inherit RT64's full (private) include set so they resolve.
    $<TARGET_PROPERTY:rt64,INCLUDE_DIRECTORIES>
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
    target_link_libraries(BeetleRecomp PRIVATE SDL2 Winmm.lib Dbghelp.lib)   # Winmm: timeBeginPeriod; Dbghelp: crash backtrace
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
# ---------------------------------------------------------------------------
# Tier-2 settings/launcher menu (src/ui) — RmlUi rendered through RT64.
# ---------------------------------------------------------------------------
if (BEETLE_ENABLE_UI)
    target_sources(BeetleRecomp PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ui/ui_renderer.cpp                  # RmlUi -> RT64 (plume) render bridge
        ${CMAKE_SOURCE_DIR}/src/ui/bar_ui.cpp                       # bootstrap + render hooks + menu controller
        ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp  # SystemInterface_SDL + SDL->RmlUi input
    )
    target_compile_definitions(BeetleRecomp PRIVATE
        BEETLE_ENABLE_UI
        RMLUI_SDL_VERSION_MAJOR=2          # the SDL platform backend targets SDL2
    )
    target_include_directories(BeetleRecomp PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/RmlUi/Include
        ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends
        ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi              # rt64_render_hooks.h
        ${CMAKE_SOURCE_DIR}/lib/rt64/src/render           # rt64_texture_cache.h
        ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/plume    # plume_render_interface_builders.h
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/concurrentqueue
        ${CMAKE_BINARY_DIR}/shaders                        # generated Interface{VS,PS}.hlsl.{dxil,spirv}.h
    )
    target_link_libraries(BeetleRecomp PRIVATE RmlUi::Core RmlUi::Debugger lunasvg)

    # ---- DXC options (RT64 doesn't export these to the parent scope) for build_*_shader() ----
    set(DXC_COMMON_OPTS "-I${CMAKE_SOURCE_DIR}/src/ui/shaders")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T" "ps_6_0")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T" "vs_6_0" "-fvk-invert-y")
    if (WIN32)
        set(DXC "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
    elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(DXC "LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
    endif()
    # Compile the RmlUi interface shaders into C blobs the render bridge #includes. Output lands in
    # ${CMAKE_BINARY_DIR}/shaders/ (matching the include dir + the InterfaceVS/PS blob names).
    build_vertex_shader(BeetleRecomp "src/ui/shaders/InterfaceVS.hlsl" "shaders/InterfaceVS.hlsl")
    build_pixel_shader(BeetleRecomp  "src/ui/shaders/InterfacePS.hlsl" "shaders/InterfacePS.hlsl")

    # ---- Ship the UI assets (documents, stylesheet, fonts) next to the exe under assets/ui/ ----
    add_custom_command(TARGET BeetleRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BeetleRecomp>/assets/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/ui/assets" "$<TARGET_FILE_DIR:BeetleRecomp>/assets/ui")
endif()

# ---- Bring-up crash diagnosis: embed debug info (/Z7) in the boot-path targets + emit a PDB.
# RT64 is deliberately left at Release (no option change -> not rebuilt) since the early-boot
# crash path is in librecomp / ultramodern / the recompiled code, not the renderer. Flip
# BEETLE_DEBUGINFO OFF once bring-up settles. ----
option(BEETLE_DEBUGINFO "Embed debug info in the boot-path targets for crash diagnosis" ON)
if (BEETLE_DEBUGINFO AND MSVC)
    foreach(dbgtgt RecompiledFuncs librecomp ultramodern BeetleRecomp)
        if (TARGET ${dbgtgt})
            target_compile_options(${dbgtgt} PRIVATE /Z7)
        endif()
    endforeach()
    target_link_options(BeetleRecomp PRIVATE /DEBUG)
endif()

set_property(TARGET BeetleRecomp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
