cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.3.2/Help/dev/experimental.rst#c-import-std-support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(mayday)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mayquill ${CMAKE_CURRENT_BINARY_DIR}/mayquill)
add_executable(mayday)

file(GLOB_RECURSE IMPL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file (GLOB_RECURSE INTERFACE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

target_sources(mayday
    PRIVATE
        ${IMPL_FILES}
    PRIVATE FILE_SET CXX_MODULES FILES
        ${INTERFACE_FILES}
    PUBLIC FILE_SET HEADERS
        BASE_DIRS src FILES
            src/mayday/macros.h

)
target_link_libraries(mayday PRIVATE mayquill)
target_link_libraries(mayday PRIVATE PkgConfig::LIBDRM)
