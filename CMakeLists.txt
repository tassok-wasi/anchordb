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
            include/anchordb/filter.h
            include/anchordb/env.h
            include/anchordb/cache.h
            include/anchordb/options.h
)

target_sources(anchordb
    PRIVATE
        src/memtable/arena.h
        src/memtable/arena.cc
        src/memtable/mwmr_skiplist.h
        src/memtable/swsr_skiplist.h
        src/memtable/skiplist.h
        src/memtable/table.h
        src/util/coding.h
        src/util/coding.cc
        src/util/crc32.h
        src/util/crc32.cc
        src/env/env.h
        src/util/aligned_buffer.h
        src/util/comparator.cc
        src/util/status.cc
        src/util/random.h
        src/util/hash.h
        src/util/hash.cc
        src/util/filter/bloom_filter.h
        src/util/filter/bloom_filter.cc
        src/util/filter/cuckoo_filter.h
        src/util/filter/cuckoo_filter.cc
        src/util/filter/ribbon_filter.h
        src/util/filter/ribbon_filter.cc
        src/util/filter/filter_dispatch.h
        src/db/db_format.h
        src/db/db_format.cc
        src/db/wal_reader.h
        src/db/wal_reader.cc
        src/db/wal_writer.h
        src/db/wal_writer.cc
        src/cache/lru.h
        src/cache/lru.cc
        src/cache/lirs.h
        src/cache/lirs.cc
        src/cache/clock.h
        src/cache/clock.cc
        src/cache/arc.h
        src/cache/arc.cc
)

if (WIN32)
  target_sources(anchordb
        PRIVATE
            src/env/env_win.h
            src/env/env_win.cc
            src/env/io_win.h
            src/env/io_win.cc
            src/port/port_win.h
            src/util/win_logger.h
    )

  # Locate and link IOCP for the Windows I/O completion port engine layer
  # IOCP is part of the Windows SDK, no separate package needed.
  # Kernel32.lib provides the IOCP functions (CreateIoCompletionPort, etc.)
  target_link_libraries(anchordb PRIVATE kernel32)
elseif (UNIX AND NOT APPLE)
  target_sources(anchordb
        PRIVATE
            src/env/env_posix.h
            src/env/env_posix.cc
            src/env/io_posix.h
            src/env/io_posix.cc
            src/port/port_posix.h
            src/util/posix_logger.h

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
#  that there is no need for multiple skiplist variants
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
    target_compile_options(anchordb PRIVATE -msse4.2 -mavx2)
  elseif(MSVC)
    target_compile_options(anchordb PRIVATE /arch:AVX2)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(anchordb PRIVATE -march=armv8-a+crc+simd)
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
