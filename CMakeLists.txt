cmake_minimum_required(VERSION 3.16)
project(shortforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)

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
target_link_directories(shortforge PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(shortforge PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(shortforge PRIVATE ${FFMPEG_CFLAGS_OTHER} -Wall -Wextra -O2)

# Unit tests need no FFmpeg at all.
add_executable(shortforge_tests
    tests/test_main.cpp
    ${SF_CORE}
)
target_include_directories(shortforge_tests PRIVATE src)
target_compile_options(shortforge_tests PRIVATE -Wall -Wextra -O1)

enable_testing()
add_test(NAME shortforge_tests COMMAND shortforge_tests)
