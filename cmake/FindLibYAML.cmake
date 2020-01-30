# FindLibYAML.cmake
#
# Finds the yaml library
#
# This will define the following variables
#
#    LibYAML_FOUND
#    LibYAML_INCLUDE_DIRS
#
# and the following imported targets
#
#     LibYAML::LibYAML

find_package(PkgConfig)
pkg_check_modules(PC_LIBYAML QUIET yaml-0.1)

find_path(LibYAML_INCLUDE_DIR
    NAMES yaml.h
    PATHS ${PC_LIBYAML_INCLUDE_DIRS}
    PATH_SUFFIXES libyaml
)

set(LibYAML_VERSION ${PC_LIBYAML_VERSION})

mark_as_advanced(LibYAML_FOUND LibYAML_INCLUDE_DIR LibYAML_VERSION)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibYAML
    REQUIRED_VARS LibYAML_INCLUDE_DIR
    VERSION_VAR LibYAML_VERSION
)

if(LibYAML_FOUND)
    set(LibYAML_INCLUDE_DIRS ${LibYAML_INCLUDE_DIR})
endif()

if(LibYAML_FOUND AND NOT TARGET LibYAML::LibYAML)
    add_library(LibYAML::LibYAML INTERFACE IMPORTED)
    set_target_properties(LibYAML::LibYAML PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LibYAML_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${PC_LIBYAML_LDFLAGS}"
    )
endif()
