
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was nodConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

set(_nod_libdir "${PACKAGE_PREFIX_DIR}/lib")
set(_nod_incdir "${PACKAGE_PREFIX_DIR}/include")

if (NOT TARGET nod::nod_shared AND NOT TARGET nod::nod_static)
    # Shared library
    if (WIN32)
        set(_nod_dll "${PACKAGE_PREFIX_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}nod${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_nod_implib "${_nod_libdir}/${CMAKE_IMPORT_LIBRARY_PREFIX}nod${CMAKE_IMPORT_LIBRARY_SUFFIX}")
        if (EXISTS "${_nod_dll}")
            add_library(nod::nod_shared SHARED IMPORTED)
            set_target_properties(nod::nod_shared PROPERTIES
                IMPORTED_LOCATION "${_nod_dll}"
                INTERFACE_INCLUDE_DIRECTORIES "${_nod_incdir}"
            )
            if (EXISTS "${_nod_implib}")
                set_target_properties(nod::nod_shared PROPERTIES
                    IMPORTED_IMPLIB "${_nod_implib}"
                )
            endif()
        endif()
    else()
        set(_nod_shared "${_nod_libdir}/${CMAKE_SHARED_LIBRARY_PREFIX}nod${CMAKE_SHARED_LIBRARY_SUFFIX}")
        if (EXISTS "${_nod_shared}")
            add_library(nod::nod_shared SHARED IMPORTED)
            set_target_properties(nod::nod_shared PROPERTIES
                IMPORTED_LOCATION "${_nod_shared}"
                INTERFACE_INCLUDE_DIRECTORIES "${_nod_incdir}"
            )
        endif()
    endif()

    # Static library
    if (MSVC)
        set(_nod_static "${_nod_libdir}/nod_static${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else()
        set(_nod_static "${_nod_libdir}/${CMAKE_STATIC_LIBRARY_PREFIX}nod${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
    if (EXISTS "${_nod_static}")
        add_library(nod::nod_static STATIC IMPORTED)
        set_target_properties(nod::nod_static PROPERTIES
            IMPORTED_LOCATION "${_nod_static}"
            INTERFACE_INCLUDE_DIRECTORIES "${_nod_incdir}"
        )
        # Rust's std library requires these system libraries when linked statically
        if (WIN32)
            set_property(TARGET nod::nod_static APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES ntdll userenv ws2_32 dbghelp bcrypt)
        endif()
    endif()

    # Default nod::nod target (respects BUILD_SHARED_LIBS)
    if (BUILD_SHARED_LIBS AND TARGET nod::nod_shared)
        add_library(nod::nod INTERFACE IMPORTED)
        set_target_properties(nod::nod PROPERTIES
            INTERFACE_LINK_LIBRARIES nod::nod_shared
        )
    elseif (TARGET nod::nod_static)
        add_library(nod::nod INTERFACE IMPORTED)
        set_target_properties(nod::nod PROPERTIES
            INTERFACE_LINK_LIBRARIES nod::nod_static
        )
    elseif (TARGET nod::nod_shared)
        add_library(nod::nod INTERFACE IMPORTED)
        set_target_properties(nod::nod PROPERTIES
            INTERFACE_LINK_LIBRARIES nod::nod_shared
        )
    else()
        set(nod_FOUND FALSE)
        if (nod_FIND_REQUIRED)
            message(FATAL_ERROR "nod library not found in ${_nod_libdir}")
        endif()
        return()
    endif()
endif()

unset(_nod_libdir)
unset(_nod_incdir)

check_required_components(nod)
