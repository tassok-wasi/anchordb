cmake_minimum_required(VERSION 3.23)

project(anchordb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(anchordb SHARED)

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
        src/util/log_writer.h
        src/util/log_writer.cc
        src/util/coding.h
        src/util/coding.cc
        src/memtable/key.h
        src/memtable/key.cc
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

target_include_directories(anchordb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
