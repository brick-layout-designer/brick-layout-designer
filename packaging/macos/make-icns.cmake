# Usage: cmake -DSRC=<icon.png> -DOUT=<icon.icns> -P make-icns.cmake
# Renders the iconset sizes with sips and packs them with iconutil (both ship
# with macOS). The source PNG is 256 px, so 512 px entries are omitted.
get_filename_component(_dir "${OUT}" DIRECTORY)
set(_iconset "${_dir}/brick-layout-designer.iconset")
file(REMOVE_RECURSE "${_iconset}")
file(MAKE_DIRECTORY "${_iconset}")
foreach(_entry 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x
               128:icon_128x128 256:icon_128x128@2x 256:icon_256x256)
    string(REPLACE ":" ";" _parts "${_entry}")
    list(GET _parts 0 _px)
    list(GET _parts 1 _name)
    execute_process(
        COMMAND sips -s format png -z ${_px} ${_px} "${SRC}" --out "${_iconset}/${_name}.png"
        OUTPUT_QUIET
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND iconutil -c icns "${_iconset}" -o "${OUT}" COMMAND_ERROR_IS_FATAL ANY)
