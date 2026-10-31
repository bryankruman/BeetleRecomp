# Build an .icns app icon from one square PNG using the stock macOS sips/iconutil tools.
# Usage: cmake -DICON_SOURCE=<png> -DICON_OUTPUT=<icns> -P MakeMacOSIcon.cmake
if (NOT ICON_SOURCE OR NOT ICON_OUTPUT)
    message(FATAL_ERROR "ICON_SOURCE and ICON_OUTPUT are required")
endif()

get_filename_component(ICON_NAME "${ICON_OUTPUT}" NAME_WE)
get_filename_component(ICON_DIR "${ICON_OUTPUT}" DIRECTORY)
set(ICONSET "${ICON_DIR}/${ICON_NAME}.iconset")
file(REMOVE_RECURSE "${ICONSET}")
file(MAKE_DIRECTORY "${ICONSET}")

foreach(SIZE 16 32 128 256 512)
    math(EXPR DOUBLE "${SIZE} * 2")
    foreach(PAIR "${SIZE};icon_${SIZE}x${SIZE}.png" "${DOUBLE};icon_${SIZE}x${SIZE}@2x.png")
        list(GET PAIR 0 PIXELS)
        list(GET PAIR 1 FILE)
        execute_process(
            COMMAND sips -z ${PIXELS} ${PIXELS} "${ICON_SOURCE}" --out "${ICONSET}/${FILE}"
            OUTPUT_QUIET RESULT_VARIABLE RESULT)
        if (RESULT)
            message(FATAL_ERROR "sips failed to render ${FILE}")
        endif()
    endforeach()
endforeach()

execute_process(COMMAND iconutil -c icns "${ICONSET}" -o "${ICON_OUTPUT}" RESULT_VARIABLE RESULT)
if (RESULT)
    message(FATAL_ERROR "iconutil failed to create ${ICON_OUTPUT}")
endif()
file(REMOVE_RECURSE "${ICONSET}")
