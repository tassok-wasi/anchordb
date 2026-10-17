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
            include/anchordb/comparator.h
            include/anchordb/compressor.h
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
        src/memtable/table.cc
        src/util/coding.h
        src/util/coding.cc
        src/util/crc32.h
        src/util/crc32.cc
        src/env/env.h
        src/util/aligned_buffer.h
        src/util/comparator.cc
        src/util/compressor.cc
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
        src/db/db_format.h
        src/db/db_format.cc
        src/db/wal_reader.h
        src/db/wal_reader.cc
        src/db/wal_writer.h
        src/db/wal_writer.cc
        src/cache/cache.cc
        src/cache/util.h
        src/cache/lru.h
        src/cache/lru.cc
        src/cache/lirs.h
        src/cache/lirs.cc
        src/cache/clock_pro.h
        src/cache/clock_pro.cc
        src/cache/circular_ring.h
        src/cache/circular_ring.cc
        src/cache/swiss_map.h
        src/cache/robinhood_map.h
        src/cache/linear_prob_map.h
        src/cache/list.h
        src/util/thread_pool.h
        src/cache/arc.h
        src/cache/arc.cc
        src/sstable/empty_iter.h
        src/sstable/format.h
        src/sstable/format.cc
        src/sstable/iterator_wrapper.h
        src/port/port.h
        src/port/port_thread.h
        src/port/port_stdcxx.h
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

include(CheckCXXSymbolExists)
check_cxx_symbol_exists(fdatasync "unistd.h" HAS_FDATASYNC)
check_cxx_symbol_exists(F_FULLFSYNC "fcntl.h" HAS_FULLFSYNC)
check_cxx_symbol_exists(O_CLOEXEC "fcntl.h" HAS_O_CLOEXEC)

if (HAVE_FDATASYNC)
    target_compile_definitions(anchordb PRIVATE HAS_FDATASYNC=1)
elseif(HAVE_FULLFSYNC)
    target_compile_definitions(anchordb PRIVATE HAS_FULLFSYNC=1)
endif()

if (HAVE_O_CLOEXEC)
    target_compile_definitions(anchordb PRIVATE HAS_O_CLOEXEC=1)
endif()

find_package(zstd CONFIG)
if(zstd_FOUND)
    target_link_libraries(anchordb PRIVATE zstd::libzstd)
    target_compile_definitions(anchordb PRIVATE HAS_ZSTD=1)
    message(STATUS "Zstd found! Enabling Zstd block compression.")
else()
    message(STATUS "Zstd NOT found. Disabling Zstd support.")
endif()


find_package(Snappy CONFIG)
if(Snappy_FOUND)
    target_link_libraries(anchordb PRIVATE Snappy::snappy)
    target_compile_definitions(anchordb PRIVATE HAS_SNAPPY=1)
    message(STATUS "Snappy found! Enabling Snappy block compression.")
else()
    message(STATUS "Snappy NOT found. Disabling Snappy support.")
endif()

find_package(lz4 CONFIG)
if(lz4_FOUND)
    target_link_libraries(anchordb PRIVATE lz4::lz4)
    target_compile_definitions(anchordb PRIVATE HAS_LZ4=1)
    message(STATUS "lz4 found! Enabling lz4 block compression.")
else()
    message(STATUS "lz4 NOT found. Disabling lz4 support.")
endif()

find_package(ZLIB)
if(ZLIB_FOUND)
    target_link_libraries(anchordb PRIVATE ZLIB::ZLIB)
    target_compile_definitions(anchordb PRIVATE HAS_ZLIB=1)
    message(STATUS "ZLIB found! Enabling ZLIB block compression.")
else()
    message(STATUS "ZLIB NOT found. Disabling ZLIB support.")
endif()

# ---------- testing not set up yet ----------

# enable_testing()

# add_executable(anchordb_test test_main.cpp)
# target_link_libraries(anchordb_test PRIVATE
#     GTest::gtest_main  # Provides the standard main() entry point for tests
#     GTest::gtest       # Core GoogleTest library
#     # Link your storage engine library/objects here too
# )

# # Enable CTest integration so you can run tests with 'ctest'
# include(GoogleTest)
# gtest_discover_tests(anchordb_test)
