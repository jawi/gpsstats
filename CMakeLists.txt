cmake_minimum_required(VERSION 3.15)

project(
    gpsstats
    VERSION 0.2.0
    LANGUAGES C
)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

find_package(PkgConfig)
# We depend on libyaml, libgps and mosquitto
pkg_search_module(PKG_LIBYAML REQUIRED IMPORTED_TARGET yaml>=0.2 yaml-0.1>=0.2)
pkg_search_module(PKG_LIBGPS REQUIRED IMPORTED_TARGET libgps>=3.19)
pkg_search_module(PKG_LIBMOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto>=1.5)
# We depend on libudaemon as well
find_package(udaemon 0.8 REQUIRED)

# Generate the gpsstats.h file with the current information
configure_file(
    cmake/gpsstats.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/gpsstats.h @ONLY
)

add_executable(gpsstats
    src/config.c
    src/gpsd.c
    src/mqtt.c
    src/main.c
)

target_include_directories(gpsstats
    PUBLIC
        include
    PRIVATE
        src
)

target_compile_options(gpsstats
    PRIVATE -Wall -Wextra -Wstrict-prototypes -Wshadow -Wconversion
)

target_compile_features(gpsstats
    PRIVATE c_std_11
)

target_link_libraries(gpsstats
    PRIVATE
        udaemon::udaemon
        PkgConfig::PKG_LIBYAML
        PkgConfig::PKG_LIBGPS
        PkgConfig::PKG_LIBMOSQUITTO
)

# Installation 

include(GNUInstallDirs)
install(TARGETS gpsstats
    RUNTIME DESTINATION bin
)

###EOF###
