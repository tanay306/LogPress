cmake_minimum_required(VERSION 3.15)
project(logpress LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use Release mode by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# macOS: Homebrew keg-only support for sqlite
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(STATUS "Using Homebrew keg-only SQLite for macOS ARM64")
    set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/sqlite")
    set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/sqlite/lib/pkgconfig")
endif()

# Enable pkg-config
find_package(PkgConfig REQUIRED)

# Find libraries
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
find_package(ZLIB REQUIRED)

find_package(CURL REQUIRED)

# Sources
set(SOURCES
    main.cpp
    compressor.cpp
    decompressor.cpp
    searcher.cpp
    sqlite_helper.cpp
)

add_executable(logpress ${SOURCES})

# Include & Link
target_include_directories(logpress PRIVATE
    ${SQLITE3_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(logpress PRIVATE
    ${SQLITE3_LIBRARIES}
    ${ZLIB_LIBRARIES}
)

target_include_directories(logpress PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(logpress PRIVATE ${CURL_LIBRARIES})
