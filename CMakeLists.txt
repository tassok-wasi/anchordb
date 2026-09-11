cmake_minimum_required(VERSION 3.23)

project(anchordb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_library(anchordb STATIC)

target_compile_definitions(anchordb PUBLIC ANCHORDB_STATIC_DEFINE)

target_sources(anchordb
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            include
        FILES
            include/anchordb/macro.h
            include/anchordb/export.h
            include/anchordb/slice.h
            include/anchordb/comparator.h
            include/anchordb/iterator.h
            include/anchordb/status.h
)

target_sources(anchordb
    PRIVATE
        src/memtable/arena.h
        src/memtable/arena.cc
        src/memtable/mwmr_skiplist.h
        src/memtable/swmr_skiplist.h
        src/memtable/swsr_skiplist.h
        src/memtable/skiplist.h
        src/memtable/table.h
        src/memtable/table.cc
        src/memtable/table_format.h
        src/memtable/table_format.cc
        src/util/coding.h
        src/util/coding.cc
        src/util/crc32.h
        src/util/crc32.cc
        src/env/env.h
        src/util/aligned_buffer.cc
        src/util/comparator.cc
        src/util/status.cc
)

if (WIN32)
  target_sources(anchordb
        PRIVATE
            src/env/env_win.h
            src/env/env_win.cc
            src/env/io_win.h
            src/env/io_win.cc
    )
elseif (UNIX AND NOT APPLE)
  target_sources(anchordb
        PRIVATE
            src/env/env_posix.h
            src/env/env_posix.cc
            src/env/io_posix.h
            src/env/io_posix.cc
    )

  # Locate and link liburing for the io_uring engine layer
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBURING REQUIRED liburing)
  target_include_directories(anchordb PRIVATE ${LIBURING_INCLUDE_DIRS})
  target_link_libraries(anchordb PRIVATE ${LIBURING_LIBRARIES})
endif()

target_include_directories(anchordb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# MemTable Skiplist Variant Selector
# this checks will be removed in future when codebase will fully ensure
#  that there is no nedd for multiple skiplist variants
set(SKIPLIST_VARIANT "MWMR" CACHE STRING "Select MemTable SkipList variant (MWMR, SWMR, SWSR)")
set_property(CACHE SKIPLIST_VARIANT PROPERTY STRINGS "MWMR" "SWMR" "SWSR")

if (SKIPLIST_VARIANT STREQUAL "SWMR")
  target_compile_definitions(anchordb PUBLIC SWMR_MEMTABLE_SKIPLIST)
elseif (SKIPLIST_VARIANT STREQUAL "SWSR")
  target_compile_definitions(anchordb PUBLIC SWSR_MEMTABLE_SKIPLIST)
else()
  target_compile_definitions(anchordb PUBLIC MWMR_MEMTABLE_SKIPLIST)
endif()

# Compiler flags for hardware acceleration
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(anchordb PRIVATE -msse4.2)
  elseif(MSVC)
    target_compile_options(anchordb PRIVATE /arch:AVX)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(anchordb PRIVATE -march=armv8-a+crc)
  endif()
endif()

# IPO (Interprocedural Optimization)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
  # Enables Link-Time Optimization for maximum cross-translation-unit inlining
  set_target_properties(anchordb PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()


# they aren't implemented yet
# include(CheckLibraryExists)
# check_library_exists(snappy snappy_compress "" HAVE_SNAPPY)
