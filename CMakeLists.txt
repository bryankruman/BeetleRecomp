cmake_minimum_required(VERSION 3.20)
project(BeetleRecomp LANGUAGES C CXX)

# BeetleRecomp — modern PC port of Beetle Adventure Racing via static recompilation.
#
# This is a STARTING SKELETON modeled on Zelda64Recompiled's CMake setup. The exact
# target/link wiring for N64ModernRuntime + RT64 + RmlUi must be adapted from upstream:
#   https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/CMakeLists.txt
#
# Prereqs: run scripts/setup.* (fetches submodules into lib/, builds the recompiler),
# then ./N64Recomp BeetleRecomp.toml (generates RecompiledFuncs/). See BUILDING.md.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Generated recompiler output -------------------------------------------------
# Produced by:  ./N64Recomp BeetleRecomp.toml   (and later ./N64Recomp patches.toml)
# Run the recompiler BEFORE configuring/building.
file(GLOB RECOMPILED_FUNCS   CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c")
file(GLOB RECOMPILED_PATCHES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/RecompiledPatches/*.c")
if(NOT RECOMPILED_FUNCS)
  message(WARNING "No RecompiledFuncs/*.c — run './N64Recomp BeetleRecomp.toml' first (see BUILDING.md).")
endif()

# --- Dependencies (fetched by scripts/setup.* into lib/) -------------------------
# Uncomment as each is wired up. Their exact target names come from upstream CMake.
# add_subdirectory(lib/N64ModernRuntime)   # librecomp, ultramodern (brings N64Recomp runtime)
# add_subdirectory(lib/rt64)               # renderer: D3D12 on Windows, Vulkan on Linux
# add_subdirectory(lib/RmlUi)              # UI
# add_subdirectory(lib/lunasvg)            # SVG icons

# --- Port executable -------------------------------------------------------------
add_executable(BeetleRecomp
  src/main/main.cpp
  ${RECOMPILED_FUNCS}
  ${RECOMPILED_PATCHES}
)
target_include_directories(BeetleRecomp PRIVATE include)

# target_link_libraries(BeetleRecomp PRIVATE
#   librecomp ultramodern RT64 RmlUi lunasvg)

# Recompiled C is machine-generated; see Zelda64Recomp for the per-file flags
# (e.g. disabling certain optimizations/warnings on RecompiledFuncs).
