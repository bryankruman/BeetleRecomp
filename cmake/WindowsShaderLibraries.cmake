# Native Windows produces the three libraries that RT64 links at runtime.
# A Linux cross-build imports that exact bank; Linux DXC handles other shaders.
set(BEETLE_WINDOWS_DXIL_BANK "" CACHE PATH "Native Windows DXIL library bank (manifest and three blobs)")
set(BEETLE_HOST_FILE_TO_C "" CACHE FILEPATH "Existing native file_to_c tool for a cross-build")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_beetle_shader_source "${CMAKE_SOURCE_DIR}/lib/rt64/src")
set(_beetle_shader_script "${CMAKE_SOURCE_DIR}/scripts/windows-shader-bank.py")
file(GLOB_RECURSE _beetle_shader_inputs CONFIGURE_DEPENDS
    "${_beetle_shader_source}/shaders/*" "${_beetle_shader_source}/shared/*"
    "${_beetle_shader_source}/common/*.h")
set(_beetle_dxc_dir "${_beetle_shader_source}/contrib/dxc/bin/x64")
set(_beetle_shader_dependencies ${_beetle_shader_inputs} "${_beetle_shader_script}"
    "${_beetle_dxc_dir}/dxc.exe" "${_beetle_dxc_dir}/dxcompiler.dll" "${_beetle_dxc_dir}/dxil.dll")
if (NOT BEETLE_WINDOWS_DXIL_BANK)
    if (NOT CMAKE_HOST_WIN32)
        message(FATAL_ERROR "Windows cross-builds require BEETLE_WINDOWS_DXIL_BANK exported with the bundled native Windows dxc.exe; see BUILDING.md")
    endif()
    set(BEETLE_WINDOWS_DXIL_BANK "${CMAKE_BINARY_DIR}/windows-dxil-bank" CACHE PATH "Native Windows DXIL library bank" FORCE)
endif()
if (CMAKE_HOST_WIN32 AND BEETLE_WINDOWS_DXIL_BANK STREQUAL "${CMAKE_BINARY_DIR}/windows-dxil-bank")
    add_custom_command(OUTPUT "${BEETLE_WINDOWS_DXIL_BANK}/manifest.json"
        COMMAND "${Python3_EXECUTABLE}" "${_beetle_shader_script}" export
            --source "${_beetle_shader_source}" --bank "${BEETLE_WINDOWS_DXIL_BANK}"
        DEPENDS ${_beetle_shader_dependencies}
        BYPRODUCTS "${BEETLE_WINDOWS_DXIL_BANK}/RasterVSLibrary.hlsl.dxil"
            "${BEETLE_WINDOWS_DXIL_BANK}/RasterPSLibrary.hlsl.dxil"
            "${BEETLE_WINDOWS_DXIL_BANK}/RasterPSLibraryMS.hlsl.dxil"
        VERBATIM)
else()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${_beetle_shader_script}" validate
        --source "${_beetle_shader_source}" --bank "${BEETLE_WINDOWS_DXIL_BANK}"
        RESULT_VARIABLE _beetle_bank_status ERROR_VARIABLE _beetle_bank_error)
    if (NOT _beetle_bank_status EQUAL 0)
        message(FATAL_ERROR "Windows shader bank validation failed: ${_beetle_bank_error}")
    endif()
endif()
if (NOT CMAKE_HOST_WIN32)
    if (NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        message(FATAL_ERROR "The Windows cross shader path currently requires an x64 Linux host")
    endif()
    if (NOT IS_ABSOLUTE "${BEETLE_HOST_FILE_TO_C}" OR NOT EXISTS "${BEETLE_HOST_FILE_TO_C}")
        message(FATAL_ERROR "Set BEETLE_HOST_FILE_TO_C to the existing native file_to_c executable")
    endif()
endif()
# Always validate at build time, including unchanged timestamps or an external bank.
add_custom_target(beetle_windows_shader_bank
    COMMAND "${Python3_EXECUTABLE}" "${_beetle_shader_script}" validate
        --source "${_beetle_shader_source}" --bank "${BEETLE_WINDOWS_DXIL_BANK}"
    DEPENDS "${BEETLE_WINDOWS_DXIL_BANK}/manifest.json" VERBATIM)

function(beetle_build_windows_library TARGETOBJ SHADERNAME)
    if (NOT DXC_LB_OPTS STREQUAL "-I${CMAKE_SOURCE_DIR}/lib/rt64/src;-D;LIBRARY;-T;lib_6_3"
        OR NOT DXC_DXIL_OPTS STREQUAL "-Wno-ignored-attributes")
        message(FATAL_ERROR "RT64 library compiler options changed; review the native bank recipe")
    endif()
    if (NOT ARGC EQUAL 3 AND NOT ARGC EQUAL 4)
        message(FATAL_ERROR "Unexpected RT64 library shader arguments")
    endif()
    set(_output "${ARGV2}")
    set(_known_outputs "src/shaders/RasterVSLibrary.hlsl" "src/shaders/RasterPSLibrary.hlsl" "src/shaders/RasterPSLibraryMS.hlsl")
    if (NOT _output IN_LIST _known_outputs)
        message(FATAL_ERROR "Unknown runtime-linked shader library: ${_output}")
    endif()
    if (ARGC EQUAL 4 AND NOT ARGV3 STREQUAL "-D MULTISAMPLING")
        message(FATAL_ERROR "Unexpected RT64 library shader definitions")
    endif()
    if (_output STREQUAL "src/shaders/RasterVSLibrary.hlsl")
        if (NOT SHADERNAME STREQUAL "src/shaders/RasterVS.hlsl" OR NOT ARGC EQUAL 3)
            message(FATAL_ERROR "Unexpected vertex library recipe")
        endif()
    else()
        if (NOT SHADERNAME STREQUAL "src/shaders/RasterPS.hlsl")
            message(FATAL_ERROR "Unexpected pixel library source")
        endif()
        if ((_output STREQUAL "src/shaders/RasterPSLibraryMS.hlsl" AND NOT ARGC EQUAL 4)
            OR (_output STREQUAL "src/shaders/RasterPSLibrary.hlsl" AND NOT ARGC EQUAL 3))
            message(FATAL_ERROR "Unexpected pixel library recipe")
        endif()
    endif()
    get_filename_component(_name "${_output}" NAME)
    get_filename_component(_symbol "${_output}" NAME_WE)
    set(_destination "${CMAKE_BINARY_DIR}/${_output}.dxil")
    add_custom_command(OUTPUT "${_destination}"
        COMMAND "${Python3_EXECUTABLE}" "${_beetle_shader_script}" import
            --source "${_beetle_shader_source}" --bank "${BEETLE_WINDOWS_DXIL_BANK}"
            --name "${_name}.dxil" --output "${_destination}"
        DEPENDS "${BEETLE_WINDOWS_DXIL_BANK}/manifest.json" "${BEETLE_WINDOWS_DXIL_BANK}/${_name}.dxil"
            ${_beetle_shader_dependencies} beetle_windows_shader_bank
        VERBATIM)
    add_custom_command(OUTPUT "${_destination}.c"
        COMMAND file_to_c "${_destination}" "${_symbol}BlobDXIL" "${_destination}.c" "${_destination}.h"
        DEPENDS "${_destination}" file_to_c BYPRODUCTS "${_destination}.h" VERBATIM)
    target_sources(${TARGETOBJ} PRIVATE "${_destination}.c")
    add_dependencies(${TARGETOBJ} beetle_windows_shader_bank)
endfunction()
