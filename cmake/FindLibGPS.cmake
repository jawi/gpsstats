# FindLibGPS.cmake
#
# Finds the gps library
#
# This will define the following variables
#
#    LibGPS_FOUND
#    LibGPS_INCLUDE_DIRS
#
# and the following imported targets
#
#     LibGPS::LibGPS

find_package(PkgConfig)
pkg_check_modules(PC_LIBGPS QUIET libgps)

find_path(LibGPS_INCLUDE_DIR
    NAMES gps.h
    PATHS ${PC_LIBGPS_INCLUDE_DIRS}
    PATH_SUFFIXES libgps
)

set(LibGPS_VERSION ${PC_LIBGPS_VERSION})

mark_as_advanced(
    LibGPS_FOUND 
    LibGPS_INCLUDE_DIR 
    LibGPS_VERSION
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibGPS
    REQUIRED_VARS LibGPS_INCLUDE_DIR
    VERSION_VAR LibGPS_VERSION
)

if(LibGPS_FOUND)
    set(LibGPS_INCLUDE_DIRS ${LibGPS_INCLUDE_DIR})
endif()

if(LibGPS_FOUND AND NOT TARGET LibGPS::LibGPS)
    add_library(LibGPS::LibGPS INTERFACE IMPORTED)
    set_target_properties(LibGPS::LibGPS PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LibGPS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${PC_LIBGPS_LDFLAGS}"
    )
endif()
