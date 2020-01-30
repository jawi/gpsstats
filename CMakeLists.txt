cmake_minimum_required(VERSION 3.5)

project(
    gpsstats
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# We depend on libudaemon, libyaml, libgpsd and mosquitto
find_package(udaemon 0.5 REQUIRED)
find_package(LibYAML 0.2 REQUIRED)
find_package(LibGPS 3.19 REQUIRED)
find_package(Mosquitto 1.5 REQUIRED)

add_executable(gpsstats
    src/config.c
    src/logging.c
    src/main.c
    src/utils.c
)

target_include_directories(gpsstats
    PUBLIC
        include
    PRIVATE
        src
)

target_compile_options(gpsstats 
    PRIVATE -Wall -Wstrict-prototypes -Wshadow -Wconversion
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