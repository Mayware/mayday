cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.3.2/Help/dev/experimental.rst#c-import-std-support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(mayday)
add_executable(mayday)

#> PkgConfig - LibDrm, LibSeat
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
target_link_libraries(mayday PRIVATE PkgConfig::LIBDRM)
pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
target_link_libraries(mayday PRIVATE PkgConfig::LIBSEAT)

#> Vulkan (build the module)
find_package(Vulkan REQUIRED)
add_library(vulkan)
target_compile_definitions(vulkan PUBLIC
      VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
      VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
      VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING
)
target_include_directories(vulkan PRIVATE "${Vulkan_INCLUDE_DIR}")
target_link_libraries(vulkan PUBLIC Vulkan::Vulkan)
target_sources(vulkan
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS "${Vulkan_INCLUDE_DIR}" FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)
target_link_libraries(mayday PRIVATE vulkan)

#> Shaders
find_package(Vulkan REQUIRED COMPONENTS glslc)
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*")
set(SHADER_PRODUCTS)
foreach(SHADER_SOURCE IN LISTS SHADER_FILES)
    cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)
    set(SHADER_OUT "${CMAKE_BINARY_DIR}/../shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SHADER_OUT}
        COMMAND Vulkan::glslc "${SHADER_SOURCE}" -o "${SHADER_OUT}"
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_NAME}"
    )
    list(APPEND SHADER_PRODUCTS ${SHADER_OUT})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_PRODUCTS})
add_dependencies(mayday shaders)

#> Mayquill
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mayquill ${CMAKE_CURRENT_BINARY_DIR}/mayquill)
target_link_libraries(mayday PRIVATE mayquill)

#> Mayday
file(GLOB_RECURSE IMPL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE INTERFACE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

target_sources(mayday
    PRIVATE FILE_SET SOURCES FILES
        ${IMPL_FILES}
    PRIVATE FILE_SET CXX_MODULES FILES
        ${INTERFACE_FILES}
    PUBLIC FILE_SET HEADERS BASE_DIRS src FILES
            src/mayday/macros.h
            src/mayday/libseat.h
)
