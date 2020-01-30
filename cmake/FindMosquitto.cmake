# FindMosquitto.cmake
#
# Finds the mosquitto library
#
# This will define the following variables
#
#    Mosquitto_FOUND
#    Mosquitto_INCLUDE_DIRS
#
# and the following imported targets
#
#     Mosquitto::Mosquitto

find_package(PkgConfig)
pkg_check_modules(PC_MOSQUITTO QUIET libmosquitto)

find_path(Mosquitto_INCLUDE_DIR
    NAMES mosquitto.h
    PATHS ${PC_MOSQUITTO_INCLUDE_DIRS}
    PATH_SUFFIXES mosquitto
)

set(Mosquitto_VERSION ${PC_MOSQUITTO_VERSION})

mark_as_advanced(Mosquitto_FOUND Mosquitto_INCLUDE_DIR Mosquitto_VERSION)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mosquitto
    REQUIRED_VARS Mosquitto_INCLUDE_DIR
    VERSION_VAR Mosquitto_VERSION
)

if(Mosquitto_FOUND)
    set(Mosquitto_INCLUDE_DIRS ${Mosquitto_INCLUDE_DIR})
endif()

if(Mosquitto_FOUND AND NOT TARGET Mosquitto::Mosquitto)
    add_library(Mosquitto::Mosquitto INTERFACE IMPORTED)
    set_target_properties(Mosquitto::Mosquitto PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Mosquitto_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${PC_MOSQUITTO_LDFLAGS}"
    )
endif()
