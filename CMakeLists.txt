cmake_minimum_required(VERSION 3.23)

project(anchordb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANCHORDB_USE_IO_URING
    "Use io_uring for asynchronous I/O on Linux when liburing is available"
    ON)
option(ANCHORDB_DIRECT_IO_DEFAULT
    "Open files with O_DIRECT by default in the POSIX environment"
    ON)
option(ANCHORDB_USE_TCMALLOC
    "Link tcmalloc process-wide so global new/delete route through it"
    ON)


add_library(anchordb STATIC)

target_compile_definitions(anchordb PUBLIC ANCHORDB_STATIC_DEFINE)

target_sources(anchordb
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            include
        FILES
            include/anchordb/anchordb_namespace.h
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
        src/memory/arena.h
        src/memory/arena.cc
        src/memtable/skiplist.h
        src/memtable/table.h
        src/memtable/table.cc
        src/util/coding.h
        src/util/coding.cc
        src/util/crc32.h
        src/util/crc32.cc
        src/env/env.cc
        src/env/env_default.h
        src/file/filename.h
        src/file/filename.cc
        src/file/io_uring_config.h
        src/file/sync_io.h
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
        src/util/filter/filter.cc
        src/db/db_format.h
        src/db/db_format.cc
        src/db/iterator.cc
        src/db/wal_format.h
        src/db/wal_reader.h
        src/db/wal_reader.cc
        src/db/wal_writer.h
        src/db/wal_writer.cc
        src/cache/cache.cc
        src/cache/cache_wrapper.h
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
        src/sstable/data_record.h
        src/sstable/data_record.cc
        src/sstable/record_file_writer.h
        src/sstable/record_file_writer.cc
        src/sstable/iterator_wrapper.h
        src/port/port.h
        src/port/port_thread.h
        src/port/port_stdcxx.h
        src/sstable/empty_iter.h
        src/sstable/idx_block_reader.cc
        src/sstable/idx_block_reader.h
        src/sstable/idx_block_writer.cc
        src/sstable/idx_block_writer.h
        src/sstable/idx_table_builder.cc
        src/sstable/idx_table_builder.h
        src/sstable/filter_block.cc
        src/sstable/filter_block.h
        src/sstable/idx_summary.cc
        src/sstable/idx_summary.h
        src/sstable/table.cc
        src/sstable/table.h
        src/sstable/table_cache.cc
        src/sstable/table_cache.h
        src/sstable/iterator_wrapper.h
)

if (WIN32)
    target_sources(anchordb
        PRIVATE
            src/port/win/env_win.h
            src/port/win/env_win.cc
            src/port/win/io_win.h
            src/port/win/io_win.cc
            src/port/port_win.h
            src/port/win/win_logger.h
    )

    # Locate and link IOCP for the Windows I/O completion port engine layer
    # IOCP is part of the Windows SDK, no separate package needed.
    # Kernel32.lib provides the IOCP functions (CreateIoCompletionPort, etc.)
    target_link_libraries(anchordb PRIVATE kernel32)
elseif (UNIX AND NOT APPLE)
    target_sources(anchordb
        PRIVATE
            src/env/posix.h
            src/env/posix.cc
            src/file/file_meta.h
            src/file/limiter.h
            src/file/lock_table.h
            src/file/random_access_reader.cc
            src/file/random_access_reader.h
            src/file/sequential_reader.h
            src/file/sequential_reader.cc
            src/file/writer.cc
            src/file/writer.h
            src/port/port_posix.h
            src/util/posix_logger.h

    )

    # Locate and link liburing for the io_uring engine layer. io_uring is
    # optional: without liburing the environment still builds and runs using
    # the synchronous fallback paths.
    if (ANCHORDB_USE_IO_URING)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBURING liburing)
        if (LIBURING_FOUND)
            target_include_directories(anchordb PRIVATE ${LIBURING_INCLUDE_DIRS})
            target_link_libraries(anchordb PRIVATE ${LIBURING_LIBRARIES})
            target_compile_definitions(anchordb PRIVATE HAS_IO_URING=1)
        else()
            message(WARNING
                "liburing not found; building without io_uring support "
                "(synchronous I/O only)")
            target_compile_definitions(anchordb PRIVATE HAS_IO_URING=0)
        endif()
    else()
        message(STATUS "ANCHORDB_USE_IO_URING=OFF: synchronous I/O only")
        target_compile_definitions(anchordb PRIVATE HAS_IO_URING=0)
    endif()

    target_compile_definitions(anchordb
        PRIVATE ANCHORDB_DIRECT_IO_DEFAULT=$<BOOL:${ANCHORDB_DIRECT_IO_DEFAULT}>)
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

# ---------- tcmalloc (process-wide allocator) ----------
# Linked PUBLIC so the override reaches every consumer of anchordb. Linking
# libtcmalloc interposes the global operator new/delete and malloc/free, so the
# Arena's blocks and any other heap allocation are served by tcmalloc instead of
# the OS allocator. No wrapper type is involved.
if(ANCHORDB_USE_TCMALLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TCMALLOC libtcmalloc)
    if(TCMALLOC_FOUND)
        target_link_libraries(anchordb PUBLIC ${TCMALLOC_LIBRARIES})
        target_include_directories(anchordb PUBLIC ${TCMALLOC_INCLUDE_DIRS})
        message(STATUS "tcmalloc found! Linking it globally as the allocator.")
    else()
        message(WARNING
            "tcmalloc not found; falling back to the system allocator.")
    endif()
else()
    message(STATUS "ANCHORDB_USE_TCMALLOC=OFF: using the system allocator")
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
