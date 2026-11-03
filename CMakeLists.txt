cmake_minimum_required(VERSION 3.16)
project(shortforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FFmpeg discovery -------------------------------------------------------
# PkgConfig on GCC/Clang; on MSVC use the vcpkg installed tree (the vcpkg
# toolchain file sets VCPKG_INSTALLED_DIR / VCPKG_TARGET_TRIPLET) or FFMPEG_ROOT.
if(MSVC)
    set(_ff_hints "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _ff_hints "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(DEFINED ENV{FFMPEG_ROOT})
        list(APPEND _ff_hints "$ENV{FFMPEG_ROOT}")
    endif()
    find_path(FFMPEG_INCLUDE_DIRS NAMES libavformat/avformat.h
              PATHS ${_ff_hints} PATH_SUFFIXES include)
    set(FFMPEG_LIBRARIES "")
    foreach(_lib avformat avcodec avutil swscale swresample)
        find_library(FFMPEG_LIB_${_lib} NAMES ${_lib}
                     PATHS ${_ff_hints} PATH_SUFFIXES lib)
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_LIB_${_lib}})
    endforeach()
    if(NOT FFMPEG_INCLUDE_DIRS OR NOT FFMPEG_LIBRARIES)
        message(FATAL_ERROR "FFmpeg not found: install vcpkg ffmpeg or set FFMPEG_ROOT "
                            "(see docs/BUILD_WINDOWS.md)")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)
endif()

# --- Warning flags (compiler-appropriate) ------------------------------------
if(MSVC)
    set(SF_WARN_FLAGS /W4)
else()
    set(SF_WARN_FLAGS -Wall -Wextra)
endif()

# Dependency-free core: JSON, Ken Burns math, project file.
set(SF_CORE
    src/json.cpp
    src/kenburns.cpp
    src/project.cpp
)

add_executable(shortforge
    src/main.cpp
    ${SF_CORE}
    src/ffmpeg_decode.cpp
    src/hwaccel.cpp
    src/renderer.cpp
)
target_include_directories(shortforge PRIVATE src ${FFMPEG_INCLUDE_DIRS})
if(NOT MSVC)
    target_link_directories(shortforge PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()
target_link_libraries(shortforge PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(shortforge PRIVATE ${FFMPEG_CFLAGS_OTHER} ${SF_WARN_FLAGS})
if(NOT MSVC)
    # Release/Debug optimization comes from CMAKE_BUILD_TYPE on all platforms;
    # -O2/-O1 are GCC/Clang-only spellings.
    target_compile_options(shortforge PRIVATE -O2)
endif()

# Unit tests need no FFmpeg at all.
add_executable(shortforge_tests
    tests/test_main.cpp
    ${SF_CORE}
)
target_include_directories(shortforge_tests PRIVATE src)
target_compile_options(shortforge_tests PRIVATE ${SF_WARN_FLAGS})
if(NOT MSVC)
    target_compile_options(shortforge_tests PRIVATE -O1)
endif()

enable_testing()
add_test(NAME shortforge_tests COMMAND shortforge_tests)
