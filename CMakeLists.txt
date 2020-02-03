cmake_minimum_required(VERSION 3.5)

project(
    gpsstats
    VERSION 0.2.0
    LANGUAGES C
)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# We depend on libudaemon, libyaml, libgpsd and mosquitto
find_package(udaemon 0.6 REQUIRED)
find_package(LibYAML 0.2 REQUIRED)
find_package(LibGPS 3.19 REQUIRED)
find_package(Mosquitto 1.5 REQUIRED)

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
    PUBLIC
        udaemon::udaemon
        LibYAML::LibYAML
        LibGPS::LibGPS
        Mosquitto::Mosquitto
)

###EOF###
