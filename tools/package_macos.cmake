cmake_minimum_required(VERSION 3.21)

set(contents "${APP}/Contents")
file(MAKE_DIRECTORY "${contents}/MacOS" "${contents}/Frameworks")
file(COPY_FILE "${EXE}" "${contents}/MacOS/CastawayRecomp")
if(MENU_BACKGROUND OR APP_ICON)
    file(MAKE_DIRECTORY "${contents}/Resources")
endif()
if(MENU_BACKGROUND)
    file(COPY_FILE "${MENU_BACKGROUND}" "${contents}/Resources/tropical-menu.png" ONLY_IF_DIFFERENT)
endif()
if(APP_ICON)
    file(COPY_FILE "${APP_ICON}" "${contents}/Resources/CastawayRecomp.icns" ONLY_IF_DIFFERENT)
endif()
set(libraries "${SDL}")
if(DXMT AND VKD3D_UTILS)
    list(APPEND libraries "${DXMT}" "${VKD3D_UTILS}")
    get_filename_component(dxmt_dir "${DXMT}" DIRECTORY)
    get_filename_component(vkd3d_dir "${VKD3D_UTILS}" DIRECTORY)
    foreach(dependency IN ITEMS libvkd3d.dylib libvkd3d-shader.dylib)
        if(EXISTS "${vkd3d_dir}/${dependency}")
            list(APPEND libraries "${vkd3d_dir}/${dependency}")
        endif()
    endforeach()
    if(EXISTS "${dxmt_dir}/libunwind.1.dylib")
        list(APPEND libraries "${dxmt_dir}/libunwind.1.dylib")
    endif()
endif()

set(binary "${contents}/MacOS/CastawayRecomp")
set(bundled)
set(library_ids)
set(library_names)
foreach(library IN LISTS libraries)
    execute_process(COMMAND otool -D "${library}" OUTPUT_VARIABLE ids COMMAND_ERROR_IS_FATAL ANY)
    string(STRIP "${ids}" ids)
    string(REGEX REPLACE "^.*:\n" "" id "${ids}")
    get_filename_component(name "${id}" NAME)
    set(destination "${contents}/Frameworks/${name}")
    file(COPY_FILE "${library}" "${destination}")
    execute_process(COMMAND install_name_tool -id "@loader_path/${name}" "${destination}"
                    COMMAND_ERROR_IS_FATAL ANY)
    list(APPEND bundled "${destination}")
    list(APPEND library_ids "${id}")
    list(APPEND library_names "${name}")
endforeach()

foreach(file IN ITEMS "${binary}" ${bundled})
    execute_process(COMMAND otool -L "${file}" OUTPUT_VARIABLE dependencies COMMAND_ERROR_IS_FATAL ANY)
    if(file STREQUAL binary)
        set(rpath "@executable_path/../Frameworks")
    else()
        set(rpath "@loader_path")
    endif()
    foreach(id name IN ZIP_LISTS library_ids library_names)
        string(FIND "${dependencies}" "\t${id} (" found)
        if(NOT found EQUAL -1 AND NOT id STREQUAL "${rpath}/${name}")
            execute_process(COMMAND install_name_tool -change "${id}" "${rpath}/${name}" "${file}"
                            COMMAND_ERROR_IS_FATAL ANY)
        endif()
    endforeach()
endforeach()
file(WRITE "${contents}/Info.plist" [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>CastawayRecomp</string>
    <key>CFBundleIdentifier</key><string>org.castawayrecomp.mac</string>
    <key>CFBundleName</key><string>CastawayRecomp</string>
    <key>CFBundleIconFile</key><string>CastawayRecomp.icns</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
]=])
foreach(library IN LISTS bundled)
    execute_process(COMMAND codesign --force --sign - "${library}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND codesign --force --sign - "${APP}" COMMAND_ERROR_IS_FATAL ANY)
