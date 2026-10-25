# These tools run on the build host, independently of the target architecture.
function(beetle_macos_shader_compiler output dxc_root host_processor)
    if (host_processor MATCHES "^(arm64|aarch64)$")
        set(tool_arch arm64)
    elseif (host_processor MATCHES "^(x86_64|AMD64|amd64)$")
        set(tool_arch x64)
    else()
        message(FATAL_ERROR "Unsupported macOS shader compiler host: ${host_processor}")
    endif()
    set(compiler "${dxc_root}/bin/${tool_arch}/dxc-macos")
    if (NOT EXISTS "${compiler}")
        message(FATAL_ERROR "Missing pinned macOS DXC: ${compiler}. Initialize recursive submodules first.")
    endif()
    # cmake -E env preserves paths containing spaces under Ninja and Make.
    set(${output} "${CMAKE_COMMAND}" -E env
        "DYLD_LIBRARY_PATH=${dxc_root}/lib/${tool_arch}"
        "${compiler}" PARENT_SCOPE)
endfunction()
