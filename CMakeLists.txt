cmake_minimum_required(VERSION 3.24)

project(HeicConverter VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "HeicConverter is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the MSVC runtime inside the executable. This must match the
# x64-windows-static vcpkg triplet used by build.ps1.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(libheif CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# JPEG export is intentionally brought in with FetchContent. libjpeg-turbo
# explicitly disallows add_subdirectory(), so FetchContent handles its pinned
# source download and ExternalProject builds that source in isolation.
include(FetchContent)
FetchContent_Declare(
    libjpeg_turbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG 3.1.4.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _heicconverter_populate_only
)
FetchContent_MakeAvailable(libjpeg_turbo)

include(ExternalProject)
set(LIBJPEG_TURBO_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo-install")
file(MAKE_DIRECTORY "${LIBJPEG_TURBO_INSTALL_DIR}/include")
ExternalProject_Add(libjpeg_turbo_external
    SOURCE_DIR "${libjpeg_turbo_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo-build"
    INSTALL_DIR "${LIBJPEG_TURBO_INSTALL_DIR}"
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>"
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF
        -DWITH_TOOLS=OFF
        -DWITH_TESTS=OFF
    BUILD_BYPRODUCTS "${LIBJPEG_TURBO_INSTALL_DIR}/lib/jpeg-static.lib"
)
add_library(libjpeg_turbo_static STATIC IMPORTED GLOBAL)
set_target_properties(libjpeg_turbo_static PROPERTIES
    IMPORTED_LOCATION "${LIBJPEG_TURBO_INSTALL_DIR}/lib/jpeg-static.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEG_TURBO_INSTALL_DIR}/include"
)
add_dependencies(libjpeg_turbo_static libjpeg_turbo_external)

if(TARGET heif)
    set(HEICCONVERTER_HEIF_TARGET heif)
elseif(TARGET libheif::heif)
    set(HEICCONVERTER_HEIF_TARGET libheif::heif)
else()
    message(FATAL_ERROR "libheif was found, but no supported CMake target was exported.")
endif()

add_library(HeicConverterCore STATIC
    src/converter.cpp
    src/converter.h
    src/exif_metadata.cpp
    src/exif_metadata.h
    src/file_operations.cpp
    src/file_operations.h
    src/file_naming.cpp
    src/file_naming.h
    src/image_converter.cpp
    src/image_converter.h
    src/image_writers.cpp
    src/image_writers.h
    src/localization.cpp
    src/localization.h
    src/output_format.h
    src/utf8.cpp
    src/utf8.h
)

target_include_directories(HeicConverterCore PUBLIC src)

target_compile_definitions(HeicConverterCore PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(HeicConverterCore PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(HeicConverterCore PRIVATE
    ${HEICCONVERTER_HEIF_TARGET}
    PNG::PNG
    libjpeg_turbo_static
)

add_executable(HeicConverter WIN32
    src/main.cpp
    app.manifest
)

target_compile_definitions(HeicConverter PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(HeicConverter PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(HeicConverter PRIVATE
    HeicConverterCore
    imgui::imgui
    d3d11
    dxgi
    ole32
    shell32
    uuid
)

install(TARGETS HeicConverter RUNTIME DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    add_executable(HeicConverterSmokeTest
        tests/smoke_test.cpp
    )
    target_compile_definitions(HeicConverterSmokeTest PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(HeicConverterSmokeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(HeicConverterSmokeTest PRIVATE HeicConverterCore)

    set(HEICCONVERTER_TEST_HEIC "" CACHE FILEPATH "Optional HEIC file used by the conversion smoke test")
    if(HEICCONVERTER_TEST_HEIC)
        add_test(NAME conversion_smoke
            COMMAND HeicConverterSmokeTest "${HEICCONVERTER_TEST_HEIC}")
    endif()
endif()
